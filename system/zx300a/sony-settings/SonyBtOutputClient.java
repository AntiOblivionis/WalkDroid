package org.walkdroid.sony.audio;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import org.walkdroid.audio.exclusive.BtLinkProtocol;
/** Binder client for the Bluetooth output service. */
final class SonyBtOutputClient {
    interface Listener{void state(BtLinkProtocol.State s);void lost();}
    private final Listener listener;private IBinder service;
    private final Binder owner=new Binder(){
        @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags){
            if(code!=FIRST_CALL_TRANSACTION||(flags&FLAG_ONEWAY)==0||data.hasFileDescriptors())return false;
            data.enforceInterface(BtLinkProtocol.DESCRIPTOR);BtLinkProtocol.State s=BtLinkProtocol.State.read(data);if(data.dataAvail()!=0)return false;listener.state(s);return true;
        }
    };
    SonyBtOutputClient(Listener listener){this.listener=listener;}
    private BtLinkProtocol.State call(int code)throws Exception{
        return call(code,0,0,0);
    }
    private BtLinkProtocol.State call(int code,long route,long revision,int value)throws Exception{
        if(service==null||!service.isBinderAlive()){
            service=(IBinder)Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,BtLinkProtocol.SERVICE);
            if(service==null||!BtLinkProtocol.DESCRIPTOR.equals(service.getInterfaceDescriptor()))throw new IllegalStateException("蓝牙输出服务尚未就绪");
            service.linkToDeath(()->listener.lost(),0);
        }
        Parcel q=Parcel.obtain(),r=Parcel.obtain();try{q.writeInterfaceToken(BtLinkProtocol.DESCRIPTOR);if(code!=BtLinkProtocol.STATE)q.writeStrongBinder(owner);
            if(code==BtLinkProtocol.VOLUME_STEP||code==BtLinkProtocol.VOLUME_MODE){q.writeLong(route);q.writeLong(revision);q.writeInt(value);}
            if(!service.transact(code,q,r,0))throw new IllegalStateException("蓝牙输出服务未响应");r.readException();BtLinkProtocol.State s=BtLinkProtocol.State.read(r);if(r.dataAvail()!=0)throw new IllegalStateException("蓝牙输出响应格式");return s;
        }finally{r.recycle();q.recycle();}
    }
    BtLinkProtocol.State watch()throws Exception{return call(BtLinkProtocol.WATCH);}
    BtLinkProtocol.State state()throws Exception{return call(BtLinkProtocol.STATE);}
    BtLinkProtocol.State connect()throws Exception{return call(BtLinkProtocol.CONNECT);}
    BtLinkProtocol.State disconnect()throws Exception{return call(BtLinkProtocol.DISCONNECT);}
    void volume(BtLinkProtocol.State expected,int value,boolean mode)throws Exception{call(mode?BtLinkProtocol.VOLUME_MODE:BtLinkProtocol.VOLUME_STEP,expected.volumeRoute,expected.volume.revision,value);}
    void unwatch()throws Exception{call(BtLinkProtocol.UNWATCH);}
}
