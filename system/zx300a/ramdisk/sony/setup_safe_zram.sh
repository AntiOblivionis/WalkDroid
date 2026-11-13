#!/sony/busybox sh
set -e
B=/sony/busybox
# Load the module built alongside the selected kernel.
$B insmod /sony/zx9_zram.ko
V=$($B cat /sys/block/zxram0/dev)
[ -e /dev/block/zxram0 ] || $B mknod /dev/block/zxram0 b ${V%:*} ${V#*:}
echo 201326592 > /sys/block/zxram0/disksize
$B mkswap /dev/block/zxram0
$B swapon -p 100 /dev/block/zxram0
echo 80 > /proc/sys/vm/swappiness
echo 0 > /proc/sys/vm/page-cluster
# Keep the previous production VM reserve and LMK thresholds during first integration.
echo ZX9_SAFE_ZRAM_192M_READY
