#!/sony/busybox sh
set -eu
B=/sony/busybox
for n in /sys/power/state /sys/power/wakeup_count /sys/power/wake_lock /sys/power/wake_unlock /sys/class/rtc/rtc0/wakealarm /sys/devices/platform/mt-rtc/power/wakeup; do
 [ -e "$n" ] || continue
 $B chown 0:1000 "$n"
 $B chmod 0660 "$n"
done
echo WALKDROID_POWER_ACCESS_READY
