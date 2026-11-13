# Partitioning and installation

[Home](../README.en.md) · [Obtaining proprietary components and building](BUILDING.en.md)

[简体中文](INSTALL.md) · [繁體中文](INSTALL.zh-TW.md) · **English** · [日本語](INSTALL.ja.md)

This guide is for a Sony NW-ZX300A running clean Walkman One firmware. WalkDroid temporarily loads a boot image into RAM through Fastboot from a computer; it does not flash the original boot partition. Android system and data occupy the rear of the internal music partition. A normal reboot returns to the original firmware.

**First installation recreates the internal music FAT filesystem. Copy all music and other files you need to keep to your computer, then back up the entire music partition first.** On a clean Walkman One installation, FAT usually occupies most of the music partition. Do not write Android images into its rear directly: sectors can belong to FAT even when they appear empty. Do not write anything until the backup and recovery path are ready. The microSD card and original critical partitions are outside this installation's scope.

## Layout

The outer partition remains `/dev/block/mmcblk0p29`; no outer partition is added and the partition table is not changed. All offsets below are relative to the start of p29:

| Use | Start byte | End byte, exclusive | Size |
| --- | ---: | ---: | ---: |
| Music FAT32 | 0 | 4,294,967,296 | 4 GiB |
| Android system ext4 | 4,294,967,296 | 6,442,450,944 | 2 GiB |
| Android data ext4 | 6,442,450,944 | 13,291,225,088 | 6,848,774,144 bytes |

The system maps the last two ranges with device-mapper; they are not files inside FAT. Do not format using the size of the entire USB LUN, enlarge FAT over the rear ranges, or use an original formatting operation that recreates the entire music disk. This layout applies to a ZX300A with the capacity above; other devices or layouts require their own installation design.

## 1. Back up

Allow at least 13.3 GB on the computer for the raw p29 contents, in addition to a complete copy of the music files. Stop reads and writes to the player's music volume, unmount it from the computer, and keep the player in the confirmed original ADB connection. A backup of a running partition is not an offline atomic snapshot of the whole device.

Use the `SERIAL`, `CID` and `PRIVATE` confirmed in the [build guide](BUILDING.en.md):

```sh
python3 "$REPO/tools/install_walkdroid.py" backup --serial "$SERIAL" --cid "$CID" \
  --backup "$PRIVATE/p29-before.img"
```

This reads only p29; it does not include the remaining eMMC partitions, RPMB, non-exportable device keys or microSD. The backup should be 13,291,225,088 bytes. While no writes occur to the music area, you can calculate SHA-256 separately for the device source and computer copy and confirm they match. This confirms the backup and is not a system or future-build requirement:

```sh
adb -s "$SERIAL" shell /sbin/busybox sha256sum /dev/block/mmcblk0p29
sha256sum "$PRIVATE/p29-before.img"
```

Also keep a normal file-by-file copy of the music; retain the raw backup on the computer. A file list or partition-header backup is not a backup of the music contents.

## 2. Prepare and write

Confirm that the music volume is neither mounted nor in use by the computer or player. The following commands create an empty 4 GiB FAT32 image, then write the specified ranges in the table above. After first installation recreates the music FAT, copy the music you need back through the player's normal file transfer; the new music volume is 4 GiB.

```sh
truncate -s 4294967296 "$PRIVATE/music-fat.img"
mkfs.vfat -F 32 -n WALKMAN "$PRIVATE/music-fat.img"
python3 "$REPO/tools/install_walkdroid.py" write --serial "$SERIAL" --cid "$CID" \
  --music-image "$PRIVATE/music-fat.img" \
  --system-image "$PRIVATE/images/system.img" \
  --data-image "$PRIVATE/images/data.img"
```

The writing tool acts only on the explicitly specified and identity-confirmed player, writing data in chunks within fixed ranges. If interrupted, a range may be only partially written: keep the computer backup and resolve the interruption first; do not switch to another device. If the layout is already installed and you only update Android, omit `--music-image` and write only system or data. Recreating data clears the existing Android data.

## 3. RAM boot

Before booting, check the load layout of the boot image you just assembled:

```sh
python3 "$REPO/tools/ramboot_layout.py" "$PRIVATE/images/boot.img"
```

This checks the memory layout needed for RAM boot, including DRAM, LK reserved areas and component boundaries; it does not write to the device. Once the check passes, switch the same player you just confirmed into Fastboot, keep it on the same physical USB port, and confirm that the Fastboot USB serial and physical port match. Then explicitly specify the Fastboot serial:

```sh
adb -s "$SERIAL" reboot bootloader
fastboot devices -l
export FASTBOOT_SERIAL='enter the same player Fastboot USB serial here'
fastboot -s "$FASTBOOT_SERIAL" boot "$PRIVATE/images/boot.img"
```

This device's Fastboot may not provide a usable `getvar serialno` or eMMC CID; do not select a different device for that reason. Stop if the transition is not continuous or the USB serial or physical port does not match. Do not run `fastboot boot` on `stock-boot.img` backed up from the original partition: its load addresses are unsuitable for this RAM boot method.

For newly created data, the Walkman volume defaults to 60 / 120 and can be adjusted in sound settings.

A normal reboot returns to Walkman One; the experimental area remains at the rear of p29. If you need to change the music volume layout or reclaim its space, first back up the music and Android data you need, then plan that operation separately. Replacing the boot image does not automatically restore the music partition layout.
