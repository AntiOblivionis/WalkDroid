package org.walkdroid.audio.exclusive;

import android.content.Context;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.widget.Toast;
import java.io.FileInputStream;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.util.ArrayDeque;
import java.util.Map;
import org.walkdroid.sony.audio.SonyAudioControlPolicy;
import org.walkdroid.sony.audio.SonyAudioStatePolicy;

/** Called only after Android has selected a LOCAL music volume destination.
 * The MediaSession lock never waits for AudioSystem, RF, file IO or a receipt.
 * Accepted key intents are bounded, serialized and never retried after failure.
 */
public final class SonyVolumeBridge extends Binder {
    private static final String TAG="WalkDroidVolume";
    private static volatile SonyVolumeBridge instance;
    private final Context context;
    private final Handler worker,main=new Handler(Looper.getMainLooper());
    private final Method get,set;
    private final ArrayDeque<Request> queue=new ArrayDeque<>();
    private boolean running;
    private long next,completed,rejected,releases;
    private volatile String status="已就绪；等待音量操作";
    private static final class Request {
        final long id,when;final int direction,flags;
        final BtLinkService.HardwareVolumeTarget bluetooth;
        Request(long id,int direction,int flags){this.id=id;this.direction=direction;this.flags=flags;
            when=SystemClock.elapsedRealtime();bluetooth=BtLinkService.captureHardwareVolume();}
    }
    private static String property(String key)throws Exception{return(String)Class.forName("android.os.SystemProperties").getMethod("get",String.class).invoke(null,key);}
    public static synchronized void start(){
        if(instance!=null)return;
        try{
            Class<?> a=Class.forName("android.app.ActivityThread");Object t=a.getMethod("currentActivityThread").invoke(null);
            SonyVolumeBridge s=new SonyVolumeBridge((Context)a.getMethod("getSystemContext").invoke(t));
            Class.forName("android.os.ServiceManager").getMethod("addService",String.class,IBinder.class).invoke(null,"walkdroid.audio.volume",s);
            instance=s;Log.i(TAG,"local-keys-v1 ready");
        }catch(Exception e){Log.e(TAG,"initialize",e);}
    }
    private SonyVolumeBridge(Context c)throws Exception{
        context=c;Class<?> audio=Class.forName("android.media.AudioSystem");
        get=audio.getMethod("getParameters",String.class);set=audio.getMethod("setParameters",String.class);
        HandlerThread t=new HandlerThread("WalkDroidVolume");t.start();worker=new Handler(t.getLooper());
    }
    public static boolean adjust(int stream,int direction,int flags){
        SonyVolumeBridge s=instance;
        if(s==null||(stream!=3&&stream!=Integer.MIN_VALUE)||(direction!=-1&&direction!=0&&direction!=1))return false;
        synchronized(s){
            if(direction==0){s.releases++;return true;}
            long id=++s.next;
            if(s.queue.size()>=16){s.rejected++;s.status="音量操作过密；本次未发送";return true;}
            s.queue.add(new Request(id,direction,flags));
            if(!s.running){s.running=true;s.worker.post(s::drain);}
        }
        return true;
    }
    private void drain(){
        final Request r;
        synchronized(this){r=queue.poll();if(r==null){running=false;return;}}
        if(SystemClock.elapsedRealtime()-r.when>1500){finish(r,false,"音量操作已过期；本次未发送");return;}
        if(r.bluetooth!=null){
            BtLinkService.hardwareVolume(r.bluetooth,r.direction,(ok,message)->worker.post(()->finish(r,ok,message)));
            return;
        }
        try{
            Map<String,String> state=readIdle();
            if(!SonyAudioStatePolicy.editable(state,"sony_volume"))throw new IllegalStateException("原厂音量暂不可调整");
            int before=Integer.parseInt(state.get("sony_volume"));
            Map<String,String> changes=SonyAudioControlPolicy.changes(state,"sony_volume",Integer.toString(Math.max(0,Math.min(120,before+r.direction))));
            String value=changes.get("sony_volume"),revision=state.get("sony_revision");
            if(!value.equals(state.get("sony_volume"))){
                if("1".equals(state.get("sony_readonly"))){
                    String generation=state.get("sony_owner_generation"),id=java.util.UUID.randomUUID().toString().replace("-","");
                    send(SonyAudioControlPolicy.request(generation,revision,id,changes));
                }else{
                    send("sony_revision="+revision+";sony_volume="+value);
                }
            }
            finish(r,true,"音量请求已发送："+value+" / 120"+("1".equals(state.get("sony_avls"))?" · AVLS":""));
        }catch(Exception e){Log.w(TAG,"key "+r.id+" not replayed",e);finish(r,false,"索尼音量未确认；本次不会重发");}
    }
    private void send(String request)throws Exception{
        if((Integer)set.invoke(null,request)!=0)throw new IllegalStateException("原厂控制未接受");
    }
    private Map<String,String> readIdle()throws Exception{
        long end=SystemClock.elapsedRealtime()+8000;
        do{Map<String,String> s=SonyAudioStatePolicy.parse((String)get.invoke(null,"sony_state"));
            if(!"1".equals(s.get("sony_pending")))return s;Thread.sleep(50);
        }while(SystemClock.elapsedRealtime()<end);
        throw new IllegalStateException("原厂控制等待超时");
    }
    private void finish(Request r,boolean ok,String message){
        synchronized(this){if(ok)completed++;else rejected++;status=message;}
        Log.i(TAG,"key="+r.id+" direction="+r.direction+" submitted="+ok+" "+message);
        if((r.flags&1)!=0)main.post(()->Toast.makeText(context,message,Toast.LENGTH_SHORT).show());
        worker.post(this::drain);
    }
    @Override protected void dump(java.io.FileDescriptor fd,PrintWriter out,String[]args){
        synchronized(this){out.println("Sony local volume bridge v1 events="+next+" submitted="+completed+" rejected="+rejected+" releases="+releases+" queued="+queue.size()+" running="+running);out.println(status);}
        out.println("Cached diagnostics only. Does not authorize setters, force Android unity gain, or prove acoustic gain.");
    }
}
