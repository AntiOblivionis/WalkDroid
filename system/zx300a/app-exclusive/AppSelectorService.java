package org.walkdroid.audio.exclusive;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.os.Binder;
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
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.security.SecureRandom;

/** Existing system_server only. Native commands are serialized and never resubmitted.
 * Package/user events revoke a selection; no polling watchdog or force-stop.
 */
public final class AppSelectorService extends Binder {
    private static final String TAG="WalkDroidSelector";
    private static AppSelectorService instance;
    private final Context context;
    private final AndroidPackageResolver resolver;
    private final Object lock=NativePolicyControl.TRANSACTIONS;
    private final Handler handler;
    private final AtomicFile store=new AtomicFile(new File("/data/system/walkdroid-app-selection"));
    private NativePolicyControl nativeControl;
    private NativePolicyControl.Pending pending;
    private NativePolicyControl.Request pendingRequest;
    private AppIdentityPolicy.Identity pendingIdentity,selected;
    private NativePolicyControl.Result actual;
    private String message="正在读取独占状态";
    private int error;
    private boolean lostStage;
    private boolean recoveryChecked,recoveredPending;
    private final ReceiverHoldCoordinator holds;
    private IBinder receiverOwner;
    private int receiverPid;
    private IBinder.DeathRecipient receiverDeath;
    private static String prop(String key)throws Exception{return(String)Class.forName("android.os.SystemProperties").getMethod("get",String.class).invoke(null,key);}
    public static synchronized void start(){
        if(instance!=null)return;
        try{
            Class<?> a=Class.forName("android.app.ActivityThread");Object thread=a.getMethod("currentActivityThread").invoke(null);
            Context context=(Context)a.getMethod("getSystemContext").invoke(thread);
            AppSelectorService service=new AppSelectorService(context);
            Class.forName("android.os.ServiceManager").getMethod("addService",String.class,IBinder.class).invoke(null,AppSelectorProtocol.SERVICE,service);
            instance=service;service.handler.post(service::initialize);Log.i(TAG,"framework-v2 published uid="+android.os.Process.myUid());
        }catch(Exception e){Log.e(TAG,"initialization failed",e);}
    }
    private AppSelectorService(Context c)throws Exception{
        context=c;resolver=new AndroidPackageResolver(c);attachInterface(null,AppSelectorProtocol.DESCRIPTOR);
        HandlerThread worker=new HandlerThread("WalkDroidSelector",android.os.Process.THREAD_PRIORITY_BACKGROUND);worker.start();handler=new Handler(worker.getLooper());
        String boot=OptionalBootLabel.read(new File("/proc/sys/kernel/random/boot_id"));
        holds=new ReceiverHoldCoordinator(new ReceiverHoldCoordinator.NativePort(){
            public ReceiverHoldCoordinator.Snapshot inspect()throws Exception{
                refresh();requireFresh();return holdSnapshot();
            }
            public ReceiverHoldCoordinator.Snapshot change(boolean enter,int kind,String nonce,long generation)throws Exception{
                operation(NativePolicyControl.Request.hold(enter,kind,nonce,generation),null);requireFresh();return holdSnapshot();
            }
            public void preparePreferredRestore()throws Exception{restorePreference();}
        },new ReceiverHoldStore(),()->{
            byte[] raw=new byte[32];new SecureRandom().nextBytes(raw);StringBuilder text=new StringBuilder(64);
            for(byte value:raw)text.append(String.format(java.util.Locale.ROOT,"%02x",value&255));return text.toString();
        },boot);
        IntentFilter packages=new IntentFilter();packages.addDataScheme("package");
        for(String action:new String[]{Intent.ACTION_PACKAGE_REMOVED,Intent.ACTION_PACKAGE_REPLACED,Intent.ACTION_PACKAGE_CHANGED,Intent.ACTION_PACKAGE_DATA_CLEARED,Intent.ACTION_PACKAGE_FULLY_REMOVED})packages.addAction(action);
        c.registerReceiver(new BroadcastReceiver(){public void onReceive(Context ignored,Intent event){String name=event.getData()==null?null:event.getData().getSchemeSpecificPart();handler.post(()->invalidatePackage(name));}},packages,null,handler);
        IntentFilter users=new IntentFilter();users.addAction("android.intent.action.USER_SWITCHED");users.addAction("android.intent.action.USER_STOPPED");
        c.registerReceiver(new BroadcastReceiver(){public void onReceive(Context ignored,Intent event){handler.post(()->revoke("用户已切换，独占选择已清空"));}},users,null,handler);
    }
    private void connect()throws Exception{
        if(nativeControl!=null&&!nativeControl.alive()){
            // The old native process is gone. Its request is never replayed.
            // Durable receiver protection/preference survives this transport.
            nativeControl=null;pending=null;pendingRequest=null;pendingIdentity=null;
            actual=null;lostStage=false;recoveryChecked=false;recoveredPending=false;
        }
        if(nativeControl==null)nativeControl=new NativePolicyControl(ticket->handler.post(()->{synchronized(lock){if(pending!=null)try{settle();}catch(Exception e){fail(e);}}}));
        if(!recoveryChecked){
            try{
                NativePolicyControl.Result r=nativeControl.recover();
                pending=nativeControl.recovered(r);pendingRequest=pending.request;pendingIdentity=null;recoveredPending=true;
                if(r.phase==1)pending.cancel();
            }catch(NativePolicyControl.RpcFailure e){if(e.status!=-2)throw e;}
            recoveryChecked=true;settle();
        }
    }
    private ReceiverHoldCoordinator.Snapshot holdSnapshot(){return new ReceiverHoldCoordinator.Snapshot(actual.healthy(),actual.held,actual.holdKind,actual.generation);}
    private void requireFresh()throws Exception{
        if(pending!=null||lostStage)throw new NativePolicyControl.RpcFailure(-115);
        if(error!=0)throw new NativePolicyControl.RpcFailure(error);
        if(actual==null||!actual.healthy()||nativeControl==null||!nativeControl.alive())throw new NativePolicyControl.RpcFailure(-19);
    }
    private void restorePreference()throws Exception{
        requireFresh();
        if(selected!=null&&!AppIdentityPolicy.stillCurrent(resolver,selected)){selected=null;saveChoice();}
        int desired=selected==null?-1:selected.uid;
        if(actual.selectedUid!=desired)operation(NativePolicyControl.Request.select(selected,actual.generation),selected);
        requireFresh();
    }
    /** Recovery observes/cancels the outstanding request; never dispatches. */
    private void recoverStage()throws Exception{
        if(!lostStage)return;
        NativePolicyControl.Result r;
        try{r=nativeControl.recover();}catch(NativePolicyControl.RpcFailure e){
            if(e.status!=-2)throw e;lostStage=false;pendingRequest=null;pendingIdentity=null;return;
        }
        pending=nativeControl.recovered(r);lostStage=false;
        if(r.phase==1)pending.cancel();settle();
    }
    private void settle()throws Exception{
        if(pending==null)return;NativePolicyControl.Result r=pending.peek();
        if(r.phase!=NativePolicyControl.DONE){message="独占切换尚未确认";error=-115;return;}
        if(r.snapshotValid)actual=r;
        pending.acknowledge(r);boolean selection=pendingRequest.operation==1&&!recoveredPending;
        AppIdentityPolicy.Identity target=pendingIdentity;pending=null;pendingRequest=null;pendingIdentity=null;lostStage=false;recoveredPending=false;
        if(r.status!=0){error=r.status;message="独占切换失败（"+error+"）";return;}
        error=0;
        if(selection){
            selected=target;
            message=target==null?"普通播放，未指定独占应用":"独占已生效："+target.packageName;
            try{saveChoice();}catch(Exception e){error=-5;message="选择已生效，但保存失败";Log.e(TAG,"save",e);}
            if(selected!=null&&!AppIdentityPolicy.stillCurrent(resolver,selected))handler.post(()->revoke("应用安装身份已变，独占选择已清空"));
        }
    }
    private void operation(NativePolicyControl.Request request,AppIdentityPolicy.Identity identity)throws Exception{
        connect();recoverStage();settle();if(pending!=null)throw new IllegalStateException("previous native command is pending");
        error=0;
        pendingRequest=request;pendingIdentity=identity;
        try{pending=nativeControl.stage(request);}
        catch(NativePolicyControl.RpcFailure definite){pendingRequest=null;pendingIdentity=null;throw definite;}
        catch(Exception ambiguous){lostStage=true;throw ambiguous;}
        pending.dispatch();
        NativePolicyControl.Result r=pending.peek();
        if(r.phase==1){pending.cancel();} // Setter never consumed it. No replay.
        settle();
        requireFresh();
    }
    private void refresh()throws Exception{
        connect();recoverStage();settle();if(pending!=null)return;
        operation(NativePolicyControl.Request.query(),null);
        if(actual==null||!actual.healthy())throw new IllegalStateException("native state unavailable");
        if(selected!=null&&!AppIdentityPolicy.stillCurrent(resolver,selected)){
            if(actual.selectedUid==selected.uid)operation(NativePolicyControl.Request.select(null,actual.generation),null);
            selected=null;saveChoice();
        }
        if(actual.selectedUid==-1){if(holds.record()==null)selected=null;if(error==0)message="普通播放，未指定独占应用";}
    }
    private AppSelectorProtocol.State state(){
        AppSelectorProtocol.State s=new AppSelectorProtocol.State();s.known=actual!=null&&actual.healthy()&&nativeControl!=null&&nativeControl.alive()&&error==0;s.pending=pending!=null||lostStage;s.error=error;
        if(actual!=null){s.uid=actual.selectedUid;s.generation=actual.generation;}
        if(selected!=null){s.packageName=selected.packageName;s.token=selected.token;}
        s.message=message;
        if(error==0&&actual!=null&&actual.healthy()&&actual.held)
            s.message=(actual.holdKind==1?"USB DAC":"蓝牙接收")+" 独占保护中；"+(selected==null?"退出后恢复普通播放":"保留应用选择："+selected.packageName);
        return s;
    }
    private void fail(Exception e){error=e instanceof NativePolicyControl.RpcFailure?((NativePolicyControl.RpcFailure)e).status:-5;message="独占状态未确认（"+error+"），请重新读取";Log.e(TAG,"control",e);}
    private void saveChoice()throws Exception{
        String text=selected==null?"WalkDroidAppSelection1\n\n\n":"WalkDroidAppSelection1\n"+selected.packageName+"\n"+selected.token+"\n";
        FileOutputStream out=null;
        try{out=store.startWrite();out.write(text.getBytes("UTF-8"));store.finishWrite(out);out=null;}
        finally{if(out!=null)store.failWrite(out);}
    }
    private void initialize(){synchronized(lock){
        try{
            AppIdentityPolicy.Identity saved=null;
            if(store.getBaseFile().exists()){
                byte[] raw=new byte[1024];int size=0;
                try(FileInputStream in=store.openRead()){
                    while(size<raw.length){int n=in.read(raw,size,raw.length-size);if(n<=0)break;size+=n;}
                }
                String[] parts=new String(raw,0,size,"UTF-8").split("\n",-1);
                if(parts.length>1&&!parts[1].isEmpty())try{saved=AppIdentityPolicy.resolve(resolver,parts[1],0);}catch(AppIdentityPolicy.Rejected ignored){}
            }
            selected=saved;connect();recoverStage();settle();
            if(pending!=null)throw new NativePolicyControl.RpcFailure(-115);
            operation(NativePolicyControl.Request.query(),null);
            if(holds.record()!=null||actual.held){message="正在保留接收保护，请打开对应接收模式确认状态";return;}
            if(saved!=null&&actual.selectedUid==-1){operation(NativePolicyControl.Request.select(saved,actual.generation),saved);}
            else if(saved!=null&&actual.selectedUid==saved.uid){selected=saved;error=0;message="独占已恢复："+saved.packageName;}
            else if(saved==null&&actual.selectedUid==-1){selected=null;saveChoice();}
        }catch(Exception e){fail(e);}
    }}
    private void invalidatePackage(String name){synchronized(lock){
        if(selected==null||!selected.packageName.equals(name))return;
        try{if(AppIdentityPolicy.stillCurrent(resolver,selected))return;}catch(Exception ignored){}
        revoke("应用安装身份已变，独占选择已清空");
    }}
    private void revoke(String reason){synchronized(lock){try{refresh();if(actual!=null&&actual.selectedUid!=-1)operation(NativePolicyControl.Request.select(null,actual.generation),null);if(actual!=null&&actual.selectedUid==-1){selected=null;saveChoice();message=reason;}}catch(Exception e){fail(e);}}}
    private List<AppSelectorProtocol.Choice> choices(){
        List<AppSelectorProtocol.Choice> result=new ArrayList<>();
        for(ApplicationInfo app:context.getPackageManager().getInstalledApplications(0)){
            if(result.size()>=256)break;
            if(context.getPackageManager().getLaunchIntentForPackage(app.packageName)==null)continue;
            try{AppIdentityPolicy.Identity i=AppIdentityPolicy.resolve(resolver,app.packageName,0);String label=app.loadLabel(context.getPackageManager()).toString();if(label.length()>256)label=label.substring(0,256);result.add(new AppSelectorProtocol.Choice(i.packageName,label,i.token));}catch(AppIdentityPolicy.Rejected ignored){}
        }
        Collections.sort(result,(a,b)->{int c=a.label.compareToIgnoreCase(b.label);return c==0?a.packageName.compareTo(b.packageName):c;});return result;
    }
    private void detachReceiverOwner(){
        if(receiverOwner!=null&&receiverDeath!=null)try{receiverOwner.unlinkToDeath(receiverDeath,0);}catch(java.util.NoSuchElementException ignored){}
        receiverOwner=null;receiverDeath=null;receiverPid=0;
    }
    private void trackReceiverOwner(IBinder owner,int pid)throws Exception{
        if(owner==null){detachReceiverOwner();return;}
        if(receiverOwner!=null&&!holds.owns(receiverOwner,receiverPid))detachReceiverOwner();
        if(!holds.owns(owner,pid))return;
        if(receiverOwner==owner&&receiverPid==pid)return;
        detachReceiverOwner();
        IBinder.DeathRecipient death=()->handler.post(()->{synchronized(lock){
            if(receiverOwner==owner&&receiverPid==pid){
                holds.orphan(owner,pid);detachReceiverOwner();message="接收界面已退出，独占保护保留；请重新打开确认关闭";
                // Death is not an authenticated native OFF receipt.
            }
        }});
        try{owner.linkToDeath(death,0);receiverOwner=owner;receiverPid=pid;receiverDeath=death;}
        catch(Exception e){receiverOwner=owner;receiverPid=pid;receiverDeath=null;}
    }
    private AppSelectorProtocol.Hold holdReply(ReceiverHoldCoordinator.View view){
        AppSelectorProtocol.Hold result=new AppSelectorProtocol.Hold();result.state=state();
        result.exists=view.exists;result.owned=view.owned;result.granted=view.granted;
        result.orphaned=view.orphaned;result.kind=view.kind;result.nonce=view.nonce;return result;
    }
    @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws android.os.RemoteException{
        if(code<1||code>AppSelectorProtocol.HOLD_ABANDON)return super.onTransact(code,data,reply,flags);
        if(reply==null)return false;
        data.readInt();data.readString();int pid=Binder.getCallingPid();
        String name=null,token=null,nonce=null;long generation=0;int kind=0;IBinder owner=null;byte[] off=null;
        if(code==AppSelectorProtocol.SELECT){name=data.readString();token=data.readString();generation=data.readLong();}
        if(code>=AppSelectorProtocol.HOLD_ACQUIRE){
            kind=data.readInt();owner=data.readStrongBinder();
            if(code==AppSelectorProtocol.HOLD_RELEASE){nonce=data.readString();off=data.createByteArray();}
        }
        long caller=Binder.clearCallingIdentity();
        try{synchronized(lock){
            ReceiverHoldCoordinator.View holdView=null;
            try{
                refresh();
                if(code==AppSelectorProtocol.SELECT){
                    AppIdentityPolicy.Identity target=null;
                    if(name!=null&&!name.isEmpty())target=AppIdentityPolicy.resolve(resolver,name,0);
                    operation(NativePolicyControl.Request.select(target,actual==null?0:actual.generation),target);
                }
                if(code==AppSelectorProtocol.HOLD_ACQUIRE){int bt=BtLinkService.prepareReceiver(kind,owner,pid);
                    if(bt!=0){error=bt;message=bt==-11?"正在关闭蓝牙输出，等待接收独占":"另一接收实例正在交接";holdView=holds.unconfirmed(owner,pid);}
                    else holdView=holds.acquire(kind,owner,pid);}
                else if(code==AppSelectorProtocol.HOLD_STATUS)holdView=holds.status(kind,owner,pid);
                else if(code==AppSelectorProtocol.HOLD_RELEASE){holdView=holds.release(kind,owner,pid,nonce,off);if(!holdView.exists)BtLinkService.receiverReleased(kind,owner,pid);}
                else if(code==AppSelectorProtocol.HOLD_ABANDON){holds.orphan(owner,pid);holdView=holds.unconfirmed(owner,pid);if(holds.record()==null)BtLinkService.receiverReleased(kind,owner,pid);}
            }catch(Exception e){fail(e);}
            if(code>=AppSelectorProtocol.HOLD_ACQUIRE){
                try{trackReceiverOwner(owner,pid);}catch(Exception e){fail(e);holdView=null;}
                if(holdView==null||error!=0)holdView=holds.unconfirmed(owner,pid);
                reply.writeNoException();holdReply(holdView).write(reply);return true;
            }
            reply.writeNoException();state().write(reply);
            if(code==AppSelectorProtocol.LIST){List<AppSelectorProtocol.Choice> list=choices();reply.writeInt(list.size());for(AppSelectorProtocol.Choice c:list)c.write(reply);}
            return true;
        }}finally{Binder.restoreCallingIdentity(caller);}
    }
    @Override protected void dump(java.io.FileDescriptor fd,PrintWriter out,String[] args){synchronized(lock){AppSelectorProtocol.State s=state();out.println("WalkDroid selector uid="+android.os.Process.myUid()+" selected_uid="+s.uid+" generation="+s.generation+" known="+s.known+" pending="+s.pending+" error="+s.error+" package="+s.packageName);out.println(s.message);ReceiverHoldCoordinator.Record r=holds.record();out.println("Receiver hold record="+(r!=null)+" kind="+(r==null?0:r.kind)+" phase="+(r==null?-1:r.phase)+" owner_pid="+receiverPid);out.println("Cached state; no native mutation is performed by dump.");}}
}
