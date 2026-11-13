package org.walkdroid.audio.exclusive;
import android.os.Parcel;
/** Versioned UI/framework wire types; no routing authority in this class. */
public final class AppSelectorProtocol {
    public static final String SERVICE="walkdroid.audio.selector",DESCRIPTOR="org.walkdroid.audio.selector.v1";
    public static final int STATE=1,LIST=2,SELECT=3;
    public static final int HOLD_ACQUIRE=4,HOLD_STATUS=5,HOLD_RELEASE=6,HOLD_ABANDON=7;
    public static final class Hold {
        public State state;
        public boolean exists,owned,granted,orphaned;
        public int kind;
        public String nonce="";
        public void write(Parcel p){state.write(p);p.writeInt(exists?1:0);p.writeInt(owned?1:0);p.writeInt(granted?1:0);p.writeInt(orphaned?1:0);p.writeInt(kind);p.writeString(nonce);}
        private static boolean flag(Parcel p){return p.readInt()!=0;}
        public static Hold read(Parcel p){
            Hold h=new Hold();h.state=State.read(p);h.exists=flag(p);h.owned=flag(p);h.granted=flag(p);h.orphaned=flag(p);h.kind=p.readInt();h.nonce=p.readString();
            if(h.nonce==null)h.nonce="";return h;
        }
    }
    public static final class State {
        public boolean known,pending;
        public int error,uid=-1;
        public long generation;
        public String packageName="",token="",message="状态尚未读取";
        public void write(Parcel p){p.writeInt(known?1:0);p.writeInt(pending?1:0);p.writeInt(error);p.writeInt(uid);p.writeLong(generation);p.writeString(packageName);p.writeString(token);p.writeString(message);}
        public static State read(Parcel p){
            State s=new State();s.known=p.readInt()!=0;s.pending=p.readInt()!=0;s.error=p.readInt();s.uid=p.readInt();s.generation=p.readLong();
            s.packageName=text(p.readString());s.token=text(p.readString());s.message=text(p.readString());return s;
        }
    }
    public static final class Choice {
        public final String packageName,label,token;
        public Choice(String name,String label,String token){packageName=name;this.label=label;this.token=token;}
        public void write(Parcel p){p.writeString(packageName);p.writeString(label);p.writeString(token);}
        public static Choice read(Parcel p){return new Choice(text(p.readString()),text(p.readString()),text(p.readString()));}
    }
    private static String text(String value){return value==null?"":value;}
    private AppSelectorProtocol(){}
}
