package org.walkdroid.sony.audio;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.PowerManager;
import org.walkdroid.audio.exclusive.BtLinkProtocol;
/** Foreground lifeline for explicit TX. Closing the settings page does not
 * close the link. Process death is observed by the framework Binder owner. */
public final class SonyBtOutputService extends Service {
    static final String ON="org.walkdroid.sony.audio.BT_OUTPUT_ON",OFF="org.walkdroid.sony.audio.BT_OUTPUT_OFF",REFRESH="org.walkdroid.sony.audio.BT_OUTPUT_REFRESH";
    public final class LocalBinder extends Binder{public SonyBtOutputService service(){return SonyBtOutputService.this;}}
    private final LocalBinder binder=new LocalBinder();
    private volatile BtLinkProtocol.State state=new BtLinkProtocol.State();
    private HandlerThread thread;private Handler work;private SonyBtOutputClient client;
    private boolean foreground,destroyed;private PowerManager.WakeLock wake;
    public BtLinkProtocol.State state(){return state;}
    public void volume(BtLinkProtocol.State expected,int value,boolean mode){
        if(expected==null||mode&&!expected.canVolumeMode||!mode&&!expected.canVolume)return;
        work.post(()->{if(destroyed)return;try{client.volume(expected,value,mode);}catch(Exception e){failed();}});
    }
    @Override public void onCreate(){super.onCreate();
        ((NotificationManager)getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(new NotificationChannel("sony-bt-output",WalkDroidText.translate(this,"Sony 蓝牙输出"),NotificationManager.IMPORTANCE_LOW));
        wake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"WalkDroid:BtOutputTransition");wake.setReferenceCounted(false);
        thread=new HandlerThread("SonyBtOutputOwner");thread.start();work=new Handler(thread.getLooper());
        client=new SonyBtOutputClient(new SonyBtOutputClient.Listener(){public void state(BtLinkProtocol.State s){work.post(()->accept(s));}public void lost(){work.post(()->failed());}});
    }
    private void notification(String text){if(destroyed)return;
        PendingIntent open=PendingIntent.getActivity(this,721,new Intent(this,SonyAudioSettings.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent off=PendingIntent.getService(this,722,new Intent(this,SonyBtOutputService.class).setAction(OFF),PendingIntent.FLAG_UPDATE_CURRENT);
        Notification n=new Notification.Builder(this,"sony-bt-output").setSmallIcon(android.R.drawable.ic_media_play).setContentTitle(WalkDroidText.translate(this,"Sony 蓝牙输出")).setContentText(WalkDroidText.translate(this,text)).setContentIntent(open).setOnlyAlertOnce(true).setOngoing(true).addAction(new Notification.Action.Builder(android.R.drawable.ic_media_pause,WalkDroidText.translate(this,"关闭"),off).build()).build();startForeground(721,n);foreground=true;
    }
    private void accept(BtLinkProtocol.State s){if(destroyed)return;state=s;
        if(s.connected||s.busy)notification(s.message);else if(foreground&&s.known){stopForeground(true);foreground=false;}
        if(s.connected||!s.busy){if(wake.isHeld())wake.release();}
    }
    private void failed(){if(destroyed)return;BtLinkProtocol.State s=new BtLinkProtocol.State();s.rootPhase=6;s.policyPhase=3;s.error=-5;s.message="蓝牙输出状态未确认，请重新读取";state=s;if(foreground)notification(s.message);if(wake.isHeld())wake.release();}
    private void refresh(){try{accept(client.watch());}catch(Exception e){failed();}}
    @Override public IBinder onBind(Intent intent){work.post(()->refresh());return binder;}
    @Override public int onStartCommand(Intent intent,int flags,int id){String action=intent==null?REFRESH:intent.getAction();
        notification(ON.equals(action)?"正在连接…":OFF.equals(action)?"正在关闭…":"正在读取蓝牙输出状态…");
        if(ON.equals(action)||OFF.equals(action)){if(!wake.isHeld())wake.acquire(120000);work.post(()->{try{client.watch();if(ON.equals(action))client.connect();else client.disconnect();}catch(Exception e){failed();}});}
        else work.post(()->refresh());return START_NOT_STICKY;
    }
    @Override protected void dump(java.io.FileDescriptor fd,java.io.PrintWriter out,String[]args){BtLinkProtocol.State s=state;out.println("SonyBtOutput known="+s.known+" connected="+s.connected+" streaming="+s.streaming+" busy="+s.busy+" error="+s.error+" foreground="+foreground);out.println(s.message);out.println(s.volumeMessage);}
    @Override public void onDestroy(){destroyed=true;if(work!=null)work.post(()->{try{client.disconnect();client.unwatch();}catch(Exception ignored){}});if(wake.isHeld())wake.release();if(foreground)stopForeground(true);if(thread!=null)thread.quitSafely();super.onDestroy();}
}
