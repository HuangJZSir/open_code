param(
    [string]$Root
)

if (-not $Root) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
    $Root = (Resolve-Path $Root).Path
}

$utf8 = [System.Text.Encoding]::GetEncoding("utf-8", [System.Text.EncoderFallback]::ExceptionFallback, [System.Text.DecoderFallback]::ExceptionFallback)
$critical = 0
$warn = 0

function Get-Rel([string]$full) {
    if ($full.Length -gt $Root.Length) {
        return $full.Substring($Root.Length).TrimStart("\", "/")
    }
    return $full
}

$toolFiles = New-Object System.Collections.Generic.List[string]
Get-ChildItem -Path $Root -File -Filter "sdkconfig*" -ErrorAction SilentlyContinue | ForEach-Object { [void]$toolFiles.Add($_.FullName) }
Get-ChildItem -Path $Root -File -Filter "Kconfig*" -ErrorAction SilentlyContinue | ForEach-Object { [void]$toolFiles.Add($_.FullName) }
Get-ChildItem -Path (Join-Path $Root "main") -File -Filter "Kconfig*" -ErrorAction SilentlyContinue | ForEach-Object { [void]$toolFiles.Add($_.FullName) }
Get-ChildItem -Path $Root -Recurse -File -Include "*.csv", "*.yml", "*.yaml", "CMakeLists.txt" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch "\\build\\" } |
    ForEach-Object { [void]$toolFiles.Add($_.FullName) }

foreach ($path in $toolFiles) {
    if (-not (Test-Path $path)) { continue }
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 3) { continue }
    $hasBom = ($b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)
    $nonAscii = 0
    foreach ($x in $b) { if ($x -gt 127) { $nonAscii++ } }
    if ($hasBom -or $nonAscii -gt 0) {
        Write-Output ("CRITICAL {0} BOM={1} non-ascii={2}" -f (Get-Rel $path), $hasBom, $nonAscii)
        $critical++
    }
}

$sources = New-Object System.Collections.Generic.List[string]
Get-ChildItem -Path (Join-Path $Root "main") -Recurse -File -Include "*.c", "*.h" -ErrorAction SilentlyContinue | ForEach-Object { [void]$sources.Add($_.FullName) }
Get-ChildItem -Path (Join-Path $Root "web") -Recurse -File -Include "*.html", "*.css", "*.js" -ErrorAction SilentlyContinue | ForEach-Object { [void]$sources.Add($_.FullName) }

foreach ($path in $sources) {
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 3) { continue }
    $rel = Get-Rel $path
    if ($b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
        Write-Output ("CRITICAL {0} has UTF-8 BOM" -f $rel)
        $critical++
        continue
    }
    try {
        $null = $utf8.GetString($b)
    } catch {
        Write-Output ("WARN {0} is not valid UTF-8" -f $rel)
        $warn++
    }
}

if ($critical -eq 0) {
    Write-Output "encoding check passed"
} else {
    Write-Output ("encoding check failed: {0} critical, {1} warnings" -f $critical, $warn)
}
