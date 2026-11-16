# 建置與驗證

此版為 Windows x86 本機先行候選。來源入口為 `src/loader`、`src/games`、`src/management`；來源搬遷對照見 [source-map.json](source-map.json)。舊 `texture_pack` 同名檔及固定 build 保留為證據，以新位置維護所選元件。

需要 Python 3.11 以上（本次 3.12.14）、Visual Studio C++ x86 工具及 Windows SDK、Perl。實際相依身份見 [工具清單](dependencies/toolchain.json)。OpenSSL 的六份必要原件已附在 `dependencies/openssl-sha`，建置前逐檔核 hash，再以 Perl／MASM 產生 SHA256 OBJ。沒有舊 BaseBuild 或預編譯 OBJ 相依。

在解開的來源根目錄，用自己的 Python／VS／Perl 路徑執行：

```powershell
python -B scripts/build/build.py --out C:/work/gehd-build --vcvars 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars32.bat' --perl 'C:/Program Files/Git/usr/bin/perl.exe'
python -B tests/native/qualify.py --build C:/work/gehd-build --out C:/work/gehd-check
python -B tests/test_release_handoff.py
```

輸出目錄須不存在。兩作各有 DLL、測試 fixture／contract；management 有原生控制器、兩種管理器、audit／copy 及測試工具。各元件另輸出相容既有內層 builder 的 build.json；兩作目錄附同版 pack-audit，已用合成資料實際產生內層媒體與 Setup。`build.json` 記原碼、相依及輸出 SHA，`build.cmd`／`build.log` 記命令與編譯診斷。CPU contract 仍需系統已裝 x86 `d3dx9_43.dll`；這是既有 DirectX runtime 相依，不隨此包複製。測試生成合成 DDS，不讀遊戲或部署。

DLL 依編譯旗標分 GER／GE2RB，不交換使用。此候選的下載包仍採固定已驗 binary；重建的結果只作來源及離線回歸核對。PE 時戳／checksum 等差異須明記，編譯成功不表示逐位元重現，也不自動取代已驗成品。

工具：參見 [tools/README.md](tools/README.md)。內層安裝媒體 builder 保留於 `scripts/package/inner`，只接受明示資料／controller／loader／tools 根；其 DataRoot 含經審查 DDS 與身份檔，並不隨來源附送。第三方安裝工具目錄仍需 `7zip-26.06/x64/7za.exe` 及 `inno-7.1.1/ISCC.exe`。內層 builder 沿用歷史收據格式；本次只沿用已驗內層，不宣稱重新從素材生成整庫。

外層封裝用 `scripts/package/prepare_media.py`，輸入既有 distribution 收據及管理器 ZIP，核 SHA 後採儲存模式分卷並解回驗證。交付驗證用 `scripts/package/verify_release.py <候選目錄>`；加 `--game GER --out <新目錄>` 可還原一作。所有命令只輸出本機檔案，沒有上傳、Tag 或 Release 操作。
