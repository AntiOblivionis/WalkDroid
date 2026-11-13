#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* The initial mode comes from the running framebuffer. With an argument,
 * request the existing WalkDroid RGBX offsets while leaving geometry intact. */
int main(int argc,char **argv){
    (void)argv;
    int fd=open("/dev/graphics/fb0",O_RDWR|O_CLOEXEC);
    if(fd<0)return 1;
    struct fb_var_screeninfo info;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&info)){close(fd);return 2;}
    printf("BEFORE %ux%u bpp=%u R=%u G=%u B=%u A=%u\n",info.xres,info.yres,info.bits_per_pixel,
           info.red.offset,info.green.offset,info.blue.offset,info.transp.offset);
    if(argc>1){
        info.red.offset=0;info.green.offset=8;info.blue.offset=16;info.transp.offset=24;
        info.activate=0;
        int rc=ioctl(fd,FBIOPUT_VSCREENINFO,&info);printf("SET_RESULT %d\n",rc);
        if(rc){close(fd);return 4;}
    }
    close(fd);return 0;
}
