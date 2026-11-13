# Android 音频框架适配来源

`DevicesFactoryHalHybrid.cpp` 基于 Android Open Source Project 的同名文件，保留了原文件中的 Android Open Source Project 版权声明及 Apache-2.0 许可声明。相对于上游 LineageOS 16 平台源码，该副本调整了 A2DP 设备选择，使其走 HIDL 工厂；Apache-2.0 许可文本见 `../graphics/APACHE-2.0.txt`。

音频框架其余源码和对应版权声明保留在仓库的上游源码快照中；所需生成头文件与平台库由源码构建产生。此说明只覆盖该文件。
