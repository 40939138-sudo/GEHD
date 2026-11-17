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


## 高清貼圖製作來源與配方

本節對應已發布的 `v0.1.0-preview.1`，提供製作資料的選定公開視圖；原始活動紀錄仍沿來源索引追溯。資料總入口為 [製作來源清單](../dependencies/texture-production/index.json)，匯出方式見 [export_texture_provenance.py](../scripts/package/export_texture_provenance.py)；匯出需要維護者保留的本機收據，不能只用公開來源包重建這些紀錄。

| 需要查的資料 | 公開內容 |
| --- | --- |
| 原始輸入與成品對照 | [GER 4,413筆](../dependencies/texture-production/GER-mapping.json)、[GE2RB 8,138筆](../dependencies/texture-production/GE2RB-mapping.json)：來源身份、成品完整SHA256、大小、尺寸、mip、格式碼與倍率，逐筆對上固定發布build的檔案表。sourceIdentity是遊戲載入器使用的DDS身份，不一概視為原檔完整SHA256 |
| 最終GER製作路線 | [GER逐項來源與修正索引](../dependencies/texture-production/GER-lineage.json)：4,413筆成品雜湊全部吻合；包含名稱、製作路線、紀錄與修正收據身份。紀錄路徑是本機追溯標籤，未表示所指原件也已公開 |
| 模型版本與參數 | [模型與參數參考](../dependencies/texture-production/model-and-parameter-references.json)：一般SR的權重／架構SHA、Torch／CUDA、精度、tile／padding／alpha策略；TVT初始GER批次的模型pins、seed、prompt、分塊與重疊等，以及上游模型來源網址 |
| 成品修正紀錄 | [1,372筆成品吻合的修正節錄](../dependencies/texture-production/final-repair-records.json)：原件／先前結果／master／成品身份與修正方法，僅納入輸出SHA與本版相同者；[GE2RB最終修正摘要](../dependencies/texture-production/GE2RB-repair-summary.json)另保留活動類別與數量。數量可能重疊，不能相加當獨立貼圖數 |
| 完整最終成品對照鏈 | [GE2RB逐項製作鏈索引](../dependencies/texture-production/GE2RB-lineage-index.json)及其分片涵蓋全部8,138筆；每筆都有同一來源身份及相同最終DDS SHA的製作／選用／修正紀錄，原先7,145筆的最終對照缺口已清零。紀錄保留選用階段、原始輸入／master／DDS參照與先前製作路線；先前路線是否被取代由sameOutputAsFinal標示 |
| 逐項配方參照 | [製作參數目錄](../dependencies/texture-production/production-parameter-catalog.json)匯出各階段可讀的參數／模型身份及原件SHA；逐項baselineProcessing連到對應配方，不把已被修正取代的早期配方當最終方法。本次211份配方參照均在本機找到並記錄原件SHA；匯出與比對規則見[trace_texture_lineage.py](../scripts/package/trace_texture_lineage.py) |

**重製範圍。** 模型／參數是各具名製作階段的紀錄，不能假定每張成品都使用同一模型、或未經後續修正。這次公開的是來源對照、參數與修正紀錄，不含原始圖像、模型權重、全部master、人工遮罩與每一步處理程式，因此仍不能承諾從原圖逐位元重製全部高清成品。檔名、雜湊與紀錄中的接受狀態也不代替完整遊戲品質驗收。

這些資料補充在目前來源庫；已發布版本的Tag與下載包保持固定，原Release ZIP不會自動包含本次補充。
