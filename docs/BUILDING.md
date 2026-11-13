# 获取闭源部件与从头编译

[返回首页](../README.md) · [分区与安装](INSTALL.md)

**简体中文** · [繁體中文](BUILDING.zh-TW.md) · [English](BUILDING.en.md) · [日本語](BUILDING.ja.md)

本文从一台运行干净 Walkman One 固件的 ZX300A 开始。Sony 音频及无线文件从自己的播放器只读提取；Arm 图形驱动由构建者自行取得。提取目录和最终组装出的 `boot.img`、`data.img` 含闭源材料，留在自己的电脑上，不要上传。

仓库的 `system/zx300a` 是设备适配源码；`upstream/lineage16` 是 Android 9 / LineageOS 16 平台源码快照；`upstream/sony-open` 是 Sony 公开的内核、BusyBox 等开放源码。原有许可及版权声明见 [NOTICE](../NOTICE)。上游编译工具需要另行准备，不需要任何以前的 WalkDroid 镜像或编译结果。

## 使用预编译的开放组件

如果已经下载同版本的 WalkDroid Release，可省去下方的平台及组件编译。将发布包解压到 Linux 目录，设定 `RELEASE`，并按第 2 节提取本人播放器的文件、取得 Arm 驱动。发布包不含 Sony 或 Arm 闭源文件；完整对应源码在同版本源码仓库。

```sh
export RELEASE="$HOME/walkdroid-release"
export REPO="$RELEASE"
export PRIVATE="$HOME/walkdroid-private"
# 完成第 2 节的 inspect、extract，并设置 MALI_LIB 后：
python3 "$RELEASE/tools/assemble_walkdroid.py" \
  --product "$RELEASE/platform" --components "$RELEASE/components" \
  --kernel "$RELEASE/kernel" --busybox "$RELEASE/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

接着阅读 [分区与安装](INSTALL.md)。以下为从源码自行构建的步骤。

## 1. 准备目录与工具

使用 x86-64 Linux 或 WSL 2 的 Linux 文件系统，预留至少 250 GB 空间。不要在不区分大小写的 Windows 目录中解压内核：其源码含仅大小写不同的文件名。Git LFS 用于取得仓库中的大源码分卷。

准备 `git`、Git LFS、`repo`、Python 3、Python 2.7、JDK 8、Node.js、`make`、C/C++ 编译工具、`zstd`、`tar`、`patch`、`patchelf`、`e2fsprogs`、`adb` 和 `fastboot`。Android 9 的构建脚本需要 Python 2；可从 [Python 官方源码](https://www.python.org/downloads/release/python-2718/)自行构建。另准备 Android SDK 的 `platforms;android-28`、`build-tools;27.0.3` 和 `platform-tools`，以及 [Gradle 4.7](https://services.gradle.org/distributions/gradle-4.7-bin.zip) 与 [Gradle 4.1](https://services.gradle.org/distributions/gradle-4.1-bin.zip)。这些是构建工具和 API 编译接口，不随系统发布。设备组件示例使用 ARM hard-float 交叉 GCC、binutils、对应 sysroot 和 glibc 2.23 头文件。内核示例使用 [Android arm-eabi GCC 4.8 工具链](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/)的带工具版本；不要选择已删除工具文件的空分支。也可自行提供兼容工具链。

下面的变量均由构建者设置；路径只是示例：

```sh
export REPO="$HOME/WalkDroid"              # 此仓库的绝对路径
export WORK="$HOME/walkdroid-build"         # 新的 Linux 工作目录
export PRIVATE="$HOME/walkdroid-private"    # 稍后由提取工具创建
export TOOLS="$HOME/lineage16-build-tools"  # 自行准备的上游工具检出
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

`GLIBC_INCLUDE` 是与 Sony 运行库相容的 ARM glibc 头文件目录，头文件可从相应开发包或本仓库的 glibc 源码生成。`ARM_SYSROOT`、工具文件名和宿主兼容库位置随发行版调整。以下命令使用新的输出目录；调整 `JOBS` 以适应内存。

