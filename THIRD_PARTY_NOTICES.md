# 第三方元件與來源

| 元件 | 本版角色 | 隨附／取得與授權入口 |
| --- | --- | --- |
| OpenSSL 3.5.9 x86 SHA256 transform／perlasm | 原生 loader／controller 的雜湊元件；非完整 libcrypto | [固定來源與 SHA](dependencies/openssl-sha/source.json)、[Apache-2.0 原授權](dependencies/openssl-sha/LICENSE.txt)；六個原件未修改 |
| Microsoft MSVC／Windows SDK、DirectX D3DX9 | 編譯、系統圖形 runtime | 使用者自備安裝；不複製本機 SDK／runtime。實際建置身份見 [相依清單](dependencies/toolchain.json) |
| DirectXTex texconv | 可選 DDS 轉換工具 | 不隨工具包附 binary；[上游專案](https://github.com/microsoft/DirectXTex)、相依清單固定本次使用版本／SHA |
| 7-Zip 26.06 | 外層儲存分卷，以及既有安裝媒體相依 | 不新增整套工具到下載包；[原授權說明](dependencies/licenses/7zip-License.txt)隨來源保存 |
| Inno Setup 7.1.1 | 既有 GEHD_Setup 的製作工具 | [原授權說明](dependencies/licenses/inno-LICENSE.TXT)隨來源保存；保留既有 Setup binary |
| REA／MCP SDK／Ghidra 等 | 保留原路徑的專門研究控制器 | [既有方法](草稿區/研究與開發計畫/04_工作封包/03_首輪判別設計.md#research-controller-design)及 `dependencies/research-runtime` 的 package／lock；不隨一般工具包附 runtime |

完整工具身份在機器可讀清單內。自製程式碼及其隨附文件採 [MIT](LICENSE.md)；以上第三方程式元件保留各自原授權。
