#!/sony/busybox sh
set -eu
B=/sony/busybox
# Source-built Android already provides these apps. An optional ramdisk
# overlay is used only when the builder explicitly included an APK.
if [ ! -f /sony/radio/apps/Bluetooth/Bluetooth.apk ] && [ ! -f /sony/radio/apps/NfcNci/NfcNci.apk ]; then
 echo WALKDROID_SYSTEM_RADIO_APPS_FROM_SYSTEM
 exit 0
fi
$B mkdir -p /dev/zx9-app-base /dev/zx9-app-tree
$B mount --bind /system/app /dev/zx9-app-base
$B mount --bind /dev/zx9-app-tree /system/app
for source in /dev/zx9-app-base/*; do
 [ -e "$source" ] || continue
 name=${source##*/}
 overlay="/sony/radio/apps/$name/$name.apk"
 if [ -d "$source" ] && [ -f "$overlay" ]; then
  $B mkdir -p "/system/app/$name"
  for part in "$source"/*; do
   [ -e "$part" ] || continue
   leaf=${part##*/}
   [ "$leaf" = "$name.apk" ] && continue
   if [ -d "$part" ]; then $B mkdir -p "/system/app/$name/$leaf"; else $B touch "/system/app/$name/$leaf"; fi
   $B mount --bind "$part" "/system/app/$name/$leaf"
  done
  $B cp "$overlay" "/system/app/$name/$name.apk"
  $B chmod 0644 "/system/app/$name/$name.apk"
  $B rm "$overlay"
 elif [ -d "$source" ]; then
  $B mkdir -p "/system/app/$name"
  $B mount --bind "$source" "/system/app/$name"
 else
  $B touch "/system/app/$name"
  $B mount --bind "$source" "/system/app/$name"
 fi
done
for name in Bluetooth NfcNci; do
 [ -f "/sony/radio/apps/$name/$name.apk" ] || continue
 $B mkdir -p "/system/app/$name"
 $B cp "/sony/radio/apps/$name/$name.apk" "/system/app/$name/$name.apk"
 $B chmod 0644 "/system/app/$name/$name.apk"
 $B rm "/sony/radio/apps/$name/$name.apk"
done
echo WALKDROID_SYSTEM_RADIO_APPS_READY
