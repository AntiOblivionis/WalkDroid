include $(srctree)/drivers/misc/mediatek/Makefile.custom
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
MMCHOST_PLATFORM_DIR := $(srctree)/drivers/misc/mediatek/mmc-host/mt8590

ccflags-y += -I$(srctree)/arch/arm/mach-$(MTK_PLATFORM)/include/cust
ccflags-y += -I$(MMCHOST_PLATFORM_DIR)

ifeq (BBDMP,$(findstring BBDMP,$(CONFIG_ARCH_MTK_PROJECT)))
  ccflags-y += -DPOWER_CONTROL_ENABLE
endif

obj-m += icx_carddet.o






