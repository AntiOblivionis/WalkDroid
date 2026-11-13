/* Sony CFW DAC tables, loaded before Android fragments the low-memory zone.
 * RAM only. No NVP, flash, tuning mutation, route enable or volume increase.
 * This driver's proc read/write holds its mutex until the WHOLE table is sent.
 */
#define _GNU_SOURCE
#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

struct table_spec {const char *node,*asset;unsigned int size;};
static const struct table_spec specs[]={
 {"ovt","ov_127x.tbl",84942},
 {"ovt_dsd","ov_dsd_127x.tbl",13068},
 {"tct","tc_127x.tbl",2880}
};
static int transfer(int fd,unsigned char *buf,unsigned int size,int writing) {
 /* Block catchable signals across the complete kernel transaction. One normal
  * syscall transfers this known table in full; retry EINTR/short progress.
  * Never introduce a partial-table dump, timeout or cancellation here. */
 sigset_t block,old;sigfillset(&block);
 if(sigprocmask(SIG_BLOCK,&block,&old))return -errno;
 unsigned int done=0;int rc=0;
 while(done<size) {
  ssize_t n=writing?write(fd,buf+done,size-done):read(fd,buf+done,size-done);
  if(n<0&&errno==EINTR)continue;
  if(n<=0){rc=n<0?-errno:-EIO;break;}
  done+=(unsigned int)n;
 }
 if(sigprocmask(SIG_SETMASK,&old,NULL)&&!rc)rc=-errno;
 return rc;
}
static int table_io(const struct table_spec *s,unsigned char *buf,int writing) {
 char path[128];snprintf(path,sizeof(path),"/proc/icx_audio_cxd3778gf_data/%s",s->node);
 int fd=open(path,(writing?O_WRONLY:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -errno;
 int rc=transfer(fd,buf,s->size+(writing?8:0),writing);close(fd);return rc;
}
static int asset_read(int root,const struct table_spec *s,unsigned char *buf) {
 int fd=openat(root,s->asset,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
 int rc=transfer(fd,buf,s->size+8,0);close(fd);return rc;
}
static int mute_route(void) {
 struct mixer *m=mixer_open(0);if(!m)return -ENODEV;
 struct mixer_ctl *mute=mixer_get_ctl_by_name(m,"playback mute"),
  *volume=mixer_get_ctl_by_name(m,"master volume"),*out=mixer_get_ctl_by_name(m,"output device");
 int rc=-ENODEV;
 if(mute&&volume&&out) {
  rc=mixer_ctl_set_value(mute,0,1);
  if(!rc)rc=mixer_ctl_set_value(volume,0,0);
  if(!rc)rc=mixer_ctl_set_enum_by_string(out,"off");
 }
 mixer_close(m);return rc;
}
int main(int argc,char **argv) {
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc!=3||strcmp(argv[1],"--initialize"))return 64;
 int rc=0;
 int lock=open("/dev/.walkdroid-sony-dac-init.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);return 91;}
 int root=open(argv[2],O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 unsigned char *tables[3]={NULL,NULL,NULL};int count=0;
 if(root<0){rc=-errno;goto done;}
 for(unsigned int i=0;i<3;++i) {
  tables[i]=malloc(specs[i].size+8);if(!tables[i]){rc=-ENOMEM;goto done;}
  rc=asset_read(root,&specs[i],tables[i]);if(rc)goto done;
 }
 rc=mute_route();if(rc)goto done;
 for(unsigned int i=0;i<3;++i) {
  rc=table_io(&specs[i],tables[i],1);if(rc)goto done;
  ++count;printf("TABLE_LOADED %s bytes=%u\n",specs[i].node,specs[i].size);
 }
 printf("DAC_TABLES_READY writes=%d route=off volume=0 muted=1\n",count);
done:
 if(rc)printf("DAC_TABLES_FAILED status=%d; audio must remain stopped\n",rc);
 for(unsigned int i=0;i<3;++i)free(tables[i]);
 if(root>=0)close(root);close(lock);return rc?1:0;
}
