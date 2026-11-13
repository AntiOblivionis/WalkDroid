/* Sony original-stack receiver worker, used by the live and epoch adapters. */
#include "sony_audio_bt_common_callbacks.h"
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <time.h>
#include "sony_audio_bt_components.h"
static unsigned my_address_seen;
static void my_info(void*a,unsigned b,unsigned char c,unsigned char*d,unsigned char e,unsigned char*f,unsigned g,void*h){
    my_device(a,b,c,d,e,f,g,h);if(a)__atomic_store_n(&my_address_seen,1,__ATOMIC_RELEASE);
}
static unsigned current_status(void){return __atomic_load_n(&last_status,__ATOMIC_ACQUIRE);}
static int wait_rf(int (*is_on)(void),int on,unsigned ms){
    for(unsigned n=0;n<ms/10;++n){unsigned state=current_status();if(on?(state>=2&&state<=5&&is_on()==1):(state==7&&is_on()==0))return 0;usleep(10000);}return -1;
}

#include "sony_audio_bt_middleware_abi.h"
#include "sony_audio_bt_journal.h"
#include "sony_audio_bt_middleware_format.h"
static struct sony_bt_journal *journal;
/* Persistent mode does not retain a raw PCM recording. The bounded WBE1
 * journal remains mandatory for the separate qualification/capture mode. */
static unsigned persistent_mode;
static int audit_event(unsigned kind,const void *data,unsigned bytes){return persistent_mode?0:sony_bt_journal_append(journal,kind,data,bytes);}
static int audit_error(void){return persistent_mode?0:sony_bt_journal_error(journal);}
static unsigned known_connection,format_known,sink_ready,player_queries;
static int player_ready_rc=-99,player_closed_rc=-99;
#ifdef SONY_BT_WITH_LIVE_WIRE
#include "sony_audio_bt_live_source.h"
static struct sony_bt_live_source live_source;
static unsigned live_mode;
static int live_finish_rc=-99;
#endif
/* Diagnostic only. Original libbtstd OS_Log19979 is void(uint8_t,fmt,...).
 * Exported by this bounded worker so the private original libraries use it.
 * No HCI, pairing, address, key, or arbitrary general-purpose logging. Limit
 * repeated audio format strings before formatting; no change to native RF or
 * decoder return values. This is not part of the production callback path. */
