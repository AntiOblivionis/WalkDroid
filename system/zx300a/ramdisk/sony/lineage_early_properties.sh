#!/sony/busybox sh
set -e
umask 022
B=/sony/busybox
# The label is a build input, not a device identity or a boot condition.
WALKDROID_BUILD=$($B cat /sony/walkdroid-build)
system_property() {
    $B awk -F= -v key="$1" '$1 == key {print substr($0, index($0,"=") + 1); exit}' /system/build.prop
}
ANDROID_RELEASE=$(system_property ro.build.version.release)
ANDROID_BUILD_ID=$(system_property ro.build.id)
ANDROID_BUILD_TYPE=$(system_property ro.build.type)
ANDROID_BUILD_TAGS=$(system_property ro.build.tags)
WALKDROID_FINGERPRINT=$($B cat /sony/walkdroid-fingerprint)
WALKDROID_FINGERPRINT=${WALKDROID_FINGERPRINT:-"Sony/zx300a/zx300a:${ANDROID_RELEASE}/${ANDROID_BUILD_ID}/WalkDroid.${WALKDROID_BUILD}.ZX300A:${ANDROID_BUILD_TYPE}/${ANDROID_BUILD_TAGS}"}
apply_build_identity() {
    $B sed -i '/^ro\.build\.display\.id=/d; /^ro\.build\.version\.incremental=/d; /^ro\.build\.description=/d; /^ro\.build\.fingerprint=/d; /^ro\.bootimage\.build\.fingerprint=/d; /^ro\.system\.build\.fingerprint=/d; /^ro\.vendor\.build\.fingerprint=/d; /^ro\.product\.build\.fingerprint=/d; /^ro\.lineage\.version=/d; /^ro\.lineage\.display\.version=/d; /^ro\.modversion=/d' "$1"
    $B cat >> "$1" <<WALKDROID_IDENTITY
ro.build.display.id=${WALKDROID_BUILD} ZX300A
ro.build.version.incremental=WalkDroid.${WALKDROID_BUILD}.ZX300A
ro.build.description=walkdroid_zx300a-${ANDROID_BUILD_TYPE} ${ANDROID_RELEASE} ${ANDROID_BUILD_ID} WalkDroid.${WALKDROID_BUILD}.ZX300A ${ANDROID_BUILD_TAGS}
ro.build.fingerprint=${WALKDROID_FINGERPRINT}
ro.bootimage.build.fingerprint=${WALKDROID_FINGERPRINT}
ro.system.build.fingerprint=${WALKDROID_FINGERPRINT}
ro.vendor.build.fingerprint=${WALKDROID_FINGERPRINT}
ro.product.build.fingerprint=${WALKDROID_FINGERPRINT}
ro.lineage.version=16.0-WalkDroid-${WALKDROID_BUILD}-ZX300A
ro.lineage.display.version=16.0-WalkDroid-${WALKDROID_BUILD}-ZX300A
ro.modversion=16.0-WalkDroid-${WALKDROID_BUILD}-ZX300A
WALKDROID_IDENTITY
}
if [ -f /system/etc/prop.default ]; then
    EARLY_PROP=/system/etc/prop.default
    $B cp "$EARLY_PROP" /dev/zx9-early-prop.default
elif [ -f /sony/platform-default.prop ]; then
    EARLY_PROP=/default.prop
    # Preserve this ramdisk's early settings and retain platform-generated defaults.
    $B cp "$EARLY_PROP" /dev/zx9-early-prop.default
    $B cat /sony/platform-default.prop >> /dev/zx9-early-prop.default
else
    EARLY_PROP=/default.prop
    $B cp "$EARLY_PROP" /dev/zx9-early-prop.default
