# 工具入口與資格

本版工具包提供下列兩組可維護 CLI；Python 3.11 以上，相依採明示路徑。演算法詳述回既有研究正文，不在此複製理論。

| 入口 | 輸入／輸出 | 已驗與限制 |
| --- | --- | --- |
| `research/offline/offline.py --help` | 保存的身份憑據、事件與幾何等；新分析輸出目錄 | 沿用離線分析入口，支援子命令由 help 列出；不是 live observer／GPU 重播；來源資料由使用者提供 |
| `textures/build_pack.py <工作目錄> <texconv.exe>` | 工作目錄的 inventory.json／original/*.dds；新 2x、conversion 與收據 | 保守 2x cubic，新頂層＋原 mip bytes；DXT1／3／5、特定 RGB32；不是 AI 高清 |
| `textures/verify_pack.py <工作目錄>` | 上列 mapping／來源／輸出；verification.json | 核尺寸、完整長度、hash、format、下層 mip 原樣；不證遊戲可見品質 |

先準備工作目錄與 `original`；`inventory.json` 的 `textures` 陣列每筆含原 DDS 大寫 `sha256`，檔名為 `<SHA>.dds`。輸出子目錄須不存在。最小合成示例由 `tests/test_texture_tools.py` 產生，無遊戲素材。執行範例：

```powershell
python -B tests/test_texture_tools.py --texconv C:/tools/texconv.exe --out C:/work/dds-check
python -B tools/research/offline/offline.py --help
python -B tools/research/offline/test_offline.py --evidence C:/work/GE-evidence --output C:/work/offline-check
```

最後一項是維護者回歸，需原研究 r31／r36 保存資料，這些不在工具包；一般使用者依 help 提供自己的完整身份憑據。

`engine_research` 控制器與 `source_observation` 的 GE 限定觀測器保留原來源路徑，連 [工程導覽](../草稿區/有界取證/README.md#engineering-roles)。本次未搬遷它們，因此不改既有 runtime／lease 相對路徑；專門 runtime 不裝進一般工具包。AI 推論、分類、修復及各活動配方由 [catalog.json](catalog.json)逐檔指定 hash／用途／遷移去向，留既有活動與工具交接；不能僅憑腳本存在就宣稱可攜或獨立合格。

已遷移的 DDS 轉換及離線分析以本目錄維護；舊同名原件保留為凍結追溯，不再雙寫。測試、模型、安裝器、調查中的注入程式與遊戲輸入不列為工具包的一般執行入口。
