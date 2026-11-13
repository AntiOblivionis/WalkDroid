package com.android.server;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
/** Attached to the existing WiredAccessoryManager; no extra VM. */
public final class WalkDroidJackObserver implements Runnable {
    private static final String TAG="WalkDroidJack";
    private static WalkDroidJackObserver instance;
    private final Object owner,lock;
    private final Handler ownerHandler,pollHandler;
    private final Field headset;
    private final Method update,get;
    private final WalkDroidJackState state=new WalkDroidJackState();
    private int lastRaw=-2;
    private static String property(String name) throws Exception {
        return (String)Class.forName("android.os.SystemProperties").getMethod("get",String.class).invoke(null,name);
    }
    public static synchronized void start(Object owner) {
        org.walkdroid.audio.exclusive.AppSelectorService.start();
        org.walkdroid.audio.exclusive.BtCatalogService.start();
        org.walkdroid.audio.exclusive.BtLinkService.start();
        org.walkdroid.audio.exclusive.SonyVolumeBridge.start();
        if(instance!=null)return;
        try {
            instance=new WalkDroidJackObserver(owner);
            instance.pollHandler.post(instance);
            Log.i(TAG,"physical-v1 attached to WiredAccessoryManager");
        }catch(Exception e){Log.e(TAG,"bridge initialization failed",e);}
    }
    private static Field field(Class<?> c,String name) throws Exception {
        Field f=c.getDeclaredField(name);f.setAccessible(true);return f;
    }
    private WalkDroidJackObserver(Object target) throws Exception {
        owner=target;Class<?> c=owner.getClass();
        lock=field(c,"mLock").get(owner);ownerHandler=(Handler)field(c,"mHandler").get(owner);
        headset=field(c,"mHeadsetState");update=c.getDeclaredMethod("updateLocked",String.class,int.class);update.setAccessible(true);
        get=Class.forName("android.media.AudioSystem").getMethod("getParameters",String.class);
        HandlerThread thread=new HandlerThread("WalkDroidJack",android.os.Process.THREAD_PRIORITY_BACKGROUND);
        thread.start();pollHandler=new Handler(thread.getLooper());
    }
    public void run() {
        int physical=-1;
        try {physical=WalkDroidJackState.parse((String)get.invoke(null,"sony_jack"));}
        catch(Exception ignored){/* Availability is removed after debounce. */}
        if(physical!=lastRaw){Log.i(TAG,"physical="+physical+" (-1 is unknown/error)");lastRaw=physical;}
        final int event=state.sample(physical);
        if(event>=0)ownerHandler.post(new Runnable(){public void run(){
            try {
                synchronized(lock) {
                    int old=headset.getInt(owner),next=WalkDroidJackState.replaceWiredBits(old,event);
                    // Native updateLocked owns its wakelock and queues the
                    // standard AudioManager -> AudioService connection path.
                    update.invoke(owner,"Sony Walkman ZX300A",next);
                    Log.i(TAG,"wired="+event+" previousMask="+old+" newMask="+next);
                }
            }catch(Exception e){
                Log.e(TAG,"wired update failed",e);
                pollHandler.post(new Runnable(){public void run(){state.retry();}});
            }
        }});
        // Handler timeouts are not wakeup alarms and hold no persistent lock.
        pollHandler.postDelayed(this,200);
    }
}
