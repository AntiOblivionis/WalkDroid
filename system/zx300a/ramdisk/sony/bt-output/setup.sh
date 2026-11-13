#!/sony/busybox sh
set -eu
exec >/dev/walkdroid-bt-output-setup.log 2>&1
B=/sony/busybox
SOURCE=/data/walkdroid/bt-output
TARGET=/vendor/walkdroid-bt-tx-runtime
trap '/system/bin/setprop sys.walkdroid.sony_bt_tx failed' EXIT
cd "$SOURCE"
$B mount --bind "$SOURCE" "$TARGET"
$B mount --make-private "$TARGET"
$B mount -o remount,bind,ro "$TARGET"
# Enabling module discovery does not connect a peer or acquire either lease.
# No route/seed is created by boot. They require a trusted explicit operation.
# Pie's stock hybrid loads A2DP locally in audioserver (UID1041). Sony
# requires the already-rooted hardware service, like the primary audio HAL.
# The factory dispatch is staged in RAM; the system ext4 is untouched.
STAGE=/dev/walkdroid-bt-output-factory
$B mkdir -p -m 700 "$STAGE"
$B cp /sony/bt-output/framework-audiohal.so "$STAGE/factory.so"
$B cp /sony/bt-output/deathhandler.so "$STAGE/deathhandler.so"
$B chmod 444 "$STAGE/factory.so" "$STAGE/deathhandler.so"
$B mount --bind "$STAGE/factory.so" /system/lib/libaudiohal@2.0.so
$B mount -o remount,bind,ro /system/lib/libaudiohal@2.0.so
$B mount --bind "$STAGE/deathhandler.so" /system/lib/libaudiohal_deathhandler.so
$B mount -o remount,bind,ro /system/lib/libaudiohal_deathhandler.so
/system/bin/setprop sys.walkdroid.sony_bt_tx link-service-v1
trap - EXIT
