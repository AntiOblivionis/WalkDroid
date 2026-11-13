# SD 卡兼容层来源

`sdcard-fuse-compat.cpp` 基于 Android 开源项目的 sdcard 实现，保留文件中的 Apache-2.0 版权及许可声明；许可文本见相邻图形目录的 `../graphics/APACHE-2.0.txt`。另一个编译输入 `oreo_sdcard/fuse.cpp` 及其相邻源码、原始 `NOTICE` 保存在 `upstream/aosp-oreo-system-core/`；所需 Android 平台库从本次平台源码构建生成。当前 exFAT 卡以只读方式挂载，写入及应用权限边界尚未验收。
