# 分区与安装

[返回首页](../README.md) · [获取闭源部件与编译](BUILDING.md)

**简体中文** · [繁體中文](INSTALL.zh-TW.md) · [English](INSTALL.en.md) · [日本語](INSTALL.ja.md)

本教程面向已运行干净 Walkman One 固件的 Sony NW-ZX300A。WalkDroid 使用电脑通过 Fastboot 临时加载 boot 镜像，不刷写原厂 boot；Android 的 system 和 data 放在内部音乐分区的后部。重启后仍从原厂固件启动。

**首次安装会重建内部音乐 FAT 文件系统。请先把音乐和需要保留的文件完整复制到电脑，再备份整个音乐分区。** 干净 Walkman One 上的 FAT 通常占据大部分音乐分区，不能直接往其后部写 Android 镜像：即使那些扇区看上去没有文件，也可能属于 FAT 文件系统。备份和恢复路径准备好之前不要写入。microSD 与原厂关键分区不属于本安装范围。

## 布局

外层仍是 `/dev/block/mmcblk0p29`，不新增外层分区、不改分区表。下表偏移均相对于 p29 起点：

| 用途 | 起始字节 | 结束字节，不含 | 大小 |
| --- | ---: | ---: | ---: |
| 音乐 FAT32 | 0 | 4,294,967,296 | 4 GiB |
| Android system ext4 | 4,294,967,296 | 6,442,450,944 | 2 GiB |
| Android data ext4 | 6,442,450,944 | 13,291,225,088 | 6,848,774,144 字节 |

系统通过 device-mapper 映射后两个范围，它们不是 FAT 内的文件。不要按整个 USB LUN 的大小格式化，不要扩大 FAT 来覆盖后部，也不要使用会重建整个音乐盘的原厂格式化操作。此布局适用于上述容量的 ZX300A，其他设备和不同布局需要各自设计安装方式。

## 1. 备份

准备至少 13.3 GB 的电脑空间来保存 p29 原始内容，并额外保存完整音乐文件。先停止对播放器音乐盘的读写，断开电脑对该盘的挂载，让播放器保持在已确认的原厂 ADB 连接；运行中的分区备份不是整机离线原子快照。

使用 [编译教程](BUILDING.md) 中已确认的 `SERIAL`、`CID` 和 `PRIVATE`：

```sh
python3 "$REPO/tools/install_walkdroid.py" backup --serial "$SERIAL" --cid "$CID" \
  --backup "$PRIVATE/p29-before.img"
```

这只读取 p29，不包含 eMMC 其余分区、RPMB、不可导出的设备密钥或 microSD。备份应为 13,291,225,088 字节。保持音乐区没有写入时，可分别计算设备源和电脑副本的 SHA-256，确认两个值一致；这一步用于备份确认，不会成为系统或后续构建的门槛：

```sh
adb -s "$SERIAL" shell /sbin/busybox sha256sum /dev/block/mmcblk0p29
sha256sum "$PRIVATE/p29-before.img"
```

还应保存通过正常文件方式复制出来的音乐；原始备份留在电脑上。仅有文件列表或分区头备份不等于音乐内容备份。

## 2. 准备并写入

确认电脑侧和播放器侧都没有挂载或正在使用音乐卷。以下命令新建一个空的 4 GiB FAT32 镜像，随后按上表写入指定范围。首次安装重建音乐 FAT 后，可通过播放器正常文件传输将需要的音乐复制回去；4 GiB 是新的音乐卷容量。

```sh
truncate -s 4294967296 "$PRIVATE/music-fat.img"
mkfs.vfat -F 32 -n WALKMAN "$PRIVATE/music-fat.img"
python3 "$REPO/tools/install_walkdroid.py" write --serial "$SERIAL" --cid "$CID" \
  --music-image "$PRIVATE/music-fat.img" \
  --system-image "$PRIVATE/images/system.img" \
  --data-image "$PRIVATE/images/data.img"
```

写入工具每次只针对明确指定并确认身份的播放器，数据按固定范围分块写入；中断时该范围可能只写入了一部分，应保留电脑备份并先处理这次中断，不要改选其他设备。已经采用本布局且只更新 Android 时，可以不提供 `--music-image`，只写 system 或 data；重建 data 会清除现有 Android 数据。

## 3. RAM 启动

启动前先校验这次组装的 boot 镜像加载布局：

```sh
python3 "$REPO/tools/ramboot_layout.py" "$PRIVATE/images/boot.img"
```

这是 RAM 启动所需的内存布局核对，包含 DRAM、LK 保留区和组件边界；它不会向设备写入。校验通过后，从刚确认的同一台播放器切入 Fastboot，保持同一 USB 物理端口，核对 Fastboot 枚举的同一 USB 序列号及物理端口，再明确指定 Fastboot 序列号执行：

```sh
adb -s "$SERIAL" reboot bootloader
fastboot devices -l
export FASTBOOT_SERIAL='在此填写同一播放器的 Fastboot USB 序列号'
fastboot -s "$FASTBOOT_SERIAL" boot "$PRIVATE/images/boot.img"
```

此设备的 Fastboot 可能不提供可用的 `getvar serialno` 或 eMMC CID；不要因此转而选择另一台设备。模式切换不连续、USB 序列号或物理端口不匹配时停止。不要对从原厂分区备份的 `stock-boot.img` 执行 `fastboot boot`，其加载地址不适合该 RAM 启动方式。

新建 data 的 Walkman 音量默认为 60 / 120，可在声音设置中调整。

正常重启会返回 Walkman One；实验区仍留在 p29 后部。需要更改音乐卷布局或腾回其空间时，先备份当前需要保留的音乐和 Android 数据，再单独规划该操作；更换 boot 镜像不会自动恢复音乐分区布局。
