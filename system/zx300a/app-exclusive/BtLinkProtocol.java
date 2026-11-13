package org.walkdroid.audio.exclusive;
import android.os.Parcel;
public final class BtLinkProtocol {
    public static final String SERVICE="walkdroid.bt.output",DESCRIPTOR="org.walkdroid.bt.output.v2";
    public static final int STATE=1,CONNECT=2,DISCONNECT=3,WATCH=4,UNWATCH=5,VOLUME_STEP=6,VOLUME_MODE=7;
    public static final class Volume {
        public static final int CONNECTION=1,REMOTE=2,VALUE=4,MODE=8,CALL=16,ECHO=32,SEALED=64;
        public long revision,command,confirmed;
        public int flags,connection,version,features,value,mode;
        public boolean has(int bits){return (flags&bits)==bits;}
        public boolean absolute(){return has(MODE|REMOTE)&&mode==1&&version>=0x104&&(features&8)!=0;}
        public void write(Parcel p){p.writeLong(revision);p.writeInt(flags);p.writeInt(connection);p.writeInt(version);p.writeInt(features);p.writeInt(value);p.writeInt(mode);p.writeLong(command);p.writeLong(confirmed);}
        public static Volume read(Parcel p){Volume v=new Volume();v.revision=p.readLong();v.flags=p.readInt();v.connection=p.readInt();v.version=p.readInt();v.features=p.readInt();v.value=p.readInt();v.mode=p.readInt();v.command=p.readLong();v.confirmed=p.readLong();return v;}
    }
    public static final class State {
        public boolean known,busy,connected,streaming,canConnect;
        public int error,rootPhase,policyPhase;
        public String selected="",peer="",message="正在读取蓝牙输出状态";
        public boolean volumeBusy,canVolume,canVolumeMode;
        public int volumeError;public long volumeRoute;
        public Volume volume=new Volume();public String volumeMessage="连接后可调整设备音量";
        public void write(Parcel p){for(boolean b:new boolean[]{known,busy,connected,streaming,canConnect})p.writeInt(b?1:0);p.writeInt(error);p.writeInt(rootPhase);p.writeInt(policyPhase);p.writeString(selected);p.writeString(peer);p.writeString(message);for(boolean b:new boolean[]{volumeBusy,canVolume,canVolumeMode})p.writeInt(b?1:0);p.writeInt(volumeError);p.writeLong(volumeRoute);volume.write(p);p.writeString(volumeMessage);}
        public static State read(Parcel p){State s=new State();s.known=flag(p);s.busy=flag(p);s.connected=flag(p);s.streaming=flag(p);s.canConnect=flag(p);s.error=p.readInt();s.rootPhase=p.readInt();s.policyPhase=p.readInt();s.selected=p.readString();s.peer=p.readString();s.message=p.readString();s.volumeBusy=flag(p);s.canVolume=flag(p);s.canVolumeMode=flag(p);s.volumeError=p.readInt();s.volumeRoute=p.readLong();s.volume=Volume.read(p);s.volumeMessage=p.readString();if(s.selected==null)s.selected="";if(s.peer==null)s.peer="";if(s.message==null)s.message="";if(s.volumeMessage==null)s.volumeMessage="";return s;}
    }
    private static boolean flag(Parcel p){return p.readInt()!=0;}
}
