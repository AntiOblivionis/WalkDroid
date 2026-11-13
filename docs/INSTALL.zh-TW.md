# 分區與安裝

[返回首頁](../README.zh-TW.md) · [取得閉源元件與編譯](BUILDING.zh-TW.md)

[简体中文](INSTALL.md) · **繁體中文** · [English](INSTALL.en.md) · [日本語](INSTALL.ja.md)

本教學適用於已執行乾淨 Walkman One 韌體的 Sony NW-ZX300A。WalkDroid 透過電腦使用 Fastboot 暫時將 boot 映像檔載入 RAM，不刷寫原廠 boot；Android 的 system 和 data 放在內部音樂分區後部。重新啟動後仍會進入原廠韌體。

**首次安裝會重建內部音樂 FAT 檔案系統。請先將音樂及需要保留的檔案完整複製到電腦，再備份整個音樂分區。** 乾淨 Walkman One 上的 FAT 通常佔據大部分音樂分區，不能直接在其後部寫入 Android 映像檔：即使磁區看起來沒有檔案，也可能屬於 FAT 檔案系統。備份和復原方式準備好之前不要寫入。microSD 與原廠關鍵分區不屬於本安裝範圍。

## 版面配置

外層仍是 `/dev/block/mmcblk0p29`，不新增外層分區、不修改分區表。下表偏移量均相對於 p29 起點：

| 用途 | 起始位元組 | 結束位元組，不含 | 大小 |
| --- | ---: | ---: | ---: |
| 音樂 FAT32 | 0 | 4,294,967,296 | 4 GiB |
| Android system ext4 | 4,294,967,296 | 6,442,450,944 | 2 GiB |
| Android data ext4 | 6,442,450,944 | 13,291,225,088 | 6,848,774,144 位元組 |

系統透過 device-mapper 映射後兩個範圍，它們不是 FAT 內的檔案。不要依整個 USB LUN 的大小格式化，不要擴大 FAT 以覆蓋後部，也不要使用會重建整個音樂磁碟的原廠格式化操作。此配置適用於上述容量的 ZX300A，其他裝置和不同配置需要各自設計安裝方式。

## 1. 備份

準備至少 13.3 GB 電腦空間保存 p29 原始內容，並額外保存完整音樂檔案。先停止對播放器音樂磁碟的讀寫，解除電腦對該磁碟的掛載，讓播放器保持在已確認的原廠 ADB 連線；執行中分區的備份不是整機離線原子快照。

使用 [編譯教學](BUILDING.zh-TW.md) 中已確認的 `SERIAL`、`CID` 和 `PRIVATE`：

```sh
python3 "$REPO/tools/install_walkdroid.py" backup --serial "$SERIAL" --cid "$CID" \
  --backup "$PRIVATE/p29-before.img"
```

這只讀取 p29，不包含 eMMC 其餘分區、RPMB、不可匯出的裝置金鑰或 microSD。備份應為 13,291,225,088 位元組。保持音樂區沒有寫入時，可分別計算裝置來源和電腦副本的 SHA-256，確認兩個值一致；這一步用於備份確認，不會成為系統或後續建置的門檻：

```sh
adb -s "$SERIAL" shell /sbin/busybox sha256sum /dev/block/mmcblk0p29
sha256sum "$PRIVATE/p29-before.img"
```

也應保留透過一般檔案方式複製出的音樂；原始備份留在電腦上。僅有檔案清單或分區標頭備份不等於音樂內容備份。

## 2. 準備並寫入

確認電腦側及播放器側都沒有掛載或正在使用音樂磁碟區。以下命令建立空的 4 GiB FAT32 映像檔，然後依上表寫入指定範圍。首次安裝重建音樂 FAT 後，可透過播放器的一般檔案傳輸將需要的音樂複製回去；新的音樂磁碟區容量為 4 GiB。

```sh
truncate -s 4294967296 "$PRIVATE/music-fat.img"
mkfs.vfat -F 32 -n WALKMAN "$PRIVATE/music-fat.img"
python3 "$REPO/tools/install_walkdroid.py" write --serial "$SERIAL" --cid "$CID" \
  --music-image "$PRIVATE/music-fat.img" \
  --system-image "$PRIVATE/images/system.img" \
  --data-image "$PRIVATE/images/data.img"
```

寫入工具每次只針對明確指定並確認身分的播放器，在固定範圍內分塊寫入；中斷時該範圍可能只寫入一部分，應保留電腦備份並先處理這次中斷，不要改選其他裝置。已採用此配置且只更新 Android 時，可不提供 `--music-image`，只寫入 system 或 data；重建 data 會清除現有 Android 資料。

## 3. RAM 啟動

啟動前先檢查這次組裝的 boot 映像檔載入配置：

```sh
python3 "$REPO/tools/ramboot_layout.py" "$PRIVATE/images/boot.img"
```

這是 RAM 啟動所需的記憶體配置檢查，包含 DRAM、LK 保留區和元件邊界；不會向裝置寫入。檢查通過後，從剛確認的同一台播放器切入 Fastboot，保持同一個 USB 實體連接埠，確認 Fastboot 列出的 USB 序號及實體連接埠一致，再明確指定 Fastboot 序號執行：

```sh
adb -s "$SERIAL" reboot bootloader
fastboot devices -l
export FASTBOOT_SERIAL='在此填入同一播放器的 Fastboot USB 序號'
fastboot -s "$FASTBOOT_SERIAL" boot "$PRIVATE/images/boot.img"
```

此裝置的 Fastboot 可能不提供可用的 `getvar serialno` 或 eMMC CID；不要因此改選另一台裝置。模式切換不連續、USB 序號或實體連接埠不符時停止。不要對從原廠分區備份的 `stock-boot.img` 執行 `fastboot boot`，其載入位址不適用於這種 RAM 啟動方式。

新建 data 的 Walkman 音量預設為 60 / 120，可在聲音設定中調整。

正常重新啟動會返回 Walkman One；實驗區仍留在 p29 後部。需要變更音樂磁碟區配置或取回其空間時，先備份目前需要保留的音樂和 Android 資料，再另外規劃該操作；更換 boot 映像檔不會自動還原音樂分區配置。
