#!/sony/busybox sh
set -e
umask 022
B=/sony/busybox
D=/dev/zx9-runtime
$B mkdir -p "$D" /vendor/lib/egl
# rootfs source files cannot be bind mounted on this kernel; use real tmpfs.
if [ -f /sony/ld.config-zx9.txt ]; then
    $B cp /sony/ld.config-zx9.txt "$D/ld.config-zx9.txt"
else
    $B cp /system/etc/ld.config.txt "$D/ld.config-zx9.txt"
fi
$B chmod 0755 "$D"
$B chmod 0644 "$D/ld.config-zx9.txt"
$B chmod 0666 /dev/mali /dev/ion 2>/dev/null || :
for config in /system/etc/ld.config.txt /system/etc/ld.config.28.txt /system/etc/ld.config.27.txt; do
    if [ -e "$config" ]; then $B mount --bind "$D/ld.config-zx9.txt" "$config"; fi
done
if [ -f /sony/libGLES_mali-pie.so ]; then
    $B cp /sony/libGLES_mali-pie.so "$D/libGLES_mali.so"
    $B chmod 0644 "$D/libGLES_mali.so"
    $B touch /vendor/lib/egl/libGLES_mali.so
    $B mount --bind "$D/libGLES_mali.so" /vendor/lib/egl/libGLES_mali.so
    if [ -e /system/lib/egl/libGLES_mali.so ]; then
        $B mount --bind "$D/libGLES_mali.so" /system/lib/egl/libGLES_mali.so
    fi
fi
if [ -f /sony/zx9-sdcard-fuse ]; then
    $B cp /sony/zx9-sdcard-fuse "$D/sdcard-fuse"
    $B chmod 0755 "$D/sdcard-fuse"
    if [ -e /system/bin/sdcard ]; then $B mount --bind "$D/sdcard-fuse" /system/bin/sdcard; fi
fi
# Corrected separate zxram was initialized by the bootstrap; built-in zram unused.
/sony/fb-format-probe rgbx || :
echo ZX9_GRAPHICS_SETUP_OK
/system/bin/setprop sys.zx300a.graphics_setup 1
