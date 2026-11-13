package org.walkdroid.sony.audio;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import org.walkdroid.audio.exclusive.AppSelectorProtocol;

/** One foreground Service instance owns one Binder token. No native ON here. */
final class SonyReceiverHoldClient {
    private final Binder owner=new Binder();
    private final int kind;
    private IBinder service;
    SonyReceiverHoldClient(int kind){if(kind!=1&&kind!=2)throw new IllegalArgumentException("receiver kind");this.kind=kind;}
    private AppSelectorProtocol.Hold call(int code,String nonce,byte[] off)throws Exception{
        if(service==null||!service.isBinderAlive()){
            service=(IBinder)Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,AppSelectorProtocol.SERVICE);
            if(service==null||!AppSelectorProtocol.DESCRIPTOR.equals(service.getInterfaceDescriptor()))throw new IllegalStateException("接收独占服务尚未就绪");
        }
        Parcel data=Parcel.obtain(),reply=Parcel.obtain();
        try{
            data.writeInterfaceToken(AppSelectorProtocol.DESCRIPTOR);data.writeInt(kind);data.writeStrongBinder(owner);
            if(code==AppSelectorProtocol.HOLD_RELEASE){data.writeString(nonce);data.writeByteArray(off);}
            if(!service.transact(code,data,reply,0))throw new IllegalStateException("接收独占服务未响应");
            reply.readException();AppSelectorProtocol.Hold result=AppSelectorProtocol.Hold.read(reply);
            if(reply.dataAvail()!=0)throw new IllegalStateException("接收独占响应格式");return result;
        }finally{reply.recycle();data.recycle();}
    }
    private AppSelectorProtocol.Hold checked(AppSelectorProtocol.Hold h){
        if(!h.state.known||h.state.pending||h.state.error!=0)throw new IllegalStateException(h.state.message);return h;
    }
    AppSelectorProtocol.Hold status()throws Exception{return checked(call(AppSelectorProtocol.HOLD_STATUS,null,null));}
    void begin()throws Exception{
        long deadline=android.os.SystemClock.elapsedRealtime()+45000;AppSelectorProtocol.Hold h;
        // EAGAIN without a prepared hold means the preceding TX handoff is
        // still pending. Once native ENTER has a prepared record, a rejected
        // ticket must be reconciled from a fresh receiver OFF receipt; do not
        // mistake its EAGAIN for permission to submit another ENTER.
        for(;;){h=call(AppSelectorProtocol.HOLD_ACQUIRE,null,null);if(h.state.error!=-11||h.exists)break;
            if(android.os.SystemClock.elapsedRealtime()>=deadline){call(AppSelectorProtocol.HOLD_ABANDON,null,null);throw new IllegalStateException("蓝牙输出关闭仍未确认");}Thread.sleep(150);}
        h=checked(h);
        if(!h.exists||!h.owned||!h.granted||h.kind!=kind)throw new IllegalStateException("接收独占保护尚未确认");
    }
    boolean granted()throws Exception{
        AppSelectorProtocol.Hold h=status();return h.exists&&h.owned&&h.granted&&h.kind==kind;
    }
    void releaseIfOwned(SonyReceiverState off)throws Exception{
        if(off==null||off.active())throw new IllegalStateException("接收尚未关闭");
        AppSelectorProtocol.Hold h=status();
        if(!h.exists||h.kind!=kind)return; // Another receiver's hold is never ours to remove.
        if(!h.owned)throw new IllegalStateException("另一接收实例仍在占用");
        h=checked(call(AppSelectorProtocol.HOLD_RELEASE,h.nonce,off.wire));
        if(h.exists||h.owned||h.granted||h.kind!=0)throw new IllegalStateException("系统声音恢复尚未确认");
    }
    void abandon()throws Exception{call(AppSelectorProtocol.HOLD_ABANDON,null,null);}
}
