#!/sony/busybox sh
set -eu
B=/sony/busybox
SOURCE=/data/walkdroid/runtime/playback
ROOT=/vendor/sony-audio/rootfs
exec >/dev/walkdroid-sony-prepare.log 2>&1
trap '/system/bin/setprop sys.walkdroid.sony_backend failed' EXIT
# Read the player's own region word from its native NVP allocation, read-only.
BLOCK="$($B dd if=/dev/block/mmcblk0p22 bs=1 skip=8714 count=2 2>/dev/null | $B od -An -tu2 | $B tr -d '[:space:]')"
if [ -n "$BLOCK" ]; then
    SHIP="$($B dd if=/dev/block/mmcblk0p22 bs=4 skip=$((BLOCK * 2048)) count=1 2>/dev/null | $B od -An -tu4 | $B tr -d '[:space:]')"
    if [ -n "$SHIP" ]; then /system/bin/setprop ro.walkdroid.sony_ship "$SHIP" || true; fi
fi
$B mount --bind "$SOURCE/rootfs" "$ROOT"
$B mount --make-private "$ROOT"
$B mount -o remount,bind,ro "$ROOT"
CONFIG=/data/sony-audio-controls-v11
$B mount --bind "$CONFIG" "$ROOT/data/config"
for NODE in null urandom snd/controlC0 snd/pcmC0D0p snd/pcmC0D4p snd/timer; do
    $B mount --bind "/dev/$NODE" "$ROOT/dev/$NODE"
done
export LD_LIBRARY_PATH=/vendor/lib:/system/lib
AI_SOURCE="${SOURCE%/playback}/ai/rootfs"
AI_ROOT=/vendor/sony-audio-ai/rootfs
$B mkdir -p "$AI_ROOT"
$B mount --bind "$AI_SOURCE" "$AI_ROOT"
$B mount --make-private "$AI_ROOT"
$B mount -o remount,bind,ro "$AI_ROOT"
/system/bin/setprop sys.walkdroid.sony_ai isolated-v1
/system/bin/setprop sys.walkdroid.sony_audio experimental-v1
/system/bin/setprop sys.walkdroid.sony_backend ready
trap - EXIT
