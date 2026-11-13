#!/sony/busybox sh
set -eu
exec >/dev/walkdroid-bt-link.log 2>&1
B=/sony/busybox
export LD_LIBRARY_PATH=/system/lib:/vendor/lib
exec /sony/bt-link/sony-bt-linkd --service
