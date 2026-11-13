#!/sony/busybox sh
set -eu
B=/sony/busybox
exec >/dev/walkdroid-app-policy-setup.log 2>&1
trap '/system/bin/setprop sys.walkdroid.app_policy failed' EXIT
STAGE=/dev/walkdroid-app-policy
$B mkdir -p -m 700 "$STAGE"
$B touch /data/wdpolicy.so
$B mount -t tmpfs -o size=524288,mode=700,nosuid,nodev tmpfs "$STAGE"
$B cp /sony/app-policy/factory.so "$STAGE/factory.so"
$B cp /sony/app-policy/service.so "$STAGE/service.so"
$B chmod 444 "$STAGE/factory.so" "$STAGE/service.so"
$B mount -o remount,ro,nosuid,nodev "$STAGE"
$B mount --bind "$STAGE/factory.so" /data/wdpolicy.so
$B mount -o remount,bind,ro /data/wdpolicy.so
$B mount --bind "$STAGE/service.so" /system/lib/libaudiopolicyservice.so
$B mount -o remount,bind,ro /system/lib/libaudiopolicyservice.so
/system/bin/setprop sys.walkdroid.app_policy ready
trap - EXIT