fi
$B sed -i '/^dalvik\.vm\.isa\.arm\.variant=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.isa\.arm\.features=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.lockprof\.threshold=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.stack-trace-dir=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.heapstartsize=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.heapgrowthlimit=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.heapsize=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.dex2oat-Xms=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.dex2oat-Xmx=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.image-dex2oat-Xms=/d' /dev/zx9-early-prop.default
$B sed -i '/^dalvik\.vm\.image-dex2oat-Xmx=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.model=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.brand=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.manufacturer=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.name=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.device=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.build\.product=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.model\.number=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.board\.platform=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.config\.low_ram=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.sf\.lcd_density=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.setupwizard\.mode=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.zygote=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.hardware\.egl=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.hardware\.gralloc=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.opengles\.version=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.treble\.enabled=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.vndk\.version=/d' /dev/zx9-early-prop.default
$B sed -i '/^qemu\.hw\.mainkeys=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.locale=/d' /dev/zx9-early-prop.default
$B sed -i '/^net\.bt\.name=/d' /dev/zx9-early-prop.default
$B sed -i '/^persist\.sys\.timezone=/d' /dev/zx9-early-prop.default
$B sed -i '/^debug\.sf\.nobootanimation=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.system\.manufacturer=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.vendor\.manufacturer=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.odm\.manufacturer=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.product\.manufacturer=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.system\.brand=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.vendor\.brand=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.odm\.brand=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.product\.brand=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.system\.model=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.vendor\.model=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.odm\.model=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.product\.model=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.marketname=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.system\.marketname=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.vendor\.marketname=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.odm\.marketname=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.product\.marketname=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.system\.device=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.vendor\.device=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.odm\.device=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.product\.device=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.system\.name=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.vendor\.name=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.odm\.name=/d' /dev/zx9-early-prop.default
$B sed -i '/^ro\.product\.product\.name=/d' /dev/zx9-early-prop.default
$B sed -i '/^media\.metrics\.enabled=/d' /dev/zx9-early-prop.default
$B cat >> /dev/zx9-early-prop.default <<'WALKDROID'
media.metrics.enabled=0
dalvik.vm.isa.arm.variant=generic
dalvik.vm.isa.arm.features=default
dalvik.vm.lockprof.threshold=500
dalvik.vm.stack-trace-dir=/data/anr
dalvik.vm.heapstartsize=8m
dalvik.vm.heapgrowthlimit=64m
dalvik.vm.heapsize=96m
dalvik.vm.dex2oat-Xms=16m
dalvik.vm.dex2oat-Xmx=64m
dalvik.vm.image-dex2oat-Xms=16m
dalvik.vm.image-dex2oat-Xmx=64m
ro.product.model=NW-ZX300A
ro.product.brand=Sony
ro.product.manufacturer=Sony
ro.product.name=zx300a
ro.product.device=zx300a
ro.build.product=zx300a
ro.model.number=NW-ZX300Series
ro.board.platform=mt8590
ro.config.low_ram=true
ro.sf.lcd_density=240
ro.setupwizard.mode=DISABLED
ro.zygote=zygote32
ro.hardware.egl=mali
ro.hardware.gralloc=default
ro.opengles.version=131072
ro.treble.enabled=false
ro.vndk.version=28
qemu.hw.mainkeys=0
ro.product.locale=zh-CN
net.bt.name=Sony Walkman ZX300A
persist.sys.timezone=Asia/Shanghai
debug.sf.nobootanimation=0
ro.product.system.manufacturer=Sony
ro.product.vendor.manufacturer=Sony
ro.product.odm.manufacturer=Sony
ro.product.product.manufacturer=Sony
ro.product.system.brand=Sony
ro.product.vendor.brand=Sony
ro.product.odm.brand=Sony
ro.product.product.brand=Sony
ro.product.system.model=NW-ZX300A
ro.product.vendor.model=NW-ZX300A
ro.product.odm.model=NW-ZX300A
ro.product.product.model=NW-ZX300A
ro.product.marketname=Sony Walkman ZX300A
ro.product.system.marketname=Sony Walkman ZX300A
ro.product.vendor.marketname=Sony Walkman ZX300A
ro.product.odm.marketname=Sony Walkman ZX300A
ro.product.product.marketname=Sony Walkman ZX300A
ro.product.system.device=zx300a
ro.product.vendor.device=zx300a
ro.product.odm.device=zx300a
ro.product.product.device=zx300a
ro.product.system.name=zx300a
ro.product.vendor.name=zx300a
ro.product.odm.name=zx300a
ro.product.product.name=zx300a
WALKDROID
apply_build_identity /dev/zx9-early-prop.default
$B chmod 0644 /dev/zx9-early-prop.default
$B mount --bind /dev/zx9-early-prop.default "$EARLY_PROP"
$B cp /system/build.prop /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.isa\.arm\.variant=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.isa\.arm\.features=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.lockprof\.threshold=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.stack-trace-dir=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.heapstartsize=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.heapgrowthlimit=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.heapsize=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.dex2oat-Xms=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.dex2oat-Xmx=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.image-dex2oat-Xms=/d' /dev/zx9-build.prop
$B sed -i '/^dalvik\.vm\.image-dex2oat-Xmx=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.model=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.brand=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.manufacturer=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.name=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.device=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.build\.product=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.model\.number=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.board\.platform=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.config\.low_ram=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.sf\.lcd_density=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.setupwizard\.mode=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.zygote=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.hardware\.egl=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.hardware\.gralloc=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.opengles\.version=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.treble\.enabled=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.vndk\.version=/d' /dev/zx9-build.prop
$B sed -i '/^qemu\.hw\.mainkeys=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.locale=/d' /dev/zx9-build.prop
$B sed -i '/^net\.bt\.name=/d' /dev/zx9-build.prop
$B sed -i '/^persist\.sys\.timezone=/d' /dev/zx9-build.prop
$B sed -i '/^debug\.sf\.nobootanimation=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.system\.manufacturer=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.vendor\.manufacturer=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.odm\.manufacturer=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.product\.manufacturer=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.system\.brand=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.vendor\.brand=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.odm\.brand=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.product\.brand=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.system\.model=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.vendor\.model=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.odm\.model=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.product\.model=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.marketname=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.system\.marketname=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.vendor\.marketname=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.odm\.marketname=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.product\.marketname=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.system\.device=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.vendor\.device=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.odm\.device=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.product\.device=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.system\.name=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.vendor\.name=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.odm\.name=/d' /dev/zx9-build.prop
$B sed -i '/^ro\.product\.product\.name=/d' /dev/zx9-build.prop
$B sed -i '/^media\.metrics\.enabled=/d' /dev/zx9-build.prop
$B cat >> /dev/zx9-build.prop <<'WALKDROID'
media.metrics.enabled=0
dalvik.vm.isa.arm.variant=generic
dalvik.vm.isa.arm.features=default
dalvik.vm.lockprof.threshold=500
dalvik.vm.stack-trace-dir=/data/anr
dalvik.vm.heapstartsize=8m
dalvik.vm.heapgrowthlimit=64m
dalvik.vm.heapsize=96m
dalvik.vm.dex2oat-Xms=16m
dalvik.vm.dex2oat-Xmx=64m
dalvik.vm.image-dex2oat-Xms=16m
dalvik.vm.image-dex2oat-Xmx=64m
ro.product.model=NW-ZX300A
ro.product.brand=Sony
ro.product.manufacturer=Sony
ro.product.name=zx300a
ro.product.device=zx300a
ro.build.product=zx300a
ro.model.number=NW-ZX300Series
ro.board.platform=mt8590
ro.config.low_ram=true
ro.sf.lcd_density=240
ro.setupwizard.mode=DISABLED
ro.zygote=zygote32
ro.hardware.egl=mali
ro.hardware.gralloc=default
ro.opengles.version=131072
ro.treble.enabled=false
ro.vndk.version=28
qemu.hw.mainkeys=0
ro.product.locale=zh-CN
net.bt.name=Sony Walkman ZX300A
persist.sys.timezone=Asia/Shanghai
debug.sf.nobootanimation=0
ro.product.system.manufacturer=Sony
ro.product.vendor.manufacturer=Sony
ro.product.odm.manufacturer=Sony
ro.product.product.manufacturer=Sony
ro.product.system.brand=Sony
ro.product.vendor.brand=Sony
ro.product.odm.brand=Sony
ro.product.product.brand=Sony
ro.product.system.model=NW-ZX300A
ro.product.vendor.model=NW-ZX300A
ro.product.odm.model=NW-ZX300A
ro.product.product.model=NW-ZX300A
ro.product.marketname=Sony Walkman ZX300A
ro.product.system.marketname=Sony Walkman ZX300A
ro.product.vendor.marketname=Sony Walkman ZX300A
ro.product.odm.marketname=Sony Walkman ZX300A
ro.product.product.marketname=Sony Walkman ZX300A
ro.product.system.device=zx300a
ro.product.vendor.device=zx300a
ro.product.odm.device=zx300a
ro.product.product.device=zx300a
ro.product.system.name=zx300a
ro.product.vendor.name=zx300a
ro.product.odm.name=zx300a
ro.product.product.name=zx300a
WALKDROID
apply_build_identity /dev/zx9-build.prop
$B chmod 0644 /dev/zx9-build.prop
$B mount --bind /dev/zx9-build.prop /system/build.prop
