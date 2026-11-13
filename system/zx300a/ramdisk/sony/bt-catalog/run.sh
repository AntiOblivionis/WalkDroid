#!/sony/busybox sh
set -eu
exec >/dev/walkdroid-bt-catalog.log 2>&1
B=/sony/busybox
export LD_LIBRARY_PATH=/sony/bt-catalog:/system/lib
exec /sony/bt-catalog/sony-bt-catalogd --catalog-only
