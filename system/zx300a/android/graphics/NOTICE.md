# Android graphics sources

`gralloc/` is based on Android / Arm gralloc sources; original copyright and
Apache-2.0 notices remain in those files. The licence text is `APACHE-2.0.txt`.

`mtk-uapi/mtkfb.h` and `mtk-uapi/mtkfb_info.h` come from Android Open Source
Project CTS, commit `e674434`, `hostsidetests/security/securityPatch/
CVE-2016-8448/`; both carry the AOSP Apache-2.0 notice. They describe the
userspace display ioctl. In an ARM32 build, the sizes, field offsets, format
values and ioctl number used by WalkDroid match the Sony-published MT8590
driver interface. The device component uses these AOSP headers by default;
the Sony GPL kernel headers are not included in the gralloc build.

Source: https://android.googlesource.com/platform/cts/+/e674434/hostsidetests/security/securityPatch/CVE-2016-8448/

Android HIDL sources and libraries are built from the supplied platform tree.
The independently supplied Mali userspace driver retains its own licence.
