#!/sony/busybox sh
export LD_LIBRARY_PATH=/vendor/lib:/system/lib:/system/lib/vndk-28:/system/lib/vndk-sp-28
exec /sony/radio/zx9-radio-service
