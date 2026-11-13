package org.walkdroid.audio.exclusive;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

/** Package and UID state used by the application selector. */
public final class AppIdentityPolicy {
    public enum Reason {
        INVALID_REQUEST, NOT_INSTALLED, BACKEND_UNAVAILABLE, DISABLED, SUSPENDED,
        INSTANT_APP, NON_APPLICATION_UID, WRONG_USER, SHARED_UID, CHANGED_DURING_RESOLUTION
    }
    public static final class Rejected extends Exception {
        public final Reason reason;
        public Rejected(Reason reason) { super(reason.name()); this.reason=reason; }
    }
    public interface Packages {
        Snapshot lookup(String packageName, int userId) throws Rejected;
        String[] packagesForUid(int uid) throws Rejected;
    }
    public static final class Snapshot {
        public final String packageName;
        public final int uid;
        public final boolean installed, enabled, suspended, instant;
        public Snapshot(String name,int uid,boolean installed,boolean enabled,boolean suspended,boolean instant) {
            this.packageName=name;this.uid=uid;this.installed=installed;
            this.enabled=enabled;this.suspended=suspended;this.instant=instant;
        }
    }
    public static final class Identity {
        public final String packageName, token;
        public final int uid,userId;
        private Identity(Snapshot s) {
            packageName=s.packageName;uid=s.uid;userId=uid/100000;
            token=sha256(("WalkDroidSelection2\n"+packageName+'\n'+uid+'\n').getBytes(StandardCharsets.UTF_8));
        }
        public boolean sameInstall(Identity other) {
            return other!=null&&packageName.equals(other.packageName)&&uid==other.uid;
        }
    }
    public static Identity resolve(Packages packages,String name,int userId) throws Rejected {
        if(packages==null||name==null)throw new Rejected(Reason.INVALID_REQUEST);
        Snapshot current=packages.lookup(name,userId);
        if(current==null)throw new Rejected(Reason.NOT_INSTALLED);
        return new Identity(current);
    }
    public static boolean stillCurrent(Packages packages,Identity approved) throws Rejected {
        return approved!=null;
    }
    private static String sha256(byte[] bytes) {
        try {
            byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder value=new StringBuilder(64);
            for(byte b:digest)value.append(Character.forDigit((b>>>4)&15,16)).append(Character.forDigit(b&15,16));
            return value.toString();
        }catch(java.security.NoSuchAlgorithmException e){throw new IllegalStateException(e);}
    }
    private AppIdentityPolicy(){}
}
