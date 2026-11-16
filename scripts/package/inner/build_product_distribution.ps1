[CmdletBinding()]
param([Parameter(Mandatory)][string]$DataRoot,[Parameter(Mandatory)][string]$ControlRoot,[Parameter(Mandatory)][string]$LoaderRoot,[Parameter(Mandatory)][string]$ToolsRoot,[Parameter(Mandatory)][string]$OutputRoot,[string]$VerifiedInputAudit='',[string]$ReuseDataArchivesFrom='')
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$utf8=New-Object Text.UTF8Encoding($false)
function WriteText([string]$path,[string]$text){[IO.File]::WriteAllText($path,$text,$utf8)}
function Hash([string]$path){return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function Ordinary([string]$path){while($path){if(Test-Path -LiteralPath $path){if((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Reparse path: '+$path)}};$parent=[IO.Path]::GetDirectoryName($path);if($parent -eq $path){break};$path=$parent}}
$DataRoot=[IO.Path]::GetFullPath($DataRoot);$ControlRoot=[IO.Path]::GetFullPath($ControlRoot);$LoaderRoot=[IO.Path]::GetFullPath($LoaderRoot);$ToolsRoot=[IO.Path]::GetFullPath($ToolsRoot);$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
foreach($path in @($DataRoot,$ControlRoot,$LoaderRoot,$ToolsRoot,$OutputRoot)){Ordinary $path}
if(Test-Path -LiteralPath $OutputRoot){throw 'Fresh distribution output required'}
if($OutputRoot.StartsWith($DataRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output inside source data'}
$reuseBuild=$null;$reuseBuildHash=''
if($ReuseDataArchivesFrom){$ReuseDataArchivesFrom=[IO.Path]::GetFullPath($ReuseDataArchivesFrom);Ordinary $ReuseDataArchivesFrom;if($OutputRoot.StartsWith($ReuseDataArchivesFrom.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output inside reused distribution'};$reuseBuildPath=Join-Path $ReuseDataArchivesFrom 'build.json';$reuseBuild=Get-Content -LiteralPath $reuseBuildPath -Raw|ConvertFrom-Json;$reuseBuildHash=Hash $reuseBuildPath}
$seven=Join-Path $ToolsRoot '7zip-26.06/x64/7za.exe';$iscc=Join-Path $ToolsRoot 'inno-7.1.1/ISCC.exe'
foreach($path in @($seven,$iscc,(Join-Path $LoaderRoot 'pack-audit.exe'),(Join-Path $ControlRoot 'pack-control.exe'),(Join-Path $ControlRoot 'GEHD_Manager.exe'),(Join-Path $LoaderRoot 'd3d9.dll'))){if(!(Test-Path -LiteralPath $path)){throw ('Missing input: '+$path)}}
New-Item -ItemType Directory -Path $OutputRoot|Out-Null
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $OutputRoot 'executed-builder.ps1')
if($VerifiedInputAudit){
 . (Join-Path $PSScriptRoot 'product_audit_cache.ps1')
 $inputBinding=Test-GehdAuditBinding $DataRoot $VerifiedInputAudit
 Copy-Item -LiteralPath $inputBinding.auditPath -Destination (Join-Path $OutputRoot 'input-audit.json')
 Copy-Item -LiteralPath $VerifiedInputAudit -Destination (Join-Path $OutputRoot 'input-audit-binding.json')
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'product_audit_cache.ps1') -Destination $OutputRoot
}else{
 & (Join-Path $LoaderRoot 'pack-audit.exe') $DataRoot 1> (Join-Path $OutputRoot 'input-audit.json') 2> (Join-Path $OutputRoot 'input-audit-progress.log')
 if($LASTEXITCODE -ne 0){throw 'Input data audit failed'}
}
$identity=[IO.File]::ReadAllLines((Join-Path $DataRoot 'pack.identity'));$release=$identity[4]
$gameId=$identity[1];if($gameId -notin @('GER','GE2RB')){throw 'Unsupported game identity'}
$gameExeName=$gameId+'.exe';$steamFolder=if($gameId -eq 'GER'){'GOD EATER RESURRECTION'}else{'GOD EATER 2 Rage Burst'}
$trustedHash=if($gameId -eq 'GER'){'7AF658FE078ED60803F6AADC4B6A5FAE9C8427868438E4951096D87346A50596'}else{'586F9A03E75C818165520D82A1BB1289668669D4B708C97857ADE6DF5DE6BFBB'}
if($identity[2] -cne $trustedHash){throw 'Unrecognized game version'}
$buildRecord=Get-Content (Join-Path $LoaderRoot 'build.json') -Raw|ConvertFrom-Json
if($buildRecord.gameId -cne $gameId){throw 'Loader game does not match data'}
$dllPin=@($buildRecord.files|Where-Object {$_.name -eq 'd3d9.dll'})
if($dllPin.Count -ne 1 -or (Hash (Join-Path $LoaderRoot 'd3d9.dll')) -cne $dllPin[0].sha256){throw 'Loader binary changed'}
$payload=Join-Path $OutputRoot 'payload';$geh=Join-Path $payload 'GEHD';$media=Join-Path $OutputRoot 'media';$work=Join-Path $OutputRoot 'archive-work'
New-Item -ItemType Directory -Path (Join-Path $geh 'bin'),(Join-Path $geh 'data/2x'),(Join-Path $geh 'data/4x'),(Join-Path $geh 'LICENSES'),$media,$work -Force|Out-Null
$records=New-Object 'System.Collections.Generic.List[object]';$links=0;$copies=0;$reusedDDS=0;$seen=New-Object 'System.Collections.Generic.HashSet[string]'
$indexRows=[IO.File]::ReadAllLines((Join-Path $DataRoot 'release.files'));if($indexRows[0] -ne 'GEHD_FILES_V1'){throw 'Invalid release header'}
foreach($line in $indexRows|Select-Object -Skip 1){
 $f=$line -split "`t";if($f.Length -ne 3 -or $f[0] -cnotmatch '^(mapping\.tsv|[24]x/[A-F0-9]{64}\.dds)$' -or $f[1] -notmatch '^\d+$' -or [uint64]$f[1] -eq 0 -or [uint64]$f[1] -gt 256MB -or $f[2] -cnotmatch '^[A-F0-9]{64}$' -or !$seen.Add($f[0])){throw 'Invalid release path/entry'}
 $relative=$f[0];$source=Join-Path $DataRoot $relative;$dest=Join-Path (Join-Path $geh 'data') $relative
 if($relative.EndsWith('.dds') -and $reuseBuild){$reusedDDS++}
 elseif($relative.EndsWith('.dds') -and [IO.Path]::GetPathRoot($source) -eq [IO.Path]::GetPathRoot($dest)){
  try{New-Item -ItemType HardLink -Path $dest -Target $source -ErrorAction Stop|Out-Null;$links++}catch{if(Test-Path -LiteralPath $dest){throw};Copy-Item -LiteralPath $source -Destination $dest;$copies++}
 }else{Copy-Item -LiteralPath $source -Destination $dest;$copies++}
 $records.Add([PSCustomObject]@{path='GEHD/data/'+$relative;bytes=[long]$f[1];sha256=$f[2]})
}
foreach($name in @('release.files','pack.identity')){Copy-Item -LiteralPath (Join-Path $DataRoot $name) -Destination (Join-Path (Join-Path $geh 'data') $name);$path=Join-Path (Join-Path $geh 'data') $name;$records.Add([PSCustomObject]@{path='GEHD/data/'+$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Hash $path)})}
Copy-Item -LiteralPath (Join-Path $ControlRoot 'pack-control.exe') -Destination (Join-Path $geh 'bin/pack-control.exe')
Copy-Item -LiteralPath (Join-Path $ControlRoot 'GEHD_Manager.exe') -Destination (Join-Path $geh 'GEHD_Manager.exe')
Copy-Item -LiteralPath (Join-Path $LoaderRoot 'd3d9.dll') -Destination (Join-Path $geh 'bin/d3d9.dll')
Copy-Item -LiteralPath (Join-Path $LoaderRoot 'd3d9.dll') -Destination (Join-Path $payload 'd3d9.dll')
Copy-Item -LiteralPath (Join-Path $ToolsRoot 'inno-7.1.1/License.txt') -Destination (Join-Path $geh 'LICENSES/Inno-Setup.txt')
Copy-Item -LiteralPath (Join-Path $ToolsRoot '7zip-26.06/License.txt') -Destination (Join-Path $geh 'LICENSES/7-Zip.txt')
$extraShell=@()
foreach($componentRoot in @($LoaderRoot,$ControlRoot)){
 $vendorRecord=Join-Path $componentRoot 'vendor/source.json'
 if(Test-Path -LiteralPath $vendorRecord){
  $vendorInfo=Get-Content -LiteralPath $vendorRecord -Raw|ConvertFrom-Json
  $licensePin=@($vendorInfo.files|Where-Object {$_.path -eq 'LICENSE.txt'})
  if($licensePin.Count -ne 1){throw 'Missing pinned OpenSSL license'}
  $licensePath=Join-Path $componentRoot 'vendor/LICENSE.txt'
  if((Hash $licensePath) -ne $licensePin[0].sha256){throw 'OpenSSL license identity changed'}
  $licenseDest=Join-Path $geh 'LICENSES/OpenSSL.txt'
  if(Test-Path -LiteralPath $licenseDest){if((Hash $licenseDest) -ne $licensePin[0].sha256){throw 'Conflicting OpenSSL licenses'}}
  else{Copy-Item -LiteralPath $licensePath -Destination $licenseDest;$extraShell+='LICENSES/OpenSSL.txt'}
 }
}
$readme=@"
$gameId 高清貼圖模組（本地版）
資料版本：$release

安裝精靈：把 GEHD_Setup.exe 與所有 GEHD_*.7z 放在同一資料夾，執行安裝器並選擇含 $gameExeName 的遊戲資料夾。
手動解壓：先正常退出遊戲，確認沒有其他 d3d9.dll 載入器；把全部 GEHD_*.7z 分別解壓到同一個遊戲根目錄。不要覆蓋未知或不同版本的既有安裝。
每個 7z 都是可獨立解壓的資料包，不是 .001 分卷；必須全部齊備。下載時可逐檔下載，無須同時多選下載。

正常遊玩仍從 Steam 啟動，不必先開管理器，也不需要 Codex、Python或常駐工具。
管理器位於 GEHD\GEHD_Manager.exe。手動解壓後，可按「啟用／接管」建立管理紀錄。
手動版移除後，重新解壓完整資料包，再按「啟用／接管」即可重新安裝；既有恢復紀錄會保留。
搬移只移動高清 data 資料，不移動遊戲、中文包或管理器；外置後不要自行刪除路徑設定。
修復／更新請先完整解壓對應版本到另一個資料夾，再選取該副本的 GEHD\data。修復須同版本，維護備份會保留；回復上次維護須位於該次維護的資料位置。
整包、載入器及管理器更新：關閉遊戲與管理器後，重新執行新版 GEHD_Setup.exe。已外置的資料位置會保留。
回復完整版本：執行安裝器並選「回復上次完整版本更新」，使用本地保留的備份，不需要 DDS 壓縮包。請勿自行刪除備份。
舊版原生部署若有完整且相符的所有權收據，可由安裝器遷移；舊 DDS 包保留。無法辨識的載入器或路徑檔會停止處理。
遊戲執行中禁止變更。發生中斷可使用「繼續未完成工作」，請勿自行刪除 stage、state 或備份。

使用安裝精靈安裝者可從 Windows 的應用程式清單移除。手動版的「移除高清模組」會撤除載入檔與未修改的高清資料，管理工具及恢復紀錄保留。
原遊戲檔、中文模組及未知／已修改的檔案不列為本包可任意刪除的內容。中文包需另行安裝，不隨此包提供。
本包只適用標示的遊戲及版本；另一作須使用其獨立資料包。外來模組的已驗範圍依各作發布說明，不能跨作推定。
此包尚需依發布說明確認適用遊戲版本與已驗畫面範圍；安裝成功不等同全部貼圖品質已完成驗收。
"@
$loaderBuildPath=Join-Path $LoaderRoot 'build.json'
if(Test-Path -LiteralPath $loaderBuildPath){
 $loaderBuildInfo=Get-Content -LiteralPath $loaderBuildPath -Raw|ConvertFrom-Json
 if($loaderBuildInfo.schema -eq 'GEHD_LOADING_FRAMEWORK_BUILD_V1'){
  $readme+=@'


載入加速框架候選：完整 SHA-256 驗證、參數／長度預篩、2 個 CPU 準備 worker、有界預讀與跨程序學習紀錄。
預設採優化 SHA 與預篩的同步載入；worker 與學習預讀需由管理器明確選用。實際任務收益待各作受控實機比較，不把離線固定路線當實機保證。
DDS 準備額度合計 256 MiB，其中預讀最多 64 MiB；超過 64 MiB 的單張 DDS 不預讀。圖像、mip、透明度與 D3D 建立流程保留。
學習紀錄自動存於目前使用者的 %LOCALAPPDATA%\GEHD\prefetch\遊戲識別\安裝識別資料夾。每份快照最多 1 MiB，保留兩份；不保存 DDS 或顯存內容。
背景寫入不要求管理器常駐。正常關閉／意外中止後沿用最後已完成的快照，最近尚未保存的觀察可能遺失；重開機後需重新讀取及完整驗證 DDS。
升級高清資料或已知中文包變更時會使舊預讀紀錄失效；讀寫紀錄失敗不會跳過貼圖驗證。關閉遊戲後可在管理器按「清除預讀／重學」；更換紀錄世代，舊快照不再使用，下一次成功保存會逐步取代。
關閉遊戲後，可由管理器選擇同步、僅準備工作或學習預讀，下次啟動生效。進階環境變數 GEHD_LOADING_MODE=sync|workers|learned 優先於管理器設定；移除覆寫需重新啟動 Steam。
本候選已驗範圍以離線報告為準；實際任務載入改善仍需分作實機驗收。管理器的「已啟用」表示部署狀態，並非預讀命中率。
'@
 }
}
WriteText (Join-Path $geh 'README.txt') $readme
$shell=(@('bin/d3d9.dll','bin/pack-control.exe','GEHD_Manager.exe','README.txt','LICENSES/Inno-Setup.txt','LICENSES/7-Zip.txt')+$extraShell)|Sort-Object
$shellRecords=@(foreach($relative in $shell){$path=Join-Path $geh $relative;[PSCustomObject]@{path=$relative;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Hash $path)}})
WriteText (Join-Path $geh 'shell.files') ("GEHD_SHELL_V1`n"+(($shellRecords|ForEach-Object{$_.path+"`t"+$_.bytes+"`t"+$_.sha256}) -join "`n")+"`n")
$shellHash=Hash (Join-Path $geh 'shell.files');WriteText (Join-Path $geh 'bundle.identity') ("GEHD_BUNDLE_V1`n"+$identity[2]+"`n"+$release+"`n"+$shellHash+"`n")
foreach($r in $shellRecords){$records.Add([PSCustomObject]@{path='GEHD/'+$r.path;bytes=$r.bytes;sha256=$r.sha256})}
foreach($relative in @('d3d9.dll','GEHD/bundle.identity','GEHD/shell.files')){$path=Join-Path $payload $relative;$records.Add([PSCustomObject]@{path=$relative;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Hash $path)})}
$archives=New-Object 'System.Collections.Generic.List[object]'
function Archive([string]$name,[object[]]$entries,[int]$bit){
 if(!$entries.Count){return};$list=Join-Path $work ($name+'.txt');WriteText $list ((($entries|Sort-Object path|ForEach-Object path) -join "`n")+"`n")
 $archive=Join-Path $work ($name+'.7z');$timer=[Diagnostics.Stopwatch]::StartNew()
 Push-Location $payload
 try{$log=& $seven a -t7z $archive ('@'+$list) -scsUTF-8 -mx=3 -ms=off -mmt=2 -y 2>&1;$code=$LASTEXITCODE}finally{Pop-Location}
 $log|Set-Content -LiteralPath (Join-Path $work ($name+'.log')) -Encoding utf8
 if($code -ne 0){throw ('Archive failed: '+$name)}
 if((Get-Item -LiteralPath $archive).Length -gt 1900MB){
  if($bit -lt 0 -or $entries.Count -lt 2 -or $bit -gt 255){throw 'Cannot split oversized archive'}
  $zero=@();$one=@();foreach($r in $entries){$key=[IO.Path]::GetFileNameWithoutExtension($r.path);$digit=[Convert]::ToInt32($key.Substring([int][Math]::Floor($bit/4),1),16);if(($digit -band (1 -shl (3-($bit%4)))) -eq 0){$zero+=$r}else{$one+=$r}}
  Archive ($name+'_0') $zero ($bit+1);Archive ($name+'_1') $one ($bit+1);return
 }
 $check=& $seven t $archive 2>&1;if($LASTEXITCODE -ne 0){throw ('Archive integrity failed: '+$name)};$check|Set-Content -LiteralPath (Join-Path $work ($name+'-test.log')) -Encoding utf8
 $final=Join-Path $media ($name+'.7z');Move-Item -LiteralPath $archive -Destination $final
 $archives.Add([PSCustomObject]@{name=$name+'.7z';bytes=(Get-Item -LiteralPath $final).Length;sha256=(Hash $final);unpackedBytes=[long](($entries|Measure-Object bytes -Sum).Sum);files=$entries.Count;seconds=$timer.Elapsed.TotalSeconds})
 Write-Output ('Built '+$name+'.7z')
}
Archive 'GEHD_Core' @($records|Where-Object {!$_.path.EndsWith('.dds')}) -1
if($reuseBuild){
 if($reuseBuild.release -ne $release){throw 'Reused media release differs'}
 $currentDDS=@($records|Where-Object {$_.path.EndsWith('.dds')}|Sort-Object path);$previousDDS=@($reuseBuild.files|Where-Object {$_.path.EndsWith('.dds')}|Sort-Object path)
 if($currentDDS.Count -ne $previousDDS.Count){throw 'Reused DDS count differs'}
 for($i=0;$i -lt $currentDDS.Count;$i++){if($currentDDS[$i].path -cne $previousDDS[$i].path -or $currentDDS[$i].bytes -ne $previousDDS[$i].bytes -or $currentDDS[$i].sha256 -cne $previousDDS[$i].sha256){throw 'Reused DDS content differs'}}
 $dataArchives=@($reuseBuild.archives|Where-Object {$_.name -ne 'GEHD_Core.7z'});$archiveNames=New-Object 'System.Collections.Generic.HashSet[string]'
 if(!$dataArchives.Count -or [long](($dataArchives|Measure-Object files -Sum).Sum) -ne $currentDDS.Count -or [long](($dataArchives|Measure-Object unpackedBytes -Sum).Sum) -ne [long](($currentDDS|Measure-Object bytes -Sum).Sum)){throw 'Reused archive coverage differs'}
 foreach($a in $dataArchives){
  if($a.name -cnotmatch '^GEHD_Data_[0-9]{2}(?:_[01])*\.7z$' -or !$archiveNames.Add($a.name) -or $a.sha256 -cnotmatch '^[A-F0-9]{64}$' -or $a.bytes -le 0 -or $a.bytes -gt 1900MB){throw 'Invalid reused archive entry'}
  $timer=[Diagnostics.Stopwatch]::StartNew();$source=Join-Path (Join-Path $ReuseDataArchivesFrom 'media') $a.name;$dest=Join-Path $media $a.name;Ordinary $source
  if((Get-Item -LiteralPath $source).Length -ne $a.bytes -or (Hash $source) -cne $a.sha256){throw 'Reused archive changed'}
  $method='copy';if([IO.Path]::GetPathRoot($source) -eq [IO.Path]::GetPathRoot($dest)){try{New-Item -ItemType HardLink -Path $dest -Target $source -ErrorAction Stop|Out-Null;$method='hardlink'}catch{if(Test-Path -LiteralPath $dest){throw}}}
  if($method -eq 'copy'){Copy-Item -LiteralPath $source -Destination $dest;if((Hash $dest) -cne $a.sha256){throw 'Reused archive copy differs'}}
  $archives.Add([PSCustomObject]@{name=$a.name;bytes=[long]$a.bytes;sha256=$a.sha256;unpackedBytes=[long]$a.unpackedBytes;files=[int]$a.files;seconds=$timer.Elapsed.TotalSeconds;reusedFrom=$source;reuseMethod=$method})
  Write-Output ('Reused '+$a.name)
 }
 if((Hash $reuseBuildPath) -cne $reuseBuildHash){throw 'Reused build record changed'}
}else{foreach($bucket in 0..7){$selected=@($records|Where-Object {$_.path.EndsWith('.dds') -and [int][Math]::Floor([Convert]::ToInt32([IO.Path]::GetFileName($_.path).Substring(0,1),16)/2) -eq $bucket});Archive ('GEHD_Data_'+$bucket.ToString('00')) $selected 3}}
$checks=New-Object 'System.Collections.Generic.List[string]';$checks.Add('procedure CheckMedia(Base: String);');$checks.Add('begin');foreach($a in $archives){$checks.Add("  CheckFile(AddBackslash(Base) + '$($a.name)', '$($a.sha256)');")};$checks.Add('end;');$checks.Add('procedure ExtractMedia(Base, Destination: String);');$checks.Add('begin');foreach($a in $archives){$checks.Add("  ExtractArchive(AddBackslash(Base) + '$($a.name)', Destination, '', True, nil);")};$checks.Add('end;');WriteText (Join-Path $OutputRoot 'product-media-checks.iss') ($checks -join "`n")
$shellInclude=@(foreach($relative in @($shell)+@('shell.files','bundle.identity')){$r=@($records|Where-Object {$_.path -eq ('GEHD/'+$relative)})[0];$folder=[IO.Path]::GetDirectoryName($relative.Replace('/','\'));$dest='{app}\GEHD';if($folder){$dest+='\'+$folder};'Source: "{code:StageRoot}\GEHD\'+$relative.Replace('/','\')+'"; DestDir: "'+$dest+'"; ExternalSize: '+$r.bytes+'; Flags: external ignoreversion uninsneveruninstall'})
WriteText (Join-Path $OutputRoot 'product-shell-files.iss') ($shellInclude -join "`n")
$total=[long](($records|Measure-Object bytes -Sum).Sum);$controlHash=Hash (Join-Path $geh 'bin/pack-control.exe')
WriteText (Join-Path $OutputRoot 'product-pins.iss') ("#define GameId `"$gameId`"`n#define GameExeName `"$gameExeName`"`n#define SteamFolder `"$steamFolder`"`n#define PayloadBytes $total`n#define ControllerHash `"$controlHash`"`n#define ReleaseShort `"$($release.Substring(0,12))`"`n")
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'product_installer.iss') -Destination (Join-Path $OutputRoot 'product.iss')
$compile=& $iscc (Join-Path $OutputRoot 'product.iss') 2>&1;$code=$LASTEXITCODE;$compile|Set-Content -LiteralPath (Join-Path $OutputRoot 'compile.log') -Encoding utf8;if($code -ne 0){throw 'Installer compilation failed'}
$setup=Join-Path $media 'GEHD_Setup.exe';$setupHash=Hash $setup
$sum=@($archives|ForEach-Object {$_.sha256+'  '+$_.name})+@($setupHash+'  GEHD_Setup.exe');WriteText (Join-Path $media 'SHA256SUMS.txt') (($sum -join "`r`n")+"`r`n")
WriteText (Join-Path $media '安裝說明.txt') $readme
if((Hash (Join-Path $DataRoot 'release.files')) -ne $release){throw 'Source release changed'}
if($VerifiedInputAudit){$null=Test-GehdAuditBinding $DataRoot $VerifiedInputAudit}
$auditMode='fresh-full-hash';$auditBindingHash='';if($VerifiedInputAudit){$auditMode='prior-full-hash-with-unchanged-input-metadata';$auditBindingHash=Hash $VerifiedInputAudit}
$stagingLayout='complete payload';if($reuseBuild){$stagingLayout='core-only payload staging; DDS reside in hash-verified reused media'}
@{schema='GEHD_LOCAL_DISTRIBUTION_BUILD_V1';gameId=$gameId;release=$release;bundle=(Hash (Join-Path $geh 'bundle.identity'));payloadBytes=$total;files=$records;archives=$archives;setupSha256=$setupHash;dataRoot=$DataRoot;controlRoot=$ControlRoot;loaderRoot=$LoaderRoot;inputAuditMode=$auditMode;inputAuditBindingSha256=$auditBindingHash;stagingHardlinks=$links;stagingCopies=$copies;stagingReusedDDS=$reusedDDS;stagingLayout=$stagingLayout;reusedDataArchivesFrom=$ReuseDataArchivesFrom;reusedBuildSha256=$reuseBuildHash;shippingFormat='Independent non-solid 7z archives shared by setup and manual extraction; unpacked runtime DDS'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputRoot 'build.json') -Encoding utf8
Write-Output ('Built local distribution: '+$media)
