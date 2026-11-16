# GEHD v0.1.0-preview.1｜預覽版

Windows 上的 GER／GE2RB 共用管理器與兩作獨立遊戲包。版本變更見文件包或來源包中的 CHANGELOG.md。本版為預覽版，已驗範圍與已知限制見下方。

## 選擇下載與安裝

1. 一般使用者取得 `GEHD-Manager-v0.1.0-preview.1.zip`，以及自己遊戲的全部 `GEHD-GER-v0.1.0-preview.1.zip.*` 或 `GEHD-GE2RB-v0.1.0-preview.1.zip.*`。兩作各自選包；不混用。
2. 依 `SHA256SUMS.txt` 核對每個下載檔。若有 Python，可執行同目錄 `verify_release.py .` 核全候選附件；只取得部分附件者依 SHA256SUMS 核所選全部卷。缺卷、大小或 SHA 不符時停止。
3. 將同一作所有外層分卷放一起，用 7-Zip 開 `.zip.001` 完整解開。得到 `GER_GamePack` 或 `GE2RB_GamePack`；不要再解它裡面的 Core／Data 7z。進階驗證可用 `verify_release.py . --game GER --out C:/work/GER-restored`；需將兩份 manifest、verify_release.py、split_zip.py、管理器ZIP及所選作全部卷放在同一目錄，不必下載另一作或來源／文件包。
4. 解開管理器 ZIP，保持 EXE 與隨附子目錄完整。正常關閉遊戲，開共用管理器選取對應 GamePack 資料夾及實際遊戲位置，沿管理器安裝／啟用。詳細操作沿包內的使用說明與原安裝流程。一般遊玩從 Steam 啟動。
5. 更新時也先正常關閉遊戲，使用管理器及包內既有流程；需要停用／還原時使用同一管理器的停用／還原功能，依其狀態確認。不要手動互換兩作 DLL，也不要把本版下載目錄覆蓋到遊戲根目錄。

## 其他附件

- Source ZIP：該來源提交的程式、文件、測試與必要第三方 SHA 原件；不含遊戲資料、模型及完整活動證據。
- Tools ZIP：已限定資格的離線研究分析與保守 DDS 轉換／核驗；Python／texconv 自備，依 tools/README.md。
- Docs ZIP：由同版既有正文產生的閱讀版；未附本機證據已標記，並列 UNAVAILABLE_REFERENCES.json。
- release-manifest.json：來源提交、元件、資料身份、附件 SHA、資格及限制的總對照；media-manifest.json 記外層卷與原內層檔案。

## 已驗與限制

原碼在隔離副本完成 x86 乾淨建置；兩作各 20 項身份、75 項合成載入契約、44 項保存契約，以及管理器 26 項流程檢查通過。離線分析 31 項、四種 DDS 格式的下層 mip 保留與損壞拒絕通過。新外層分卷完整還原後逐檔 SHA 回核；資格細節見 VERIFICATION.json。

四個主要重建 binary 的 .text 與固定基線相同；控制器／共用管理器扣除 PE metadata 後相同；兩作 DLL 另有 assert 的建置路徑及編譯器 RTTI 名稱差異，扣除具名欄位後相同。這不是逐位元重現。本下載沿用原已驗 binary／DDS。

沿用既有兩作標題／讀檔／據點及恢復測試前部署的實機資格（非純淨原版基線）；本輪沒有啟動遊戲或部署，不宣稱完整任務、全部素材外觀、跨機效能或 HELLO 永久修復已通過。

歷史實機測試曾出現 GE2RB 還原約 86 分鐘、GER 移除約 41 分鐘；這是該次環境的觀測，不是每台電腦的預估時間。維護時保持遊戲關閉並等候管理器結果，避免同時啟用其他模組管理器。遇到錯誤時保留日誌與目前狀態，透過 [Issues](https://github.com/40939138-sudo/GEHD/issues) 回報；未經驗證的強制中斷與恢復方式不列為支援流程。

自製程式碼及其隨附文件採 MIT，見 LICENSE.md；第三方程式元件的原授權保留在 THIRD_PARTY_NOTICES.md 及各包內。
