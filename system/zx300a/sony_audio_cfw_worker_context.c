#include "sony_audio_cfw_worker_context.h"
#include "sony_audio_cfw_signature.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static int decimal(const char *s,unsigned int *out){
    if(!s||s[0]<'0'||s[0]>'9')return -1;
    errno=0;char *end;unsigned long value=strtoul(s,&end,10);
    if(*end||errno==ERANGE)return -1;
#if ULONG_MAX > UINT_MAX
    if(value>UINT_MAX)return -1;
#endif
    *out=(unsigned int)value;return 0;
}
int sony_cfw_worker_context_args(int *argc,char **argv){
    if(!argc||!argv||*argc<1)return -1;
    sony_audio_feature_config_unbind();
    int at=-1;
    for(int i=1;i<*argc;++i){
        if(!argv[i])return -1;
        if(!strcmp(argv[i],"--cfw-context")){if(at>=0)return -1;at=i;}
    }
    if(at<0)return 0;
    if(at!=*argc-3)return -1;
    unsigned int signature,ship;
    if(decimal(argv[at+1],&signature)||decimal(argv[at+2],&ship))return -1;
    struct sony_audio_feature_context context;
    if(sony_cfw_signature_context(signature,ship,&context)||sony_audio_feature_config_bind(&context))return -1;
    *argc=at;return 1;
}
