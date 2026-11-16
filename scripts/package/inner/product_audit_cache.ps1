# Developer-build cache only. Reuses a completed full audit of an immutable input;
# it does not replace the mandatory independent audit of extracted distribution bytes.
function Get-GehdInputFacts([string]$DataRoot){
 $root=[IO.Path]::GetFullPath($DataRoot).TrimEnd('\');$text=New-Object Text.StringBuilder
 foreach($f in Get-ChildItem -LiteralPath $root -Recurse -Force|Sort-Object FullName){
  if($f.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse entry in audit input'}
  if(!$f.PSIsContainer){[void]$text.Append($f.FullName.Substring($root.Length+1).Replace('\','/')).Append("`t").Append($f.Length).Append("`t").Append($f.CreationTimeUtc.Ticks).Append("`t").Append($f.LastWriteTimeUtc.Ticks).Append("`n")}
 }
 $sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text.ToString())))).Replace('-','')}finally{$sha.Dispose()}
}
function New-GehdAuditBinding([string]$DataRoot,[string]$AuditJson,[string]$OutputPath){
 if(Test-Path -LiteralPath $OutputPath){throw 'Fresh audit binding required'}
 $root=[IO.Path]::GetFullPath($DataRoot);$auditPath=[IO.Path]::GetFullPath($AuditJson);$audit=Get-Content -LiteralPath $auditPath -Raw|ConvertFrom-Json
 $release=(Get-FileHash -LiteralPath (Join-Path $root 'release.files')).Hash
 $identity=[IO.File]::ReadAllLines((Join-Path $root 'pack.identity'))
 if(!$audit.verified -or $audit.release -ne $release -or $identity.Count -ne 5 -or $identity[4] -ne $release){throw 'Completed matching full audit required'}
 $binding=@{schema='GEHD_INPUT_AUDIT_CACHE_V1';dataRoot=$root;auditPath=$auditPath;auditSha256=(Get-FileHash -LiteralPath $auditPath).Hash;release=$release;inputFactsSha256=(Get-GehdInputFacts $root);createdUtc=[DateTime]::UtcNow.ToString('o');scope='Prior full hash audit plus unchanged paths/sizes/creation/write timestamps. Trusted immutable build inputs only; round-trip output hash audit is mandatory.'}
 $binding|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $OutputPath -Encoding utf8
}
function Test-GehdAuditBinding([string]$DataRoot,[string]$BindingPath){
 $b=Get-Content -LiteralPath $BindingPath -Raw|ConvertFrom-Json;$root=[IO.Path]::GetFullPath($DataRoot)
 if($b.schema -ne 'GEHD_INPUT_AUDIT_CACHE_V1' -or ![string]::Equals($root,$b.dataRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Audit cache scope mismatch'}
 if((Get-FileHash -LiteralPath $b.auditPath).Hash -ne $b.auditSha256 -or (Get-FileHash -LiteralPath (Join-Path $root 'release.files')).Hash -ne $b.release -or (Get-GehdInputFacts $root) -ne $b.inputFactsSha256){throw 'Audit cache invalidated'}
 $a=Get-Content -LiteralPath $b.auditPath -Raw|ConvertFrom-Json;if(!$a.verified -or $a.release -ne $b.release){throw 'Audit cache report invalid'}
 return $b
}
