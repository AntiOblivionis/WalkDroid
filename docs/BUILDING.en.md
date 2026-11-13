# Obtaining proprietary components and building from source

[Home](../README.en.md) · [Partitioning and installation](INSTALL.en.md)

[简体中文](BUILDING.md) · [繁體中文](BUILDING.zh-TW.md) · **English** · [日本語](BUILDING.ja.md)

This guide starts with a ZX300A running clean Walkman One firmware. Extract the Sony audio and radio files from your own player in read-only mode; obtain the Arm graphics driver yourself. The extracted directory and the assembled `boot.img` and `data.img` contain proprietary material. Keep them on your own computer and do not upload them.

`system/zx300a` contains the device adaptation source, `upstream/lineage16` is a snapshot of the Android 9 / LineageOS 16 platform source, and `upstream/sony-open` contains open source published by Sony, including the kernel and BusyBox. See [NOTICE](../NOTICE) for the original licences and copyright notices. Obtain the upstream build tools separately; no previous WalkDroid images or build outputs are required.

## Use prebuilt open components

If you have downloaded a WalkDroid Release of the same version, you can skip the platform and component builds below. Extract the release package into a Linux directory, set `RELEASE`, then extract files from your own player and obtain the Arm driver as described in section 2. The package contains no proprietary Sony or Arm files; the complete corresponding source is in the source repository of the same version.

