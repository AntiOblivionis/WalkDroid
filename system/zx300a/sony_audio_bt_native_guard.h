#ifndef WALKDROID_SONY_BT_NATIVE_GUARD_H
#define WALKDROID_SONY_BT_NATIVE_GUARD_H
/* Shared Bluetooth supervisor process helpers. */
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
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
extern int __system_property_get(const char *,char *);
static int sony_bt_guard_driver_fds(void) {
    return 0;
}
static int sony_bt_guard_close_fds(int keep1,int keep2,int keep3) {
    DIR *dir=opendir("/proc/self/fd");if(!dir)return -errno;int held=dirfd(dir),rc=0;struct dirent *e;
    while((e=readdir(dir))){char *end;long fd=strtol(e->d_name,&end,10);
        if(*end||fd<=2||fd>0x7fffffff||fd==held||fd==keep1||fd==keep2||fd==keep3)continue;
        if(close((int)fd)&&errno!=EBADF&&!rc)rc=-errno;
    }
    closedir(dir);return rc;
}
static int sony_bt_guard_spawn_volume(int root,int log,int control,uint64_t nonce,int volume) {
    pid_t pid=fork();if(pid)return pid;
    (void)prctl(PR_SET_PDEATHSIG,SIGKILL);if(dup2(log,1)<0||dup2(log,2)<0)_exit(126);
    int null=open("/dev/null",O_RDONLY|O_CLOEXEC);if(null<0||dup2(null,0)<0)_exit(126);
    if(sony_bt_guard_close_fds(root,control,volume)||fchdir(root)||chroot(".")||chdir("/"))_exit(126);
    close(root);
    int workspace=open("/etc/sony-bt-properties",O_RDONLY|O_NOFOLLOW);struct stat st;
    if(workspace<0||fstat(workspace,&st))_exit(125);
    if(control>=0&&fcntl(control,F_SETFD,0))_exit(126);
    if(volume>=0&&fcntl(volume,F_SETFD,0))_exit(126);
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
    for(int n=1;n<NSIG;++n)if(n!=SIGKILL&&n!=SIGSTOP)sigaction(n,&action,NULL);
    sigset_t empty;sigemptyset(&empty);if(sigprocmask(SIG_SETMASK,&empty,NULL))_exit(126);
    char property[96],descriptor[24],session[24],volume_descriptor[24];
    snprintf(property,sizeof(property),"ANDROID_PROPERTY_WORKSPACE=%d,%lld",workspace,(long long)st.st_size);
    snprintf(descriptor,sizeof(descriptor),"%d",control);snprintf(session,sizeof(session),"%llx",(unsigned long long)nonce);
    snprintf(volume_descriptor,sizeof(volume_descriptor),"%d",volume);
    char *env[]={"LD_LIBRARY_PATH=/lib","PATH=/bin",property,NULL};
#ifndef SONY_TX_CONTROL_ARGUMENT
#define SONY_TX_CONTROL_ARGUMENT "--session-fd"
#endif
    char *args[]={control>=0?"/bin/sony-bt-tx-worker":"/bin/mtkbt",control>=0?SONY_TX_CONTROL_ARGUMENT:NULL,descriptor,session,volume>=0?"--volume-fd":NULL,volume_descriptor,NULL};
    execve(args[0],args,env);_exit(127);
}
static int sony_bt_guard_spawn(int root,int log,int control,uint64_t nonce){return sony_bt_guard_spawn_volume(root,log,control,nonce,-1);}
#endif
