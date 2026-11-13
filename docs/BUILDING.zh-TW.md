# 取得閉源元件與從頭編譯

[返回首頁](../README.zh-TW.md) · [分區與安裝](INSTALL.zh-TW.md)

[简体中文](BUILDING.md) · **繁體中文** · [English](BUILDING.en.md) · [日本語](BUILDING.ja.md)

本文從一台執行乾淨 Walkman One 韌體的 ZX300A 開始。Sony 音訊及無線檔案從自己的播放器唯讀擷取；Arm 圖形驅動由建置者自行取得。擷取目錄和最終組裝出的 `boot.img`、`data.img` 含閉源材料，請保留在自己的電腦上，不要上傳。

倉庫中的 `system/zx300a` 是裝置適配原始碼；`upstream/lineage16` 是 Android 9 / LineageOS 16 平台原始碼快照；`upstream/sony-open` 是 Sony 公開的核心、BusyBox 等開放原始碼。原有授權及版權聲明見 [NOTICE](../NOTICE)。上游編譯工具需要另行準備，不需要任何舊版 WalkDroid 映像檔或編譯結果。

## 使用預先編譯的開放元件

如果已下載相同版本的 WalkDroid Release，可以省略下方平台及元件的編譯。將發布包解壓到 Linux 目錄，設定 `RELEASE`，並依第 2 節擷取自己播放器的檔案、取得 Arm 驅動。發布包不含 Sony 或 Arm 閉源檔案；完整對應原始碼位於相同版本的原始碼倉庫。

```sh
export RELEASE="$HOME/walkdroid-release"
export REPO="$RELEASE"
export PRIVATE="$HOME/walkdroid-private"
# 完成第 2 節的 inspect、extract，並設定 MALI_LIB 後：
python3 "$RELEASE/tools/assemble_walkdroid.py" \
  --product "$RELEASE/platform" --components "$RELEASE/components" \
  --kernel "$RELEASE/kernel" --busybox "$RELEASE/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

接著閱讀 [分區與安裝](INSTALL.zh-TW.md)。以下說明自行從原始碼建置的步驟。

## 1. 準備目錄與工具

使用 x86-64 Linux 或 WSL 2 的 Linux 檔案系統，預留至少 250 GB 空間。不要在不區分大小寫的 Windows 目錄中解壓核心：其原始碼含有僅大小寫不同的檔名。Git LFS 用來取得倉庫中的大型原始碼分卷。

準備 `git`、Git LFS、`repo`、Python 3、Python 2.7、JDK 8、Node.js、`make`、C/C++ 編譯工具、`zstd`、`tar`、`patch`、`patchelf`、`e2fsprogs`、`adb` 和 `fastboot`。Android 9 的建置指令碼需要 Python 2；可從 [Python 官方原始碼](https://www.python.org/downloads/release/python-2718/)自行建置。另準備 Android SDK 的 `platforms;android-28`、`build-tools;27.0.3` 和 `platform-tools`，以及 [Gradle 4.7](https://services.gradle.org/distributions/gradle-4.7-bin.zip) 與 [Gradle 4.1](https://services.gradle.org/distributions/gradle-4.1-bin.zip)。這些是建置工具及 API 編譯介面，不隨系統發布。裝置元件範例使用 ARM hard-float 交叉 GCC、binutils、對應 sysroot 和 glibc 2.23 標頭檔。核心範例使用包含工具檔案的 [Android arm-eabi GCC 4.8 工具鏈](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/)版本；不要選擇已移除工具檔案的空分支。也可自行提供相容工具鏈。

以下變數均由建置者設定；路徑只是範例：

```sh
export REPO="$HOME/WalkDroid"              # 此倉庫的絕對路徑
export WORK="$HOME/walkdroid-build"         # 新的 Linux 工作目錄
export PRIVATE="$HOME/walkdroid-private"    # 稍後由擷取工具建立
export TOOLS="$HOME/lineage16-build-tools"  # 自行準備的上游工具檢出
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

`GLIBC_INCLUDE` 是與 Sony 執行階段函式庫相容的 ARM glibc 標頭檔目錄，可從對應開發套件或本倉庫的 glibc 原始碼產生。`ARM_SYSROOT`、工具檔名和宿主相容函式庫位置請依發行版調整。以下命令使用新的輸出目錄；請依記憶體容量調整 `JOBS`。

