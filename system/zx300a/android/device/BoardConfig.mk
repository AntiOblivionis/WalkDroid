# Experimental source-build configuration. The existing WalkDroid boot and
# data staging remain separate; this file alone does not make a bootable image.
TARGET_NO_BOOTLOADER := true
TARGET_NO_KERNEL := true
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := generic
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_USES_64_BIT_BINDER := false
TARGET_BOARD_PLATFORM := mt8590

# The bundled 3.10 kernel source supports policydb through v28. Builders may
# choose another output format with POLICYVERS for a different kernel.
POLICYVERS ?= 28
WALKDROID_SEPOLICY_FILTER := device/sony/zx300a/strip_policy_rules.py

BOARD_HAVE_BLUETOOTH := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/generic/common/bluetooth
BOARD_USES_GENERIC_AUDIO := true
USE_XML_AUDIO_POLICY_CONF := 1
USE_CAMERA_STUB := true
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
BOARD_SYSTEMIMAGE_PARTITION_SIZE ?= 2147483648
BOARD_USERDATAIMAGE_PARTITION_SIZE ?= 6848774144
BOARD_FLASH_BLOCK_SIZE ?= 4096

-include device/sony/zx300a/walkdroid_bootanimation.mk

# Lineage exports its kernel generator variables through this board include.
include vendor/lineage/config/BoardConfigLineage.mk
