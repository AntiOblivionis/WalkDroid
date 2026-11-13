package org.walkdroid.audio.exclusive;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.concurrent.atomic.AtomicInteger;

/** Fixed public-metadata protocol. The system-server Binder boundary must
 * authenticate the installed controller before using SELECT. No RF/paths.
 * Every operation uses a new packet connection; failed SELECT is never retried.
 */
public final class BtCatalogNative {
    public static final String SOCKET="walkdroid.sony.bt.catalog.v1";
    private static final int MAGIC=0x31434257,MAX=80+40*264;
    private static final AtomicInteger requests=new AtomicInteger();
    public static final class Peer {
        public final String address,name;public final int deviceClass;
        Peer(String address,String name,int deviceClass){this.address=address;this.name=name;this.deviceClass=deviceClass;}
    }
    public static final class State {
        public final int status,publication;public final long generation;
        public final String instance,selected;public final Peer[] peers;
        State(int status,int publication,long generation,String instance,String selected,Peer[] peers){this.status=status;this.publication=publication;this.generation=generation;this.instance=instance;this.selected=selected;this.peers=peers;}
    }
    private static String hex(byte[] raw){StringBuilder s=new StringBuilder();for(byte b:raw)s.append(Character.forDigit((b>>>4)&15,16)).append(Character.forDigit(b&15,16));return s.toString();}
    private static byte[] decode(String s,int bytes){byte[] raw=new byte[bytes];if(s==null)return raw;for(int i=0;i<bytes&&2*i+1<s.length();++i){int a=Character.digit(s.charAt(i*2),16),b=Character.digit(s.charAt(i*2+1),16);if(a>=0&&b>=0)raw[i]=(byte)((a<<4)|b);}return raw;}
    private static byte[] peer(String address){if(address==null||address.isEmpty())return new byte[6];if(!address.matches("(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}"))throw new IllegalArgumentException("peer");byte[] raw=new byte[6];for(int i=0;i<6;++i)raw[i]=(byte)Integer.parseInt(address.substring(i*3,i*3+2),16);return raw;}
    private static String address(byte[] b){boolean zero=true;StringBuilder s=new StringBuilder();for(byte v:b){zero&=v==0;if(s.length()!=0)s.append(':');s.append(String.format(java.util.Locale.ROOT,"%02X",v&255));}return zero?"":s.toString();}
    private static byte[] bytes(ByteBuffer b,int count){byte[] out=new byte[count];b.get(out);return out;}
    private static boolean zero(byte[] b){for(byte v:b)if(v!=0)return false;return true;}
    public State query()throws Exception{return exchange(1,"",0,"");}
    public State select(String instance,long generation,String address)throws Exception{
        return exchange(2,instance,generation,address);
    }
    private State exchange(int operation,String instance,long generation,String address)throws Exception{
        int request=requests.incrementAndGet();if(request==0)request=requests.incrementAndGet();
        byte[] nonce=new byte[16],service=new byte[16];if(operation==2){service=decode(instance,16);new SecureRandom().nextBytes(nonce);}
        ByteBuffer q=ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN);q.putInt(MAGIC).putInt(1).putInt(operation).putInt(request).putLong(generation).put(service).put(nonce).put(peer(address)).putShort((short)0);
        byte[] packet=new byte[MAX];int count;
        try(LocalSocket socket=new LocalSocket(LocalSocket.SOCKET_SEQPACKET)){
            socket.connect(new LocalSocketAddress(SOCKET,LocalSocketAddress.Namespace.ABSTRACT));
            socket.setSoTimeout(6000);socket.getOutputStream().write(q.array());
            // One read of maximum packet size. Reading an80-byte header first
            // would discard the remainder of a SOCK_SEQPACKET response.
            count=socket.getInputStream().read(packet);
        }
        if(count<80)throw new java.io.IOException("catalog response missing");ByteBuffer b=ByteBuffer.wrap(packet,0,count).order(ByteOrder.LITTLE_ENDIAN);
        b.getInt();b.getInt();b.getInt();b.getInt();
        int status=b.getInt(),publication=b.getInt();long actual=b.getLong();byte[] returned=bytes(b,16),echo=bytes(b,16),selected=bytes(b,6),reserved=bytes(b,2);int peers=b.getInt(),length=b.getInt();
        peers=Math.max(0,Math.min(peers,Math.min(40,(count-80)/264)));
        Peer[] list=new Peer[peers];
        for(int n=0;n<peers;++n){byte[] identity=bytes(b,6);int nameBytes=b.getShort()&65535,cod=b.getInt();byte[] name=bytes(b,249),padding=bytes(b,3);String id=address(identity);
            nameBytes=Math.min(nameBytes,249);
            String display=new String(name,0,nameBytes,StandardCharsets.UTF_8).replaceAll("[\\p{Cc}\\p{Cf}]","");list[n]=new Peer(id,display,cod);
        }
        String chosen=address(selected);
        return new State(status,publication,actual,hex(returned),chosen,list);
    }
}
