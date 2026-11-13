#!/sony/busybox sh
export LD_LIBRARY_PATH=/system/lib:/system/lib/vndk-28:/system/lib/vndk-sp-28:/vendor/lib
exec /sony/zx9-codec-service