如果通过 Git LFS 克隆且源码分卷尚未下载，先在仓库目录运行 `git lfs pull`。已有完整分卷时，可直接将源码解压到 Linux 文件系统：

```sh
cd "$REPO"
cat upstream/lineage16/lineage16-source.tar.zst.part-* | zstd -dc | tar -xf - -C "$WORK/platform"
cat upstream/sony-open/linux-3.10.26.tar.zst.part-* > "$WORK/sony-kernel.tar.zst"
```

Android 构建系统还需要上游预编译**构建工具**，例如 Clang、Java 工具及图像工具。可以自行准备 LineageOS 16 的完整工具检出：

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

工具检出只供应构建工具；Android 运行组件从上一步解出的源码构建。也可自行提供其他工具检出。某些 Linux 发行版需要为旧构建工具额外提供兼容宿主库，`build_walkdroid_platform.py --host-lib-dir` 可指定其目录；需要替换 Clang 时可指定 `--llvm-prebuilts-base`。这些参数不会写入系统，也不是设备身份要求。

## 2. 从自己的播放器提取

让播放器停留在 Walkman One 正常界面并通过 USB 连接电脑，暂不进入 USB 大容量存储模式。先用 `adb devices -l` 查看连接；ZX300A 原厂界面不是 Android，没有 Android 的“开发者选项”。若设备没有出现，先排查数据线、USB 模式及电脑端 ADB 驱动。以下命令始终由你明确指定播放器 ADB 序列号。查看输出中的型号、平台及 eMMC CID，确认它们属于手上的播放器后，将该 CID 原样传给提取命令：

```sh
export SERIAL='在此填写播放器 ADB 序列号'
python3 "$REPO/tools/extract_walkman_device.py" inspect --serial "$SERIAL"
export CID='在此填写刚才读到的 eMMC CID'
python3 "$REPO/tools/extract_walkman_device.py" extract \
  --serial "$SERIAL" --cid "$CID" --output "$PRIVATE"
```

工具从设备读取 `/system` 中所需文件和原厂 boot 分区，生成 `vendor-system/`、`vendor-boot/` 与 `stock-boot.img`；它不写入设备。读取 boot 分区需要原厂环境提供 root shell。**不要对 `stock-boot.img` 执行 Fastboot RAM 启动**：原厂头中的加载地址不适合本机 Fastboot。

从 [Arm 官方 Mali 用户空间驱动页面](https://developer.arm.com/Downloads/-/Mali%20GPUs%20User-Space%20Binary%20Drivers)获取适用于 Mali-450、Android 的驱动并阅读所选包的许可，将解出的 `lib/egl/libGLES_mali.so` 路径设为 `MALI_LIB`。本仓库不提供该文件。图形适配使用未经修改的驱动文件；候选系统已在播放器上显示锁屏和桌面。

```sh
export MALI_LIB="$HOME/your-arm-driver/lib/egl/libGLES_mali.so"
```

## 3. 构建平台、内核和设备组件

先生成开机动画。它使用随仓库提供的图形源和字体；`fonttools`、Pillow 和 `sharp` 是本机生成工具。

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

然后编译内核和静态 BusyBox：

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

使用刚构建的平台文件编译设备组件：

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

默认构建为 `userdebug`，使用上游公开的测试签名；正式分发若改用自己的签名，密钥必须由构建者自行保管，不能提交到仓库或发布包。

## 4. 私有组装

```sh
python3 tools/assemble_walkdroid.py --product "$PRODUCT" --components "$WORK/components/results" \
  --kernel "$WORK/kernel" --busybox "$WORK/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

生成的文件是 `boot.img`、`system.img` 和 `data.img`。接下来阅读 [分区与安装](INSTALL.md)。组装完成表示本地镜像已生成，并不等于设备功能已经通过实机测试。
