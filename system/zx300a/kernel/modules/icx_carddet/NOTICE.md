# Sony card-detection driver

`icx_carddet.c`, `Makefile` and `Android.mk` are from Sony's published
`icx_carddet` kernel-module sources. The source retains its Sony copyright
notice and declares `MODULE_LICENSE("GPL")`. The corresponding kernel
license text is in `system/zx300a/kernel/COPYING`.

The module is compiled against the caller-supplied Sony kernel source during
the normal kernel build; it does not require a precompiled module.
Callback types have been adjusted to match the kernel's driver interfaces.
