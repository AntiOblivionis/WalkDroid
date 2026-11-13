<div align="center">
  <img src="assets/walkdroid/logo.png" width="176" alt="WalkDroid 標誌">
  <h1>WalkDroid</h1>
  <p><strong>面向 Sony Walkman ZX300A HiFi 音樂播放器的實驗性 Android 系統。</strong></p>
  <p align="center">
  <a href="https://ko-fi.com/J1F02AL174"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi"></a>
</p>
  <p>Android 9 · LineageOS 16 · NW-ZX300A</p>
  <p><a href="README.md">简体中文</a> · <strong>繁體中文</strong> · <a href="README.en.md">English</a> · <a href="README.ja.md">日本語</a></p>
  <p><a href="https://tgwgroup.ltd/2025/10/19/support-me/">💛 支持這個專案</a></p>
</div>

---

WalkDroid 為 ZX300A 帶來 Android 桌面、應用程式及聲音控制，同時使用裝置本身的 Sony 音訊與無線元件。專案處於實驗階段，與 Sony、LineageOS 和 Walkman One 作者無關。

**從這裡開始**

1. 假設你的 ZX300A 已執行乾淨的 Walkman One 韌體。依照 [閉源元件取得與編譯](docs/BUILDING.zh-TW.md) 準備自己裝置上的原廠檔案、Arm 圖形驅動及建置環境。
2. 依照 [分區與安裝](docs/INSTALL.zh-TW.md) 備份音樂區、準備空間、安裝系統並從電腦啟動。

**目前的功能範圍**

| 功能 | 已驗證的範圍 |
| --- | --- |
| 系統與介面 | Android 桌面、應用程式列表、聲音設定及獨佔應用程式選擇已在裝置上使用；介面提供簡體中文、繁體中文、英語和日語。 |
| 3.5 mm | PCM、DSD64 轉 PCM 已實機播放；192 kHz 播放與 384 kHz 直出有實機記錄。 |
| USB DAC | 48 kHz 播放、即時音量及退出後恢復已實機複驗。 |
| 藍牙 | 接收鏈路及 3.5 mm 出聲已複驗；向已配對 DAC 傳送曾由使用者實聽。正常傳送程式兩次重新連線成功；一次靜音資料測試記錄兩次緩衝欠載。 |
| microSD | exFAT 卡唯讀存取已驗證。 |
| 尚未完成 | 4.4 mm 實體輸出、原生 DSD、實體按鍵及完整插拔流程尚未完成實機驗收；不適用的網路入口已移除或關閉。 |

以上是先前開發版本的實機記錄，不代表每次新建置都已完成相同複驗。本次從原始碼重建的 B116 已在實機進入桌面，並透過獨佔應用程式以 3.5 mm PCM 播放雙音；結尾記錄一次緩衝欠載，其他已完成範圍尚未在本次建置中逐項複驗。音效開關及預設可操作，不代表所有音效、格式或聽感組合都已驗收。Android 目前需要電腦透過 Fastboot 載入 RAM；重新啟動會返回原系統，沒有離線雙系統選單。目前建置為 `userdebug`，並非面向安全認證的正式韌體。

倉庫與發布包不包含 Sony 韌體、專有音訊函式庫、Arm 閉源圖形驅動、配對資料或個人金鑰。上游開放原始碼及各項授權見 [NOTICE](NOTICE)；專案自有程式碼授權見 [LICENSE](LICENSE)。

---

<sub>Android 是 Google LLC 的商標。Android 機器人是依據 Google 原創並分享的作品重製或修改，並依 [創用 CC 姓名標示 3.0 授權](https://creativecommons.org/licenses/by/3.0/)所述條款使用。</sub>