若以 Git LFS 複製倉庫且尚未下載原始碼分卷，先在倉庫目錄執行 `git lfs pull`。已有完整分卷時，可直接將原始碼解壓至 Linux 檔案系統：

```sh
cd "$REPO"
cat upstream/lineage16/lineage16-source.tar.zst.part-* | zstd -dc | tar -xf - -C "$WORK/platform"
cat upstream/sony-open/linux-3.10.26.tar.zst.part-* > "$WORK/sony-kernel.tar.zst"
```

Android 建置系統還需要上游預先編譯的**建置工具**，例如 Clang、Java 工具及映像檔工具。可以自行準備 LineageOS 16 的完整工具檢出：

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

工具檢出只供應建置工具；Android 執行元件從上一步解出的原始碼建置。也可自行提供其他工具檢出。某些 Linux 發行版需要為舊建置工具額外提供相容宿主函式庫，可用 `build_walkdroid_platform.py --host-lib-dir` 指定其目錄；需要替換 Clang 時可指定 `--llvm-prebuilts-base`。這些參數不會寫入系統，也不是裝置身分要求。

## 2. 從自己的播放器擷取

讓播放器停留在 Walkman One 正常介面，並透過 USB 連接電腦，暫時不要進入 USB 大容量儲存模式。先用 `adb devices -l` 查看連線；ZX300A 原廠介面不是 Android，沒有 Android 的「開發人員選項」。若裝置沒有出現，先檢查資料線、USB 模式及電腦端 ADB 驅動。以下命令皆由你明確指定播放器的 ADB 序號。查看輸出中的型號、平台及 eMMC CID，確認它們屬於手上的播放器後，將該 CID 原樣傳給擷取命令：

```sh
export SERIAL='在此填入播放器 ADB 序號'
python3 "$REPO/tools/extract_walkman_device.py" inspect --serial "$SERIAL"
export CID='在此填入剛才讀到的 eMMC CID'
python3 "$REPO/tools/extract_walkman_device.py" extract \
  --serial "$SERIAL" --cid "$CID" --output "$PRIVATE"
```

工具從裝置讀取 `/system` 中所需檔案及原廠 boot 分區，產生 `vendor-system/`、`vendor-boot/` 與 `stock-boot.img`；它不會寫入裝置。讀取 boot 分區需要原廠環境提供 root shell。**不要對 `stock-boot.img` 執行 Fastboot RAM 啟動**：原廠標頭中的載入位址不適用於本機 Fastboot。

從 [Arm 官方 Mali 使用者空間驅動頁面](https://developer.arm.com/Downloads/-/Mali%20GPUs%20User-Space%20Binary%20Drivers)取得適用於 Mali-450、Android 的驅動並閱讀所選套件的授權，將解出的 `lib/egl/libGLES_mali.so` 路徑設為 `MALI_LIB`。本倉庫不提供該檔案。圖形適配使用未修改的驅動檔案；候選系統已在播放器上顯示鎖定畫面和桌面。

```sh
export MALI_LIB="$HOME/your-arm-driver/lib/egl/libGLES_mali.so"
```

## 3. 建置平台、核心和裝置元件

先產生開機動畫，使用倉庫提供的圖形來源及字型；`fonttools`、Pillow 和 `sharp` 是本機產生工具。

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

接著編譯核心和靜態 BusyBox：

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

使用剛建置的平台檔案編譯裝置元件：

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

預設建置為 `userdebug`，使用上游公開的測試簽章；正式發布若改用自己的簽章，金鑰必須由建置者自行保管，不可提交至倉庫或發布包。

## 4. 私人組裝

```sh
python3 tools/assemble_walkdroid.py --product "$PRODUCT" --components "$WORK/components/results" \
  --kernel "$WORK/kernel" --busybox "$WORK/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

產生的檔案是 `boot.img`、`system.img` 和 `data.img`。接著閱讀 [分區與安裝](INSTALL.zh-TW.md)。組裝完成表示本機映像檔已產生，並不等於裝置功能已通過實機測試。
