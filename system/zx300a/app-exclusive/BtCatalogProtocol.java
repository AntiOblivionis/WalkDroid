package org.walkdroid.audio.exclusive;
import android.os.Parcel;
/** Public pairing metadata only. Selection is a saved target, not RF state. */
public final class BtCatalogProtocol {
    public static final String SERVICE="walkdroid.bt.catalog",DESCRIPTOR="org.walkdroid.bt.catalog.v1";
    public static final int STATE=1,SELECT=2;
    public static final class Peer {
        public String address="",name="";public int deviceClass;
        public void write(Parcel p){p.writeString(address);p.writeString(name);p.writeInt(deviceClass);}
        public static Peer read(Parcel p){Peer v=new Peer();v.address=p.readString();v.name=p.readString();v.deviceClass=p.readInt();if(v.address==null)v.address="";if(v.name==null)v.name="";return v;}
    }
    public static boolean address(String s,boolean empty){return s!=null&&(empty&&s.isEmpty()||s.matches("(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}"));}
    public static final class State {
        public boolean known;public int error,publication;public long generation;
        public String instance="",selected="",message="蓝牙设备状态尚未读取";public Peer[] peers=new Peer[0];
        public void write(Parcel p){p.writeInt(known?1:0);p.writeInt(error);p.writeInt(publication);p.writeLong(generation);p.writeString(instance);p.writeString(selected);p.writeString(message);p.writeInt(peers.length);for(Peer v:peers)v.write(p);}
        public static State read(Parcel p){
            State s=new State();int k=p.readInt();s.known=k==1;s.error=p.readInt();s.publication=p.readInt();s.generation=p.readLong();s.instance=p.readString();s.selected=p.readString();s.message=p.readString();int count=p.readInt();
            count=Math.max(0,Math.min(count,40));s.peers=new Peer[count];for(int i=0;i<count;++i)s.peers[i]=Peer.read(p);
            if(s.instance==null)s.instance="";if(s.selected==null)s.selected="";if(s.message==null)s.message="";return s;
        }
    }
    private BtCatalogProtocol(){}
}
