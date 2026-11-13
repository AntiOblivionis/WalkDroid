#!/sony/busybox sh
set -eu
B=/sony/busybox
$B insmod /sony/radio/mtk_wmt_detect.ko
$B insmod /sony/radio/mtk_stp_wmt_soc.ko
$B insmod /sony/radio/mtk_stp_bt_soc.ko
echo ZX9_RADIO_KERNEL_MODULES_READY
