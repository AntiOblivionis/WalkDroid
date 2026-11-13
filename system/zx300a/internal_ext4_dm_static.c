/* Fixed bounded block ranges inside the authorized music partition only.
 * No formatting, no partition-table writes, no regular-file backing. */
#include <stddef.h>
#include <linux/fs.h>
#include <linux/dm-ioctl.h>
typedef unsigned long long u64;
static long call3(long nr,long a,long b,long c){register long r0 __asm__("r0")=a,r1 __asm__("r1")=b,r2 __asm__("r2")=c,r7 __asm__("r7")=nr;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory","cc");return r0;}
static long op(const char*p,long mode){return call3(5,(long)p,mode|02000000,0);}
static void cl(long f){call3(6,f,0,0);}
static long io(long f,long cmd,void*p){return call3(54,f,cmd,(long)p);}
static void cp(char*d,const char*s){while((*d++=*s++));}
static void params(char *out,const char *device,const char *offset){
 while((*out=*device++))++out;
 *out++=' ';cp(out,offset);
}
static union {u64 align; unsigned char b[4096];} buffer;
static struct dm_ioctl* init(const char*name,unsigned flags){
 for(unsigned i=0;i<sizeof(buffer.b);i++)buffer.b[i]=0;
 struct dm_ioctl*d=(void*)buffer.b;d->version[0]=4;d->data_size=sizeof(buffer.b);d->data_start=sizeof(*d);d->flags=flags;cp(d->name,name);return d;
}
static int map(long ctl,const char*name,const char*node,u64 sectors,const char*params,int readonly){
 struct dm_ioctl*d=init(name,0);if(io(ctl,DM_DEV_CREATE,d))return 10;
 unsigned encoded=(unsigned)d->dev;
 d=init(name,readonly?DM_READONLY_FLAG:0);d->target_count=1;
 struct dm_target_spec*t=(void*)(buffer.b+d->data_start);t->length=sectors;cp(t->target_type,"linear");
 char*p=(void*)(t+1);cp(p,params);unsigned n=0;while(params[n])n++;
 t->next=(sizeof(*t)+n+1+7)&~7;
 if(io(ctl,DM_TABLE_LOAD,d))goto fail;
 d=init(name,0);if(io(ctl,DM_DEV_SUSPEND,d))goto fail;
 if(call3(14,(long)node,0060600,encoded))goto fail;
 return 0;
fail:
 d=init(name,0);io(ctl,DM_DEV_REMOVE,d);return 11;
}
__attribute__((used)) int main(void){
 char device[32],music[48],system[48],data[48];
 long source=op("/sys/class/block/mmcblk0p29/dev",0);
 if(source<0)return 2;
 long n=call3(3,source,(long)device,sizeof(device)-1);cl(source);
 if(n<=0)return 2;
 while(n>0&&(device[n-1]=='\n'||device[n-1]=='\r'))--n;
 device[n]=0;
 params(music,device,"0");params(system,device,"8388608");params(data,device,"12582912");
 /* A preexisting control node is acceptable only because bootstrap creates /dev. */
 long ctl=op("/dev/device-mapper",2);if(ctl<0)return 3;
 int r=map(ctl,"zx9_music_fat","/dev/block/zx9-music-fat",8388608ULL,music,0);
#ifndef ZX9_MUSIC_ONLY
 if(!r)r=map(ctl,"zx9_system_ext4","/dev/block/zx9-system-ext4",4194304ULL,system,1);
 if(!r)r=map(ctl,"zx9_data_ext4","/dev/block/zx9-data-ext4",13376512ULL,data,0);
#endif
 cl(ctl);
 if(!r){const char msg[]="ZX9_INTERNAL_DIRECT_EXT4_MAPPINGS_READY\n";call3(4,1,(long)msg,sizeof(msg)-1);}
 return r;
}
__attribute__((naked,noreturn)) void _start(void){__asm__ volatile("bl main\nmov r7,#1\nsvc 0\nb .\n");}
