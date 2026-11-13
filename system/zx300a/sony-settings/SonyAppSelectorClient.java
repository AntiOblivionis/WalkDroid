package org.walkdroid.sony.audio;
import android.os.IBinder;
import android.os.Parcel;
import org.walkdroid.audio.exclusive.AppSelectorProtocol;
/** Settings UI transport only; all identity decisions belong to system_server. */
final class SonyAppSelectorClient {
    static final class Listing {AppSelectorProtocol.State state;AppSelectorProtocol.Choice[] choices;}
    private final IBinder service;
    SonyAppSelectorClient()throws Exception{
        service=(IBinder)Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,AppSelectorProtocol.SERVICE);
        if(service==null||!AppSelectorProtocol.DESCRIPTOR.equals(service.getInterfaceDescriptor()))throw new IllegalStateException("应用独占服务尚未就绪");
    }
    private Listing call(int code,String name,String token,long generation)throws Exception{
        Parcel data=Parcel.obtain(),reply=Parcel.obtain();
        try{data.writeInterfaceToken(AppSelectorProtocol.DESCRIPTOR);
            if(code==AppSelectorProtocol.SELECT){data.writeString(name);data.writeString(token);data.writeLong(generation);}
            if(!service.transact(code,data,reply,0))throw new IllegalStateException("应用独占服务未响应");
            reply.readException();Listing result=new Listing();result.state=AppSelectorProtocol.State.read(reply);
            if(code==AppSelectorProtocol.LIST){int n=reply.readInt();if(n<0||n>256)throw new IllegalStateException("应用列表");result.choices=new AppSelectorProtocol.Choice[n];for(int i=0;i<n;i++)result.choices[i]=AppSelectorProtocol.Choice.read(reply);}
            if(reply.dataAvail()!=0)throw new IllegalStateException("独占回读格式");return result;
        }finally{reply.recycle();data.recycle();}
    }
    AppSelectorProtocol.State state()throws Exception{return call(AppSelectorProtocol.STATE,null,null,0).state;}
    Listing list()throws Exception{return call(AppSelectorProtocol.LIST,null,null,0);}
    AppSelectorProtocol.State select(String name,String token,long generation)throws Exception{return call(AppSelectorProtocol.SELECT,name,token,generation).state;}
}
