<div align="center">
  <img src="assets/walkdroid/logo.png" width="176" alt="WalkDroid logo">
  <h1>WalkDroid</h1>
  <p><strong>An experimental Android system for the Sony Walkman ZX300A HiFi music player.</strong></p>
  <p>Android 9 · LineageOS 16 · NW-ZX300A</p>
  <p><a href="README.md">简体中文</a> · <a href="README.zh-TW.md">繁體中文</a> · <strong>English</strong> · <a href="README.ja.md">日本語</a></p>
  <p><a href="https://tgwgroup.ltd/2025/10/19/support-me/">💛 Support the project</a></p>
</div>

---

WalkDroid brings an Android desktop, apps and sound controls to the ZX300A while using Sony audio and radio components obtained from your own player. The project is experimental and independent of Sony, LineageOS and the author of Walkman One.

**Get started**

1. Start with a ZX300A running a clean Walkman One installation. Follow [obtaining proprietary components and building](docs/BUILDING.en.md) to prepare files from your player, an Arm graphics driver and a build environment.
2. Follow [partitioning and installation](docs/INSTALL.en.md) to back up the music partition, prepare space, install the system and boot it from a computer.

**Current feature boundaries**

| Feature | Scope verified |
| --- | --- |
| System and interface | The Android desktop, app list, sound settings and exclusive-app selection have been used on the device. The interface supports Simplified Chinese, Traditional Chinese, English and Japanese. |
| 3.5 mm | PCM and DSD64 converted to PCM have been played on the device; 192 kHz playback and the 384 kHz direct-output path have device records. |
| USB DAC | 48 kHz playback, live volume adjustment and recovery on exit have been retested on the device. |
| Bluetooth | The receive path and 3.5 mm output have been retested; transmission to a paired DAC was heard by the user. The normal transmitter reconnected successfully twice; a muted data test recorded two buffer underruns. |
| microSD | Read-only access to an exFAT card has been verified. |
| Not yet complete | Physical 4.4 mm output, native DSD, hardware buttons and the full hot-plug lifecycle have not completed device acceptance. Inapplicable network entries have been removed or disabled. |

These are records from earlier development builds; they do not mean every new build has passed the same retests. This source-rebuilt B116 reached the desktop on the device and played a two-tone PCM test through the 3.5 mm exclusive-app path, with one underrun at the end. The other previously completed areas have not each been retested on this build. Sound controls and presets are operable, but not every effect, format or listening combination has been accepted. Android currently boots into RAM through Fastboot from a computer; rebooting returns to the original system, with no offline dual-boot menu. The current build is `userdebug`, not a security-certified production firmware.

The repository and release package do not include Sony firmware, proprietary audio libraries, Arm's closed graphics driver, pairing data or personal keys. See [NOTICE](NOTICE) for upstream source and licence information, and [LICENSE](LICENSE) for the project's own code.

---

<sub>Android is a trademark of Google LLC. The Android robot is reproduced or modified from work created and shared by Google and used according to the terms of the [Creative Commons Attribution 3.0 License](https://creativecommons.org/licenses/by/3.0/).</sub>
