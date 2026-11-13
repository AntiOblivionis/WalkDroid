# 非公開コンポーネントの取得とソースからのビルド

[ホームへ戻る](../README.ja.md) · [パーティションとインストール](INSTALL.ja.md)

[简体中文](BUILDING.md) · [繁體中文](BUILDING.zh-TW.md) · [English](BUILDING.en.md) · **日本語**

このガイドは、クリーンな Walkman One ファームウェアを実行している ZX300A から始めます。Sony の音声・無線ファイルは自分のプレーヤーから読み取り専用で取得し、Arm のグラフィックスドライバーはビルドする人が自分で入手します。取得したディレクトリと組み立て後の `boot.img`、`data.img` には非公開コンポーネントが含まれます。自分のパソコンに保管し、アップロードしないでください。

リポジトリの `system/zx300a` は端末向けの適合ソース、`upstream/lineage16` は Android 9 / LineageOS 16 プラットフォームのソーススナップショット、`upstream/sony-open` は Sony が公開したカーネルや BusyBox などのオープンソースです。元のライセンスと著作権表示は [NOTICE](../NOTICE) を参照してください。上流のビルドツールは別途準備します。以前の WalkDroid イメージやビルド成果物は必要ありません。

## ビルド済みのオープンコンポーネントを使う

同じバージョンの WalkDroid Release をダウンロード済みの場合、以下のプラットフォームとコンポーネントのビルドを省略できます。リリースパッケージを Linux のディレクトリに展開して `RELEASE` を設定し、第 2 節に従って自分のプレーヤーからファイルを取得し、Arm ドライバーを入手してください。パッケージに Sony や Arm の非公開ファイルは含まれません。完全な対応ソースは同じバージョンのソースリポジトリにあります。

```sh
export RELEASE="$HOME/walkdroid-release"
export REPO="$RELEASE"
export PRIVATE="$HOME/walkdroid-private"
# 第 2 節の inspect・extract と MALI_LIB の設定後：
python3 "$RELEASE/tools/assemble_walkdroid.py" \
  --product "$RELEASE/platform" --components "$RELEASE/components" \
  --kernel "$RELEASE/kernel" --busybox "$RELEASE/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

続いて [パーティションとインストール](INSTALL.ja.md)を参照してください。以下はソースから自分でビルドする手順です。

## 1. ディレクトリとツールの準備

x86-64 Linux または WSL 2 の Linux ファイルシステムを使用し、250 GB 以上の空き容量を確保してください。カーネルを大文字・小文字を区別しない Windows のディレクトリに展開しないでください。ソースには大文字・小文字だけが異なるファイル名が含まれます。リポジトリ内の大きなソース分割ファイルの取得には Git LFS を使用します。

`git`、Git LFS、`repo`、Python 3、Python 2.7、JDK 8、Node.js、`make`、C/C++ ビルドツール、`zstd`、`tar`、`patch`、`patchelf`、`e2fsprogs`、`adb`、`fastboot` を準備します。Android 9 のビルドスクリプトには Python 2 が必要です。[Python 公式ソース](https://www.python.org/downloads/release/python-2718/)から自分でビルドできます。さらに Android SDK の `platforms;android-28`、`build-tools;27.0.3`、`platform-tools`、[Gradle 4.7](https://services.gradle.org/distributions/gradle-4.7-bin.zip)、[Gradle 4.1](https://services.gradle.org/distributions/gradle-4.1-bin.zip) を準備します。これらはビルドツールと API コンパイル用インターフェースであり、システムには同梱されません。端末コンポーネントの例では ARM hard-float クロス GCC、binutils、対応する sysroot と glibc 2.23 ヘッダーを使います。カーネルの例ではツールファイルを含む [Android arm-eabi GCC 4.8 ツールチェーン](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/)のバージョンを使います。ツールファイルが削除された空のブランチを選ばないでください。互換ツールチェーンを自分で用意することもできます。

以下の変数はビルドする人が設定します。パスは例です。

```sh
export REPO="$HOME/WalkDroid"              # このリポジトリの絶対パス
export WORK="$HOME/walkdroid-build"         # 新しい Linux 作業ディレクトリ
export PRIVATE="$HOME/walkdroid-private"    # 後で取得ツールが作成
export TOOLS="$HOME/lineage16-build-tools"  # 自分で準備する上流ツールのチェックアウト
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

`GLIBC_INCLUDE` は Sony のランタイムライブラリと互換性のある ARM glibc ヘッダーディレクトリです。ヘッダーは対応する開発パッケージまたは本リポジトリの glibc ソースから生成できます。`ARM_SYSROOT`、ツールのファイル名、ホスト互換ライブラリの位置はディストリビューションに合わせて調整してください。以下のコマンドは新しい出力ディレクトリを使用します。メモリに合わせて `JOBS` を調整してください。

