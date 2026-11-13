#!/sony/busybox sh
set -eu
umask 077
RUNTIME=/data/walkdroid/bt-receiver
export LD_LIBRARY_PATH="$RUNTIME/lib:/vendor/lib:/system/lib"
exec "$RUNTIME/sony-bt-receiverd" "$RUNTIME" /data/sony-bt-receiver/b99-sessions - 2>>/dev/sony-bt-receiver.log
