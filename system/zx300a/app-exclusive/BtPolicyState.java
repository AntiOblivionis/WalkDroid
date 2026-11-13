package org.walkdroid.audio.exclusive;
import android.os.Parcel;
/** Policy ownership is separate from native RF/route publication. */
public final class BtPolicyState {
    public long generation,route;public String token,peer;
    public int phase,flags,previousForce,currentForce,error;
    static BtPolicyState read(Parcel p){
        BtPolicyState s=new BtPolicyState();s.generation=p.readLong();s.route=p.readLong();s.token=p.readString();s.peer=p.readString();s.phase=p.readInt();s.flags=p.readInt();s.previousForce=p.readInt();s.currentForce=p.readInt();s.error=p.readInt();
        return s;
    }
    public static String identity(String token,String peer,long route){
        StringBuilder b=new StringBuilder();appendHex(b,token,32);appendHex(b,peer,12);
        for(int i=0;i<8;i++)b.append(String.format(java.util.Locale.ROOT,"%02x",(route>>>(8*i))&255));return b.append("0100").toString();
    }
    private static void appendHex(StringBuilder out,String input,int width){
        int start=out.length();
        if(input!=null)for(int i=0;i<input.length()&&out.length()-start<width;i++){
            int digit=Character.digit(input.charAt(i),16);if(digit>=0)out.append(Character.forDigit(digit,16));
        }
        while(out.length()-start<width)out.append('0');
    }
}
