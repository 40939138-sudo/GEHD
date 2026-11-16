$ErrorActionPreference = 'Stop'
# Only metadata for relevant native-analysis processes; never command lines,
# arbitrary process memory, environment variables, or termination.
$rows = @(Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -match '^javaw?\.exe$') -or
    ($_.Name -match '^node\.exe$' -and $_.CommandLine -match '(?i)rea-agents[\\/].*rea\.(m?js)|[\\/]rea\.(m?js)["\s]+mcp')
} | ForEach-Object {
    [pscustomobject]@{
        pid = $_.ProcessId
        parentPid = $_.ParentProcessId
        name = $_.Name
        image = $_.ExecutablePath
        created = $_.CreationDate.ToUniversalTime().ToString('o')
    }
})
[pscustomobject]@{
    checkedAt = [DateTime]::UtcNow.ToString('o')
    complete = $true
    matchingProcesses = $rows
    scope = 'All Java processes and recognizable REA Node command lines; requires single shared operator, not protection from uncooperative future launches.'
} | ConvertTo-Json -Depth 5 -Compress
