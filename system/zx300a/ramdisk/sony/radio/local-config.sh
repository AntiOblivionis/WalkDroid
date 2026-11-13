#!/sony/busybox sh
# Android init remounts /dev, so load the device's radio configuration after
# that mount. The factory partition is read-only and its contents stay in RAM.
B=/sony/busybox
$B mkdir -p /dev/walkdroid-native-data
if $B mount -t ext4 -o ro,noload /dev/block/mmcblk0p28 /dev/walkdroid-native-data; then
    $B cp /dev/walkdroid-native-data/BT_Addr /dev/walkdroid-BT_Addr
    $B umount /dev/walkdroid-native-data
fi
$B rmdir /dev/walkdroid-native-data
