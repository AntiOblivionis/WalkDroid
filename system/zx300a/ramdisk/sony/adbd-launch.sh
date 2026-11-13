#!/sony/busybox sh
exec /sbin/adbd --root_seclabel=u:r:su:s0 >/sony/adbd.log 2>&1
