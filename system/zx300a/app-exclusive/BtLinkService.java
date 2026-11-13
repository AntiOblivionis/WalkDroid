package org.walkdroid.audio.exclusive;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Binder;
import android.os.FileObserver;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Parcel;
import android.util.AtomicFile;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
/** System-server coordinator. Root RF ownership, APM ownership and the
 * foreground APK lifeline are distinct; no operation is automatically replayed. */
public final class BtLinkService extends Binder {
    private static final String TAG="WalkDroidBtOutput";
    private static volatile BtLinkService instance;
    interface HardwareVolumeReply {void done(boolean confirmed,String message);}
    static final class HardwareVolumeTarget {
        final BtLinkService service;final IBinder owner;final long route,generation;final String token,instance;
        HardwareVolumeTarget(BtLinkService s,Record r,IBinder o){service=s;owner=o;route=r==null?0:r.route;generation=r==null?0:r.generation;token=r==null?"":r.token;instance=r==null?"":r.instance;}
    }
    private volatile HardwareVolumeTarget hardwareVolumeTarget;
    private HardwareVolumeReply hardwareVolumeReply;
    private final Context context;private final Handler work;
    private final String boot;
    private final AtomicFile store=new AtomicFile(new File("/data/system/walkdroid-bt-output"));
    private final BluetoothAdapter adapter=BluetoothAdapter.getDefaultAdapter();
    private static final class Watcher {IBinder token;IBinder.DeathRecipient death;}
    private final ArrayList<Watcher> watchers=new ArrayList<>();
    private volatile BtLinkProtocol.State latest=new BtLinkProtocol.State();
    private volatile BtLinkNative link;private BtLinkNative.State root;
    private NativePolicyControl policyControl;private NativePolicyControl.Pending pendingPolicy;
    private NativePolicyControl.Request lostPolicyStage;
    private BtPolicyState policy;private boolean policyHeld;private int policyHoldKind;
    private BtCatalogNative.State catalog;
    private volatile Record record;private IBinder owner;private IBinder.DeathRecipient ownerDeath;
    private boolean starting,closing,commitSent,disarmSent,rootPending,ownsRootSocket,loaded;
    private int error,volumeError;private boolean volumePending;private String message="正在读取蓝牙输出状态";
    private long commandSerial;
    private IBinder queuedStart;
    private final FileObserver roots;
    private static final class Handoff {final int kind,pid;final IBinder owner;final boolean ready;Handoff(int k,IBinder o,int p,boolean r){kind=k;owner=o;pid=p;ready=r;}}
    private volatile Handoff handoff;
    private static final class Record {String boot,instance,token,peer,stage="arm";long route,generation;boolean adapterOn,adapterChanged,policyStopSent;int receiver;}
    private static String property(String name)throws Exception{return(String)Class.forName("android.os.SystemProperties").getMethod("get",String.class).invoke(null,name);}
    private static String read(String path){byte[]b=new byte[128];try(FileInputStream in=new FileInputStream(path)){int n=in.read(b);return n>0?new String(b,0,n,"US-ASCII").trim():"";}catch(Exception ignored){return "";}}
    public static synchronized void start(){
        if(instance!=null)return;
        try{
            Class<?> a=Class.forName("android.app.ActivityThread");Object thread=a.getMethod("currentActivityThread").invoke(null);Context c=(Context)a.getMethod("getSystemContext").invoke(thread);
            BtLinkService service=new BtLinkService(c);Class.forName("android.os.ServiceManager").getMethod("addService",String.class,IBinder.class).invoke(null,BtLinkProtocol.SERVICE,service);instance=service;
        }catch(Exception e){Log.e(TAG,"initialize",e);}
    }
    private BtLinkService(Context c)throws Exception{
        hardwareVolumeTarget=new HardwareVolumeTarget(this,null,null);
        context=c;attachInterface(null,BtLinkProtocol.DESCRIPTOR);boot=read("/proc/sys/kernel/random/boot_id");
        HandlerThread thread=new HandlerThread("WalkDroidBtCoordinator");thread.start();work=new Handler(thread.getLooper());
        roots=new FileObserver("/dev",FileObserver.DELETE|FileObserver.MOVED_FROM){public void onEvent(int event,String path){if("walkdroid-bt-tx".equals(path))work.post(()->refresh());}};roots.startWatching();
        IntentFilter f=new IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED);f.addAction(Intent.ACTION_BOOT_COMPLETED);f.addAction("android.intent.action.USER_SWITCHED");
        c.registerReceiver(new BroadcastReceiver(){public void onReceive(Context ignored,Intent intent){work.post(()->{
            if("android.intent.action.USER_SWITCHED".equals(intent.getAction()))stop();
            else if(starting&&adapter!=null&&adapter.getState()==BluetoothAdapter.STATE_OFF&&!rootPending&&record!=null&&record.route==0)arm();
            else if(Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())||record!=null&&"restore".equals(record.stage))refresh();
        });}},f,null,work);
        try{Class.forName("android.os.SystemProperties").getMethod("addChangeCallback",Runnable.class).invoke(null,(Runnable)()->{
            if(link==null&&record!=null)try{if(property("init.svc.zx9_bt_link").equals("running"))work.post(()->refresh());}catch(Exception ignored){}
        });}catch(Exception e){Log.w(TAG,"property observer unavailable",e);}
    }
    private void save()throws Exception{
        if(record==null)return;Record r=record;String s="WalkDroidBtOutput1\n"+r.boot+'\n'+r.instance+'\n'+r.token+'\n'+r.route+'\n'+r.generation+'\n'+r.peer+'\n'+(r.adapterOn?1:0)+'\n'+(r.adapterChanged?1:0)+'\n'+r.receiver+'\n'+r.stage+'\n'+(r.policyStopSent?1:0)+'\n';
        FileOutputStream out=null;try{out=store.startWrite();out.write(s.getBytes("US-ASCII"));store.finishWrite(out);out=null;}finally{if(out!=null)store.failWrite(out);}
    }
    private static long number(String value){try{return Long.parseLong(value);}catch(Exception ignored){return 0;}}
    private void load()throws Exception{
        if(loaded)return;
        if(!store.getBaseFile().exists()&&!new File(store.getBaseFile().getPath()+".bak").exists()){loaded=true;return;}
        byte[] raw=new byte[2048];int size=0;try(FileInputStream in=store.openRead()){while(size<raw.length){int n=in.read(raw,size,raw.length-size);if(n<=0)break;size+=n;}}
        if(size==0){loaded=true;return;}String[]v=Arrays.copyOf(new String(raw,0,size,"US-ASCII").split("\n",-1),12);
        for(int i=0;i<v.length;i++)if(v[i]==null)v[i]="";
        Record r=new Record();r.boot=v[1];r.instance=v[2];r.token=v[3];r.route=number(v[4]);r.generation=number(v[5]);r.peer=v[6];r.adapterOn=v[7].equals("1");r.adapterChanged=v[8].equals("1");r.receiver=(int)number(v[9]);r.stage=v[10].isEmpty()?"arm":v[10];r.policyStopSent=v[11].equals("1");record=r;closing=r.receiver==0;loaded=true;
    }
    private NativePolicyControl.Result settlePolicy()throws Exception{
        synchronized(NativePolicyControl.TRANSACTIONS){return settlePolicyLocked();}
    }
    private NativePolicyControl.Result settlePolicyLocked()throws Exception{
        if(lostPolicyStage!=null){try{NativePolicyControl.Result r=policyControl.recover();NativePolicyControl.Request q=lostPolicyStage;if(r.operation!=q.operation||r.requestedUid!=q.uid||r.expectedGeneration!=q.expectedGeneration||!r.identity.equals(q.identity))throw new IllegalStateException("foreign pending policy ticket");pendingPolicy=policyControl.recovered(r);if(r.phase==1)pendingPolicy.cancel();lostPolicyStage=null;}catch(NativePolicyControl.RpcFailure e){if(e.status!=-2)throw e;lostPolicyStage=null;}}
        if(pendingPolicy==null)return null;NativePolicyControl.Result r=pendingPolicy.peek();if(r.phase!=NativePolicyControl.DONE)throw new NativePolicyControl.RpcFailure(-115);
        if(r.bt!=null)policy=r.bt;policyHeld=r.held;policyHoldKind=r.holdKind;pendingPolicy.acknowledge(r);pendingPolicy=null;return r;
    }
    private NativePolicyControl.Result policyCall(NativePolicyControl.Request request)throws Exception{
        synchronized(NativePolicyControl.TRANSACTIONS){return policyCallLocked(request);}
    }
    private NativePolicyControl.Result policyCallLocked(NativePolicyControl.Request request)throws Exception{
        if(policyControl!=null&&!policyControl.alive()){policyControl=null;pendingPolicy=null;lostPolicyStage=null;policy=null;}
        if(policyControl==null)policyControl=new NativePolicyControl(new NativePolicyControl.Completion(){
            public void ready(String ticket){work.post(()->{if(pendingPolicy!=null&&pendingPolicy.ticket.equals(ticket))try{settlePolicy();if(closing)driveOff();}catch(Exception e){fail(e);}});}
            public void routeChanged(){work.post(()->{try{policyCall(NativePolicyControl.Request.btQuery());if(record!=null&&record.receiver==0&&policy!=null&&policy.phase==3)stop();else emit();}catch(Exception e){fail(e);}});}
        });
        settlePolicy();lostPolicyStage=request;
        try{pendingPolicy=policyControl.stage(request);lostPolicyStage=null;}catch(NativePolicyControl.RpcFailure e){lostPolicyStage=null;throw e;}
        pendingPolicy.dispatch();NativePolicyControl.Result r=settlePolicy();if(r==null||!r.executed||!r.healthy()||r.bt==null)throw new NativePolicyControl.RpcFailure(-19);
        if(r.status!=0)throw new NativePolicyControl.RpcFailure(r.status);return r;
    }
    private void connectRoot()throws Exception{
        if(link!=null)return;final BtLinkNative[] exact=new BtLinkNative[1];BtLinkNative next=new BtLinkNative(new BtLinkNative.Listener(){
            public void event(BtLinkNative.State state){work.post(()->{if(link==exact[0])acceptRoot(state);});}
            public void closed(Exception e){work.post(()->{if(link!=exact[0])return;link=null;root=null;ownsRootSocket=false;rootPending=volumePending=false;volumeError=-5;finishHardwareVolume(false);if(record!=null&&record.receiver==0){closing=true;starting=false;try{closePolicy();}catch(Exception x){Log.w(TAG,"policy cleanup pending",x);}}fail(e);});}
        });exact[0]=next;link=next;root=next.initial;
    }
    private void command(int op,BtLinkNative.State expected,byte[]token)throws Exception{
        command(op,expected,token,0);
    }
    private void command(int op,BtLinkNative.State expected,byte[]token,int volumeValue)throws Exception{
        if(rootPending)throw new NativePolicyControl.RpcFailure(-115);rootPending=true;boolean isVolume=op==BtLinkNative.VOLUME_STEP||op==BtLinkNative.VOLUME_MODE;volumePending=isVolume;long serial=++commandSerial;BtLinkNative exact=link;
        try{link.command(op,expected,token,volumeValue,new BtLinkNative.Reply(){
            public void done(BtLinkNative.State state){work.post(()->{if(link!=exact||serial!=commandSerial)return;rootPending=volumePending=false;
                if(isVolume){volumeError=state.status;acceptRoot(state);finishHardwareVolume(state.status==0);return;}
                if(state.status!=0){root=state;error=state.status;message="蓝牙操作未获确认（"+error+"）";starting=false;closing=record!=null;driveOff();return;}if(op==BtLinkNative.ARM)ownsRootSocket=true;acceptRoot(state);if(record==null&&queuedStart!=null&&!rootPending){IBinder pending=queuedStart;queuedStart=null;startConnection(pending);}});}
            public void failed(Exception e){work.post(()->{if(link==exact){rootPending=volumePending=false;finishHardwareVolume(false);fail(e);}});}
        });}catch(Exception e){rootPending=volumePending=false;exact.close();throw e;}
        work.postDelayed(()->{if(link==exact&&rootPending&&serial==commandSerial){error=-110;message="蓝牙回执未确认，正在关闭本次连接";starting=false;closing=record!=null;exact.close();}},6000);
    }
    private boolean rootMatches(){return root!=null&&record!=null;}
    private boolean policyMatches(){return record!=null&&policy!=null;}
    private boolean policyClosed(){return policy!=null&&policy.phase==0&&(policy.flags&3)==0;}
    private void acceptRoot(BtLinkNative.State s){root=s;
        try{
            if(s.operation==BtLinkNative.STATUS&&record==null&&root.idle()&&policyClosed()){
                error=0;message=catalog==null||catalog.selected.isEmpty()?"请先选择蓝牙输出设备":"未连接";
            }
            if(record!=null&&record.route==0&&rootMatches()&&root.route>0){record.route=root.route;save();}
            if(starting&&rootMatches()&&root.phase==2&&!commitSent&&!closing){commitSent=true;record.stage="commit";save();command(BtLinkNative.COMMIT,root,root.token);}
            else if(starting&&rootMatches()&&root.phase==3&&!closing){
                policyCall(NativePolicyControl.Request.btQuery());record.stage="policy";save();policyCall(NativePolicyControl.Request.btRoute(true,policy,record.token,record.peer,record.route));
                record.stage="active";save();starting=false;message="已连接";error=0;
            }
            if(starting&&record!=null&&ownsRootSocket&&root.idle()&&root.lastError!=0){
                error=root.lastError;message="蓝牙连接失败（"+error+"）";starting=false;closing=record.receiver==0;
            }
            if(record!=null&&(root.phase==5||root.phase==6)){starting=false;closing=record.receiver==0;}
            if(closing)driveOff();else if(record!=null&&root.idle()&&record.receiver==0&&!starting){closing=true;driveOff();}else emit();
        }catch(Exception e){error=e instanceof NativePolicyControl.RpcFailure?((NativePolicyControl.RpcFailure)e).status:-5;message="蓝牙连接未获确认，正在关闭";Log.w(TAG,"native state",e);starting=false;closing=record!=null;driveOff();}
    }
    private void readCatalog()throws Exception{catalog=new BtCatalogNative().query();if(catalog.status!=0)throw new NativePolicyControl.RpcFailure(catalog.status);}
    private void refresh(){try{load();readCatalog();policyCall(NativePolicyControl.Request.btQuery());connectRoot();
        if(record!=null&&record.receiver>0){if(!policyHeld){record.receiver=0;closing=true;}else{message="接收模式独占中，蓝牙输出已关闭";emit();return;}}
        if(record!=null&&!starting&&owner==null&&record.receiver==0)closing=true;
        if(!rootPending)command(BtLinkNative.STATUS,root,new byte[16]);else emit();
    }catch(Exception e){fail(e);}}
    private void attachOwner(IBinder token)throws Exception{detachOwner();if(!token.isBinderAlive())throw new IllegalStateException("BT owner dead");owner=token;ownerDeath=()->work.post(()->{if(owner==token)stop();});token.linkToDeath(ownerDeath,0);}
    private void detachOwner(){if(owner!=null&&ownerDeath!=null)try{owner.unlinkToDeath(ownerDeath,0);}catch(Exception ignored){}owner=null;ownerDeath=null;}
    private void startConnection(IBinder token){Record created=null;try{
        if(owner==token&&record!=null&&!closing){emit();return;}
        if(rootPending){if(queuedStart!=null&&queuedStart!=token)throw new NativePolicyControl.RpcFailure(-16);queuedStart=token;message="正在读取蓝牙输出状态";emit();return;}
        load();if(record!=null||starting||closing||handoff!=null)throw new NativePolicyControl.RpcFailure(-16);readCatalog();if(catalog.selected.isEmpty())throw new IllegalStateException("先选择蓝牙输出设备");
        policyCall(NativePolicyControl.Request.btQuery());if(policyHeld||!policyClosed())throw new NativePolicyControl.RpcFailure(-16);connectRoot();if(!root.idle())throw new NativePolicyControl.RpcFailure(-16);
        attachOwner(token);Record r=new Record();r.boot=boot;r.instance=BtLinkNative.hex(root.instance);byte[]nonce=new byte[16];do{new SecureRandom().nextBytes(nonce);}while(BtLinkNative.hex(nonce).matches("0+"));r.token=BtLinkNative.hex(nonce);r.peer=catalog.selected;r.generation=catalog.generation;r.adapterOn=adapter!=null&&adapter.getState()==BluetoothAdapter.STATE_ON;
        if(adapter!=null&&adapter.getState()!=BluetoothAdapter.STATE_OFF&&!r.adapterOn)throw new NativePolicyControl.RpcFailure(-16);
        record=r;created=r;starting=true;closing=false;commitSent=disarmSent=false;ownsRootSocket=false;error=volumeError=0;message="正在连接 "+r.peer;save();
        if(r.adapterOn){r.adapterChanged=true;save();if(!adapter.disable()){r.adapterChanged=false;save();throw new IllegalStateException("Android 蓝牙尚未关闭");}work.postDelayed(()->{if(starting&&record==r&&r.route==0&&!rootPending){error=-110;message="Android 蓝牙关闭未获确认";stop();}},12000);}
        else arm();emit();
        work.postDelayed(()->{if(starting&&record==r){error=-110;message="蓝牙连接超时，正在关闭";stop();}},90000);
    }catch(Exception e){fail(e);if(created!=null&&record==created){starting=false;closing=true;driveOff();}else if(record==null)detachOwner();}}
    private static byte[] unhex(String s){if(s==null)return new byte[0];byte[]out=new byte[s.length()/2];for(int n=0;n<out.length;n++){int a=Character.digit(s.charAt(2*n),16),b=Character.digit(s.charAt(2*n+1),16);if(a>=0&&b>=0)out[n]=(byte)((a<<4)|b);}return out;}
    private void arm(){try{
        if(record==null||!starting||rootPending)return;
        BtLinkNative.State expected=new BtLinkNative.State();expected.generation=record.generation;command(BtLinkNative.ARM,expected,unhex(record.token));emit();
    }catch(Exception e){fail(e);stop();}}
    private void stop(){queuedStart=null;starting=false;closing=record!=null;if(record==null){refresh();return;}message="正在关闭蓝牙输出";driveOff();}
    private void closePolicy()throws Exception{
        policyCall(NativePolicyControl.Request.btQuery());if(policyClosed())return;
        if(!policyMatches())throw new IllegalStateException("Android 路由属于其他连接");
        if(record.policyStopSent)throw new NativePolicyControl.RpcFailure(-115);record.policyStopSent=true;save();
        policyCall(NativePolicyControl.Request.btRoute(false,policy,record.token,record.peer,record.route));if(!policyClosed())throw new IllegalStateException("Android 路由关闭尚未确认");
    }
    private void driveOff(){try{
        if(record==null){closing=false;emit();return;}if(!record.stage.equals("restore"))record.stage="stopping";save();closePolicy();
        if(root==null){emit();return;}
        if(!root.idle()){
            if(ownsRootSocket&&rootMatches()&&!rootPending&&!disarmSent&&(root.phase==1||root.phase==2||root.phase==3)){disarmSent=true;command(BtLinkNative.DISARM,root,unhex(record.token));}
            emit();return;
        }
        detachOwner();closing=starting=false;ownsRootSocket=false;
        Handoff h=handoff;
        if(h!=null){record.receiver=h.kind;record.stage="receiver";save();handoff=new Handoff(h.kind,h.owner,h.pid,true);message="蓝牙输出已关闭，可以进入接收模式";emit();return;}
        if(policyHeld){record.receiver=policyHoldKind;record.stage="receiver";save();message="接收独占尚未释放";emit();return;}
        restoreAdapter();
    }catch(Exception e){if(link!=null&&ownsRootSocket){ownsRootSocket=false;link.close();}fail(e);}}
    private void restoreAdapter()throws Exception{
        Record r=record;if(r==null)return;
        if(r.adapterOn&&r.adapterChanged&&adapter!=null){
            String nativeState=(String)Class.forName("android.media.AudioSystem").getMethod("getParameters",String.class).invoke(null,"sony_state");
            boolean idle=false;for(String field:nativeState.split(";"))if(field.equals("sony_owner_kind=0"))idle=true;if(!idle)throw new NativePolicyControl.RpcFailure(-16);
            int state=adapter.getState();if(state==BluetoothAdapter.STATE_OFF){if(!r.stage.equals("restore")){r.stage="restore";save();if(!adapter.enable())throw new IllegalStateException("Android 蓝牙恢复未获接受");}message="蓝牙输出已关闭，正在恢复 Android 蓝牙";emit();return;}
            if(state==BluetoothAdapter.STATE_TURNING_ON){message="正在恢复 Android 蓝牙";emit();return;}
            if(state!=BluetoothAdapter.STATE_ON)throw new IllegalStateException("Android 蓝牙状态已变化");
        }
        store.delete();record=null;message=error==0?"未连接":"未连接（上次操作未获确认）";emit();
    }
    private void fail(Exception e){error=e instanceof NativePolicyControl.RpcFailure?((NativePolicyControl.RpcFailure)e).status:-5;message="蓝牙输出状态未确认（"+error+"）";Log.w(TAG,"control",e);emit();}
    private boolean adjustVolume(IBinder token,long route,long revision,int value,boolean mode){
        try{
            if(record==null||root==null)throw new NativePolicyControl.RpcFailure(-19);
            if(rootPending)throw new NativePolicyControl.RpcFailure(-16);
            volumeError=0;command(mode?BtLinkNative.VOLUME_MODE:BtLinkNative.VOLUME_STEP,root,unhex(record.token),value);emit();return true;
        }catch(Exception e){volumeError=e instanceof NativePolicyControl.RpcFailure?((NativePolicyControl.RpcFailure)e).status:-5;Log.w(TAG,"volume command not replayed",e);emit();return false;}
    }
    static HardwareVolumeTarget captureHardwareVolume(){BtLinkService s=instance;return s==null?null:s.hardwareVolumeTarget;}
    static void hardwareVolume(HardwareVolumeTarget target,int direction,HardwareVolumeReply reply){
        BtLinkService s=target.service;
        s.work.post(()->{
            Record r=s.record;BtLinkProtocol.State state=s.latest;
            if(r==null||s.hardwareVolumeReply!=null){reply.done(false,"蓝牙音量暂不可调整");return;}
            s.hardwareVolumeReply=reply;
            if(!s.adjustVolume(target.owner,target.route,state.volume.revision,direction,false))s.finishHardwareVolume(false);
        });
    }
    private void finishHardwareVolume(boolean ok){
        HardwareVolumeReply r=hardwareVolumeReply;hardwareVolumeReply=null;if(r==null)return;
        r.done(ok,ok?"已发送蓝牙设备音量指令；实际音量以设备回报为准":"蓝牙设备音量未获确认；本次不会重发");
    }
    private void volumeState(BtLinkProtocol.State s){
        s.volumeError=volumeError;s.volumeBusy=volumePending;
        if(root!=null){s.volume=root.volume;s.volumeRoute=root.route;}
        BtLinkProtocol.Volume v=s.volume;
        boolean ready=root!=null&&record!=null&&!rootPending;
        s.canVolumeMode=ready;s.canVolume=ready;
        if(!s.connected)return;
        String current=v.has(BtLinkProtocol.Volume.VALUE)?"设备音量 "+v.value+" / 127":"设备未回报当前音量";
        s.volumeMessage=volumePending?"正在调整设备音量…":v.has(BtLinkProtocol.Volume.ECHO)?"已发送，等待设备确认":volumeError!=0?"调节未获确认（"+volumeError+"）；"+current:current;
    }
    private void emit(){BtLinkProtocol.State s=new BtLinkProtocol.State();s.error=error;s.rootPhase=root==null?6:root.phase;s.policyPhase=policy==null?3:policy.phase;s.selected=catalog==null?"":catalog.selected;s.peer=record==null?"":record.peer;s.known=root!=null&&policy!=null;s.connected=s.known&&record!=null&&policyMatches()&&policy.phase==1&&rootMatches()&&root.phase==3&&(root.flags&3)==3;s.streaming=s.connected&&root.nativePhase==2;s.busy=starting||closing;s.canConnect=s.known&&record==null&&root.idle()&&policyClosed()&&!policyHeld&&handoff==null&&!s.selected.isEmpty();s.message=s.connected?(s.streaming?"正在传输音频":"已连接，等待播放"):message;
        volumeState(s);latest=s;
        // Publish a cached route fence. No framework audio lock takes this
        // coordinator's lock or performs synchronous native/Radio IPC.
        hardwareVolumeTarget=record!=null&&record.receiver==0?new HardwareVolumeTarget(this,record,owner):(!s.known?new HardwareVolumeTarget(this,null,null):null);
        for(int n=watchers.size()-1;n>=0;n--){IBinder target=watchers.get(n).token;Parcel p=Parcel.obtain();try{p.writeInterfaceToken(BtLinkProtocol.DESCRIPTOR);s.write(p);if(!target.transact(IBinder.FIRST_CALL_TRANSACTION,p,null,IBinder.FLAG_ONEWAY))unwatch(target);}catch(Exception e){unwatch(target);}finally{p.recycle();}}
    }
    private void unwatch(IBinder target){for(int n=watchers.size()-1;n>=0;n--){Watcher w=watchers.get(n);if(w.token==target){try{w.token.unlinkToDeath(w.death,0);}catch(Exception ignored){}watchers.remove(n);}}}
    private void watch(IBinder target)throws Exception{for(Watcher w:watchers)if(w.token==target){emit();return;}if(watchers.size()>=4||!target.isBinderAlive())throw new IllegalStateException("BT watcher");Watcher w=new Watcher();w.token=target;w.death=()->work.post(()->unwatch(target));target.linkToDeath(w.death,0);watchers.add(w);refresh();}
    public static void catalogChanged(){BtLinkService s=instance;if(s!=null)s.work.post(()->{try{s.readCatalog();if(s.record==null&&s.root!=null&&s.root.idle()&&s.policyClosed()&&s.error==0)s.message=s.catalog.selected.isEmpty()?"请先选择蓝牙输出设备":"未连接";s.emit();}catch(Exception e){s.fail(e);}});}
    public static int prepareReceiver(int kind,IBinder token,int pid){BtLinkService s=instance;if(s==null)return 0;Handoff h=s.handoff;if(h!=null&&h.owner==token&&h.pid==pid&&h.kind==kind&&h.ready)return 0;
        if(h!=null&&h.owner!=token&&h.owner.isBinderAlive())return -16;
        s.work.post(()->{try{Handoff old=s.handoff;if(old!=null&&old.owner==token&&old.pid==pid&&old.kind==kind)return;if(old!=null&&old.owner!=token&&old.owner.isBinderAlive())return;
            if(!token.isBinderAlive())return;s.handoff=new Handoff(kind,token,pid,false);token.linkToDeath(()->receiverReleased(kind,token,pid),0);
            s.load();s.policyCall(NativePolicyControl.Request.btQuery());s.connectRoot();if(s.record==null&&s.root.idle()&&s.policyClosed()){s.handoff=new Handoff(kind,token,pid,true);s.emit();}else s.stop();}catch(Exception e){s.fail(e);}});return -11;
    }
    public static void receiverReleased(int kind,IBinder token,int pid){BtLinkService s=instance;if(s==null)return;s.work.post(()->{Handoff h=s.handoff;if(h!=null&&(h.owner!=token||h.pid!=pid||h.kind!=kind))return;s.handoff=null;try{s.policyCall(NativePolicyControl.Request.btQuery());if(s.record!=null&&s.record.receiver==kind&&!s.policyHeld){s.record.receiver=0;s.closing=true;s.driveOff();}else s.emit();}catch(Exception e){s.fail(e);}});}
    @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws android.os.RemoteException{
        if(code<1||code>7)return super.onTransact(code,data,reply,flags);if(reply==null||(flags&IBinder.FLAG_ONEWAY)!=0||data.hasFileDescriptors()||data.dataSize()>1024)return false;
        data.enforceInterface(BtLinkProtocol.DESCRIPTOR);
        IBinder token=code!=BtLinkProtocol.STATE?data.readStrongBinder():null;
        boolean volume=code==BtLinkProtocol.VOLUME_STEP||code==BtLinkProtocol.VOLUME_MODE;
        long route=volume?data.readLong():0,revision=volume?data.readLong():0;int value=volume?data.readInt():0;
        if(data.dataAvail()!=0||code!=BtLinkProtocol.STATE&&token==null||volume&&(route<1||revision<1)||code==BtLinkProtocol.VOLUME_STEP&&value!=1&&value!=-1||code==BtLinkProtocol.VOLUME_MODE&&value!=0&&value!=1)throw new IllegalArgumentException("BT request");
        long caller=Binder.clearCallingIdentity();try{
            if(code==BtLinkProtocol.CONNECT)work.post(()->{try{startConnection(token);}catch(Exception e){fail(e);}});
            else if(code==BtLinkProtocol.DISCONNECT)work.post(()->{if(owner!=null&&owner!=token&&owner.isBinderAlive()){emit();return;}if(record!=null)record.policyStopSent=false;stop();});
            else if(code==BtLinkProtocol.WATCH)work.post(()->{try{watch(token);}catch(Exception e){fail(e);}});
            else if(code==BtLinkProtocol.UNWATCH)work.post(()->unwatch(token));
            else if(volume)work.post(()->{try{adjustVolume(token,route,revision,value,code==BtLinkProtocol.VOLUME_MODE);}catch(Exception e){volumeError=-1;emit();}});
            else work.post(()->refresh());reply.writeNoException();latest.write(reply);return true;
        }finally{Binder.restoreCallingIdentity(caller);}
    }
    @Override protected void dump(java.io.FileDescriptor fd,PrintWriter out,String[]args){BtLinkProtocol.State s=latest;out.println("Sony BT output known="+s.known+" connected="+s.connected+" streaming="+s.streaming+" busy="+s.busy+" error="+s.error+" root_phase="+s.rootPhase+" policy_phase="+s.policyPhase);out.println(s.message);BtLinkProtocol.Volume v=s.volume;out.println("volume_route="+s.volumeRoute+" volume_revision="+v.revision+" volume_flags="+v.flags+" volume_value="+v.value+" volume_mode="+v.mode+" volume_commands="+v.command+" volume_confirmed="+v.confirmed+" volume_error="+s.volumeError+" volume_busy="+s.volumeBusy+" can_volume="+s.canVolume);out.println(s.volumeMessage);out.println("Cached state only; RF/policy state are observed independently.");}
}
