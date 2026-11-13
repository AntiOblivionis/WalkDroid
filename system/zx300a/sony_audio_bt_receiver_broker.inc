/* Included only by the BT broker, before any owner or RF operation. */
static int copy_peer_tree(int from,int to,unsigned depth,unsigned *files,uint64_t *bytes){
    int held=fcntl(from,F_DUPFD_CLOEXEC,3);if(held<0)return -errno;
    DIR *dir=fdopendir(held);if(!dir){close(held);return -errno;}int rc=0;struct dirent *e;
    while(!rc&&(e=readdir(dir))){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        struct stat s;if(fstatat(from,e->d_name,&s,AT_SYMLINK_NOFOLLOW)){rc=-errno;break;}
        if(S_ISDIR(s.st_mode)){
            if(mkdirat(to,e->d_name,0700)){rc=-errno;break;}
            int a=openat(from,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),b=openat(to,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(a<0||b<0)rc=-errno;else rc=copy_peer_tree(a,b,depth+1,files,bytes);if(a>=0)close(a);if(b>=0)close(b);
        }else if(S_ISREG(s.st_mode)){
            ++*files;
            int a=openat(from,e->d_name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC),b=openat(to,e->d_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(a<0||b<0)rc=-errno;
            char data[4096];uint64_t copied=0;
            while(!rc){ssize_t n=read(a,data,sizeof(data));if(n<0&&errno==EINTR)continue;if(n<0){rc=-errno;break;}if(!n)break;
                size_t at=0;while(at<(size_t)n){ssize_t w=write(b,data+at,n-at);if(w<0&&errno==EINTR)continue;if(w<=0){rc=w?-errno:-EIO;break;}at+=(unsigned)w;}copied+=(unsigned)n;
            }
            if(a>=0)close(a);if(b>=0){if(!rc&&fsync(b))rc=-errno;close(b);}*bytes+=copied;
        }else rc=-EPERM;
    }
    closedir(dir);return rc;
}
static int radio_roots(const char *runtime,const char *session){
    char source[2048],target[2048];int rc=path(source,runtime,"peer-template"),a=-1,b=-1;
    if(!rc){a=directory(source);rc=path(target,session,"radio-data");}if(!rc){b=directory(target);if(a<0||b<0)rc=-errno;}
    unsigned files=0;uint64_t bytes=0;if(!rc)rc=copy_peer_tree(a,b,0,&files,&bytes);if(a>=0)close(a);if(b>=0)close(b);
    if(!rc)rc=path(target,runtime,"radio-root");if(!rc)rc=bind_mount(target,target,1);
    if(!rc)rc=path(source,session,"radio-data");if(!rc)rc=path(target,runtime,"radio-root/data");if(!rc)rc=bind_mount(source,target,0);
    /* Use the current player's RAM copy when present; never bake its address
     * into the seed. The empty mount point remains in the session directory. */
    if(!rc&&access("/dev/walkdroid-BT_Addr",R_OK)==0){
        rc=path(target,runtime,"radio-root/data/BT_Addr");
        if(!rc){int fd=open(target,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(fd<0)rc=-errno;else close(fd);}
        if(!rc)rc=bind_mount("/dev/walkdroid-BT_Addr",target,0);
    }
    if(!rc)rc=path(source,session,"tmp-radio");if(!rc)rc=path(target,runtime,"radio-root/tmp");if(!rc)rc=bind_mount(source,target,0);
    if(!rc)rc=path(source,runtime,"radio-root/empty-nvram");if(!rc)rc=path(target,runtime,"radio-root/data/nvram");if(!rc)rc=bind_mount(source,target,1);
    if(!rc)rc=path(target,runtime,"radio-root/dev/shm");
    if(!rc&&mount("walkdroid-bt-shm",target,"tmpfs",MS_NOSUID|MS_NODEV|MS_NOEXEC,"size=8388608,mode=0700"))rc=-errno;
    const char *nodes[]={"null","urandom","stpbt"};
    for(unsigned i=0;!rc&&i<3;++i){char name[80];rc=path(source,"/dev",nodes[i]);
        snprintf(name,sizeof(name),"radio-root/dev/%s",nodes[i]);if(!rc)rc=path(target,runtime,name);if(!rc)rc=bind_mount(source,target,0);
    }return rc;
}
static void receiver_child(const char *runtime,const char *session,int stop,int updates,uint64_t id){
    int rc=close_others(stop,updates);signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);
    char setup[2048];if(!rc)rc=path(setup,session,"setup.log");if(!rc){journal=open(setup,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(journal<0)rc=-errno;}
    if(!rc)rc=await_idle(stop);
    if(journal>=0)dprintf(journal,"BT_SETUP stage=idle result=%d\n",rc);
    if(!rc)rc=mounted_roots(runtime,session);if(journal>=0)dprintf(journal,"BT_SETUP stage=output-roots result=%d\n",rc);
    if(!rc)rc=radio_roots(runtime,session);if(journal>=0)dprintf(journal,"BT_SETUP stage=radio-roots result=%d\n",rc);
    char executable[2048],root[2048],dsp[2048],sink[2048],config[2048],dl[2048],sl[2048],native[2048],daemon[2048],log[2048];
    const char *tails[]={"sony-bt-live-check","radio-root","receiver-root","sink-root"};char *dest[]={executable,root,dsp,sink};
    for(unsigned i=0;!rc&&i<4;++i)rc=path(dest[i],runtime,tails[i]);
    const char *leaves[]={"private","dsp.log","sink.log","probe.log","daemon.log","controller.log"};char *paths[]={config,dl,sl,native,daemon,log};
    for(unsigned i=0;!rc&&i<6;++i)rc=path(paths[i],session,leaves[i]);
    int fd=-1;if(!rc){fd=open(log,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)rc=-errno;}
    if(!rc&&(dup2(fd,1)<0||dup2(fd,2)<0))rc=-errno;if(fd>=0)close(fd);
    if(!rc&&(fcntl(stop,F_SETFD,0)||fcntl(updates,F_SETFD,0)))rc=-errno;
    char control[24],report[24],nonce[24],library[2304];snprintf(control,sizeof(control),"%d",stop);snprintf(report,sizeof(report),"%d",updates);snprintf(nonce,sizeof(nonce),"%llx",(unsigned long long)id);
    int n=snprintf(library,sizeof(library),"LD_LIBRARY_PATH=%s/lib:/vendor/lib:/system/lib",runtime);if(n<0||(unsigned)n>=sizeof(library))rc=-ENAMETOOLONG;
    if(!rc){char *argv[]={executable,verify_muted?"--verify-muted":"--play",root,dsp,sink,config,daemon,native,dl,sl,"--broker",control,report,nonce,NULL};char *env[]={library,"PATH=/system/bin",NULL};execve(executable,argv,env);rc=-errno;}
    struct wdr_reply r={.magic=WDR_MAGIC,.version=1,.phase=WDR_OFFLINE,.error=rc,.session=id,.flags=WDR_CLEAN|WDR_REFUSED|WDR_CHILD_TERMINAL};int sent=update(updates,&r,1);_exit(sent?1:0);
}