```sh
export RELEASE="$HOME/walkdroid-release"
export REPO="$RELEASE"
export PRIVATE="$HOME/walkdroid-private"
# After inspect and extract in section 2, and after setting MALI_LIB:
python3 "$RELEASE/tools/assemble_walkdroid.py" \
  --product "$RELEASE/platform" --components "$RELEASE/components" \
  --kernel "$RELEASE/kernel" --busybox "$RELEASE/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

Continue to [partitioning and installation](INSTALL.en.md). The remaining steps describe building from source.

## 1. Prepare directories and tools

Use x86-64 Linux or the Linux filesystem of WSL 2, with at least 250 GB of free space. Do not extract the kernel into a case-insensitive Windows directory: the source contains names that differ only in case. Git LFS retrieves the large source archive parts in the repository.

Prepare `git`, Git LFS, `repo`, Python 3, Python 2.7, JDK 8, Node.js, `make`, C/C++ build tools, `zstd`, `tar`, `patch`, `patchelf`, `e2fsprogs`, `adb` and `fastboot`. Android 9 build scripts require Python 2; you can build it from the [official Python source](https://www.python.org/downloads/release/python-2718/). Also prepare Android SDK `platforms;android-28`, `build-tools;27.0.3` and `platform-tools`, plus [Gradle 4.7](https://services.gradle.org/distributions/gradle-4.7-bin.zip) and [Gradle 4.1](https://services.gradle.org/distributions/gradle-4.1-bin.zip). These are build tools and API compilation interfaces; they are not distributed with the system. The device component example uses an ARM hard-float cross GCC, binutils, a matching sysroot and glibc 2.23 headers. The kernel example uses a version of the [Android arm-eabi GCC 4.8 toolchain](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/) that includes its tool files; do not select an empty branch from which they were removed. You may supply a compatible toolchain instead.

Set all variables below yourself; the paths are examples:

```sh
export REPO="$HOME/WalkDroid"              # absolute path of this repository
export WORK="$HOME/walkdroid-build"         # new Linux working directory
export PRIVATE="$HOME/walkdroid-private"    # created by the extraction tool later
export TOOLS="$HOME/lineage16-build-tools"  # upstream tools checkout you prepare
export PY2_BIN="$HOME/tools/python2/bin"
export JAVA_HOME="$HOME/tools/jdk8"
export ANDROID_SDK="$HOME/Android/Sdk"
export GRADLE47="$HOME/tools/gradle-4.7/bin/gradle"
export GRADLE41="$HOME/tools/gradle-4.1/bin/gradle"
export GCC48_PREFIX="$HOME/tools/arm-eabi-4.8/bin/arm-eabi-"
export KERNEL_LD="${GCC48_PREFIX}ld.bfd"
export CC=/usr/bin/arm-linux-gnueabihf-gcc
export CXX=/usr/bin/arm-linux-gnueabihf-g++
export BIN_DIR=/usr/bin
export HOST_LIB=/usr/lib/x86_64-linux-gnu
export GCC_INCLUDE="$($CC -print-file-name=include)"
export ARM_SYSROOT=/
export GLIBC_INCLUDE="$HOME/tools/glibc-2.23-armhf/include"
export JOBS="$(nproc)"
mkdir -p "$WORK/platform"
```

`GLIBC_INCLUDE` points to ARM glibc headers compatible with the Sony runtime. The headers can be generated from the matching development package or from the glibc source in this repository. Adjust `ARM_SYSROOT`, tool filenames and host compatibility-library locations for your distribution. The commands below use new output directories; adjust `JOBS` to suit available memory.

If you cloned with Git LFS and have not downloaded the source parts, first run `git lfs pull` in the repository. Once the parts are present, extract the sources into a Linux filesystem:

```sh
cd "$REPO"
cat upstream/lineage16/lineage16-source.tar.zst.part-* | zstd -dc | tar -xf - -C "$WORK/platform"
cat upstream/sony-open/linux-3.10.26.tar.zst.part-* > "$WORK/sony-kernel.tar.zst"
```

The Android build system also needs upstream prebuilt **build tools**, such as Clang, Java tools and image tools. You can prepare a complete LineageOS 16 tools checkout:

```sh
mkdir -p "$TOOLS"
cd "$TOOLS"
repo init -u https://github.com/LineageOS/android.git -b lineage-16.0
cp "$REPO/upstream/lineage16-revisions.xml" .repo/manifests/walkdroid.xml
repo init -m walkdroid.xml
repo sync -c -j"$JOBS"
cd "$REPO"
python3 tools/prepare_platform_tools.py --tools-source "$TOOLS" --platform-root "$WORK/platform"
```

The tools checkout supplies build tools only; Android runtime components are built from the sources extracted above. You may provide another tools checkout instead. Some Linux distributions need additional compatible host libraries for older build tools; use `build_walkdroid_platform.py --host-lib-dir` to specify their directory. To substitute Clang, use `--llvm-prebuilts-base`. These arguments are not written into the system and are not device identity requirements.

## 2. Extract files from your own player

Leave the player on its normal Walkman One interface and connect it to the computer by USB. Do not enter USB mass storage mode yet. Use `adb devices -l` to view connections. The ZX300A's original interface is not Android and has no Android “Developer options”. If the player does not appear, check the cable, USB mode and computer-side ADB driver. In every command below, explicitly specify the player's ADB serial. Review the reported model, platform and eMMC CID; once you have confirmed they belong to your player, pass that CID unchanged to the extraction command:

```sh
export SERIAL='enter your player ADB serial here'
python3 "$REPO/tools/extract_walkman_device.py" inspect --serial "$SERIAL"
export CID='enter the eMMC CID just read here'
python3 "$REPO/tools/extract_walkman_device.py" extract \
  --serial "$SERIAL" --cid "$CID" --output "$PRIVATE"
```

The tool reads the necessary files from `/system` and the original boot partition, producing `vendor-system/`, `vendor-boot/` and `stock-boot.img`; it does not write to the device. Reading the boot partition requires a root shell in the original environment. **Do not use `stock-boot.img` for a Fastboot RAM boot**: its original header addresses are unsuitable for this device's Fastboot loader.

Obtain an Android driver for Mali-450 from the [official Arm Mali userspace driver page](https://developer.arm.com/Downloads/-/Mali%20GPUs%20User-Space%20Binary%20Drivers), read the licence for the selected package, and set `MALI_LIB` to the extracted `lib/egl/libGLES_mali.so`. This repository does not provide that file. The graphics adaptation uses the driver binary unmodified; the candidate system has displayed the lock screen and desktop on the player.

```sh
export MALI_LIB="$HOME/your-arm-driver/lib/egl/libGLES_mali.so"
```

## 3. Build the platform, kernel and device components

First generate the boot animation using the artwork and fonts included in the repository. `fonttools`, Pillow and `sharp` are local generation tools.

```sh
python3 -m venv "$WORK/py"
"$WORK/py/bin/pip" install fonttools Pillow
npm install --prefix "$WORK/node" sharp
export WALKDROID_SHARP="$WORK/node/node_modules/sharp"
"$WORK/py/bin/python" "$REPO/assets/walkdroid/build-assets.py" --output "$WORK/art" --no-previews

