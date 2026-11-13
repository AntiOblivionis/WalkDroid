package org.walkdroid.sony.audio;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.VolumeProvider;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.SystemClock;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Foreground owner of one authenticated control connection. No AudioTrack,
 * root shell, automatic ON, persisted switch or guessed completion. */
public class SonyUsbReceiverService extends Service {
    static final String ON="org.walkdroid.sony.audio.USB_ON",OFF="org.walkdroid.sony.audio.USB_OFF",REFRESH="org.walkdroid.sony.audio.USB_REFRESH";
    public static final class ViewState {
        public final boolean checked,known,toggleEnabled,exclusive;
        public final String message;
        ViewState(boolean checked,boolean known,boolean toggleEnabled,boolean exclusive,String message){this.checked=checked;this.known=known;this.toggleEnabled=toggleEnabled;this.exclusive=exclusive;this.message=message;}
    }
    public final class LocalBinder extends Binder {public SonyUsbReceiverService service(){return SonyUsbReceiverService.this;}}
    private final LocalBinder binder=new LocalBinder();
    private volatile ViewState view=makeView(false,false,false,false,"正在读取 USB DAC 状态…");
    private HandlerThread thread;private Handler work;private volatile boolean destroyed;
    private volatile LocalSocket socket;private long request;private volatile SonyReceiverState state;
    private AudioManager audio;private AudioFocusRequest focus;private boolean focusHeld,attempted,foreground;
    private PowerManager.WakeLock wake;private int consecutiveFailures;
    private MediaSession media;private VolumeProvider nativeVolume;
    private SonyReceiverHoldClient policyHold;
    private long protectionStopSession;
    private volatile boolean volumeAllowed,volumeClosing;private volatile long volumeGeneration;
    private boolean volumeQueued;private int pendingVolumeDelta;private Integer pendingVolumeAbsolute;
    private final SonyDsfVolumeControl.Port volumePort=new SonyDsfVolumeControl.Port(){
        public String get(String q)throws Exception{return (String)Class.forName("android.media.AudioSystem").getMethod("getParameters",String.class).invoke(null,q);}
        public int set(String q)throws Exception{return (Integer)Class.forName("android.media.AudioSystem").getMethod("setParameters",String.class).invoke(null,q);}
        public long now(){return SystemClock.elapsedRealtime();}public void pause()throws Exception{Thread.sleep(50);}
        public boolean alive(){SonyReceiverState s=state;return !destroyed&&!volumeClosing&&volumeAllowed&&s!=null&&s.generation==volumeGeneration&&(s.phase==SonyReceiverState.READY||s.phase==SonyReceiverState.STREAMING);}
    };
    protected String receiverLabel(){return "USB DAC";}
    protected String peerLabel(){return "电脑";}
    protected String channel(){return "sony-receiver";}
    protected int notificationId(){return 701;}
    protected String controlSocket(){return "walkdroid.sony.receiver.v1";}
    protected int activateCommand(){return 2;}
    protected int receiverHoldKind(){return 1;}
    protected String onAction(){return ON;} protected String offAction(){return OFF;} protected String refreshAction(){return REFRESH;}
    protected boolean nativeVolumeSupported(){return true;}
    protected java.util.Map<String,String> readNativeVolume(SonyDsfVolumeControl.Port port,long generation)throws Exception{return SonyDsfVolumeControl.readUsb(port,generation);}
    protected int changeNativeVolume(SonyDsfVolumeControl.Port port,long generation,int delta,Integer absolute,String id)throws Exception{return SonyDsfVolumeControl.changeUsb(port,generation,delta,absolute,id);}
    protected String stateMessage(SonyReceiverState next){return next.message();}
    private String localize(String message){return message.replace("USB DAC",receiverLabel()).replace("电脑",peerLabel());}
    private ViewState makeView(boolean checked,boolean known,boolean toggleEnabled,boolean exclusive,String message){return new ViewState(checked,known,toggleEnabled,exclusive,localize(message));}
    public ViewState state(){return view;}
    @Override public void onCreate(){super.onCreate();
        ((NotificationManager)getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(new NotificationChannel(channel(),"Sony "+WalkDroidText.translate(this,receiverLabel()),NotificationManager.IMPORTANCE_LOW));
        audio=(AudioManager)getSystemService(AUDIO_SERVICE);wake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"WalkDroid:SonyReceiver");wake.setReferenceCounted(false);
        thread=new HandlerThread("SonyReceiverControl");thread.start();work=new Handler(thread.getLooper());
        policyHold=new SonyReceiverHoldClient(receiverHoldKind());
        if(nativeVolumeSupported()){media=new MediaSession(this,"Sony "+receiverLabel());
        // Android 9 dispatches VolumeProvider events through the session's
        // callback handler too. Without a callback those volume events vanish.
        media.setCallback(new MediaSession.Callback(){});
        nativeVolume=new VolumeProvider(VolumeProvider.VOLUME_CONTROL_ABSOLUTE,120,0){
            @Override public void onAdjustVolume(int direction){if(direction==1||direction==-1)queueVolume(direction,null);}
            @Override public void onSetVolumeTo(int value){if(value>=0&&value<=120)queueVolume(0,value);}
        };
        media.setPlaybackToRemote(nativeVolume);
        media.setMetadata(new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE,"Sony "+receiverLabel()).build());
        media.setPlaybackState(new PlaybackState.Builder().setState(PlaybackState.STATE_PLAYING,PlaybackState.PLAYBACK_POSITION_UNKNOWN,1).build());}
    }
    @Override public IBinder onBind(Intent intent){work.post(()->refresh());return binder;}
    private synchronized void notification(String message){
        if(destroyed)return;
        Intent open=new Intent(this,SonyAudioSettings.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent content=PendingIntent.getActivity(this,notificationId(),open,PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,notificationId()+1,new Intent(this,getClass()).setAction(offAction()),PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder b=new Notification.Builder(this,channel()).setSmallIcon(android.R.drawable.ic_media_play).setContentTitle("Sony "+WalkDroidText.translate(this,receiverLabel())).setContentText(WalkDroidText.translate(this,localize(message))).setContentIntent(content).setOnlyAlertOnce(true).setOngoing(true);
        if(state==null||!state.retained)b.addAction(new Notification.Action.Builder(android.R.drawable.ic_media_pause,WalkDroidText.translate(this,"关闭"),stop).build());
        startForeground(notificationId(),b.build());foreground=true;
    }
    private synchronized void finishAudio(){
        disableVolume();
        if(focusHeld&&focus!=null)audio.abandonAudioFocusRequest(focus);focusHeld=false;
        if(wake.isHeld())wake.release();if(foreground){stopForeground(true);foreground=false;}
    }
    private synchronized void hold(){if(!destroyed&&!wake.isHeld())wake.acquire();}
    private synchronized boolean acquireFocus(){
        if(destroyed)return false;
        focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()).setOnAudioFocusChangeListener(value->{/* Native mode remains exclusive until explicit OFF. */},work).build();
        focusHeld=audio.requestAudioFocus(focus)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;return focusHeld;
    }
    @Override public int onStartCommand(Intent intent,int flags,int id){
        String action=intent==null?refreshAction():intent.getAction();
        if(onAction().equals(action)||offAction().equals(action)){
            volumeClosing=offAction().equals(action);if(volumeClosing)disableVolume();
            notification(onAction().equals(action)?"正在开启 USB DAC…":"正在关闭 USB DAC…");
            ViewState old=view;view=makeView(old.checked,false,false,onAction().equals(action)||old.exclusive,onAction().equals(action)?"正在开启 USB DAC…":"正在关闭并恢复系统声音…");
            work.post(()->change(onAction().equals(action)));
        }else{
            // startForegroundService requires promotion even if REFRESH finds
            // OFF and immediately stops. Android 9 otherwise crashes the APK
            // while bringing this short-lived Service down.
            notification("正在读取 USB DAC 状态…");work.post(()->refresh());
        }
        return START_NOT_STICKY;
    }
    private void connect() throws Exception {
        if(socket!=null)return;LocalSocket next=new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
        try{next.connect(new LocalSocketAddress(controlSocket(),LocalSocketAddress.Namespace.ABSTRACT));next.setSoTimeout(3000);
            synchronized(this){if(destroyed)throw new IOException("接收服务已退出");socket=next;request=0;}
        }catch(Exception e){next.close();throw e;}
    }
    private SonyReceiverState exchange(int command,long session) throws Exception {
        connect();if(destroyed)throw new IOException("接收服务已退出");if(request==Long.MAX_VALUE)throw new IOException("接收请求编号已用尽");long id=++request;
        ByteBuffer b=ByteBuffer.allocate(32).order(ByteOrder.LITTLE_ENDIAN);b.putInt(SonyReceiverState.MAGIC).putInt(1).putInt(command).putInt(0).putLong(id).putLong(session);
        LocalSocket current=socket;current.getOutputStream().write(b.array());byte[] raw=new byte[72];int n=current.getInputStream().read(raw);
        java.io.FileDescriptor[] ancillary=current.getAncillaryFileDescriptors();
        if(ancillary!=null&&ancillary.length!=0){for(java.io.FileDescriptor fd:ancillary)try{new java.io.FileInputStream(fd).close();}catch(IOException ignored){}throw new IOException("接收状态含有不支持的描述符");}
        if(n!=72)throw new IOException("接收状态响应不完整");return SonyReceiverState.decode(raw,id);
    }
    private synchronized void closeSocket(){LocalSocket old=socket;socket=null;if(old!=null)try{old.close();}catch(IOException ignored){}}
    private void observe(SonyReceiverState next)throws Exception{
        if(destroyed)return;
        if(next.active()){
            if(!policyHold.granted()){
                // A native service restart may have lost HOLD while the root
                // receiver still runs. Close this exact session once; no ON.
                disableVolume();attempted=true;hold();
                if(protectionStopSession==next.session)throw new IOException("接收独占保护缺失，关闭结果尚未确认");
                protectionStopSession=next.session;
                SonyReceiverState closed=exchange(3,next.session);
                if(closed.active())throw new IOException("接收独占保护缺失，正在确认关闭");
                observe(closed);view=makeView(false,true,true,false,"接收保护已重置，本次接收已关闭；请重新开启。");return;
            }
        }else{
            policyHold.releaseIfOwned(next);protectionStopSession=0;
        }
        state=next;consecutiveFailures=0;work.removeCallbacks(poll);
        refreshVolume(next);
        boolean checked=next.known?next.enabled:view.checked;
        view=makeView(checked,next.known,!next.busy&&!next.retained,next.active(),stateMessage(next));
        if(next.active()){attempted=true;hold();notification(stateMessage(next));work.postDelayed(poll,next.busy?250:1000);}
        else{attempted=false;finishAudio();stopSelf();}
    }
    private synchronized void disableVolume(){
        volumeAllowed=false;pendingVolumeDelta=0;pendingVolumeAbsolute=null;volumeQueued=false;
        if(work!=null)work.removeCallbacks(volumeTask);if(media!=null)media.setActive(false);
    }
    private void refreshVolume(SonyReceiverState next){
        if(!nativeVolumeSupported()||volumeClosing||!next.enabled||next.generation==0||next.retained||(next.phase!=SonyReceiverState.READY&&next.phase!=SonyReceiverState.STREAMING)){disableVolume();return;}
        volumeGeneration=next.generation;volumeAllowed=true;
        try{
            int value=Integer.parseInt(readNativeVolume(volumePort,next.generation).get("sony_volume"));
            if(destroyed||volumeClosing||!volumeAllowed||volumeGeneration!=next.generation)return;
            if(nativeVolume.getCurrentVolume()!=value)nativeVolume.setCurrentVolume(value);
            if(!destroyed&&volumeAllowed)media.setActive(true);
        }catch(Exception e){android.util.Log.w("SonyUsbVolume","Current volume not confirmed",e);}
    }
    private synchronized void queueVolume(int delta,Integer absolute){
        if(!volumePort.alive())return;
        if(absolute!=null){pendingVolumeAbsolute=absolute;pendingVolumeDelta=0;}
        else if(pendingVolumeAbsolute!=null)pendingVolumeAbsolute=Math.max(0,Math.min(120,pendingVolumeAbsolute+delta));
        else pendingVolumeDelta=Math.max(-120,Math.min(120,pendingVolumeDelta+delta));
        if(!volumeQueued){volumeQueued=true;work.post(volumeTask);}
    }
    private final Runnable volumeTask=()->runVolume();
    private void runVolume(){
        int delta;Integer absolute;long generation;
        synchronized(this){delta=pendingVolumeDelta;absolute=pendingVolumeAbsolute;pendingVolumeDelta=0;pendingVolumeAbsolute=null;generation=volumeGeneration;}
        try{
            if(volumePort.alive()&&(delta!=0||absolute!=null)){
                String id=java.util.UUID.randomUUID().toString().replace("-","");
                int value=changeNativeVolume(volumePort,generation,delta,absolute,id);
                if(volumePort.alive()&&generation==volumeGeneration){nativeVolume.setCurrentVolume(value);android.util.Log.i("SonyUsbVolume","Applied generation="+generation+" value="+value+" request="+id);}
            }
        }catch(Exception e){android.util.Log.w("SonyUsbVolume","Volume result unconfirmed; no replay",e);}
        finally{synchronized(this){if(volumePort.alive()&&(pendingVolumeDelta!=0||pendingVolumeAbsolute!=null))work.post(volumeTask);else volumeQueued=false;}}
    }
    private void failed(){
        closeSocket();work.removeCallbacks(poll);ViewState old=view;
        view=makeView(old.checked,false,false,old.exclusive||attempted,"USB DAC 状态未确认，正在重新读取…");
        if(attempted||old.exclusive){hold();notification(view.message);}
        if(++consecutiveFailures<=3&&!destroyed)work.postDelayed(poll,250L*consecutiveFailures);
        else{view=makeView(old.checked,false,false,old.exclusive||attempted,"无法确认 USB DAC 状态，请重新打开此页读取。");if(attempted||old.exclusive)notification(view.message);else{finishAudio();stopSelf();}}
    }
    private final Runnable poll=()->refresh();
    private void refresh(){if(destroyed)return;try{observe(exchange(1,0));}catch(Exception e){if(!destroyed)failed();}}
    private void change(boolean on){
        if(destroyed)return;work.removeCallbacks(poll);
        try{
            SonyReceiverState fresh=exchange(1,0);
            if(on){
                if(fresh.active()){observe(fresh);return;}
                policyHold.releaseIfOwned(fresh);
                attempted=true;hold();policyHold.begin();
                if(!acquireFocus()){observe(fresh);view=makeView(false,true,true,false,"未开启，请先停止其他播放。");return;}
                attempted=true;hold();observe(exchange(activateCommand(),0));
            }else{if(fresh.phase==SonyReceiverState.OFF){observe(fresh);return;}attempted=true;hold();observe(exchange(3,fresh.session));}
        }catch(Exception e){if(!destroyed)failed();}
    }
    @Override protected synchronized void dump(java.io.FileDescriptor fd,java.io.PrintWriter writer,String[] args){
        SonyReceiverState current=state;ViewState shown=view;
        writer.println("SonyUsbReceiver known="+shown.known+" checked="+shown.checked+" exclusive="+shown.exclusive+" foreground="+foreground+" focus="+focusHeld+" wake="+wake.isHeld());
        if(current!=null)writer.println("SonyUsbReceipt phase="+current.phase+" error="+current.error+" session="+current.session+" generation="+current.generation+" sourceFrames="+current.sourceFrames+" outputBytes="+current.outputBytes+" rate="+current.rate+" flags="+current.flags+" cleanup="+current.cleanup+" retained="+current.retained);
    }
    @Override public void onDestroy(){destroyed=true;closeSocket();if(work!=null){work.removeCallbacksAndMessages(null);work.post(()->{try{policyHold.abandon();}catch(Exception e){android.util.Log.w("SonyReceiverHold","Owner detach unconfirmed; protection retained",e);}});}finishAudio();if(media!=null)media.release();if(thread!=null)thread.quitSafely();super.onDestroy();}
}
