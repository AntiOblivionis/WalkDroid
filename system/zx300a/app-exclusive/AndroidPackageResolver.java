package org.walkdroid.audio.exclusive;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;

/** PackageManager adapter for the application's current UID. */
public final class AndroidPackageResolver implements AppIdentityPolicy.Packages {
    private final PackageManager packages;
    public AndroidPackageResolver(Context context) {
        if(context==null)throw new IllegalArgumentException("context");
        packages=context.getPackageManager();
    }
    public AppIdentityPolicy.Snapshot lookup(String name,int userId) throws AppIdentityPolicy.Rejected {
        try {
            ApplicationInfo app=packages.getApplicationInfo(name,0);
            return new AppIdentityPolicy.Snapshot(app.packageName,app.uid,
                (app.flags&ApplicationInfo.FLAG_INSTALLED)!=0,app.enabled,
                (app.flags&ApplicationInfo.FLAG_SUSPENDED)!=0,packages.isInstantApp(name));
        }catch(PackageManager.NameNotFoundException e){throw new AppIdentityPolicy.Rejected(AppIdentityPolicy.Reason.NOT_INSTALLED);}
        catch(SecurityException|IllegalArgumentException e){throw new AppIdentityPolicy.Rejected(AppIdentityPolicy.Reason.BACKEND_UNAVAILABLE);}
    }
    public String[] packagesForUid(int uid) throws AppIdentityPolicy.Rejected {
        try {String[] names=packages.getPackagesForUid(uid);return names==null?null:names.clone();}
        catch(SecurityException|IllegalArgumentException e){throw new AppIdentityPolicy.Rejected(AppIdentityPolicy.Reason.BACKEND_UNAVAILABLE);}
    }
}
