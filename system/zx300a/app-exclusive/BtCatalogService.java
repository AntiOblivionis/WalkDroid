package org.walkdroid.audio.exclusive;
import android.content.Context;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.util.Log;
import java.io.FileInputStream;
import java.io.PrintWriter;
/** Existing system_server metadata boundary. No RF/audio operations.
 * A failed SELECT is reported once and never replayed after reconnect.
 */
public final class BtCatalogService extends Binder {
    private static final String TAG="WalkDroidBtCatalog";
    private static BtCatalogService instance;
    private final Context context;private final Object lock=new Object();
    private final BtCatalogNative nativeControl=new BtCatalogNative();
    private BtCatalogProtocol.State latest=new BtCatalogProtocol.State();
    private static String prop(String key)throws Exception{return(String)Class.forName("android.os.SystemProperties").getMethod("get",String.class).invoke(null,key);}
    public static synchronized void start(){
        if(instance!=null)return;
        try{
            Class<?> a=Class.forName("android.app.ActivityThread");Object thread=a.getMethod("currentActivityThread").invoke(null);Context context=(Context)a.getMethod("getSystemContext").invoke(thread);
            BtCatalogService service=new BtCatalogService(context);
            Class.forName("android.os.ServiceManager").getMethod("addService",String.class,IBinder.class).invoke(null,BtCatalogProtocol.SERVICE,service);
            instance=service;Log.i(TAG,"metadata-v1 published uid="+android.os.Process.myUid());
        }catch(Exception e){Log.e(TAG,"initialization failed",e);}
    }
    private BtCatalogService(Context c)throws Exception{
        context=c;attachInterface(null,BtCatalogProtocol.DESCRIPTOR);
    }
    private BtCatalogProtocol.State result(BtCatalogNative.State raw){
        BtCatalogProtocol.State s=new BtCatalogProtocol.State();s.error=raw.status;s.publication=raw.publication;
        if(raw.status==0){s.known=true;s.generation=raw.generation;s.instance=raw.instance;s.selected=raw.selected;s.peers=new BtCatalogProtocol.Peer[raw.peers.length];
            for(int n=0;n<raw.peers.length;++n){BtCatalogProtocol.Peer p=new BtCatalogProtocol.Peer();p.address=raw.peers[n].address;p.name=raw.peers[n].name;p.deviceClass=raw.peers[n].deviceClass;s.peers[n]=p;}
            s.message=s.selected.isEmpty()?"未选择蓝牙输出设备":"蓝牙输出设备已选择";
        }else s.message=raw.status==-116?"设备列表已变化，请重新打开列表":raw.status==-115?"保存结果尚未确认，请重新读取":"蓝牙设备状态未确认，请重新读取";
        return s;
    }
    @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws android.os.RemoteException{
        if(code!=BtCatalogProtocol.STATE&&code!=BtCatalogProtocol.SELECT)return super.onTransact(code,data,reply,flags);
        if(reply==null||(flags&IBinder.FLAG_ONEWAY)!=0||data.hasFileDescriptors()||data.dataSize()>1024)return false;
        data.enforceInterface(BtCatalogProtocol.DESCRIPTOR);
        String service=null,peer=null;long generation=0;
        if(code==BtCatalogProtocol.SELECT){service=data.readString();generation=data.readLong();peer=data.readString();}
        if(data.dataAvail()!=0)throw new IllegalArgumentException("trailing Bluetooth request");
        long caller=Binder.clearCallingIdentity();
        try{synchronized(lock){
            try{latest=result(code==BtCatalogProtocol.STATE?nativeControl.query():nativeControl.select(service,generation,peer));}
            catch(Exception e){latest=new BtCatalogProtocol.State();latest.error=-5;latest.message="蓝牙设备状态未确认，请重新读取";Log.w(TAG,"catalog operation failed",e);}
            if(code==BtCatalogProtocol.SELECT&&latest.known&&latest.error==0)BtLinkService.catalogChanged();
            reply.writeNoException();latest.write(reply);return true;
        }}finally{Binder.restoreCallingIdentity(caller);}
    }
    @Override protected void dump(java.io.FileDescriptor fd,PrintWriter out,String[] args){synchronized(lock){out.println("Sony Bluetooth catalog known="+latest.known+" error="+latest.error+" generation="+latest.generation+" publication="+latest.publication+" peers="+latest.peers.length);out.println("Cached metadata only; selection does not request RF or an audio connection.");}}
}
