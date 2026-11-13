package org.walkdroid.audio.exclusive;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import java.lang.reflect.Method;

/** Native ticket transport for framework operations. */
public final class NativePolicyControl {
    /** One native ticket slot serves all coordinators in system_server.
     * Hold across stage/dispatch/observe/ack, not just individual Binder calls.
     * An unresolved ticket still stays unresolved; this never replays it. */
    public static final Object TRANSACTIONS=new Object();
    public static final String SERVICE="walkdroid.audio.policy";
    public static final String DESCRIPTOR="org.walkdroid.audio.policy.control.v2";
    public static final String PREFIX="walkdroid.policy.ticket.v2:";
    public static final String DEVICE_NAME="WalkDroid policy ticket v2";
    public static final int BUS=0x1000000,AVAILABLE=1,DONE=3;
    public interface Completion {void ready(String ticket);default void routeChanged(){}}
    public static final class RpcFailure extends Exception {
        public final int status;
        RpcFailure(int status){super("Native policy RPC status "+status);this.status=status;}
    }
    public static final class Request {
        public final int operation,uid;
        public final long expectedGeneration;
        public final String identity;
        private Request(int op,int uid,long generation,String identity){this.operation=op;this.uid=uid;expectedGeneration=generation;this.identity=identity;}
        public static Request query(){return new Request(0,-1,0,"");}
        public static Request btQuery(){return new Request(6,-1,0,"");}
        public static Request btRoute(boolean connect,BtPolicyState expected,String token,String peer,long route){
            return new Request(connect?4:5,-1,expected==null?0:expected.generation,BtPolicyState.identity(token,peer,route));
        }
        public static Request select(AppIdentityPolicy.Identity identity,long expected){
            return identity==null?new Request(1,-1,expected,""):new Request(1,identity.uid,expected,identity.token);
        }
        public static Request hold(boolean enter,int kind,String nonce,long expected){
            return new Request(enter?2:3,kind,expected,nonce==null?"":nonce);
        }
    }
    public static final class Result {
        public int phase,status,operation,requestedUid;
        public boolean executed,ownerDead,snapshotValid;
        public long expectedGeneration,generation;
        public String identity,ticket;
        public int selectedUid,references,retiredReferences,outputs,duplicates,unityCalls,effectsRefused;
        public int adapterError,observerError,releaseError,coreError;
        public boolean held;
        public int holdKind;
        public BtPolicyState bt;
        public boolean healthy(){return snapshotValid&&adapterError==0&&observerError==0&&releaseError==0&&coreError==0;}
    }
    private final IBinder remote;
    private final Completion completion;
    private final Binder owner=new Binder(){
        @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags){
            if((code!=IBinder.FIRST_CALL_TRANSACTION&&code!=IBinder.FIRST_CALL_TRANSACTION+1)||(flags&IBinder.FLAG_ONEWAY)==0)return false;
            data.readInt();data.readString();
            if(code==IBinder.FIRST_CALL_TRANSACTION+1){if(completion!=null)completion.routeChanged();return true;}
            String ticket=data.readString();
            if(completion!=null)completion.ready(ticket);return true;
        }
    };
    public NativePolicyControl(Completion completion) throws Exception {
        this.completion=completion;
        remote=(IBinder)Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,SERVICE);
        if(remote==null)throw new RpcFailure(-19);
    }
    public boolean alive(){return remote.isBinderAlive();}
    private interface Writer {void write(Parcel data);}
    private interface Reader<T> {T read(Parcel reply) throws Exception;}
    private <T>T rpc(int code,Writer writer,Reader<T> reader) throws Exception {
        Parcel data=Parcel.obtain(),reply=Parcel.obtain();long caller=Binder.clearCallingIdentity();
        try {
            data.writeInterfaceToken(DESCRIPTOR);if(writer!=null)writer.write(data);
            if(!remote.transact(code,data,reply,0))throw new RpcFailure(-38);
            reply.readException();int status=reply.readInt();if(status!=0)throw new RpcFailure(status);
            T value=reader==null?null:reader.read(reply);
            return value;
        }finally{Binder.restoreCallingIdentity(caller);reply.recycle();data.recycle();}
    }
    private static boolean flag(Parcel p){return p.readInt()!=0;}
    private static Result result(Parcel p){
        Result r=new Result();r.phase=p.readInt();r.executed=flag(p);r.ownerDead=flag(p);r.snapshotValid=flag(p);r.status=p.readInt();
        r.operation=p.readInt();r.requestedUid=p.readInt();r.expectedGeneration=p.readLong();r.identity=p.readString();r.ticket=p.readString();
        if(r.snapshotValid){
            r.generation=p.readLong();r.selectedUid=p.readInt();r.references=p.readInt();r.retiredReferences=p.readInt();r.outputs=p.readInt();r.duplicates=p.readInt();r.unityCalls=p.readInt();r.effectsRefused=p.readInt();
            r.adapterError=p.readInt();r.observerError=p.readInt();r.releaseError=p.readInt();r.coreError=p.readInt();
            r.held=flag(p);r.holdKind=p.readInt();
            if(r.operation>=4&&flag(p))r.bt=BtPolicyState.read(p);
        }
        return r;
    }
    public final class Pending {
        public final String ticket;
        public final Request request;
        private boolean dispatched,acknowledged;
        public Integer dispatchStatus;
        public Throwable dispatchFailure;
        private Pending(String ticket,Request request){this.ticket=ticket;this.request=request;}
        /** Submit exactly once. A failure is retained on this Pending; it does
         * not authorize retry or discard the issued request identity. */
        public synchronized void dispatch(){
            if(dispatched||acknowledged)throw new IllegalStateException("Ticket already submitted/acknowledged");
            dispatched=true;long caller=Binder.clearCallingIdentity();
            try {
                Method method=Class.forName("android.media.AudioSystem").getMethod("setDeviceConnectionState",int.class,int.class,String.class,String.class);
                dispatchStatus=(Integer)method.invoke(null,BUS,AVAILABLE,PREFIX+ticket,DEVICE_NAME);
            }catch(Throwable failure){dispatchFailure=failure;}
            finally{Binder.restoreCallingIdentity(caller);}
        }
        public Result peek() throws Exception {
            if(acknowledged)throw new IllegalStateException("Acknowledged ticket");
            return rpc(2,p->p.writeString(ticket),NativePolicyControl::result);
        }
        public void cancel() throws Exception {if(acknowledged)throw new IllegalStateException("Acknowledged ticket");rpc(4,p->p.writeString(ticket),null);}
        public void acknowledge(Result observed) throws Exception {
            if(acknowledged||observed==null||observed.phase!=DONE)throw new IllegalStateException("No terminal outcome");
            rpc(3,p->p.writeString(ticket),null);acknowledged=true;
        }
    }
    public Pending stage(Request request) throws Exception {
        if(request==null)throw new IllegalArgumentException("request");
        String ticket=rpc(1,p->{p.writeInt(request.operation);p.writeInt(request.uid);p.writeLong(request.expectedGeneration);p.writeString(request.identity);p.writeStrongBinder(owner);},Parcel::readString);
        return new Pending(ticket,request);
    }
    public Result recover() throws Exception{return rpc(6,null,NativePolicyControl::result);}
    public Pending recovered(Result observed){
        if(observed==null)throw new IllegalArgumentException("result");
        Pending pending=new Pending(observed.ticket,new Request(observed.operation,observed.requestedUid,observed.expectedGeneration,observed.identity));
        pending.dispatched=true;return pending; // Recovery observes/cancels/acks; never resubmits.
    }
    // Diagnostic only. Native endpoint permanently rejects forged death events.
    void rejectedOwnerDeath(String ticket) throws Exception {rpc(5,p->p.writeString(ticket),null);}
}