Git LFS でクローンし、ソースの分割ファイルがまだダウンロードされていない場合は、まずリポジトリのディレクトリで `git lfs pull` を実行します。分割ファイルがそろっていれば、ソースを Linux ファイルシステムに展開できます。

```sh
cd "$REPO"
cat upstream/lineage16/lineage16-source.tar.zst.part-* | zstd -dc | tar -xf - -C "$WORK/platform"
cat upstream/sony-open/linux-3.10.26.tar.zst.part-* > "$WORK/sony-kernel.tar.zst"
```

Android ビルドシステムには Clang、Java ツール、イメージツールなど、上流のビルド済み**ビルドツール**も必要です。LineageOS 16 の完全なツールチェックアウトを自分で準備できます。

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

ツールチェックアウトはビルドツールのみを提供します。Android の実行コンポーネントは前の手順で展開したソースからビルドします。別のツールチェックアウトを自分で用意することもできます。一部の Linux ディストリビューションでは、古いビルドツール用に互換ホストライブラリが必要です。`build_walkdroid_platform.py --host-lib-dir` でディレクトリを指定できます。Clang を置き換える場合は `--llvm-prebuilts-base` を指定できます。これらの引数はシステムに書き込まれず、端末の識別要件でもありません。

## 2. 自分のプレーヤーから取得する

プレーヤーを Walkman One の通常画面にしたまま USB でパソコンに接続し、まだ USB マスストレージモードには切り替えないでください。まず `adb devices -l` で接続を確認します。ZX300A の純正画面は Android ではないため、Android の「開発者向けオプション」はありません。端末が表示されない場合は、データケーブル、USB モード、パソコン側の ADB ドライバーを確認してください。以下のコマンドでは、常にプレーヤーの ADB シリアルを明示します。出力されたモデル、プラットフォーム、eMMC CID が手元のプレーヤーのものだと確認してから、その CID をそのまま取得コマンドに渡します。

```sh
export SERIAL='ここにプレーヤーの ADB シリアルを入力'
python3 "$REPO/tools/extract_walkman_device.py" inspect --serial "$SERIAL"
export CID='ここに読み取った eMMC CID を入力'
python3 "$REPO/tools/extract_walkman_device.py" extract \
  --serial "$SERIAL" --cid "$CID" --output "$PRIVATE"
```

このツールは `/system` の必要なファイルと純正 boot パーティションを読み取り、`vendor-system/`、`vendor-boot/`、`stock-boot.img` を生成します。端末への書き込みは行いません。boot パーティションの読み取りには純正環境の root shell が必要です。**`stock-boot.img` を Fastboot の RAM 起動に使用しないでください。** 純正ヘッダーのロードアドレスはこの端末の Fastboot に適していません。

[Arm 公式 Mali ユーザー空間ドライバーページ](https://developer.arm.com/Downloads/-/Mali%20GPUs%20User-Space%20Binary%20Drivers)から Mali-450 と Android に対応するドライバーを入手し、選択したパッケージのライセンスを読んでください。展開した `lib/egl/libGLES_mali.so` のパスを `MALI_LIB` に設定します。本リポジトリはこのファイルを提供しません。グラフィックスの適合部分はドライバーファイルを変更せずに使います。候補システムではプレーヤー上にロック画面とデスクトップが表示されています。

```sh
export MALI_LIB="$HOME/your-arm-driver/lib/egl/libGLES_mali.so"
```

## 3. プラットフォーム、カーネル、端末コンポーネントのビルド

まず、リポジトリに含まれる画像ソースとフォントを使って起動アニメーションを生成します。`fonttools`、Pillow、`sharp` はローカルの生成ツールです。

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

次にカーネルと静的 BusyBox をビルドします。

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

ビルドしたばかりのプラットフォームファイルを使って端末コンポーネントをビルドします。

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

既定のビルドは `userdebug` で、上流が公開するテスト署名を使用します。正式な配布で独自の署名を使う場合は、鍵をビルドする人が管理し、リポジトリやリリースパッケージに含めないでください。

## 4. 非公開で組み立てる

```sh
python3 tools/assemble_walkdroid.py --product "$PRODUCT" --components "$WORK/components/results" \
  --kernel "$WORK/kernel" --busybox "$WORK/busybox/busybox" \
  --vendor-system "$PRIVATE/vendor-system" --vendor-boot "$PRIVATE/vendor-boot" \
  --mali-lib "$MALI_LIB" --work "$PRIVATE/assembly-work" --output "$PRIVATE/images"
```

`boot.img`、`system.img`、`data.img` が生成されます。続いて [パーティションとインストール](INSTALL.ja.md)を参照してください。組み立て完了はローカルイメージの生成を意味し、端末の機能が実機試験に合格したことを意味しません。
