$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

PRODUCT_NAME := walkdroid_zx300a
PRODUCT_DEVICE := zx300a
PRODUCT_BRAND := Sony
PRODUCT_MANUFACTURER := Sony
PRODUCT_MODEL := NW-ZX300A
PRODUCT_BUILD_PROP_OVERRIDES += PRODUCT_NAME=zx300a
-include device/sony/zx300a/walkdroid_overlay.mk
DEVICE_PACKAGE_OVERLAYS += $(WALKDROID_DEVICE_OVERLAY)
PRODUCT_PACKAGES += \
    WalkDroidSonyAudio \
    mount.exfat-fuse \
    android.hardware.bluetooth@1.0 \
    android.hardware.nfc@1.0

PRODUCT_COPY_FILES += \
    device/sony/zx300a/empty-egl-target:system/lib/egl/libGLES_mali.so \
    device/sony/zx300a/no-webview.xml:system/etc/permissions/walkdroid-no-webview.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.software.app_widgets.xml:system/etc/permissions/android.software.app_widgets.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:system/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:system/etc/default_volume_tables.xml