cd "$REPO"
python3 tools/build_icu4j_data.py --platform-root "$WORK/platform" --work-dir "$WORK/icu" --jobs "$JOBS"
python3 tools/build_walkdroid_android_libraries.py --platform-root "$WORK/platform" \
  --work-dir "$WORK/android-libraries" --sdk "$ANDROID_SDK" --java-home "$JAVA_HOME" \
  --gradle-47 "$GRADLE47" --gradle-41 "$GRADLE41" --jobs "$JOBS"
python3 tools/stage_walkdroid_device_tree.py --platform-root "$WORK/platform" \
  --bootanimation "$WORK/art/bootanimation/bootanimation.zip"
python3 tools/build_walkdroid_platform.py --platform-root "$WORK/platform" \
  --product walkdroid_zx300a-userdebug --target systemimage --apply-policy-compat \
  --python-bin "$PY2_BIN" --java-home "$JAVA_HOME" --host-lib-dir "$HOST_LIB" \
  --jobs "$JOBS" --out-dir "$WORK/android-out"
```

Next build the kernel and static BusyBox:

```sh
python3 tools/build_walkdroid_kernel.py --source-archive "$WORK/sony-kernel.tar.zst" \
  --cross-prefix "$GCC48_PREFIX" --ld "$KERNEL_LD" --jobs "$JOBS" --output "$WORK/kernel"
python3 tools/build_walkdroid_busybox.py \
  --source-archive "$REPO/upstream/sony-open/busybox-1.23.1.tar.zst" --use-source-config \
  --patch "$REPO/system/zx300a/busybox-patches/0001-date-clock-settime.patch" \
  --enable USE_BB_CRYPT --enable USE_BB_CRYPT_SHA --disable FEATURE_HAVE_RPC \
  --disable FEATURE_INETD_RPC --disable FEATURE_MOUNT_NFS \
  --cc "$CC" --binutils-dir "$BIN_DIR" --host-lib-dir "$HOST_LIB" --sysroot "$ARM_SYSROOT" \
  --cflags '-fcommon -std=gnu17' --jobs "$JOBS" --output "$WORK/busybox"
```

Build the device components using the platform files you just built:

```sh
export PRODUCT="$WORK/android-out/target/product/zx300a"
python3 tools/stage_walkdroid_platform_headers.py --platform-root "$WORK/platform" \
  --extra "oreo_sdcard=$REPO/upstream/aosp-oreo-system-core/sdcard" --output "$WORK/headers"
python3 tools/verify_walkdroid_clean_build.py \
  --only native --only sony-runtime --only audiohal --only radio --only graphics \
  --only audio-services --only policy \
  --sony-libs "$PRIVATE/vendor-boot/lib" --glibc-include "$GLIBC_INCLUDE" \
  --cc "$CC" --cxx "$CXX" --binutils-dir "$BIN_DIR" --host-lib-dir "$HOST_LIB" \
  --gcc-include "$GCC_INCLUDE" --android-headers "$WORK/headers" \
  --android-crt "$PRODUCT/obj/lib" --native-libs "$PRODUCT/system/lib" \
  --generated-root "$WORK/android-out/soong/.intermediates" \
  --policy-include "$WORK/platform/frameworks/av/services/audiopolicy" \
  --policy-libs "$PRODUCT/system/lib" --base-policy-service "$PRODUCT/system/lib/libaudiopolicyservice.so" \
  --patchelf "$(command -v patchelf)" \
  --output "$WORK/components"
```

The default build is `userdebug` and uses upstream public test signing keys. If you use your own keys for a production distribution, keep them private; never commit them to the repository or include them in a release package.

## 4. Assemble privately

```sh
python3 tools/assemble_walkdroid.py --product "$PRODUCT" --components "$WORK/components/results" \
  --kernel "$WORK/kernel" --busybox "$WORK/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

The resulting files are `boot.img`, `system.img` and `data.img`. Continue to [partitioning and installation](INSTALL.en.md). Successful assembly means the local images were generated; it does not mean device functionality has passed real-device testing.
