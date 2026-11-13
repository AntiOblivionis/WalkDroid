/* Apply a selected Sony CFW GMD table set. The driver's proc write holds its
 * mutex until the complete table has been sent. */
#define _GNU_SOURCE
#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static const unsigned int sizes[]={84942,13068};
static const char *nodes[]={"ovt","ovt_dsd"};
static const char *assets[]={"ov_127x.tbl","ov_dsd_127x.tbl"};
static const char *modes[]={"gain_n","gain_h","gain_l"};

static int transfer(int fd,unsigned char *buffer,unsigned int bytes,int writing){
    sigset_t block,old;sigfillset(&block);
    if(sigprocmask(SIG_BLOCK,&block,&old))return -errno;
    unsigned int at=0;int rc=0;
    while(at<bytes){
        ssize_t n=writing?write(fd,buffer+at,bytes-at):read(fd,buffer+at,bytes-at);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){rc=n<0?-errno:-EIO;break;}at+=(unsigned int)n;
    }
    if(sigprocmask(SIG_SETMASK,&old,NULL)&&!rc)rc=-errno;
    return rc;
}
static int mute_route(void){
    struct mixer *m=mixer_open(0);if(!m)return -ENODEV;
    struct mixer_ctl *mute=mixer_get_ctl_by_name(m,"playback mute");
    struct mixer_ctl *out=mixer_get_ctl_by_name(m,"output device");
    struct mixer_ctl *vol=mixer_get_ctl_by_name(m,"master volume");
    int rc=-ENODEV;
    if(mute&&out&&vol){rc=mixer_ctl_set_value(mute,0,1);if(!rc)rc=mixer_ctl_set_enum_by_string(out,"off");if(!rc)rc=mixer_ctl_set_value(vol,0,0);}
    mixer_close(m);return rc;
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);
    int legacy=argc==5&&(!strcmp(argv[1],"--switch")||!strcmp(argv[1],"--boot-switch"));
    if(!legacy&&(argc!=4||strcmp(argv[1],"--apply")))return 64;
    const char *choice=legacy?argv[4]:argv[3];
    if(strlen(choice)!=1||choice[0]<'0'||choice[0]>'2')return 64;
    int selected=choice[0]-'0',rc=0,writes=0;
    int lock=open("/dev/.walkdroid-sony-dac-init.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);return 91;}
    int root=open(argv[2],O_RDONLY|O_DIRECTORY|O_CLOEXEC),dir=-1;
    unsigned char *tables[2]={NULL,NULL};
    if(root<0){rc=-errno;goto done;}
    dir=openat(root,modes[selected],O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0){rc=-errno;goto done;}
    for(unsigned int i=0;i<2;++i){
        tables[i]=malloc(sizes[i]+8);if(!tables[i]){rc=-ENOMEM;goto done;}
        int fd=openat(dir,assets[i],O_RDONLY|O_CLOEXEC);if(fd<0){rc=-errno;goto done;}
        rc=transfer(fd,tables[i],sizes[i]+8,0);close(fd);if(rc)goto done;
    }
    rc=mute_route();if(rc)goto done;
    for(unsigned int i=0;i<2;++i){
        char path[128];snprintf(path,sizeof(path),"/proc/icx_audio_cxd3778gf_data/%s",nodes[i]);
        int fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0){rc=-errno;goto done;}
        rc=transfer(fd,tables[i],sizes[i]+8,1);if(close(fd)&&!rc)rc=-errno;if(rc)goto done;
        ++writes;printf("GAIN_TABLE_LOADED %s bytes=%u\n",nodes[i],sizes[i]);
    }
done:
    printf("GAIN_RESULT status=%d requested=%d writes=%d\n",rc,selected,writes);
    for(unsigned int i=0;i<2;++i)free(tables[i]);
    if(dir>=0)close(dir);if(root>=0)close(root);close(lock);return rc?1:0;
}
