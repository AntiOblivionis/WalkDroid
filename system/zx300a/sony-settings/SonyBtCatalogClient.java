package org.walkdroid.sony.audio;
import android.os.IBinder;
import android.os.Parcel;
import org.walkdroid.audio.exclusive.BtCatalogProtocol;
/** UI transport; package and native-store authority stay outside the APK. */
final class SonyBtCatalogClient {
    private final IBinder service;
    SonyBtCatalogClient()throws Exception{
        service=(IBinder)Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,BtCatalogProtocol.SERVICE);
        if(service==null||!BtCatalogProtocol.DESCRIPTOR.equals(service.getInterfaceDescriptor()))throw new IllegalStateException("蓝牙设备服务尚未就绪");
    }
    private BtCatalogProtocol.State call(int code,BtCatalogProtocol.State expected,String address)throws Exception{
        Parcel data=Parcel.obtain(),reply=Parcel.obtain();
        try{data.writeInterfaceToken(BtCatalogProtocol.DESCRIPTOR);
            if(code==BtCatalogProtocol.SELECT){if(expected==null||!expected.known)throw new IllegalStateException("设备列表未确认");data.writeString(expected.instance);data.writeLong(expected.generation);data.writeString(address);}
            if(!service.transact(code,data,reply,0))throw new IllegalStateException("蓝牙设备服务未响应");
            reply.readException();BtCatalogProtocol.State s=BtCatalogProtocol.State.read(reply);if(reply.dataAvail()!=0)throw new IllegalStateException("蓝牙设备回读格式");return s;
        }finally{reply.recycle();data.recycle();}
    }
    BtCatalogProtocol.State state()throws Exception{return call(BtCatalogProtocol.STATE,null,null);}
    BtCatalogProtocol.State select(BtCatalogProtocol.State expected,String address)throws Exception{return call(BtCatalogProtocol.SELECT,expected,address);}
}
