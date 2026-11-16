# 有界取證｜原碼與活動資料

本頁負責工程入口與保存角色；工作架構回[計畫](../研究與開發計畫/01_決策與計畫/整體計畫.md#operating-architecture)，具體輸入、入口、產物與失敗分流回[工具交接](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#tool-handoffs)。當前任務沿[權威指標](../研究與開發計畫/01_決策與計畫/研究支線狀態.md#current-handoff-checkpoint)查Issue／Project；本頁不另排下一步。

<a id="engineering-roles"></a>
## 工程區域與保存角色

所選產品原碼、離線分析與DDS工具已按[source-map.json](../../source-map.json)移交新維護位置；[建置](../../BUILDING.md)與[工具](../../tools/README.md)為現行入口。表內舊texture_pack及offline_observation對應原件保留作凍結追溯，不再雙寫；其餘未遷移研究入口仍依原角色維護。固定build與活動source副本一律不改。本機候選及限定資格見[來源索引](../研究與開發計畫/03_證據與歷史/來源索引.md#source-release-local-candidate)。

| 區域／檔案類別 | 角色與選用規則 |
| --- | --- |
| [texture_pack](texture_pack) | 貼圖分類、[巢狀／壓縮名稱回接](../研究與開發計畫/03_證據與歷史/來源索引.md#texture-recursive-name-classification)、4K倍率規劃、AI模型取得／推論／批次、審查修復選取與核對、獨立純色載入辨識，以及兩倍／四倍試作的提取、轉換、載入、啟停與離線控制原碼；另含[固定mip0診斷](../研究與開發計畫/03_證據與歷史/來源索引.md#mip0-comparison)的選用載入器、原生控制宿主與可逆模式／部署入口；固定build／失敗版保留在toolchain，插值資格沿[原來源](../研究與開發計畫/03_證據與歷史/來源索引.md#texture-pack-2x)、AI與純色辨識資格沿[AI執行證據](../研究與開發計畫/03_證據與歷史/來源索引.md#texture-ai-production)採用，不等高清品質接受 |
| [載入框架建置](texture_pack/build_loading_framework.ps1)／[實際DLL驗證](texture_pack/run_loading_framework.py)／[候選封包](texture_pack/package_loading_framework.py) | 把SHA／預篩、有界pool及profile接入pack_loader.cpp的選用GEHD_LOADING_FRAMEWORK版本；採[固定r6資格及封包](../研究與開發計畫/03_證據與歷史/來源索引.md#texture-loading-framework)。舊build_product入口不自動取得此旗標／SHA相依；由專用builder沿固定基底重建，封包由既有build_product_distribution承接。EX03不動，禁止把候選資格當已部署 |
| [engine_research](engine_research)／[run_engine_research.mjs](run_engine_research.mjs)／[相容REA runner](run_rea_queries.mjs) | 靜態研究的inspect／replay／run、依賴傳參、判讀續接與獨立收尾核對；CLI、scope及限制見[方法](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#research-controller-design)，原碼版本、45項控制及首次N2真REA接續見[資格來源](../研究與開發計畫/03_證據與歷史/來源索引.md#engine-research-controller-execution)。使用既存runtime；合成控制不代GE機制或動態觀測 |
| [offline_observation](offline_observation)及根目錄窄用途原碼 | 前者為已納管的離線入口；其他按問題及固定binding選用，不把root同名檔當已驗最新版。程式修改才維護原碼，本輪不改 |
| elevator-observer-build-*／ceil-observer-build-*與活動中的source副本 | 固定建置、當時原碼及相依身份；屬可重現證據，不是每輪繼續修改的程式主位置。r36從[build-binding](elevator-observer-build-r36/build-binding.json)選定，不由資料夾版本排序猜資格 |
| *qualification*、*synthetic*及fixture／test_* | 合成輸入、正反控制、結果與當時harness；成功、拒絕及修正版各自保留，不能因內容相似去重原件 |
| *observed*、game-*、rea-*靜態活動及各分析目錄 | 正式來源、原始事件、圖像、Evidence／profile、退出／清理收據及衍生分析；結果以來源索引的身份和範圍採用 |
| [rea-toolchain](rea-toolchain)、[toolchain](../../toolchain)的Ghidra／apitrace／DirectXTex／GitHub CLI發布目錄 | 第三方工具、runtime、lock、授權與隨附文件；保存版本不是本案能力通過。REA與Ghidra的英文工作路徑沿既有相依保留 |
| toolchain的game-*、named-*、source-*、state-*、draw-*、target-*、selected-*、offline-entry-*等 | 避中文路徑相容問題或後續具名活動的工作產物；與本目錄的活動同樣依來源索引管理，不是一個全新工具或正文分類 |
| toolchain/git-remote-whitelist-r1、r36-continuation-preservation-r1 | 分別是管理遷移收據與最小不可覆寫保全，後者不是可刪快取；[來源／管理邊界](../研究與開發計畫/03_證據與歷史/來源索引.md#v09-r36-input-audit) |
| .git／.worktrees、previous-research及研究正文 | 分別由[全案分類](../研究與開發計畫/README.md#classification-map-20261017)與既有主位置管理，不搬進工程區 |

<a id="engineering-preservation"></a>
**保存與未知項目：**__pycache__等可辨識快取、rea-temp等工具工作目錄仍須按內容判斷，不能僅憑temp字樣刪除。根目錄異常名稱路徑內有一份3行observer JSONL，已按保存事件類型辨識為孤立觀測、未能歸屬活動；位置、身份及界線見[來源登錄](../研究與開發計畫/03_證據與歷史/來源索引.md#orphan-observer-record)。本輪均原位保留。

下列按功能分組是查找入口，沒有授予全部程式當前執行資格。混合列中的原碼、固定build、合成結果及GE結果依各自連結閱讀；詳細實驗經過只在來源／歷史維護。

## 原碼與環境資料

### REA與成熟工具的接線

| 檔案／定位 | 角色與採用界線 |
| --- | --- |
| [rea-toolchain](rea-toolchain)／[英文執行目錄](../../toolchain) | 前者保存REA／JDK、發布metadata、相依lockfile及診斷；Ghidra與runtime暫存置後者以避本機中文路徑的日誌載入錯誤。工具相依不是另一份研究正文 |
| [run_rea_queries.mjs](run_rea_queries.mjs) | 專案內REA stdio查詢入口；保存原始請求／回覆／Evidence，可同session接續查詢並finish收尾，不改全域client設定 |
| [rea_read_observer.js](rea_read_observer.js)／[run_rea_read_observer.mjs](run_rea_read_observer.mjs) | 固定五角色的REA Frida觀測；模組／入口身份、呼叫配對、buffer hash與停止收尾 |
| [make_analysis_window.py](make_analysis_window.py) | 從固定PE新映射具名程式碼區間，產保留VA的分析專用PE；不執行、不代完整映像引用 |

### 來源、表示與資產解析入口

| 檔案／定位 | 角色與採用界線 |
| --- | --- |
| [export_qpck_key.py](export_qpck_key.py) | 以已驗來源／key／索引收據重新定位並建立成員首次身份；[13項資格](cafe-qpck-member-qualification-r1/qualification.json)、[Cafe取件](cafe-qpck-member-r1/complete.json)、[解碼與引用核對](cafe-ixo-references-r2/verification.json)。舊PTCSG拒絕結果保留，引用reader明選ptccsg-v2的[10項新資格與舊回歸](ixo-references-ptccsg-qualification-r1/qualification.json)；不證遊戲使用 |
| [inspect_qpck_key.py](inspect_qpck_key.py) | 重用唯讀鎖定與收據，保存完整來源身份／原始索引並核unsigned排序、全部同key及範圍；[19項資格](cafe-qpck-index-qualification-r1/qualification.json)、[Cafe patch／base結果](cafe-qpck-index-r1/verification.json)。不取件或解析成員 |
| [derive_qpck_name_key.py](derive_qpck_name_key.py) | 由已格式化名稱推導ASCII模型下的QPCK key；[REA與算法核對](rea-source-selection-20261020-r1/provider-key/verification.json)、[Cafe候選profile](rea-source-selection-20261020-r1/provider-key/cafe-key.json)。不讀封包，不證命中或runtime採用 |
| [audit_saved_ixo_targets.py](audit_saved_ixo_targets.py) | 依固定保存outer與索引覆蓋18個內嵌IXO的FileTexture目標欄位，保留兩個外部B引用而不追內容；[結果與範圍核對](ixo-target-occurrences-r1/verification.json)，不證全遊戲唯一性 |
| [profile_ixo_draw_geometry.py](profile_ixo_draw_geometry.py)／[test_profile_ixo_draw_geometry.py](test_profile_ixo_draw_geometry.py) | 依來源憑據重定位索引／XYZ／UV／COLOR，產與觀測器同canonical順序的原始幾何身份及usage條件；[22項資格](ixo-draw-profile-synthetic-r1/qualification.json)、[CafeWall V1](ixo-cafewall-draw-profile-r1/result.json)／[V2基準](ixo-cafewall-draw-profile-r2/result.json)及[實際C++交叉核對](ixo-cafewall-draw-profile-v2-qualification/verification.json)；明選schema，預設V1不變。WRITEONLY取得缺口及[REA上傳分支](rea-ixo-static-20261020-r1/buffer-upload/verification.json)另列，未證GPU使用 ；[Cafe PTCCSG基準](ixo-cafe-ceil-draw-profile-r1/verification.json)明選ptccsg-v2／V2，三stream含同stream雙色，39條共享使用鏈；[新增與回歸資格](ixo-ptccsg-draw-profile-qualification-r1/qualification.json)不改舊預設或封存結果 |
| [export_qpck_outer.py](export_qpck_outer.py)／[test_export_qpck_outer.py](test_export_qpck_outer.py) | 新來源outer保存、回讀及完成／失敗收尾；另版重用固定外層比對helper，測試只生成合成包 |
| [extract_pres_candidates.py](extract_pres_candidates.py)／[test_extract_pres_candidates.py](test_extract_pres_candidates.py) | 驗證保存憑據後，逐表定位字面F候選、保存名稱／B引用及未知；不聲稱引擎唯一性，合成涵蓋zero-data分支 |
| [inspect_ixo_candidate.py](inspect_ixo_candidate.py)／[test_inspect_ixo_candidate.py](test_inspect_ixo_candidate.py) | 保存候選的有界IXO型別／名稱盤點；不解讀body／引用。依據見[字串指令核對](rea-ixo-static-20261020-r1/dictionary-string/verification.json)，產物見[合成資格](ixo-inventory-synthetic-20261020-r1/qualification.json)、[CafeWall取件](ixo-cafewall-observed-20261020-r1/result.json)及[盤點](ixo-cafewall-inventory-20261020-r1/result.json) |
| [read_ixo_references.py](read_ixo_references.py)／[test_read_ixo_references.py](test_read_ixo_references.py) | 依已核派送讀選定型別的entries／connections，保留未解析區間與預設差異；[28項資格](ixo-references-synthetic-20261021-r2/qualification.json)、[CafeWall引用](ixo-cafewall-references-20261021-r1/result.json)及[REA派送核對](rea-ixo-static-20261020-r1/ptcsg-object/verification.json)供追溯，不模擬屬性求值或證明draw |
| [read_ixo_filetextures.py](read_ixo_filetextures.py)／[test_read_ixo_filetextures.py](test_read_ixo_filetextures.py) | 讀具憑據IXO的assetPath並比對同outer保存索引；[39項資格](ixo-filetextures-synthetic-20261021-r2/qualification.json)、[實讀](ixo-cafewall-filetextures-20261021-r1/result.json)、[REA核對](rea-ixo-static-20261020-r1/filetexture-object/verification.json)。全部重名保留，不追B或斷言runtime選中；r1失敗測試及原碼快照另存 |
| [read_ixo_buffers.py](read_ixo_buffers.py)／[test_read_ixo_buffers.py](test_read_ixo_buffers.py) | 頂點／索引header、資料範圍及Geometry row比較；[26項資格](ixo-buffers-synthetic-20261021-r2/qualification.json)、[CafeWall實讀](ixo-cafewall-buffers-20261021-r1/result.json)、[REA核對](rea-ixo-static-20261020-r1/buffer-objects/verification.json)。不推頂點宣告、拓撲或draw；r1合成字典失敗及原測試碼保留 |
| [read_ixo_geometry.py](read_ixo_geometry.py)／[test_read_ixo_geometry.py](test_read_ixo_geometry.py) | Geometry繪製參數、SG選擇值與stream範圍；[26項合成資格](ixo-geometry-synthetic-20261021-r1/qualification.json)、[CafeWall結果](ixo-cafewall-geometry-20261021-r1/result.json)、[REA核對](rea-ixo-static-20261020-r1/geometry-layout/verification.json)。不解碼位置／UV，不證實際draw或表面 |
| [disassemble_shader_blob.py](disassemble_shader_blob.py) | 已保存shader的有界D3DDisassemble；[3項合成資格](shader-disassembly-synthetic-20261021-r1/result.json)。不建立裝置、不執行shader |
| [inspect_fx9.py](inspect_fx9.py)／[test_inspect_fx9.py](test_inspect_fx9.py) | 限定FX metadata與shader資源定位；[17項合成資格](fx9-synthetic-20261021-r2/qualification.json)、[PTCSG來源](rea-ixo-static-20261020-r1/ptcsg-declaration/shaders/effects-source.json)、[語義憑據](rea-ixo-static-20261020-r1/ptcsg-declaration/semantic-evidence.json)。非完整FX evaluator，r1資格與整體FX反組譯拒絕保留 |
| [find_shader_body.mjs](find_shader_body.mjs) | 保存EXE內的PS2／3有界body候選定位，內含15項合成控制；[固定來源與結果](rea-ixo-static-20261020-r1/shader-body-search/result.json)。完整hash匹配後另核FX建立引用，不是shader合法性驗證器 |
| [read_ixo_surface.py](read_ixo_surface.py)／[test_read_ixo_surface.py](test_read_ixo_surface.py) | 普通PTCSG局部位置／UV與FixedMaterial裁切輸入；[32項資格](ixo-surface-synthetic-20261021-r1/qualification.json)、[首案結果](ixo-cafewall-surface-20261021-r1/result.json)。連接未解時只保留raw UV，不證當次遊戲表面 |
| [plot_ixo_surface.py](plot_ixo_surface.py) | 綁保存表面與候選預覽身份，輸出[局部幾何／UV圖](ixo-cafewall-surface-20261021-r1/plot/surface-uv.svg)；重複背景為圖解，不模擬世界變換／光照／過濾 |
| [inspect_bc3_candidate.py](inspect_bc3_candidate.py) | 唯讀驗legacy DXT5完整mip並比較候選與基準的像素統計；不把統計當產品驗收 |
| [decode_ceil_candidate.py](decode_ceil_candidate.py)／[test_decode_ceil_candidate.py](test_decode_ceil_candidate.py) | 驗候選憑據後預設按REA首塊延後規則解碼、讀GNF圖數／長度並取DDS；舊Candidate884模式僅作明確重現選項，靜態規則不代遊戲實際使用 |
| [compare_ceil_member.py](compare_ceil_member.py) | 獨立Ceil唯讀定位／stored身份比較；v3固定PC F與B四／五名稱子集；讀齊與唯一性分判，非空輔助欄即停身份採用，只按新表指標，不追外部內容／解碼／輸出成員 |
| [compare_qpck_outer.py](compare_qpck_outer.py) | 獨立唯讀outer身份比對；候選與參考JSON分離，不含S64包／offset預設；只存索引／報告，不匯出outer |

### 離線分析與observer適配

| 檔案／定位 | 角色與採用界線 |
| --- | --- |
| [uploaded_geometry_snapshot.hpp](uploaded_geometry_snapshot.hpp)／[prepare_uploaded_observer.py](prepare_uploaded_observer.py)／[uploaded_geometry_fixture.hpp](uploaded_geometry_fixture.hpp)／[run_uploaded_fixture.py](run_uploaded_fixture.py)／[verify_uploaded_fixture.py](verify_uploaded_fixture.py) | 將原上傳內容／版本接到既有多串流幾何判讀；[build r3](elevator-observer-build-r3/source-binding.json)、[組合r2資格](uploaded-observer-qualification-r2/result.json)與[GE caller profile](elevator-observer-build-r3/game-upload-caller-profile.json)。r1停止摘要遭濾除的失敗保留；[遊戲入口](game_upload_bridge.hpp)、[production r5綁定](elevator-observer-build-r5/binary-binding.json)與[組合r3資格](uploaded-observer-qualification-r3/result.json)分開保存，該組合資格活動未執行遊戲 |
| [native_upload_tap.hpp](native_upload_tap.hpp)／[native_buffer_tap.hpp](native_buffer_tap.hpp)／[segmented_reader_fixture.cpp](segmented_reader_fixture.cpp)／[qualify_native_upload.cpp](qualify_native_upload.cpp)／[verify_native_upload.py](verify_native_upload.py) | 原生reader vtable／模組IAT及D3D9 VB／IB事件接線、自有宿主與獨立bytes／影像核對；[r2資格](native-upload-qualification-r2/verification.json)、[REA與原始返回核對](native-upload-qualification-r2/abi-verification.json)，r1保留。[r4資格](native-upload-qualification-r4/verification.json)補觀察／採用及駐留轉交；[r5 caller控制](native-upload-qualification-r5/verification.json)另驗指定／錯誤呼叫點。組合資格另見上列，仍缺GE當次來源／draw證據 |
| [d3dx_source_replacement.hpp](d3dx_source_replacement.hpp)／[r21來源接線](elevator-observer-build-r21/d3dx_source_bridge.hpp)／[固定profile](source_replacement_profile.hpp)／[設定](source_replacement_runtime.hpp)／[唯讀預檢](source_profile_preflight.py)／[設定資格](d3dx-source-profile-qualification-r2/verification.json)／[來源到draw資格](d3dx-source-draw-qualification-r1/verification.json)、[已匹配shader雙色彩／法線對照](observed-shader-native-r1/verification.json)、[r22欄位原碼](elevator-observer-build-r22/indexed_geometry_snapshot.hpp)／[合成](indexed-layout-qualification-r1/verification.json)／[原生布局](observed-shader-native-r2/verification.json)／[WRITEONLY來源布局](observed-shader-upload-r1/verification.json)／[整合回歸](d3dx-source-draw-qualification-r2/verification.json)／[Elevator高清候選r4](elevator-hd-candidate-r4/generation.json)／[保存UV對照](elevator-hd-uv-r3/result.json)／[候選原生對照](elevator-hd-native-r3/verification.json) | 建立期輸入替換、來源／目前內容分層與幾何選取；各版資格、反例與原件分開保留。停用後既有資源需重新載入，非GE產品驗收；當前交接沿支線權威指標查Issue／Project |
| [loaded_texture_replacement.hpp](loaded_texture_replacement.hpp)／[宿主及原相依](replacement-entry-qualification-r1/source-binding.json)／[核對程式](verify_loaded_replacement.py)／[結果](replacement-entry-qualification-r1/verification.json) | 載入後raw-mip身份的draw暫換／恢復，獨立BC3與影像核對；原歷史provider不改，尚非遊戲接線或高清素材 |
| [observer_replacement_bridge.hpp](observer_replacement_bridge.hpp)／[裝置派送](device_method_dispatch.hpp)／[寫入追蹤](texture_write_tracker.hpp)／[准入元件](device_activity_admission.hpp)／[build r17](elevator-observer-build-r17/source-binding.json)／[准入核對](device-admission-qualification-r5/verification.json)／[draw回歸](replacement-observer-qualification-r12/verification.json) | 底層draw、裝置／錄製派送、已知寫入期間及在途／Reset准入；同表外層轉交保留只具本機資格，尚非整筆交易排他；各版失敗與原結果保留 |
| [segmented_upload_witness.hpp](segmented_upload_witness.hpp)／[qualify_segmented_upload.cpp](qualify_segmented_upload.cpp)／[qualify_writeonly_upload.cpp](qualify_writeonly_upload.cpp) | 分段來源與底層提交事件模型、[49項控制](segmented-upload-qualification-r1/result.json)、[原生WRITEONLY宿主](writeonly-upload-qualification-r1/result.json)及[入口身份](segmented-upload-qualification-r1/entry-profile.json)；保存原碼快照與binary綁定。顯式callbacks資格，不是已安裝遊戲hook |
| [indexed_geometry_snapshot.hpp](indexed_geometry_snapshot.hpp)／[qualify_indexed_geometry.cpp](qualify_indexed_geometry.cpp)／[prepare_indexed_observer.py](prepare_indexed_observer.py) | 限定triangle list多stream原始幾何讀取、CPU正反控制及固定r4差異建置；[build綁定](elevator-observer-build-r1/source-binding.json)與[35項資格](elevator-observer-build-r1/cpu-result.json)。不把draw後快照當成GPU／像素見證 |
| [indexed_geometry_fixture.hpp](indexed_geometry_fixture.hpp)／[prepare_indexed_fixture.py](prepare_indexed_fixture.py)／[run_indexed_fixture.py](run_indexed_fixture.py)／[verify_indexed_fixture.py](verify_indexed_fixture.py) | 由既有DDS宿主建立0x54三串流合成幾何，執行前綁定、讀取紀錄與獨立已知bytes核對；[37項結果](elevator-observer-qualification-r1/result.json)列明未覆蓋範圍，不啟動GE |
| [離線觀測入口](offline_observation/offline.py)／[資格驅動](offline_observation/test_offline.py) | 從保存JSONL、Frida與raw影像按新binding重算，O／D及來源／draw／query／投影分層；[r3資格](../../toolchain/offline-entry-qualification-r3/verification.json)、[主位置CLI／移交](../../toolchain/offline-entry-main-cli-r1/verification.json)與[原件封存](../../toolchain/r36-continuation-preservation-r1/manifest.json)。歷史腳本原樣保留；方法回封包3，狀態沿支線權威指標查Issue／Project，不部署或操作遊戲 |
| [純觀測參數入口](source_observation/run.mjs)／[Frida接線](source_observation/observer.js)／[原生宿主](source_observation/fixture.cpp)／[資格驅動](source_observation/qualify.mjs) | 原輸入參數→loader receiver／輸出槽→成功返回指標的限定見證，不啟用替換；[身份、r1失敗及r3合成資格](../研究與開發計畫/03_證據與歷史/來源索引.md#source-parameter-observation)。另有[真實D3DX宿主](source_observation/d3dx_fixture.cpp)／[驅動](source_observation/qualify_d3dx.mjs)及[限定資格](../../toolchain/source-d3dx-qualification-r5/summary.json)。需新profile、全新輸出目錄及既存REA runtime；未具COM世代、resolver／材質共享或GE部署資格 |
| [callback→表項接線](source_observation/handoff_observer.js)／[宿主](source_observation/handoff_fixture.cpp)／[驅動](source_observation/qualify_handoff.mjs) | 沿同一run.mjs生命周期、獨立GEHD_OBJECT_HANDOFF_V1 profile；reader／callback／尾跳append的有界觀測。[原指令、合成資格與限制](../研究與開發計畫/03_證據與歷史/來源索引.md#object-handoff-observation)；另有[材質宿主](source_observation/material_fixture.cpp)／[驅動](source_observation/qualify_material.mjs)與[限定資格](../研究與開發計畫/03_證據與歷史/來源索引.md#material-handoff-observation)。另有[resolver接線](source_observation/resolver_observer.js)／[宿主](source_observation/resolver_fixture.cpp)／[驅動](source_observation/qualify_resolver.mjs)與[分支限定資格](../研究與開發計畫/03_證據與歷史/來源索引.md#resolver-handoff-observation)。快取由來繼承與GE有效期仍缺；同窗配套另列下項，不是GE部署入口 |
| [同窗協調](source_observation/combined_observer.js)／[真實D3DX組合宿主](source_observation/combined_fixture.cpp)／[驅動](source_observation/qualify_combined.mjs) | 同一run.mjs以GEHD_COMBINED_OBSERVER_V1組合既有來源與引用payload，統一窗口／事件順序並記三個物件邊界；[限定參數接合資格](../研究與開發計畫/03_證據與歷史/來源索引.md#combined-observation)。不把觀察到的世代／欄位相等當GE生命期全覆蓋、內容或共享證明 |
| [prepare_ceil_observer.py](prepare_ceil_observer.py)／[qualify_ceil_observer.cpp](qualify_ceil_observer.cpp)／[observer_flush_session.py](observer_flush_session.py) | 固定歷史source的窄篩選衍生、內容／寫入失效及幀界限宿主、活動flush事件持有器 |

### 部署與收尾入口

| 檔案／定位 | 角色與採用界線 |
| --- | --- |
| [manage_observer_install.py](manage_observer_install.py)／[test_manage_observer_install.py](test_manage_observer_install.py) | 兩檔舊模式及固定profile四檔套件；stage同檔身份／完整hash撤除，不覆寫既有檔。[r2資格](source-package-qualification-r2/verification.json)／[待測套件](elevator-source-package-r1/package.json)；檔案撤除不代遊戲恢復，不改原包或自動還原存檔 |

### 合成控制與歷史取得器

| 檔案／定位 | 角色與採用界線 |
| --- | --- |
| [create_rea_fixture.py](create_rea_fixture.py) | 生成不執行的小型PE32；已知原指令／位址／字串供REA局部查詢資格比對 |
| [rea_frida_fixture.cpp](rea_frida_fixture.cpp)／[qualify_rea_frida.mjs](qualify_rea_frida.mjs) | 自有x86程式與REA Frida生命週期資格；只附加自建child，不是正式遊戲入口 |
| [rea_read_fixture.cpp](rea_read_fixture.cpp)／[qualify_rea_read_observer.mjs](qualify_rea_read_observer.mjs) | 自有x86交錯呼叫、短讀、AL與slot重用及拒絕資格，不讀遊戲資料 |
| [qualify_apitrace_d3d9.cpp](qualify_apitrace_d3d9.cpp)／[verify_apitrace_fixture.py](verify_apitrace_fixture.py) | 自製x86已知答案場景與保存trace／影像核對；[活動資料](apitrace-qualification-r1/result.json)分功能結果與部署缺件，不是GE解析器或遊戲資格 |
| [observe_dds_smoke.cpp](observe_dds_smoke.cpp) | 本機D3D9 HAL／MANAGED全mip上傳與draw的觀測器控制宿主；不是GE遊戲驗證 |
| [test_compare_ceil_member.py](test_compare_ceil_member.py) | 生成合成包的138項資格驅動；`--supplement`仍只選54–57，不能代表v3完整資格；不接受正式來源 |
| [test_compare_qpck_outer.py](test_compare_qpck_outer.py) | 只生成合成來源的必要資格驅動；不開正式遊戲包 |
| [Get-Ge2rbBoundedEvidence.ps1](Get-Ge2rbBoundedEvidence.ps1) | 固定S64身份的來源取得器；SavedOuter／QpckMember |
| [Invoke-Ge2rbPostGateControl.ps1](Invoke-Ge2rbPostGateControl.ps1) | 後段控制父程序 |
| [Invoke-Ge2rbPostGateChild.ps1](Invoke-Ge2rbPostGateChild.ps1) | 後段控制子程序 |
| [Test-Ge2rbBoundedEvidence.ps1](Test-Ge2rbBoundedEvidence.ps1) | 16組隔離函式控制驅動 |
| [Test-Ge2rbAdmission.ps1](Test-Ge2rbAdmission.ps1) | 3項分段讀取＋5項入口拒絕驅動 |
| [Test-Ge2rbHostQualification.ps1](Test-Ge2rbHostQualification.ps1) | 13案宿主資格驅動 |
| [Invoke-Ge2rbHostQualificationWorker.ps1](Invoke-Ge2rbHostQualificationWorker.ps1) | 13案Relay／Probe工作程序 |
| [Ge2rbHostQualificationFixture.ps1](Ge2rbHostQualificationFixture.ps1) | 13案合成替身；不是遊戲素材 |

### 固定建置與研究結果定位

| 檔案／定位 | 角色與採用界線 |
| --- | --- |
| [分段持有分析](rea-ixo-static-20261020-r1/segment-ownership/verification.json)／[替換入口核對](rea-ixo-static-20261020-r1/segment-ownership/replacement-entry-audit.json) | REA查描述表清理與單byte讀取，DUMPBIN／原bytes交叉核對；保留缺函式失敗及入口修正，替換原碼核對不等可部署資格 |
| [r13診斷差異](replacement-transaction-interleaving-r1/diagnostic-source.diff)／[交錯反例宿主](replacement-transaction-interleaving-r1/verification.json) | 固定停點重現在途改綁與晚到寫入；反例驗證，不是新部署版或並行資格 |
| [REA裝置生命週期](rea-ixo-static-20261020-r1/device-retirement/verification.json)／[命令完成邊界](rea-ixo-static-20261020-r1/transaction-boundary/verification.json)／[交錯模型](rea-ixo-static-20261020-r1/transaction-boundary/schedule-result.json) | 引用／worker停止、描述游標與Present標記；模型反例不證GE死鎖或writer排他，缺函式拒絕與改入口結果分存 |
| [貼圖分流原件](rea-ixo-static-20261020-r1/texture-write-routing/verification.json)／[寫入宿主](texture-write-qualification-r1/verification.json) | REA建立／更新分流與MANAGED BC3的Texture／Surface別名寫入；同執行緒控制，不代並行資格 |
| [幾何入口r24](elevator-observer-build-r24/build-binding.json)、[目標關聯r26](elevator-observer-build-r26/build-binding.json) | r24來源／draw限定資格沿用；[r25窗口失效](../../toolchain/named-geometry-r25-target-r1/verification.json)另存，r26以[同宿主](../../toolchain/named-geometry-r26-target-r2/verification.json)及[PUREDEVICE目標切換](../../toolchain/named-geometry-r26-target-r3-pure/verification.json)補frame／RT／viewport及模組檔案身份；不證GE表面，界線回[來源](../研究與開發計畫/03_證據與歷史/來源索引.md#current-named-geometry) |
| [一次請求場景入口r27](elevator-observer-build-r27/build-binding.json) | [六個原生情境與事件交接](../../toolchain/named-scene-r27-r1/verification.json)核同幀draw／圖檔、失敗收尾及兩檔設定；原六案限32×32；[尺寸補核](../../toolchain/named-scene-r27-size-r2/verification.json)另驗2560×1440完整讀回及超16MiB拒絕，仍不證GE像素、表面或效能；[正式GE單次場景](../../toolchain/game-scene-elevator-r27-r1/result.json)另取得同幀draw／目標／影像，幾何及中間目標消費尚缺。操作契約回[封包3](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#named-scene-capture) |
| [緩衝描述入口r28](elevator-observer-build-r28/build-binding.json) | [五個描述控制](../../toolchain/named-buffer-metadata-r28-r1/verification.json)及[正式GE描述結果](../../toolchain/game-scene-elevator-r28-r1/result.json)；索引失敗仍保留IB／VB metadata，完整來源與像素關係仍未證。現行交接回[支線](../研究與開發計畫/01_決策與計畫/研究支線狀態.md#v08-work) |
| [活動緩衝選取r29](elevator-observer-build-r29/build-binding.json) | [種類／尺寸兩控制](../../toolchain/named-buffer-selection-r29-r1/verification.json)、[GE來源觀察](../../toolchain/game-scene-elevator-r29-r1/result.json)與[保存候選比對](../../toolchain/game-scene-elevator-r29-r1/interpretation.json)；完整副本仍未採用，現行下一步回支線。 |
| [Elevator載入對照](elevator01-d3dx-20261023-r1/result.json)／[比較碼](elevator01-d3dx-20261023-r1/compare.py) | 重用既有r6宿主／r3 observer，保存同參數載入及r5九層比對；[profile](elevator01-d3dx-20261023-r1/target-profile.json)只作後續完整內容候選條件 |
| [縮限build r2](ceil-observer-build-20261020-r2/result.json)／[修正版r3](ceil-observer-build-20261020-r3/result.json)／[D3DX身份r4](ceil-observer-build-20261020-r4/result.json) | 分別保留舊永久等待版本及非阻塞terminal版本；不能只按較新檔名採用 |
| [窄宿主r1](ceil-observer-qualification-20261020-r1/result.json)／[r2](ceil-observer-qualification-20261020-r2/result.json)／[修正版r3](ceil-observer-qualification-20261020-r3/result.json)／[幀上限r4](ceil-observer-qualification-20261020-r4/result.json)／[D3DX r5](ceil-observer-qualification-20261020-r5/result.json)／[重現r6](ceil-observer-qualification-20261020-r6/result.json)／[精確身份r7](ceil-observer-qualification-20261020-r7/result.json) | texture／surface、內容反例、失效重建、flush及跨幀上限繼續執行的分版控制；不代遊戲像素 |
| [安裝資格r2](observer-install-qualification-20261020-r2/summary.json)／[r3](observer-install-qualification-20261020-r3/summary.json)／[GE r2](game-observer-ceil-20261020-r2/result.json)／[r3](game-observer-ceil-20261020-r3/result.json)／[r4](game-observer-ceil-20261020-r4/result.json)／[r5](game-observer-ceil-20261020-r5/result.json) | 事件配置拒絕控制、人工關閉、Escape取消、視窗據點觀測及恢復；現況沿支線權威指標查Issue／Project，不在此另列下一步 |
| [host-qualification-20261018.json](host-qualification-20261018.json) | 取得器候選主機唯讀快照 |
| [signing-availability-20261018.json](signing-availability-20261018.json) | 當時簽章能力查核 |
| [host-preflight-20261019.json](host-preflight-20261019.json) | 13案版本、政策、路徑及身分快照 |

具體方法與相依見[取得器契約](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#bounded-implementation-handoff)、[後段控制設計](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#bounded-postgate-design)、[13案契約](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#host-qualification-activity)。13案僅服務選用該宿主的活動；此頁是導覽，不是執行指令。

原碼內的「靜態交付／未驗證」註解按交付時點與範圍讀取，既有局部控制見下方結果；不為改註解而更換固定原碼身份。快照只記當時環境，不證目前條件或行為已合格。

## 已保存活動

先讀各活動summary，再回逐案結果、執行身分與原碼副本；started記錄是事件，不代表程序現在仍在執行。各目錄彼此獨立，首次失敗不刪除，修正後結果不覆蓋舊版本。

| 活動目錄 | 結果入口與適用範圍 |
| --- | --- |
| [outer-export-synthetic-r1](outer-export-synthetic-20261020-r1/summary.json)／[r2](outer-export-synthetic-20261020-r2/summary.json) | 初版harness編碼失敗與修正後34案；各自版本／失敗保留 |
| [outer-export-observed](outer-export-observed-20261020-r1/output/complete.json) | 現存來源唯一key的outer保存、回讀與三次完整包身份收尾 |
| [pres-candidates-synthetic-r1](pres-candidates-synthetic-20261020-r1/summary.json)／[r2](pres-candidates-synthetic-20261020-r2/summary.json)／[r3](pres-candidates-synthetic-20261020-r3/summary.json) | 名稱起點碰撞判別修正與新增zero-data分支資格，各自測試及原碼副本 |
| [pres-candidates-observed-r1](pres-candidates-observed-20261020-r1/output/failed.json)／[r2](pres-candidates-observed-20261020-r2/output/complete.json) | 首次未知型別停止及新版完整表／字面候選保存；名稱覆蓋不代語義唯一性 |
| [Elevator_01取件](elevator01-observed-20261021-r1/complete.json)／[表示](elevator01-representation-20261021-r1/complete.json) | 沿未變入口與資格由保存outer取得單一F span及BC3表示；供同源材質鏈核對，不代遊戲表面使用 |
| [rea-native-qualification-r1](rea-native-qualification-20261020-r1/result.json)／[r2](rea-native-qualification-20261020-r2/qualification.json)／[r3](rea-native-qualification-20261020-r3/qualification.json) | r1產生器欄位格式失敗；r2靜態查詢通過但有日誌載入限制；r3英文執行路徑下的局部資格、原始回覆與Evidence |
| [ceil-decode-synthetic-r1](ceil-decode-synthetic-20261020-r1/summary.json)／[r2](ceil-decode-synthetic-20261020-r2/summary.json) | 10項解碼／布局控制與補充完整result讀取後12項資格；只用合成資料 |
| [ceil-decode-observed-r1](ceil-decode-observed-20261020-r1/output/failed.json)／[r2](ceil-decode-observed-20261020-r2/output/complete.json) | 首次profile大小停止及修正後候選raw／DDS／預覽保存，未證引擎使用 |
| [ceil-hd-candidate](ceil-hd-candidate-20261020-r1/generation.json) | 舊排列預覽衍生PNG／DDS與檢查，退出目前採用，保留追溯 |
| [rea-ge2rb-read-decode](rea-ge2rb-read-decode-20261020-r1/source-binding.json) | 當前EXE唯讀副本、PE映像及重新換算／核符的歷史種子；是否完成REA查詢依活動結果與支線判定 |
| [REA r4資格](rea-native-qualification-20261020-r4/qualification.json)／[r5資格](rea-native-qualification-20261020-r5/qualification.json)／[正式r2](rea-ge2rb-read-decode-20261020-r2/analysis/result.json) | 停用analyzer及同session接續資格；正式函式證據、末次錯誤及Evidence收尾各自保存 |
| [Frida r1](../../toolchain/rea-frida-fixture/run-r1/result.json)／[r2](../../toolchain/rea-frida-fixture/run-r2/result.json) | 回覆欄位誤讀失敗與修正後局部資格分列；各版原碼快照、腳本、原始回覆與正常detach保存 |
| [五角色Frida資格r1](../../toolchain/rea-read-fixture/run-r1/summary.json)／[重疊buffer r2](../../toolchain/rea-read-fixture/run-r2/summary.json)／[正式profile與啟動嘗試](rea-game-read-20261020-r1/launch-attempts.json) | 六情境及固定source快照；正式遊戲尚未附加，現況回支線 |
| [來源讀取r2](rea-ge2rb-source-read-20261020-r2/analysis/result.json)／[r3](rea-ge2rb-source-read-20261020-r3/analysis/result.json) | resfile／記憶體stream原始函式及末次未建函式拒絕；r3保存provider／HANDLE讀取、範圍、stub與slot關閉證據 |
| [raw拒絕](rea-raw-qualification-20261020-r1/analysis/000-open_binary.json)／[window資格](rea-window-qualification-20261020-r1/qualification.json)／[GNF window](rea-gnf-window-20261020-r1/analysis/result.json)／[stream表項window](rea-stream-window-20261020-r1/analysis/result.json)／[vector雙工具window](rea-vector-static-20261020-r1/analysis/result.json)／[選路與清理](rea-vector-consumer-20261020-r1/verification.json)／[來源準備](rea-source-selection-20261020-r1/verification.json)／[來源尾綴分支](rea-source-selection-20261020-r1/path-predicate/verification.json)／[來源provider與封包配對](rea-source-selection-20261020-r1/provider-open/verification.json)／[PRES修正](rea-pres-relocation-20261020-r1/verification.json)／[模組與貼圖消費](rea-handler-static-20261020-r1/verification.json)／[IXO讀取與引用](rea-ixo-static-20261020-r1/verification.json)／[材質及屬性連接](rea-ixo-static-20261020-r1/type-factory/verification.json)／[幾何與網格引用](rea-ixo-static-20261020-r1/geometry-handle/verification.json)／[SG材質欄與網格雙層連接](rea-ixo-static-20261020-r1/mesh-links/verification.json) | 供應器格式限制及保留VA的局部分析替代；與完整EXE同源，非另一份機制實證 |
| [解碼資格r3](ceil-decode-qualification-20261020-r3/summary.json)／[r4](ceil-decode-qualification-20261020-r4/summary.json)／[實物r3](ceil-decode-observed-20261020-r3/output/result.json) | 24案首塊延後／封裝控制；相對路徑呼叫錯誤與絕對路徑通過分列；保存副本的新表示，不覆蓋舊模型結果 |
| [D3D9宿主r1](observer-ceil-qualification-20261020-r1/result.json)／[r2](observer-ceil-qualification-20261020-r2/result.json) | 固定R21 DLL與宿主：舊輸入20 mip、新r3輸入9 mip及完整內容身份；沙箱HAL拒絕另存，正常桌面成功不代GE整合 |
| [安裝資格](observer-install-qualification-20261020-r1/summary.json)／[實際遊戲觀測](game-observer-ceil-20261020-r1/result.json)／[撤除後啟動](game-observer-ceil-20261020-r1/restored-startup.json) | 自有檔案交易、初始載入期觀測及恢復對照；未達場景，不能採零命中為內容反例 |
| [ceil-b5-observed-20261019-bb592664](ceil-b5-observed-20261019-bb592664) | [failed](ceil-b5-observed-20261019-bb592664/output/failed.json)、[審閱](ceil-b5-observed-20261019-bb592664/result-review.json)：v3正式唯讀在dataset5的F位址／fence門檻停止；保存4400-byte局部metadata，無名稱／B引用／member比較或後置包hash |
| [ceil-b5-synthetic-20261019-r1](ceil-b5-synthetic-20261019-r1) | [summary](ceil-b5-synthetic-20261019-r1/summary.json)：v3共138項PASS；[審閱](ceil-b5-synthetic-20261019-r1/result-review.json)核metadata、語義停止與收據；只有合成來源，無新正式包讀取 |
| [ceil-b5-assessment-20261019](ceil-b5-assessment-20261019) | [assessment](ceil-b5-assessment-20261019/assessment.json)：第五欄結構／語義靜態評估；含三份既有Extra文字原碼副本，PC副本沿前次；無新遊戲bytes、入口修改或測試，方法及不足回研究正文 |
| [ceil-b-observed-20261019-9f4aceb8](ceil-b-observed-20261019-9f4aceb8) | [failed](ceil-b-observed-20261019-9f4aceb8/output/failed.json)、[審閱](ceil-b-observed-20261019-9f4aceb8/result-review.json)：v2正式唯讀在B nameCount5處停止；前置包／outer相符，無名稱覆蓋、member比較或後置包hash |
| [ceil-b-synthetic-20261019-r4](ceil-b-synthetic-20261019-r4) | [summary](ceil-b-synthetic-20261019-r4/summary.json)：v2共97項PASS；[審閱](ceil-b-synthetic-20261019-r4/result-review.json)核來源、metadata與收據，只含合成資料 |
| [r1](ceil-b-synthetic-20261019-r1/summary.json)、[r2](ceil-b-synthetic-20261019-r2/summary.json)、[r3](ceil-b-synthetic-20261019-r3/summary.json) | B控制失敗與修正歷程：94／96、96／97、96／97；r3為未成功套修補而重複r2，非新增證據；各自source副本與產物保留 |
| [ceil-b-reference-assessment-20261019](ceil-b-reference-assessment-20261019) | [assessment](ceil-b-reference-assessment-20261019/assessment.json)：既有B名稱／引用原碼及保存record回核；同目錄四份文字原碼副本，無新遊戲bytes、入口實作或測試 |
| [ceil-observed-20261019-c4e21a90](ceil-observed-20261019-c4e21a90) | [failed](ceil-observed-20261019-c4e21a90/output/failed.json)、[審閱](ceil-observed-20261019-c4e21a90/result-review.json)：當次包／outer前置身份相符，PRES遇B型引用停止；exit1／INVALID，無Ceil比較或完成收據 |
| [ceil-synthetic-20261019-r1](ceil-synthetic-20261019-r1) | [summary](ceil-synthetic-20261019-r1/summary.json)：首53項PASS；原碼副本／合成輸入／逐案observations及CLI產物保留 |
| [ceil-synthetic-20261019-supplement](ceil-synthetic-20261019-supplement) | [summary](ceil-synthetic-20261019-supplement/summary.json)：補充4項PASS；[審閱](ceil-synthetic-20261019-supplement/result-review.json)核兩組同一入口、保全與收據；model-sources只含既有文字原碼副本 |
| [outer-observed-20261019-a91b70e9](outer-observed-20261019-a91b70e9) | [result](outer-observed-20261019-a91b70e9/output/result.json)、[審閱](outer-observed-20261019-a91b70e9/result-review.json)：獨立現存包前後身份相符、唯一key與outer長度同S64，outer hash不同即停；僅索引與報告，無內部解析／outer匯出 |
| [outer-synthetic-20261019-r1](outer-synthetic-20261019-r1) | [summary](outer-synthetic-20261019-r1/summary.json)：29項中24 PASS、5 ERROR；驅動以預設cp950讀UTF-8 JSON出錯，原碼及結果保留 |
| [outer-synthetic-20261019-r2](outer-synthetic-20261019-r2) | [summary](outer-synthetic-20261019-r2/summary.json)：修正驅動後29項PASS；[審閱](outer-synthetic-20261019-r2/result-review.json)保存資格範圍及身份。僅合成來源 |
| [unit-20261018-92f2854c](unit-20261018-92f2854c) | [controls-summary](unit-20261018-92f2854c/controls-summary.json)：10項PASS，第11項full-scan-rejections為FAIL_OR_CONTROL_ERROR；未完成16組。此目錄的harness副本保留失敗版本 |
| [unit-20261018-92f2854c-r2](unit-20261018-92f2854c-r2) | [controls-summary](unit-20261018-92f2854c-r2/controls-summary.json)、[result-review](unit-20261018-92f2854c-r2/result-review.json)：修正後16項PASS，限隔離函式；未執行正式取得main |
| [unit-entry-20261018-04863d46](unit-entry-20261018-04863d46) | [summary](unit-entry-20261018-04863d46/summary.json)、[result-review](unit-entry-20261018-04863d46/result-review.json)：8項PASS，限分段讀取及入口拒絕；fullAcquisitionQualified=false |

逐次原因與批准範圍回[16組歷史](../研究與開發計畫/03_證據與歷史/歷史研究紀錄.md#bounded-control-run-audit)與[8項歷史](../研究與開發計畫/03_證據與歷史/歷史研究紀錄.md#bounded-admission-audit)。成功拒絕的案例會保存failed.json，這不與該拒絕控制PASS矛盾；需一起讀預期、actual及觀測檔。

獨立比對活動的`target-source.py`／`harness-source.py`固定當次原碼；`started.json`／`summary.json`保存入口、Python及判定。各`test_*`子目錄的`fixture.qpck`、candidate／reference／input JSON均由驅動生成，`observation.json`為斷言觀測；output內僅索引與身份報告，沒有正式原件或outer匯出。方法與JSON欄位見[封包3](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#bounded-source-branches)，實際來源身份見[登錄](../研究與開發計畫/03_證據與歷史/來源索引.md#outer-comparison-synthetic-sources)。

正式身份活動的`candidate.json`與`reference.json`分別綁來源及歷史目標，`invocation.json`／`target-source.py`保存入口及範圍，stdout／stderr／process-result保存程序收尾；`output/index.bin`是本次保存的QPCK原索引，result／complete及`result-review.json`保存結果與收據核對。此活動不含outer副本或內部成員。

Ceil合成活動的fixture.qpck與profiles全由驅動生成；metadata以result內hex及offset保存，只有合成索引另存index.bin，沒有outer／成員匯出。所有測試檔案位於其test_*子目錄，真實CLI與注入控制分開讀；首批及補充的當時harness副本保留，最終驅動不能冒充首批原版本。完整模型、輸入欄位與停止條件見[封包3](../研究與開發計畫/04_工作封包/03_首輪判別設計.md#ceil-member-identity-design)，精確身份見[來源](../研究與開發計畫/03_證據與歷史/來源索引.md#ceil-member-locator-sources)。

Ceil正式活動的candidate／outer／member JSON分開保存現存來源與歷史packed參考；invocation及runner固定唯一呼叫，target／helper副本核符資格版。output只有started、原QPCK index.bin與含3152-byte局部metadata的failed.json；無outer／成員輸出、成功result或complete。process-result確認程序結束，審閱只核已保存產物，沒有重新開啟正式包。

## 活動內檔案歸屬

以下清單按實際檔案列出，未移動、重命名或去除副本。歷史source副本用來還原各次身份，不能拿現在根目錄的同名驅動替代。

<details>
<summary>unit-20261018-92f2854c：26個檔案</summary>

| 類別 | 原位置檔案 |
| --- | --- |
| 活動彙總／逐案判定／當時自檢 | [cases.jsonl](unit-20261018-92f2854c/cases.jsonl)、[controls-summary.json](unit-20261018-92f2854c/controls-summary.json) |
| 合成輸入／哨兵（非遊戲原件） | [compare-saved.bin](unit-20261018-92f2854c/compare-saved.bin)、[compare-source.bin](unit-20261018-92f2854c/compare-source.bin)、[copy-input.bin](unit-20261018-92f2854c/copy-input.bin)、[existing.bin](unit-20261018-92f2854c/existing.bin)、[index-duplicate.bin](unit-20261018-92f2854c/index-duplicate.bin)、[index-valid.bin](unit-20261018-92f2854c/index-valid.bin)、[range-input.bin](unit-20261018-92f2854c/range-input.bin)、[record-empty.bin](unit-20261018-92f2854c/record-empty.bin)、[record-index.bin](unit-20261018-92f2854c/record-index.bin)、[record-key.bin](unit-20261018-92f2854c/record-key.bin)、[record-offset.bin](unit-20261018-92f2854c/record-offset.bin)、[record-outside.bin](unit-20261018-92f2854c/record-outside.bin)、[record-size.bin](unit-20261018-92f2854c/record-size.bin) |
| 啟動、輸入、版本／政策與既有語法觀察 | [controls-started.json](unit-20261018-92f2854c/controls-started.json)、[host-before.json](unit-20261018-92f2854c/host-before.json)、[invocation.json](unit-20261018-92f2854c/invocation.json) |
| 合成複製產物 | [copy-output.bin](unit-20261018-92f2854c/copy-output.bin) |
| 當次原碼副本 | [harness-source.ps1](unit-20261018-92f2854c/harness-source.ps1)、[target-source.ps1](unit-20261018-92f2854c/target-source.ps1) |
| 函式序列化／索引產物（可能不完整） | [index-duplicate.jsonl](unit-20261018-92f2854c/index-duplicate.jsonl)、[index-valid.jsonl](unit-20261018-92f2854c/index-valid.jsonl)、[integer.json](unit-20261018-92f2854c/integer.json) |
| 當次標準輸出／錯誤文字 | [stderr.txt](unit-20261018-92f2854c/stderr.txt)、[stdout.txt](unit-20261018-92f2854c/stdout.txt) |

</details>

<details>
<summary>unit-20261018-92f2854c-r2：43個檔案</summary>

| 類別 | 原位置檔案 |
| --- | --- |
| 活動彙總／逐案判定／當時自檢 | [cases.jsonl](unit-20261018-92f2854c-r2/cases.jsonl)、[controls-summary.json](unit-20261018-92f2854c-r2/controls-summary.json)、[result-review.json](unit-20261018-92f2854c-r2/result-review.json) |
| 合成輸入／哨兵（非遊戲原件） | [compare-saved.bin](unit-20261018-92f2854c-r2/compare-saved.bin)、[compare-source.bin](unit-20261018-92f2854c-r2/compare-source.bin)、[copy-input.bin](unit-20261018-92f2854c-r2/copy-input.bin)、[existing.bin](unit-20261018-92f2854c-r2/existing.bin)、[index-duplicate.bin](unit-20261018-92f2854c-r2/index-duplicate.bin)、[index-late-invalid.bin](unit-20261018-92f2854c-r2/index-late-invalid.bin)、[index-missing.bin](unit-20261018-92f2854c-r2/index-missing.bin)、[index-overlaps.bin](unit-20261018-92f2854c-r2/index-overlaps.bin)、[index-short.bin](unit-20261018-92f2854c-r2/index-short.bin)、[index-valid.bin](unit-20261018-92f2854c-r2/index-valid.bin)、[multi-input.bin](unit-20261018-92f2854c-r2/multi-input.bin)、[range-input.bin](unit-20261018-92f2854c-r2/range-input.bin)、[record-empty.bin](unit-20261018-92f2854c-r2/record-empty.bin)、[record-index.bin](unit-20261018-92f2854c-r2/record-index.bin)、[record-key.bin](unit-20261018-92f2854c-r2/record-key.bin)、[record-offset.bin](unit-20261018-92f2854c-r2/record-offset.bin)、[record-outside.bin](unit-20261018-92f2854c-r2/record-outside.bin)、[record-size.bin](unit-20261018-92f2854c-r2/record-size.bin)、[sharing.bin](unit-20261018-92f2854c-r2/sharing.bin)、[zero-empty.bin](unit-20261018-92f2854c-r2/zero-empty.bin)、[zero-source.bin](unit-20261018-92f2854c-r2/zero-source.bin) |
| 啟動、輸入、版本／政策與既有語法觀察 | [controls-started.json](unit-20261018-92f2854c-r2/controls-started.json)、[host-after.json](unit-20261018-92f2854c-r2/host-after.json)、[host-before.json](unit-20261018-92f2854c-r2/host-before.json)、[invocation-r2.json](unit-20261018-92f2854c-r2/invocation-r2.json)、[syntax.json](unit-20261018-92f2854c-r2/syntax.json) |
| 合成複製產物 | [copy-output.bin](unit-20261018-92f2854c-r2/copy-output.bin)、[multi-output.bin](unit-20261018-92f2854c-r2/multi-output.bin)、[zero-copy.bin](unit-20261018-92f2854c-r2/zero-copy.bin) |
| 當次原碼副本 | [harness-source.ps1](unit-20261018-92f2854c-r2/harness-source.ps1)、[target-source.ps1](unit-20261018-92f2854c-r2/target-source.ps1) |
| 函式序列化／索引產物（可能不完整） | [index-duplicate.jsonl](unit-20261018-92f2854c-r2/index-duplicate.jsonl)、[index-late-invalid.jsonl](unit-20261018-92f2854c-r2/index-late-invalid.jsonl)、[index-missing.jsonl](unit-20261018-92f2854c-r2/index-missing.jsonl)、[index-short.jsonl](unit-20261018-92f2854c-r2/index-short.jsonl)、[index-valid.jsonl](unit-20261018-92f2854c-r2/index-valid.jsonl)、[integer.json](unit-20261018-92f2854c-r2/integer.json)、[overlaps.jsonl](unit-20261018-92f2854c-r2/overlaps.jsonl) |
| 當次標準輸出／錯誤文字 | [stderr-r2.txt](unit-20261018-92f2854c-r2/stderr-r2.txt)、[stdout-r2.txt](unit-20261018-92f2854c-r2/stdout-r2.txt) |

</details>

<details>
<summary>unit-entry-20261018-04863d46：47個檔案</summary>

| 類別 | 原位置檔案 |
| --- | --- |
| 合成輸入／哨兵（非遊戲原件） | [existing-output/sentinel.bin](unit-entry-20261018-04863d46/existing-output/sentinel.bin)、[seven.bin](unit-entry-20261018-04863d46/seven.bin)、[zero-outer.bin](unit-entry-20261018-04863d46/zero-outer.bin) |
| 逐案讀取軌跡／main觀測 | [fragment-eof-trace.json](unit-entry-20261018-04863d46/fragment-eof-trace.json)、[fragment-exception-trace.json](unit-entry-20261018-04863d46/fragment-exception-trace.json)、[fragment-success-trace.json](unit-entry-20261018-04863d46/fragment-success-trace.json)、[main-existing-output-observation.json](unit-entry-20261018-04863d46/main-existing-output-observation.json)、[main-missing-source-observation.json](unit-entry-20261018-04863d46/main-missing-source-observation.json)、[main-short-outer-observation.json](unit-entry-20261018-04863d46/main-short-outer-observation.json)、[main-short-package-observation.json](unit-entry-20261018-04863d46/main-short-package-observation.json)、[main-wrong-hash-observation.json](unit-entry-20261018-04863d46/main-wrong-hash-observation.json) |
| 逐案預期與判定 | [fragment-eof.json](unit-entry-20261018-04863d46/fragment-eof.json)、[fragment-exception.json](unit-entry-20261018-04863d46/fragment-exception.json)、[fragment-success.json](unit-entry-20261018-04863d46/fragment-success.json)、[main-existing-output.json](unit-entry-20261018-04863d46/main-existing-output.json)、[main-missing-source.json](unit-entry-20261018-04863d46/main-missing-source.json)、[main-short-outer.json](unit-entry-20261018-04863d46/main-short-outer.json)、[main-short-package.json](unit-entry-20261018-04863d46/main-short-package.json)、[main-wrong-hash.json](unit-entry-20261018-04863d46/main-wrong-hash.json) |
| 當次原碼副本 | [harness-source.ps1](unit-entry-20261018-04863d46/harness-source.ps1)、[target-source.ps1](unit-entry-20261018-04863d46/target-source.ps1) |
| 啟動、輸入、版本／政策與既有語法觀察 | [host-after.json](unit-entry-20261018-04863d46/host-after.json)、[host-before.json](unit-entry-20261018-04863d46/host-before.json)、[inputs.json](unit-entry-20261018-04863d46/inputs.json)、[invocation.json](unit-entry-20261018-04863d46/invocation.json)、[started.json](unit-entry-20261018-04863d46/started.json)、[syntax.json](unit-entry-20261018-04863d46/syntax.json) |
| 當次標準輸出／錯誤文字 | [main-existing-output-stderr.txt](unit-entry-20261018-04863d46/main-existing-output-stderr.txt)、[main-existing-output-stdout.txt](unit-entry-20261018-04863d46/main-existing-output-stdout.txt)、[main-missing-source-stderr.txt](unit-entry-20261018-04863d46/main-missing-source-stderr.txt)、[main-missing-source-stdout.txt](unit-entry-20261018-04863d46/main-missing-source-stdout.txt)、[main-short-outer-stderr.txt](unit-entry-20261018-04863d46/main-short-outer-stderr.txt)、[main-short-outer-stdout.txt](unit-entry-20261018-04863d46/main-short-outer-stdout.txt)、[main-short-package-stderr.txt](unit-entry-20261018-04863d46/main-short-package-stderr.txt)、[main-short-package-stdout.txt](unit-entry-20261018-04863d46/main-short-package-stdout.txt)、[main-wrong-hash-stderr.txt](unit-entry-20261018-04863d46/main-wrong-hash-stderr.txt)、[main-wrong-hash-stdout.txt](unit-entry-20261018-04863d46/main-wrong-hash-stdout.txt)、[stderr.txt](unit-entry-20261018-04863d46/stderr.txt)、[stdout.txt](unit-entry-20261018-04863d46/stdout.txt) |
| 活動彙總／逐案判定／當時自檢 | [result-review.json](unit-entry-20261018-04863d46/result-review.json)、[summary.json](unit-entry-20261018-04863d46/summary.json) |
| 預期拒絕後的開始／失敗紀錄 | [short-outer-output/failed.json](unit-entry-20261018-04863d46/short-outer-output/failed.json)、[short-outer-output/started.json](unit-entry-20261018-04863d46/short-outer-output/started.json)、[short-package-output/failed.json](unit-entry-20261018-04863d46/short-package-output/failed.json)、[short-package-output/started.json](unit-entry-20261018-04863d46/short-package-output/started.json)、[wrong-hash-output/failed.json](unit-entry-20261018-04863d46/wrong-hash-output/failed.json)、[wrong-hash-output/started.json](unit-entry-20261018-04863d46/wrong-hash-output/started.json) |

</details>
