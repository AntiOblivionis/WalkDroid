/* Included by the serialized receiver session. Only the admitted one-shot
 * writer runs concurrently; it never touches PCM, DSP or session controls.
 * Ownership stays borrowed until the writer has exited and been collected. */
static void *volume_save_worker(void *context)
{
    struct receiver_volume_save *j=context;
    struct sched_param param={.sched_priority=0};int policy=-1;
    (void)pthread_setschedparam(pthread_self(),SCHED_OTHER,&param);
    (void)pthread_getschedparam(pthread_self(),&policy,&param);int rc=0;
    uint64_t a=volume_clock(),b,c;
    if(!rc)rc=owner_check(j->session);
    if(!rc)rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,j->before,j->next);
    b=volume_clock();
    if(!rc)rc=owner_check(j->session);
    if(!rc)rc=sony_snapshot_commit(j->session->path,j->before,j->next);
    if(!rc)rc=owner_check(j->session);
    c=volume_clock();
    fprintf(stderr,"USB_VOLUME_SAVE rc=%d next=%u policy=%d priority=%d global_us=%llu private_us=%llu\n",
        rc,j->volume,policy,param.sched_priority,(unsigned long long)(b-a),(unsigned long long)(c-b));
    j->result=rc;
    __atomic_store_n(&j->done,1,__ATOMIC_RELEASE);
    /* Nonblocking event only. The serialized worker alone collects the saved
     * result and publishes it. The descriptors outlive this writer's join. */
    unsigned char byte=1;ssize_t notified;
    do{notified=write(j->session->volume_notify[1],&byte,1);}while(notified<0&&errno==EINTR);
    return NULL;
}
static int volume_save_start(struct sony_receiver_session *s)
{
    uint64_t clock0=volume_clock();int rc=lock(s);if(rc)return rc;
    if(s->save)return unlock(s,-EBUSY);
    rc=volume_context(s);if(rc)return unlock(s,rc);
    if(!s->state.started||!s->state.stream_open||s->revision==UINT32_MAX)return unlock(s,-EINVAL);
    struct sony_audio_jack_state jack;rc=sony_audio_jack_read(&jack);if(rc)return unlock(s,rc);
    const struct sony_controls *c=&s->controls;
    struct sony_volume_state current={c->ship_selector,c->settings.volume,c->output.device,c->volume_memory.last_headphone,
        !!jack.se,!!jack.btl,c->output.se_gain,c->output.btl_gain,c->volume_memory.avls,c->avls_threshold};
    struct sony_volume_plan plan;rc=sony_volume_plan_set(&current,(int)s->command_volume,&plan);
    if(!rc&&plan.next.volume!=(int)s->command_volume)rc=-ERANGE;
    if(rc)return unlock(s,rc);
    struct receiver_volume_save *j=calloc(1,sizeof(*j));if(!j)return unlock(s,-ENOMEM);
    j->session=s;j->revision=s->revision;j->volume=s->command_volume;j->pending=s->command_base;
    memcpy(j->before,s->snapshot,sizeof(j->before));
    rc=sony_snapshot_encode_volume(j->before,sizeof(j->before),(int)j->volume,j->next);
    pthread_attr_t attr;int initialized=0;
    if(!rc){int e=pthread_attr_init(&attr);if(e)rc=-e;else initialized=1;}
    if(!rc){int e=pthread_attr_setstacksize(&attr,256*1024);if(e)rc=-e;}
    if(rc){if(initialized)pthread_attr_destroy(&attr);free(j);return unlock(s,rc);}
    rc=(s->flags&SONY_RECEIVER_SESSION_BT_PERIOD_WRITES)&&s->playback?sony_receiver_playback_flush(s->playback):0;
    if(!rc)rc=sony_audio_owner_retire(s->owner);
    if(!rc){struct wur_state notified;rc=sony_receiver_client_volume(sony_receiver_process_client(s->dsp),(unsigned)c->settings.volume,j->volume,&notified);}
    if(!rc)rc=sony_audio_route_live_volume(s->route,(unsigned)c->settings.volume,j->volume,&c->output);
    if(!rc){
        s->controls.settings.volume=(int)j->volume;s->save=j;
        int e=pthread_create(&j->thread,&attr,volume_save_worker,j);if(e){rc=-e;s->save=NULL;}
    }
    pthread_attr_destroy(&attr);
    fprintf(stderr,"USB_VOLUME_RT rc=%d next=%u frontend_us=%llu\n",rc,j->volume,(unsigned long long)(volume_clock()-clock0));
    if(rc){free(j);s->state.config_error=rc;(void)mute(s);return unlock(s,fail(s,rc));}
    return unlock(s,0);
}
static int volume_save_collect(struct sony_receiver_session *s)
{
    struct receiver_volume_save *j=s->save;
    if(!j||!__atomic_load_n(&j->done,__ATOMIC_ACQUIRE))return -EAGAIN;
    if(j->join_error)return j->join_error;
    int e=pthread_join(j->thread,NULL),rc=e?-e:j->result;
    /* A failed join cannot authorize freeing a possibly live writer. */
    if(e){j->join_error=rc;s->state.config_error=rc;(void)mute(s);return fail(s,rc);}
    if(!rc)rc=owner_check(s);
    if(!rc){memcpy(s->snapshot,j->next,sizeof(s->snapshot));}
    s->save=NULL;
    if(!rc)rc=bind_config_file(s);
    if(!rc&&!s->closing)rc=volume_context(s);
    if(!rc){++s->revision;s->command_pending=0;if(!s->closing)rc=publish(s);}
    free(j);
    if(rc){s->state.config_error=rc;(void)mute(s);return fail(s,rc);}
    return 0;
}
