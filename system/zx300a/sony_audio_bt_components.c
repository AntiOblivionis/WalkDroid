#include "sony_audio_bt_components.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* libBtMw retains callback pointers. Keep fallback tables alive until the
 * worker exits, including when a later registration fails part-way through. */
struct fallback_tables {
    struct sony_bt_snk_callbacks snk;
    struct sony_bt_avrcp_snk_callbacks avrcp_snk;
    struct sony_bt_tx_callbacks src;
    struct sony_bt_tx_avrcp_callbacks avrcp_src;
    struct fallback_tables *next;
};
static struct fallback_tables *kept_tables;

#define LOAD(table,member,name) do { \
    void *symbol=dlsym(comp,name); \
    if(!symbol)goto missing; \
    memcpy(&(table).member,&symbol,sizeof((table).member)); \
} while(0)

static int prepare(void *mw,void *comp,const struct sony_bt_snk_callbacks *snk,
    const struct sony_bt_avrcp_snk_callbacks *avrcp,
    const struct sony_bt_tx_callbacks *source,
    const struct sony_bt_tx_avrcp_callbacks *source_avrcp,unsigned *mask){
    if(!mw||!comp||!mask)return -EINVAL;
    *mask=0;
    struct fallback_tables *t=calloc(1,sizeof(*t));if(!t)return -ENOMEM;
    if(!snk){
        t->snk.bytes=sizeof(t->snk);
        LOAD(t->snk,connection_status,"BtAvSnkCompIfCallbackNotifyConnectionStatus");
        LOAD(t->snk,connection_information,"BtAvSnkCompIfCallbackNotifyConnectInformation");
        LOAD(t->snk,sound_status,"BtAvSnkCompIfCallbackNotifySoundStatus");
        LOAD(t->snk,pcm,"BtAvSnkCompIfCallbackNotifySendData");
        LOAD(t->snk,drop_buffer,"BtAvSnkCompIfCallbackNotifyDropBuffer");
        LOAD(t->snk,multi_status,"BtAvSnkCompIfCallbackNotifyBtMultiStatus");
        LOAD(t->snk,bitrate,"BtAvSnkCompIfCallbackNotifyGetBitrate");
        LOAD(t->snk,audio_state,"BtAvSnkCompIfCallbackNotifyAudioState");
        LOAD(t->snk,get_player_state,"BtAvSnkCompIfCallbackNotifyGetPlayerState");
        snk=&t->snk;
    }
    if(!avrcp){
        t->avrcp_snk.bytes=sizeof(t->avrcp_snk);
        LOAD(t->avrcp_snk,connection,"BtAvSnkCompIfCallbackNotifyAvrcpConnectionStatus");
        LOAD(t->avrcp_snk,play_status,"BtAvSnkCompIfCallbackNotifyAvrcpGetPlayStatus");
        LOAD(t->avrcp_snk,volume_down,"BtAvSnkCompIfCallbackNotifyAvrcpVolumeDown");
        LOAD(t->avrcp_snk,volume_up,"BtAvSnkCompIfCallbackNotifyAvrcpVolumeUp");
        LOAD(t->avrcp_snk,set_volume,"BtAvSnkCompIfCallbackNotifyAvrcpSetVolume");
        LOAD(t->avrcp_snk,remote_version,"BtAvSnkCompIfCallbackNotifyAvrcpRemoteVersion");
        LOAD(t->avrcp_snk,track_number,"BtAvSnkCompIfCallbackNotifyAvrcpTrackNumber");
        LOAD(t->avrcp_snk,media,"BtAvSnkCompIfCallbackNotifyAvrcpReceiveMedia");
        LOAD(t->avrcp_snk,error,"BtAvSnkCompIfCallbackNotifyAvrcpError");
        LOAD(t->avrcp_snk,register_absolute_volume,"BtAvSnkCompIfCallbackNotifyRegisterForAbsVolume");
        avrcp=&t->avrcp_snk;
    }
    if(!source){
        t->src.bytes=sizeof(t->src);
        LOAD(t->src,connection_status,"BtAvSrcCompIfCallbackNotifyConnectionStatus");
        LOAD(t->src,connection_information,"BtAvSrcCompIfCallbackNotifyConnectInformation");
        LOAD(t->src,sound_status,"BtAvSrcCompIfCallbackNotifySoundStatus");
        LOAD(t->src,sep_information,"BtAvSrcCompIfCallbackNotifySepInformation");
        LOAD(t->src,pcm_preferred_size,"BtAvSrcCompIfCallbackNotifyPcmPreferredSize");
        source=&t->src;
    }
    if(!source_avrcp){
        t->avrcp_src.bytes=sizeof(t->avrcp_src);
        LOAD(t->avrcp_src,connection_status,"BtAvSrcCompIfCallbackNotifyAvrcpConnectionStatus");
        LOAD(t->avrcp_src,receive_command,"BtAvSrcCompIfCallbackNotifyAvrcpReceiveCommand");
        LOAD(t->avrcp_src,get_play_status,"BtAvSrcCompIfCallbackNotifyAvrcpGetPlayStatus");
        LOAD(t->avrcp_src,get_media_attribute,"BtAvSrcCompIfCallbackNotifyAvrcpGetMediaAttribute");
        LOAD(t->avrcp_src,current_volume,"BtAvSrcCompIfCallbackNotifyAvrcpCurrentVolume");
        LOAD(t->avrcp_src,remote_version,"BtAvSrcCompIfCallbackNotifyAvrcpRemoteVersion");
        source_avrcp=&t->avrcp_src;
    }
    const void *tables[]={snk,avrcp,source,source_avrcp};
    const char *names[]={"BtMwAvSnkInit","BtMwAvrcpSnkInit","BtMwAvSrcInit","BtMwAvrcpSrcInit"};
    int (*init[4])(const void *);
    for(unsigned i=0;i<4;++i){void *symbol=dlsym(mw,names[i]);if(!symbol)goto missing;memcpy(&init[i],&symbol,sizeof(init[i]));}
    do{t->next=__atomic_load_n(&kept_tables,__ATOMIC_ACQUIRE);}
    while(!__atomic_compare_exchange_n(&kept_tables,&t->next,t,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED));
    for(unsigned i=0;i<4;++i){if(init[i](tables[i]))return -EIO;*mask|=1u<<i;}
    return 0;
missing:
    free(t);return -ENOSYS;
}
#undef LOAD

int sony_bt_components_prepare(void *mw,void *comp,const struct sony_bt_snk_callbacks *snk,unsigned *mask){
    return prepare(mw,comp,snk,NULL,NULL,NULL,mask);
}
int sony_bt_components_prepare_receiver(void *mw,void *comp,const struct sony_bt_snk_callbacks *snk,
    const struct sony_bt_avrcp_snk_callbacks *avrcp,unsigned *mask){
    return prepare(mw,comp,snk,avrcp,NULL,NULL,mask);
}
int sony_bt_components_prepare_transmitter(void *mw,void *comp,const struct sony_bt_tx_callbacks *source,
    const struct sony_bt_tx_avrcp_callbacks *avrcp,unsigned *mask){
    if(!source||!avrcp)return -EINVAL;
    return prepare(mw,comp,NULL,NULL,source,avrcp,mask);
}
