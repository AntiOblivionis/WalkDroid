/* Optional native streaming diagnostic. Target selection belongs to the
 * operation entry, and the caller coordinates access to shared resources. */
#define _GNU_SOURCE
#include "sony_audio_owner.h"
#include "sony_audio_route.h"
#include "sony_audio_bt_radio_owner.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef SONY_BT_SUPERVISOR_HELPERS_ONLY
static int soc_chip_readonly(void){
    int fd=open("/dev/wmtdetect",O_RDONLY|O_CLOEXEC);if(fd<0)return -errno;
    /* GPL wmt_detect.c COMBO_IOCTL_GET_SOC_CHIP_ID: returns the existing SoC
     * chip identity, no power/detect/init/set operation and no output pointer. */
    int rc=ioctl(fd,_IOR('w',3,int),0);int saved=errno;if(close(fd))return -errno;return rc<0?-saved:rc;
}
static int driver_fds(void){
    struct stat expected;if(stat("/dev/stpbt",&expected)||!S_ISCHR(expected.st_mode))return -1;
    DIR *proc=opendir("/proc");if(!proc)return -1;struct dirent *e;int count=0;
    while((e=readdir(proc))){if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;char path[512];snprintf(path,sizeof(path),"/proc/%s/fd",e->d_name);DIR *fds=opendir(path);if(!fds){if(errno==ENOENT||errno==ESRCH)continue;count=-1;break;}struct dirent *d;
        while((d=readdir(fds))){if(d->d_name[0]=='.')continue;char item[1024];snprintf(item,sizeof(item),"%s/%s",path,d->d_name);struct stat s;if(!stat(item,&s)){if(S_ISCHR(s.st_mode)&&s.st_rdev==expected.st_rdev)++count;}else if(errno!=ENOENT&&errno!=ESRCH){count=-1;break;}}
        closedir(fds);if(count<0)break;
    }closedir(proc);return count;
}
#endif
static int spawn(int root,int log,const char *name,const char *mode){
    pid_t pid=fork();if(pid)return pid;
    (void)prctl(PR_SET_PDEATHSIG,SIGKILL);
    if(dup2(log,1)<0||dup2(log,2)<0||fchdir(root)||chroot(".")||chdir("/"))_exit(126);
    int workspace=open("/etc/sony-bt-properties",O_RDONLY);struct stat prop;
    if(workspace<0||fstat(workspace,&prop))_exit(125);
    /* Intentional inherited read-only FD: the original glibc constructor
     * maps this private legacy ABI. No Android9 property files are shared. */
    char property_env[96];snprintf(property_env,sizeof(property_env),"ANDROID_PROPERTY_WORKSPACE=%d,%lld",workspace,(long long)prop.st_size);
    char *env[]={"LD_LIBRARY_PATH=/lib","PATH=/bin",property_env,NULL};char *args[]={(char *)name,(char *)mode,NULL};if(!mode)args[1]=NULL;execve(name,args,env);_exit(127);
}
static int reap(pid_t pid,unsigned ms,int *exit_code){
    for(unsigned n=0;n<ms/10;++n){int status=0;pid_t r=waitpid(pid,&status,WNOHANG);if(r==pid){*exit_code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);return 0;}if(r<0&&errno!=EINTR)return -errno;usleep(10000);}return -ETIMEDOUT;
}
static int terminate(pid_t pid,int *code,int *forced){
    int status=0;pid_t p=waitpid(pid,&status,WNOHANG);if(p==pid){*code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);return 0;}if(p<0)return -errno;
    if(kill(pid,SIGTERM)&&errno!=ESRCH)return -errno;int rc=reap(pid,2000,code);if(rc==-ETIMEDOUT){*forced=1;kill(pid,SIGKILL);rc=reap(pid,2000,code);}return rc;
}
#ifndef SONY_BT_SUPERVISOR_HELPERS_ONLY
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--chip-id")){int chip=soc_chip_readonly();printf("{\"read_only\":true,\"soc_chip_id\":%d}\n",chip);return chip<0?65:0;}
    if(argc!=5||(strcmp(argv[1],"--software-init")&&strcmp(argv[1],"--rf-stream")))return 64;setvbuf(stdout,NULL,_IONBF,0);
    int root=open(argv[2],O_DIRECTORY|O_RDONLY|O_CLOEXEC),dl=open(argv[3],O_CREAT|O_APPEND|O_WRONLY|O_CLOEXEC,0600),pl=open(argv[4],O_CREAT|O_APPEND|O_WRONLY|O_CLOEXEC,0600);if(root<0||dl<0||pl<0)return 66;
    struct sony_audio_owner *owner=NULL;int rc=sony_audio_owner_acquire(&owner,SONY_OWNER_BT_RX);if(rc)return 67;uint64_t generation=sony_audio_owner_generation(owner);
    struct sony_bt_radio_owner *radio=NULL;rc=sony_bt_radio_acquire(&radio,SONY_BT_RADIO_NATIVE_RX);if(rc){sony_audio_owner_finish(&owner,1);return 68;}uint64_t radio_generation=sony_bt_radio_generation(radio);
    int daemon=spawn(root,dl,"/bin/mtkbt",NULL),probe=-1,ready=0,de=-99,pe=-99,dc=-99,pc=0,df=0,pf=0;
    if(daemon<1)rc=-errno;
    for(unsigned n=0;!rc&&n<300;++n){struct stat s;if(!fstatat(root,"tmp/bt.int.adp",&s,0)){ready=1;break;}usleep(10000);}
    if(!ready&&!rc)rc=-ETIMEDOUT;
    if(!rc){probe=spawn(root,pl,"/bin/sony-bt-stream-check",argv[1]);if(probe<1)rc=-errno;else{pc=reap(probe,74000,&pe);if(pc)rc=pc;else if(pe)rc=-EPROTO;}}
    if(probe>0&&pc){int er=terminate(probe,&pe,&pf);if(!rc)rc=er;}
    if(daemon>0)dc=terminate(daemon,&de,&df);
    int fds=driver_fds(),idle=sony_audio_route_idle(),completed=!rc&&!dc&&!pf&&!df;
    int radio_finish=sony_bt_radio_finish(&radio,0);
    int finish=sony_audio_owner_finish(&owner,0);int passed=completed&&!radio_finish&&!finish;
    fprintf(stderr,"PAIR_RADIO_LEASE generation=%llu finish=%d verified_off=0\n",(unsigned long long)radio_generation,radio_finish);
    printf("{\"supervisor_result\":true,\"passed\":%s,\"generation\":%llu,\"status\":%d,\"daemon_socket_ready\":%d,\"daemon_pid\":%d,\"probe_pid\":%d,\"probe_exit\":%d,\"probe_wait\":%d,\"daemon_exit\":%d,\"daemon_wait\":%d,\"probe_forced\":%d,\"daemon_forced\":%d,\"driver_fds_after\":%d,\"independent_idle\":%d,\"owner_clean\":false,\"owner_finish\":%d,\"audio_started\":false,\"pairing_capable\":true}\n",passed?"true":"false",(unsigned long long)generation,rc,ready,daemon,probe,pe,pc,de,dc,pf,df,fds,idle,finish);
    close(root);close(dl);close(pl);return passed?0:70;
}
#endif
