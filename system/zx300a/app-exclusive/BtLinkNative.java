package org.walkdroid.audio.exclusive;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
/** One event-driven WLC2 connection in system_server. Closing it revokes this
 * connection's root owner. No request replay or RF action in a read query. */
final class BtLinkNative implements AutoCloseable {
    static final int STATUS=1,ARM=2,COMMIT=3,DISARM=4,VOLUME_STEP=5,VOLUME_MODE=6;
    static final String SOCKET="walkdroid.sony.bt.control.v2";
    interface Listener{void event(State state);void closed(Exception error);}
    interface Reply{void done(State state);void failed(Exception error);}
    static final class State {
        int operation,request,status,phase,nativePhase,flags,pid,lastError;long generation,route,epoch;
        byte[] instance,token,peer;
        BtLinkProtocol.Volume volume;
        String token(){return hex(token);}String address(){return addressOf(peer);}
        boolean idle(){return phase==0&&pid==0&&(flags&~12)==0&&(nativePhase==0||nativePhase==3);}
    }
    private final LocalSocket socket;
    private final Listener listener;
    private final Object lock=new Object();
    private boolean closed;private int next=1;private Reply pending;
    private final byte[] instance;
    final State initial;
    BtLinkNative(Listener listener)throws Exception{
        this.listener=listener;socket=new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
        try{
            socket.connect(new LocalSocketAddress(SOCKET,LocalSocketAddress.Namespace.ABSTRACT));socket.setSoTimeout(5000);
            initial=receive();instance=initial.instance.clone();socket.setSoTimeout(0);
        }catch(Exception e){socket.close();throw e;}
        Thread thread=new Thread(this::readLoop,"WalkDroidBtLinkEvents");thread.setDaemon(true);thread.start();
    }
    static String hex(byte[] raw){StringBuilder b=new StringBuilder();for(byte v:raw)b.append(String.format(java.util.Locale.ROOT,"%02x",v&255));return b.toString();}
    static String addressOf(byte[] raw){boolean zero=true;StringBuilder b=new StringBuilder();for(byte v:raw){zero&=v==0;if(b.length()!=0)b.append(':');b.append(String.format(java.util.Locale.ROOT,"%02X",v&255));}return zero?"":b.toString();}
    private static byte[] bytes(ByteBuffer b,int n){byte[]v=new byte[n];b.get(v);return v;}
    private State receive()throws Exception{
        byte[]packet=new byte[153];int count=socket.getInputStream().read(packet);
        java.io.FileDescriptor[]fds=socket.getAncillaryFileDescriptors();if(fds!=null)for(java.io.FileDescriptor fd:fds)try{new FileInputStream(fd).close();}catch(IOException ignored){}
        if(count!=152)throw new IOException("native BT packet length");ByteBuffer b=ByteBuffer.wrap(packet,0,152).order(ByteOrder.LITTLE_ENDIAN);
        b.getInt();b.getInt();State s=new State();s.operation=b.getInt();s.request=b.getInt();s.status=b.getInt();s.phase=b.getInt();s.generation=b.getLong();s.route=b.getLong();s.instance=bytes(b,16);s.token=bytes(b,16);s.peer=bytes(b,6);bytes(b,2);s.nativePhase=b.getInt();s.flags=b.getInt();s.pid=b.getInt();s.lastError=b.getInt();s.epoch=b.getLong();
        BtLinkProtocol.Volume v=new BtLinkProtocol.Volume();v.revision=b.getLong();v.flags=b.getInt();v.connection=b.getInt();v.version=b.getInt();v.features=b.getInt();v.value=b.getInt();v.mode=b.getInt();v.command=b.getLong();v.confirmed=b.getLong();s.volume=v;
        return s;
    }
    private void readLoop(){
        try{for(;;){State s=receive();Reply callback=null;
            synchronized(lock){if(closed)return;if(s.operation!=0&&pending!=null){callback=pending;pending=null;}}
            if(callback!=null)callback.done(s);else listener.event(s);
        }}catch(Exception e){fail(e);}
    }
    void command(int op,State expected,byte[]token,Reply reply)throws Exception{
        command(op,expected,token,0,reply);
    }
    void command(int op,State expected,byte[]token,int volumeValue,Reply reply)throws Exception{
        synchronized(lock){if(closed||pending!=null)throw new IOException("native BT command unavailable");
            boolean volume=op==VOLUME_STEP||op==VOLUME_MODE;
            if(expected==null)expected=initial;
            byte[] selectedToken=token==null?new byte[16]:Arrays.copyOf(token,16);
            int id=next++;ByteBuffer q=ByteBuffer.allocate(80).order(ByteOrder.LITTLE_ENDIAN);q.putInt(0x32434c57).putInt(2).putInt(op).putInt(id);
            q.putLong(op==STATUS?0:expected.generation).putLong(op==STATUS||op==ARM?0:expected.route).put(instance).put(op==STATUS?new byte[16]:selectedToken);
            q.putLong(volume&&expected.volume!=null?expected.volume.revision:0).putInt(volumeValue).putInt(0);
            pending=reply;
            try{socket.getOutputStream().write(q.array());}catch(Exception e){pending=null;throw e;}
        }
    }
    private void fail(Exception error){Reply p;synchronized(lock){if(closed)return;closed=true;p=pending;pending=null;}
        try{socket.close();}catch(Exception ignored){}if(p!=null)p.failed(error);listener.closed(error);
    }
    public void close(){fail(new IOException("native BT owner closed"));}
}
