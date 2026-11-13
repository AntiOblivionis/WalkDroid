obj-m += zx9_hwbinder.o
zx9_hwbinder-y := zx9_binder.o zx9_compat.o
ccflags-y += -I$(src) -DCONFIG_ANDROID_BINDER_IPC_32BIT=1 -DCONFIG_ANDROID_BINDER_DEVICES=\"hwbinder,vndbinder\"
