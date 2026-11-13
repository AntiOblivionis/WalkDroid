/* Final GLES client target -> MTK display ION import. No CPU pixel access.
 * HWC2OnFbAdapter has already waited and closed the GPU acquire fence.
 * Its legacy API cannot return fences, so wait for the real MTK apply fence.
 * Retain the active DMA-BUF until a replacement has been applied. */
#include <poll.h>
#include <time.h>
#include <mtkfb.h>
static int zx9_front_fd=-1,zx9_pending_fd=-1;
static bool zx9_direct_disabled=false;
static unsigned zx9_direct_frames=0,zx9_copy_frames=0,zx9_direct_errors=0;
static uint64_t zx9_queue_ns=0,zx9_wait_ns=0,zx9_max_ns=0;
static uint64_t zx9_now_ns(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
static void zx9_direct_release_after_fb_restore(){
 if(zx9_pending_fd>=0){close(zx9_pending_fd);zx9_pending_fd=-1;}
 if(zx9_front_fd>=0){close(zx9_front_fd);zx9_front_fd=-1;}
 ++zx9_copy_frames;
}
static int zx9_direct_post(private_module_t*m,const private_handle_t*h){
 if(zx9_direct_disabled)return -ENOTSUP;
 if(!(h->flags&private_handle_t::PRIV_FLAGS_USES_ION)||h->share_fd<0||h->stride<h->width||
    (uint64_t)h->size<(uint64_t)h->stride*(uint64_t)h->height*4u)return -ENOTSUP;
 MTK_FB_FORMAT fmt;
 if(h->format==HAL_PIXEL_FORMAT_RGBA_8888)fmt=MTK_FB_FORMAT_ABGR8888;
 else if(h->format==HAL_PIXEL_FORMAT_RGBX_8888)fmt=MTK_FB_FORMAT_XBGR8888;
 else return -ENOTSUP;
 int keep=fcntl(h->share_fd,F_DUPFD_CLOEXEC,0);if(keep<0)return -errno;
 fb_overlay_config c={};c.fence=-1;
 for(int i=0;i<4;i++){c.layers[i].layer_id=i;c.layers[i].fence_fd=-1;c.layers[i].ion_fd=-1;c.layers[i].next_buff_idx=-1;}
 auto&l=c.layers[0];l.layer_enable=1;l.src_fmt=fmt;l.src_pitch=h->stride;l.src_width=l.tgt_width=h->width;l.src_height=l.tgt_height=h->height;l.alpha=255;l.ion_fd=keep;
 uint64_t begin=zx9_now_ns();int ret=ioctl(m->framebuffer->fd,MTKFB_QUEUE_OVERLAY_CONFIG,&c);uint64_t queued=zx9_now_ns();
 if(ret||c.fence<0){if(c.fence>=0)close(c.fence);close(keep);zx9_direct_disabled=true;++zx9_direct_errors;ALOGE("ZX9_DIRECT queue failed ret=%d fence=%d errno=%d; restoring fb copy",ret,c.fence,errno);return -EIO;}
 zx9_pending_fd=keep;
 pollfd wait={c.fence,POLLIN,0};int n;
 do{n=poll(&wait,1,1000);}while(n<0&&errno==EINTR);
 uint64_t done=zx9_now_ns();close(c.fence);
 if(n!=1||!(wait.revents&POLLIN)||(wait.revents&(POLLERR|POLLNVAL))){zx9_direct_disabled=true;++zx9_direct_errors;ALOGE("ZX9_DIRECT fence failed n=%d events=%x; retain DMA-BUF until fb restore",n,wait.revents);return -EIO;}
 if(zx9_front_fd>=0)close(zx9_front_fd);
 zx9_front_fd=zx9_pending_fd;zx9_pending_fd=-1;
 zx9_queue_ns+=queued-begin;zx9_wait_ns+=done-queued;if(done-begin>zx9_max_ns)zx9_max_ns=done-begin;
 if(!(++zx9_direct_frames%32))ALOGI("ZX9_PRESENT direct=%u copy=%u errors=%u queue_avg_us=%llu wait_avg_us=%llu max_us=%llu",zx9_direct_frames,zx9_copy_frames,zx9_direct_errors,(unsigned long long)(zx9_queue_ns/zx9_direct_frames/1000),(unsigned long long)(zx9_wait_ns/zx9_direct_frames/1000),(unsigned long long)(zx9_max_ns/1000));
 return 0;
}
