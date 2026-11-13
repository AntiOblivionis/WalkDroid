<div align="center">
  <img src="assets/walkdroid/logo.png" width="176" alt="WalkDroid 标志">
  <h1>WalkDroid</h1>
  <p><strong>面向Sony Walkman ZX300A HIFI音乐播放器的实验性Android系统。</strong></p>
  <p align="center">
  <a href="https://ko-fi.com/J1F02AL174"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi"></a>
</p>
  <p>Android 9 · LineageOS 16 · NW-ZX300A</p>
  <p><strong>简体中文</strong> · <a href="README.zh-TW.md">繁體中文</a> · <a href="README.en.md">English</a> · <a href="README.ja.md">日本語</a></p>
  <p><a href="https://tgwgroup.ltd/2025/10/19/support-me/">💛 支持这个项目</a></p>
</div>

---

WalkDroid 为 ZX300A 带来 Android 桌面、应用和声音控制，同时使用设备自己的 Sony 音频与无线组件。项目处于实验阶段，与 Sony、LineageOS 和 Walkman One 的作者无关联。

**从这里开始**

1. 假定你的 ZX300A 已运行干净的 Walkman One 固件。按 [闭源部件获取与编译](docs/BUILDING.md) 准备自己设备上的原厂文件、Arm 图形驱动和构建环境。
2. 按 [分区与安装](docs/INSTALL.md) 备份音乐区、准备空间、安装系统并从电脑启动。

**目前的功能边界**

| 功能 | 已验证的范围 |
| --- | --- |
| 系统与界面 | Android 桌面、应用列表、声音设置和独占应用选择已在设备上使用；界面提供简体中文、繁体中文、英语和日语。 |
| 3.5 mm | PCM、DSD64 转 PCM 已实机播放；192 kHz 播放与 384 kHz 直出有实机记录。 |
| USB DAC | 48 kHz 播放、实时音量和退出恢复已实机复验。 |
| 蓝牙 | 接收链路及 3.5 mm 出声已复验；向已配对 DAC 发送曾由用户实听。正常发送程序两次重连成功；一次静音数据测试报告两次缓冲欠载。 |
| microSD | exFAT 卡只读访问已验证。 |
| 尚未完成 | 4.4 mm 物理输出、原生 DSD、真实实体按键和完整插拔流程尚无完整实机验收；不适用的网络入口已移除或关闭。 |

以上是此前开发版本的实机记录，不代表每个新构建都已完成同样的复验。本次 B116 从源码重建的系统已实机进入桌面，并在独占应用路径通过 3.5 mm PCM 双音出声测试；播放末尾记录一次缓冲欠载。其他已完成范围尚未在这次构建上逐项复验。音效开关与预设的状态可操作，但这不代表所有音效、格式或听感组合都已验收。当前采用电脑辅助的 Fastboot RAM 启动；重启后回到原系统，没有脱机双系统菜单。当前构建为 `userdebug`，并非面向安全认证的正式固件。

仓库和发布包不包含 Sony 固件、私有音频库、Arm 闭源图形驱动、配对资料或个人密钥。上游开放源码和各自许可见 [NOTICE](NOTICE)；项目自有代码的许可见 [LICENSE](LICENSE)。

---

<sub>Android 是 Google LLC 的商标。Android 机器人是在 Google 原创及共享成果的基础上再创作或修改而成，须遵照 [知识共享署名 3.0 许可](https://creativecommons.org/licenses/by/3.0/)所述条款付诸应用。</sub>
