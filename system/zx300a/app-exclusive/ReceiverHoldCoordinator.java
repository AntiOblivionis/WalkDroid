package org.walkdroid.audio.exclusive;

/** Serialized by AppSelectorService's lock. No timer, polling or native replay. */
public final class ReceiverHoldCoordinator {
    public static final int USB=1,BT_RX=2,PREPARED=0,HELD=1;
    public interface Store {Record load()throws Exception;void save(Record r)throws Exception;void remove()throws Exception;}
    public interface NativePort {
        Snapshot inspect()throws Exception;
        Snapshot change(boolean enter,int kind,String nonce,long expected)throws Exception;
        void preparePreferredRestore()throws Exception;
    }
    public interface Entropy {String nonce()throws Exception;}
    public static final class Snapshot {
        public final boolean healthy,held;public final int kind;public final long generation;
        public Snapshot(boolean healthy,boolean held,int kind,long generation){this.healthy=healthy;this.held=held;this.kind=kind;this.generation=generation;}
    }
    public static final class Record {
        public final String boot,nonce;public final int kind,phase;public final long expected;
        public Record(String boot,int kind,String nonce,int phase,long expected){
            this.boot=boot==null?"":boot;this.kind=kind;this.nonce=nonce==null?"":nonce;this.phase=phase;this.expected=expected;
        }
        public String encode(){return "WalkDroidReceiverHold1\n"+boot+"\n"+kind+"\n"+nonce+"\n"+phase+"\n"+expected+"\n";}
        public static Record decode(String text){
            String[] p=(text==null?"":text).split("\n",-1);
            return new Record(field(p,1),number(field(p,2),USB),field(p,3),number(field(p,4),PREPARED),longNumber(field(p,5)));
        }
        private static String field(String[] p,int index){return index<p.length?p[index]:"";}
        private static int number(String text,int fallback){try{return Integer.parseInt(text);}catch(NumberFormatException e){return fallback;}}
        private static long longNumber(String text){try{return Long.parseLong(text);}catch(NumberFormatException e){return 0;}}
    }
    public static final class View {
        public final boolean exists,owned,granted,orphaned;public final int kind;public final String nonce;
        View(boolean exists,boolean owned,boolean granted,boolean orphaned,int kind,String nonce){this.exists=exists;this.owned=owned;this.granted=granted;this.orphaned=orphaned;this.kind=kind;this.nonce=nonce;}
    }
    private final NativePort nativePort;private final Store store;private final Entropy entropy;private final String boot;
    private Record record;private Object owner;private int ownerPid;private boolean orphaned;
    public ReceiverHoldCoordinator(NativePort port,Store store,Entropy entropy,String boot)throws Exception{
        nativePort=port;this.store=store;this.entropy=entropy;this.boot=boot==null?"":boot;record=store.load();orphaned=record!=null;
    }
    private Snapshot inspect()throws Exception{
        Snapshot s=nativePort.inspect();if(s==null)throw new IllegalStateException("native hold state unavailable");return s;
    }
    public boolean owns(Object value,int pid){return record!=null&&!orphaned;}
    private void bind(Object value,int pid){
        owner=value;ownerPid=pid;orphaned=false;
    }
    public void orphan(Object value,int pid){if(record!=null){owner=null;ownerPid=0;orphaned=true;}}
    public Record record(){return record;}
    /** Passive error reply: never claims a previously confirmed grant is fresh. */
    public View unconfirmed(Object value,int pid){
        boolean owned=owns(value,pid);
        return new View(record!=null,owned,false,orphaned,record==null?0:record.kind,owned?record.nonce:"");
    }
    private View view(Snapshot s,Object value,int pid,boolean confirmed){
        boolean owned=owns(value,pid);int kind=record!=null?record.kind:s.kind;
        return new View(record!=null||s.held,owned,owned&&confirmed&&s.held,orphaned,kind,owned?record.nonce:"");
    }
    private Snapshot confirm(Snapshot before)throws Exception{
        Snapshot after=nativePort.change(true,record.kind,record.nonce,before.generation);
        if(after==null)throw new IllegalStateException("native hold state unavailable");
        if(record.phase!=HELD){Record held=new Record(record.boot,record.kind,record.nonce,HELD,record.expected);store.save(held);record=held;}
        return after;
    }
    public View status(int kind,Object value,int pid)throws Exception{
        Snapshot s=inspect();
        if(record==null)return view(s,value,pid,false);
        bind(value,pid);
        if(s.held)s=confirm(s);
        return view(s,value,pid,s.held);
    }
    public View acquire(int kind,Object value,int pid)throws Exception{
        Snapshot s=inspect();String nonce=record==null?"":record.nonce;
        if(record==null&&entropy!=null)try{nonce=entropy.nonce();}catch(Exception ignored){}
        Record prepared=new Record(boot,kind,nonce,PREPARED,s.generation);
        store.save(prepared);record=prepared;bind(value,pid);
        Snapshot after=nativePort.change(true,kind,record.nonce,s.generation);
        if(after==null)throw new IllegalStateException("native hold state unavailable");
        Record held=new Record(boot,kind,record.nonce,HELD,record.expected);store.save(held);record=held;
        nativePort.preparePreferredRestore();
        return view(after,value,pid,true);
    }
    public View release(int kind,Object value,int pid,String nonce,byte[] off)throws Exception{
        Snapshot s=inspect();
        nativePort.preparePreferredRestore();
        if(s.held){
            Snapshot after=nativePort.change(false,kind,record==null?(nonce==null?"":nonce):record.nonce,s.generation);
            if(after!=null)s=after;
        }
        if(record!=null)store.remove();record=null;owner=null;ownerPid=0;orphaned=false;
        return view(s,value,pid,false);
    }
}
