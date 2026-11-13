#ifndef WALKDROID_BT_COMMON_CALLBACKS_H
#define WALKDROID_BT_COMMON_CALLBACKS_H
/* Common Sony Bluetooth callback definitions for a worker process. */
#define _GNU_SOURCE
#include "sony_audio_bt_common_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
static unsigned counts[22],last_status;
static void status(unsigned v){__atomic_add_fetch(&counts[0],1,__ATOMIC_RELAXED);__atomic_store_n(&last_status,v,__ATOMIC_RELEASE);fprintf(stderr,"COMMON_STATUS raw=%u\n",v);}
#define IGNORE(v) (void)(v)
static void searched(void*a,unsigned b,unsigned char c,unsigned char*d){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);++counts[1];}
static void passkey(void*a,unsigned b,unsigned char c,unsigned char*d){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);++counts[2];}
static void numeric(void*a,unsigned b,unsigned char c,unsigned char*d,unsigned e){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);IGNORE(e);++counts[3];}
static void paired(unsigned a,void*b,void*c,unsigned d,unsigned char e,unsigned char*f){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);IGNORE(e);IGNORE(f);++counts[4];}
static void iap(void*a,_Bool b){IGNORE(a);IGNORE(b);++counts[5];}
static void device(void*a,void*b,unsigned c,unsigned char d,unsigned char*e,unsigned f){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);IGNORE(e);IGNORE(f);++counts[6];}
static void paired_done(void){++counts[7];}
static void deleted(void*a){IGNORE(a);++counts[8];}
static void deleted_done(void){++counts[9];}
static void my_device(void*a,unsigned b,unsigned char c,unsigned char*d,unsigned char e,unsigned char*f,unsigned g,void*h){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);IGNORE(e);IGNORE(f);IGNORE(g);IGNORE(h);++counts[10];}
static void error(unsigned char a){++counts[11];fprintf(stderr,"COMMON_ERROR raw=%u\n",a);}
static void disconnect(void*a,_Bool b){IGNORE(a);IGNORE(b);++counts[12];}
static void reset(unsigned a){IGNORE(a);++counts[13];}
static void coex(unsigned short a,unsigned short b){IGNORE(a);IGNORE(b);++counts[14];}
static void remote(void*a,unsigned char b,void*c){IGNORE(a);IGNORE(b);IGNORE(c);++counts[15];}
static void di(void*a,unsigned short b,unsigned short c,unsigned short d){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);++counts[16];}
static void rssi(int a){IGNORE(a);++counts[17];}
static void sw(void){++counts[18];}
static void acl(void*a,unsigned b){IGNORE(a);IGNORE(b);++counts[19];}
static void ssp(void*a,unsigned char b,unsigned char*c,unsigned d,unsigned e,unsigned f){IGNORE(a);IGNORE(b);IGNORE(c);IGNORE(d);IGNORE(e);IGNORE(f);++counts[20];}
static void classes(void*a,unsigned char b,unsigned short*c){IGNORE(a);IGNORE(b);IGNORE(c);++counts[21];}
static struct sony_bt_common_callbacks cb={sizeof(cb),status,searched,passkey,numeric,paired,iap,device,paired_done,deleted,deleted_done,my_device,error,disconnect,reset,coex,remote,di,rssi,sw,acl,ssp,classes};
static void *symbol(void*h,const char*n){void*p=dlsym(h,n);if(!p){fprintf(stderr,"missing %s: %s\n",n,dlerror());exit(70);}return p;}

#endif