#include <stdarg.h>
struct audio_log_slot {const char *format;unsigned count;};
static struct audio_log_slot audio_log_slots[96];
static unsigned audio_log_lock,audio_log_emitted;
void OS_Log(unsigned char level,const char *format,...){
    if(!format||strlen(format)>512)return;
    if(!strcasestr(format,"a2dp")&&!strcasestr(format,"ldac")&&!strcasestr(format,"decod")&&!strcasestr(format,"audio module"))return;
    if(strcasestr(format,"key")||strcasestr(format,"pair")||strcasestr(format,"pin")||strcasestr(format,"address")||strcasestr(format,"bdaddr"))return;
    unsigned acquired=0;for(unsigned n=0;n<32;++n){unsigned zero=0;if(__atomic_compare_exchange_n(&audio_log_lock,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){acquired=1;break;}}if(!acquired)return;
    unsigned accepted=0;for(unsigned n=0;n<96;++n){if(!audio_log_slots[n].format)audio_log_slots[n].format=format;if(audio_log_slots[n].format==format){if(audio_log_slots[n].count++<4){accepted=1;++audio_log_emitted;}break;}}
    __atomic_store_n(&audio_log_lock,0,__ATOMIC_RELEASE);if(!accepted)return;
    char text[768];va_list args;va_start(args,format);int bytes=vsnprintf(text,sizeof(text),format,args);va_end(args);if(bytes<0)return;
    fprintf(stderr,"NATIVE_AUDIO_LOG level=%u %s%s",level,text,bytes&&text[strlen(text)-1]=='\n'?"":"\n");
}
#include <signal.h>
static unsigned peer_paired,peer_in_db,numeric_seen,invalid_pcm,pcm_calls,pcm_bytes,pcm_nonzero,sound_events;
static int (*reply_numeric)(void *,_Bool);
static void known_numeric(void*a,unsigned b,unsigned char c,unsigned char*d,unsigned key){
    (void)b;(void)c;(void)d;(void)key;__atomic_add_fetch(&numeric_seen,1,__ATOMIC_RELAXED);
    int rc=reply_numeric(a,1);fprintf(stderr,"PAIR_NUMERIC reply_rc=%d key_not_logged=1\n",rc);
}
static void known_pair(unsigned result,void*address,void*key,unsigned type,unsigned char bytes,unsigned char*name){
    (void)address;(void)key;(void)type;(void)bytes;(void)name;
    fprintf(stderr,"PAIR_COMPLETE status_raw=%u key_not_logged=1\n",result);
    if(result==0)__atomic_store_n(&peer_paired,1,__ATOMIC_RELEASE);
}
static void known_database(void*address,void*key,unsigned type,unsigned char bytes,unsigned char*name,unsigned last){
    (void)address;(void)key;(void)type;(void)bytes;(void)name;(void)last;__atomic_store_n(&peer_in_db,1,__ATOMIC_RELEASE);
}
static void snk_connection(unsigned raw){audit_event(SONY_BT_EVENT_CONNECTION,&raw,4);fprintf(stderr,"SNK_CONNECTION state_raw=%u\n",raw);}
static void snk_peer(unsigned char*a,unsigned char bytes,unsigned char*name){(void)bytes;(void)name;if(a)audit_event(SONY_BT_EVENT_PEER,a,6);__atomic_store_n(&known_connection,1,__ATOMIC_RELEASE);fprintf(stderr,"SNK_PEER received\n");}
static void snk_sound(unsigned state,unsigned codec,unsigned rate,unsigned channel,_Bool flag){struct sony_bt_middleware_sound raw={state,codec,rate,channel,(unsigned)flag};struct sony_bt_middleware_format f;int mapped=sony_bt_middleware_format(&raw,&f);
#ifdef SONY_BT_WITH_LIVE_WIRE
if(live_mode)sony_bt_live_source_format(&live_source,&raw);
#endif
if(!mapped&&state==1)__atomic_store_n(&format_known,1,__ATOMIC_RELEASE);fprintf(stderr,"SNK_FORMAT mapped_rc=%d receiver_codec=%u service_codec=%u\n",mapped,mapped?0:f.pcm.codec,mapped?0:f.service_codec);unsigned words[5]={state,codec,rate,channel,(unsigned)flag};audit_event(SONY_BT_EVENT_SOUND,words,sizeof(words));__atomic_add_fetch(&sound_events,1,__ATOMIC_RELAXED);fprintf(stderr,"SNK_SOUND state_raw=%u codec=%u rate=%u channel=%u flag=%u\n",state,codec,rate,channel,(unsigned)flag);}
static void snk_pcm(unsigned short n,unsigned char*p){
    audit_event(SONY_BT_EVENT_PCM,p,n);
#ifdef SONY_BT_WITH_LIVE_WIRE
    if(live_mode)sony_bt_live_source_pcm(&live_source,n,p);
#endif
    if(n&&!p){__atomic_store_n(&invalid_pcm,1,__ATOMIC_RELEASE);return;}unsigned nonzero=0;if(!persistent_mode)for(unsigned i=0;i<n;++i)nonzero+=p[i]!=0;
    __atomic_add_fetch(&pcm_calls,1,__ATOMIC_RELAXED);__atomic_add_fetch(&pcm_bytes,n,__ATOMIC_RELAXED);__atomic_add_fetch(&pcm_nonzero,nonzero,__ATOMIC_RELAXED);
}
static void snk_drop(void){audit_event(SONY_BT_EVENT_DROP,NULL,0);fprintf(stderr,"SNK_DROP\n");
#ifdef SONY_BT_WITH_LIVE_WIRE
if(live_mode)sony_bt_live_source_error(&live_source,-ENOBUFS);
#endif
}
static void snk_raw(unsigned raw){(void)raw;}
static void snk_audio(unsigned raw){audit_event(SONY_BT_EVENT_AUDIO_STATE,&raw,4);fprintf(stderr,"SNK_AUDIO state_raw=%u\n",raw);}
static void snk_get_player_state(unsigned *state){unsigned value=__atomic_load_n(&sink_ready,__ATOMIC_ACQUIRE)?1:0;
#ifdef SONY_BT_WITH_LIVE_WIRE
if(live_mode)value=sony_bt_live_source_ready(&live_source);
#endif
if(state)*state=value;__atomic_add_fetch(&player_queries,1,__ATOMIC_RELAXED);}
static struct sony_bt_snk_callbacks snk={sizeof(snk),snk_connection,snk_peer,snk_sound,snk_pcm,snk_drop,snk_raw,snk_raw,snk_audio,snk_get_player_state};
#ifdef SONY_BT_WITH_LIVE_WIRE
static unsigned avrcp_up,avrcp_down,avrcp_absolute,avrcp_register,avrcp_ignored_peer;
static unsigned avrcp_count(const unsigned *value){return __atomic_load_n(value,__ATOMIC_ACQUIRE);}
static void avrcp_relative(int step){
    if(!live_mode){__atomic_add_fetch(&avrcp_ignored_peer,1,__ATOMIC_RELAXED);return;}
    if(step==1)__atomic_add_fetch(&avrcp_up,1,__ATOMIC_RELAXED);else __atomic_add_fetch(&avrcp_down,1,__ATOMIC_RELAXED);
    (void)sony_bt_live_source_volume_step(&live_source,step);
}
static void avrcp_volume_up(void){avrcp_relative(1);}
static void avrcp_volume_down(void){avrcp_relative(-1);}
static void avrcp_set_volume(unsigned char value){(void)value;__atomic_add_fetch(&avrcp_absolute,1,__ATOMIC_RELAXED);}
static void avrcp_register_volume(void){__atomic_add_fetch(&avrcp_register,1,__ATOMIC_RELAXED);}
static void avrcp_connection(unsigned value){fprintf(stderr,"AVRCP_CONNECTION raw=%u\n",value);}
static void avrcp_play_status(unsigned a,unsigned b,unsigned c){(void)a;(void)b;(void)c;}
static void avrcp_version(unsigned short value){(void)value;}
static void avrcp_number(unsigned short n,unsigned char *p){(void)n;(void)p;}
static void avrcp_media(unsigned a,unsigned short n,unsigned char *p){(void)a;(void)n;(void)p;}
static void avrcp_error(int value){fprintf(stderr,"AVRCP_ERROR raw=%d\n",value);}
static struct sony_bt_avrcp_snk_callbacks avrcp={sizeof(avrcp),avrcp_connection,avrcp_play_status,avrcp_volume_down,avrcp_volume_up,avrcp_set_volume,avrcp_version,avrcp_number,avrcp_media,avrcp_error,avrcp_register_volume};
#endif

#ifndef SONY_BT_SOURCE_ENTRY
#define SONY_BT_SOURCE_ENTRY main
#endif
int SONY_BT_SOURCE_ENTRY(int argc,char **argv){
    int rf=0;
#ifdef SONY_BT_WITH_LIVE_WIRE
    persistent_mode=argc==4&&!strcmp(argv[1],"--rf-persistent");
    live_mode=argc==4&&(!strcmp(argv[1],"--rf-live")||persistent_mode);
    if(live_mode){char *end;long fd=strtol(argv[2],&end,10);if(*end||fd<0||fd>0x7fffffffL)return 64;uint64_t cookie=strtoull(argv[3],&end,16);if(*end||sony_bt_live_source_init(&live_source,(int)fd,getppid(),cookie,1))return 76;rf=1;}
    else
#endif
    {if(argc!=2||(strcmp(argv[1],"--software-init")&&strcmp(argv[1],"--rf-stream")))return 64;rf=!strcmp(argv[1],"--rf-stream");}
    setvbuf(stdout,NULL,_IONBF,0);
    alarm(persistent_mode?0:70);
    if(!persistent_mode&&sony_bt_journal_create(&journal,8u*1024*1024))return 74;
    void *mw=dlopen("libBtMw.so",RTLD_NOW|RTLD_LOCAL);if(!mw)return 66;
    int (*init)(const struct sony_bt_common_callbacks*)=symbol(mw,"BtMwCommonInit");
    int (*deinit)(void)=symbol(mw,"BtMwCommonDeinit");
    int (*is_on)(void)=symbol(mw,"btmwCommonIsRfOn");
    int (*request)(unsigned)=symbol(mw,"BtMwCommonRequestRfOnOff");
    int (*get_info)(void)=symbol(mw,"BtMwCommonRequestGetMyDeviceInfo");
    cb.my_device=my_info;cb.numeric_comparison=known_numeric;cb.pairing_complete=known_pair;cb.paired_device=known_database;reply_numeric=symbol(mw,"BtMwCommonRequestNumericComparison");int init_rc=init(&cb),on_rc=-99,off_rc=-99,on_seen=0,off_seen=0,info_rc=-99,deinit_rc=-99;unsigned components_mask=0;
    fprintf(stderr,"NATIVE_INIT rc=%d state=%u\n",init_rc,current_status());
    if(init_rc==0&&current_status()==7){
        void *components=dlopen("libBtCompIf.so",RTLD_NOW|RTLD_LOCAL);
        if(!components)return 73;
#ifdef SONY_BT_WITH_LIVE_WIRE
        if(sony_bt_components_prepare_receiver(mw,components,&snk,&avrcp,&components_mask))return 73;
#else
        if(sony_bt_components_prepare(mw,components,&snk,&components_mask))return 73;
#endif
        printf("NATIVE_COMPONENTS prepared_mask=%u\n",components_mask);
        off_seen=1;
        if(rf){
            off_seen=0;on_rc=request(1);fprintf(stderr,"RF_ON_REQUEST rc=%d\n",on_rc);
            if(!on_rc&&!wait_rf(is_on,1,10000)){
                on_seen=1;fprintf(stderr,"RF_ON_OBSERVED state=%u\n",current_status());
                info_rc=get_info();
                for(unsigned n=0;n<200&&!__atomic_load_n(&my_address_seen,__ATOMIC_ACQUIRE);++n)usleep(10000);

                int (*name_set)(unsigned char,unsigned char*)=symbol(mw,"BtMwCommonRequestSetMyDeviceName");
                int (*wait_start)(void)=symbol(mw,"BtMwAvSnkRequestConnectWaitStart"),(*wait_stop)(void)=symbol(mw,"BtMwAvSnkRequestConnectWaitStop");
                int (*discoverable)(unsigned)=symbol(mw,"BtMwCommonRequestDiscoverable");
                unsigned char label[]="Sony Walkman ZX300A";
                int nr=name_set(sizeof(label)-1,label),wr=wait_start(),dr=discoverable(0);
                int (*get_paired)(void)=symbol(mw,"BtMwCommonRequestGetPairedDeviceInfo");int gr=get_paired();
                for(unsigned n=0;n<200&&!__atomic_load_n(&peer_in_db,__ATOMIC_ACQUIRE);++n)usleep(10000);
                int ready=!nr&&!wr&&!dr&&!gr&&__atomic_load_n(&peer_in_db,__ATOMIC_ACQUIRE);
                fprintf(stderr,"STREAM_READY name_rc=%d wait_rc=%d discoverable_rc=%d database_rc=%d expected_db_peer=%u\n",nr,wr,dr,gr,__atomic_load_n(&peer_in_db,__ATOMIC_ACQUIRE));
                /* Passive native sink: these RequestOpen/Start exports send
                 * AVDTP commands to the peer. They are not a local PCM gate.
                 * The exact assigned source owns stream start/stop here. */
                int (*set_player)(unsigned)=symbol(mw,"BtMwAvSnkRequestLdacNotifyAppState");
#ifdef SONY_BT_WITH_LIVE_WIRE
                if(persistent_mode&&ready&&sony_bt_live_source_listening(&live_source))ready=0;
#endif
                if(ready)for(uint64_t n=0;(persistent_mode||n<3200)&&!__atomic_load_n(&invalid_pcm,__ATOMIC_ACQUIRE)&&!audit_error();++n){
#ifdef SONY_BT_WITH_LIVE_WIRE
                    if(live_mode){
                        if(__atomic_load_n(&live_source.error,__ATOMIC_ACQUIRE))break;
                        /* STOP is a mode command, valid even without a peer
                         * or FORMAT. READY before start remains a failure. */
                        int command=sony_bt_live_source_poll(&live_source);if(command<0||command==2)break;
                        if(command==1){player_ready_rc=set_player(1);fprintf(stderr,"LIVE_DECODER_READY rc=%d\n",player_ready_rc);if(player_ready_rc)break;}
                        if(!live_source.started&&__atomic_load_n(&known_connection,__ATOMIC_ACQUIRE)&&__atomic_load_n(&format_known,__ATOMIC_ACQUIRE)){
                            int rc=sony_bt_live_source_start(&live_source);fprintf(stderr,"LIVE_FORMAT_SENT rc=%d\n",rc);if(rc)break;
                        }
                    }else
#endif
                    if(player_ready_rc==-99&&__atomic_load_n(&known_connection,__ATOMIC_ACQUIRE)&&__atomic_load_n(&format_known,__ATOMIC_ACQUIRE)){
                        /* The bounded prefaulted journal is already ready.
                         * Production must use real downstream readiness. */
                        __atomic_store_n(&sink_ready,1,__ATOMIC_RELEASE);player_ready_rc=set_player(1);
                        fprintf(stderr,"STREAM_PLAYER_READY rc=%d\n",player_ready_rc);
                    }
                    if(!persistent_mode&&!access("/data/STREAM_STOP",F_OK))break;
                    usleep(10000);
                }
                __atomic_store_n(&sink_ready,0,__ATOMIC_RELEASE);
#ifdef SONY_BT_WITH_LIVE_WIRE
                if(live_mode)sony_bt_live_source_stop_begin(&live_source);
#endif
                if(player_ready_rc!=-99)player_closed_rc=set_player(0);
#ifdef SONY_BT_WITH_LIVE_WIRE
                if(live_mode){live_finish_rc=sony_bt_live_source_finish(&live_source);for(unsigned n=0;live_finish_rc==-EAGAIN&&n<400;++n){usleep(10000);live_finish_rc=sony_bt_live_source_finish(&live_source);}}
#endif
                fprintf(stderr,"STREAM_INPUT_CLOSED passive_sink=1 player_closed_rc=%d\n",player_closed_rc);
                fprintf(stderr,"PAIR_WAIT_STOP rc=%d discoverable_off_rc=%d\n",wait_stop(),discoverable(0));

                off_rc=request(0);fprintf(stderr,"RF_OFF_REQUEST rc=%d\n",off_rc);
                if(!off_rc&&!wait_rf(is_on,0,10000))off_seen=1;
            }
        }
        /* On uncertainty the outer owner remains ACTIVE. Deinit requests
         * native shutdown, never claims its return alone is RF-off proof. */
        deinit_rc=deinit();fprintf(stderr,"NATIVE_DEINIT rc=%d off_seen=%d\n",deinit_rc,off_seen);
    }
    int cleanup_ok=init_rc==0&&deinit_rc==0&&off_seen&&(!rf||(on_seen&&info_rc==0&&__atomic_load_n(&my_address_seen,__ATOMIC_ACQUIRE)));
    int journal_rc=persistent_mode?0:sony_bt_journal_close(journal);for(unsigned n=0;journal_rc==-EAGAIN&&n<100;++n){usleep(10000);journal_rc=sony_bt_journal_close(journal);}
    const void *journal_data=NULL;unsigned journal_bytes=0;if(!persistent_mode&&!journal_rc)journal_rc=sony_bt_journal_view(journal,&journal_data,&journal_bytes);
    if(!persistent_mode&&!journal_rc){
        int fd=open("/data/bt-events.wbe1",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)journal_rc=-errno;else{unsigned at=0;while(at<journal_bytes){ssize_t n=write(fd,(const unsigned char *)journal_data+at,journal_bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0){journal_rc=n<0?-errno:-EIO;break;}at+=(unsigned)n;}if(close(fd)&&!journal_rc)journal_rc=-errno;}
    }
    /* Keep journal and callback table alive through process exit. Closing
     * recording is not evidence that native callback registration ended. */
    int transport_ok=1;
#ifdef SONY_BT_WITH_LIVE_WIRE
    if(live_mode)transport_ok=!__atomic_load_n(&live_source.error,__ATOMIC_ACQUIRE)&&!live_finish_rc&&live_source.sealed&&(persistent_mode||live_source.frames>0)&&live_source.frames==live_source.offered_frames&&live_source.bytes==live_source.offered_bytes&&live_source.packets==live_source.offered_packets&&live_source.volume_offered==live_source.volume_sent;
#endif
    int pair_ok=!rf||(__atomic_load_n(&peer_in_db,__ATOMIC_ACQUIRE)&&__atomic_load_n(&known_connection,__ATOMIC_ACQUIRE)&&__atomic_load_n(&pcm_bytes,__ATOMIC_ACQUIRE)>0&&__atomic_load_n(&sound_events,__ATOMIC_ACQUIRE)>0&&!__atomic_load_n(&invalid_pcm,__ATOMIC_ACQUIRE)&&!journal_rc&&player_ready_rc==0&&player_closed_rc==0&&transport_ok);
#ifdef SONY_BT_WITH_LIVE_WIRE
    if(persistent_mode)pair_ok=live_source.listening&&live_source.stop_requested&&!__atomic_load_n(&invalid_pcm,__ATOMIC_ACQUIRE)&&!journal_rc&&transport_ok&&(player_ready_rc==-99||(!player_ready_rc&&!player_closed_rc));
#endif
    int passed=pair_ok&&cleanup_ok;
    unsigned total=0;for(unsigned n=0;n<22;++n)total+=__atomic_load_n(&counts[n],__ATOMIC_RELAXED);
    printf("{\"native_stack_result\":true,\"passed\":%s,\"rf_requested\":%s,\"components_mask\":%u,\"init_rc\":%d,\"on_rc\":%d,\"off_rc\":%d,\"on_seen\":%d,\"off_seen\":%d,\"info_rc\":%d,\"address_seen\":%u,\"deinit_rc\":%d,\"callbacks\":%u,\"last_state_raw\":%u,\"pairing_capable\":true,\"audio_started\":false}\n",passed?"true":"false",rf?"true":"false",components_mask,init_rc,on_rc,off_rc,on_seen,off_seen,info_rc,__atomic_load_n(&my_address_seen,__ATOMIC_ACQUIRE),deinit_rc,total,current_status());
    printf("{\"stream_probe_result\":true,\"passed\":%s,\"peer_paired\":%u,\"database_entry_seen\":%u,\"numeric_events\":%u,\"invalid_pcm\":%u,\"pcm_callbacks\":%u,\"pcm_bytes\":%u,\"pcm_nonzero_bytes\":%u,\"sound_events\":%u,\"physical_audio_output\":false}\n",passed?"true":"false",__atomic_load_n(&peer_paired,__ATOMIC_ACQUIRE),__atomic_load_n(&peer_in_db,__ATOMIC_ACQUIRE),__atomic_load_n(&numeric_seen,__ATOMIC_ACQUIRE),__atomic_load_n(&invalid_pcm,__ATOMIC_ACQUIRE),__atomic_load_n(&pcm_calls,__ATOMIC_ACQUIRE),__atomic_load_n(&pcm_bytes,__ATOMIC_ACQUIRE),__atomic_load_n(&pcm_nonzero,__ATOMIC_ACQUIRE),__atomic_load_n(&sound_events,__ATOMIC_ACQUIRE));
    printf("{\"stream_input_ops\":true,\"passive_sink\":true,\"remote_avdtp_commands_sent\":false,\"diagnostic_logs\":%u,\"player_ready_rc\":%d,\"player_closed_rc\":%d,\"player_queries\":%u}\n",audio_log_emitted,player_ready_rc,player_closed_rc,__atomic_load_n(&player_queries,__ATOMIC_ACQUIRE));
    fprintf(stderr,"STREAM_JOURNAL bytes=%u status=%d\n",journal_bytes,journal_rc);
#ifdef SONY_BT_WITH_LIVE_WIRE
    printf("{\"avrcp_receiver_controls\":true,\"relative_up\":%u,\"relative_down\":%u,\"absolute_ignored_as_native_gui\":%u,\"register_ignored_as_native_gui\":%u,\"inactive_live_ignored\":%u,\"events_offered\":%llu,\"events_sent\":%llu,\"closing_drops\":%llu}\n",avrcp_count(&avrcp_up),avrcp_count(&avrcp_down),avrcp_count(&avrcp_absolute),avrcp_count(&avrcp_register),avrcp_count(&avrcp_ignored_peer),(unsigned long long)live_source.volume_offered,(unsigned long long)live_source.volume_sent,(unsigned long long)__atomic_load_n(&live_source.volume_closing_drops,__ATOMIC_ACQUIRE));
    if(live_mode)printf("{\"live_source_queue\":true,\"peak_frames\":%u,\"offered_frames\":%llu,\"offered_bytes\":%llu,\"offered_packets\":%u,\"sender_joined_before_quiet\":%s}\n",live_source.queued_peak_frames,(unsigned long long)live_source.offered_frames,(unsigned long long)live_source.offered_bytes,live_source.offered_packets,!live_finish_rc?"true":"false");
    if(live_mode)printf("{\"live_source_result\":true,\"passed\":%s,\"frames\":%llu,\"bytes\":%llu,\"packets\":%u,\"source_error\":%d,\"finish_rc\":%d,\"sealed\":%u,\"closing_packets\":%u,\"closing_bytes\":%llu}\n",transport_ok?"true":"false",(unsigned long long)live_source.frames,(unsigned long long)live_source.bytes,live_source.packets,__atomic_load_n(&live_source.error,__ATOMIC_ACQUIRE),live_finish_rc,live_source.sealed,live_source.closing_packets,(unsigned long long)live_source.closing_bytes);
#endif
    fprintf(stderr,"PAIR_CLEANUP verified=%d pairing_verified=%d\n",cleanup_ok,pair_ok);
    return cleanup_ok&&(!persistent_mode||pair_ok)?0:70;
}
