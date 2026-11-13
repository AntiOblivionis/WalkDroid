package com.android.server;
/** Pure state machine shared by the framework bridge and host transition tests. */
public final class WalkDroidJackState {
    private int candidate=-1, count, committed=-1;
    public static int parse(String text) {
        if(text==null||text.length()>160)return -1;
        int se=-2,btl=-2,error=Integer.MIN_VALUE,seen=0;
        try {
            for(String part:text.split(";",-1)) {
                String[] field=part.split("=",-1);if(field.length!=2||!field[1].matches("-?[0-9]+"))return -1;
                int value=Integer.parseInt(field[1]),bit;
                if(field[0].equals("sony_jack_se")){bit=1;se=value;}
                else if(field[0].equals("sony_jack_btl")){bit=2;btl=value;}
                else if(field[0].equals("sony_jack_error")){bit=4;error=value;}
                else return -1;
                if((seen&bit)!=0)return -1;seen|=bit;
            }
        }catch(NumberFormatException e){return -1;}
        if(seen!=7||error!=0||se<0||se>1||btl<0||btl>1)return -1;
        // Both headphones use Android's wired port; HAL owns BTL priority
        // and the muted transition between physical amps.
        return se==1||btl==1?1:0;
    }
    /** Three consecutive polls; unknown/failure removes availability safely.
     * Returns -1 for no event. Error state remains distinct in bridge logging. */
    public int sample(int physical) {
        int target=physical==1?1:0;
        if(candidate!=target){candidate=target;count=1;}
        else if(count<3)++count;
        if(count<3||committed==target)return -1;
        committed=target;return target;
    }
    public void retry(){committed=-1;}
    public static int replaceWiredBits(int current,int connected) {
        if(connected!=0&&connected!=1)throw new IllegalArgumentException("connection");
        return (current&~35)|(connected==1?2:0);
    }
}
