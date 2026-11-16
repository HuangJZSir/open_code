param(
    [string]$Source = "$PSScriptRoot\..\main\app_httpd.c"
)

$ErrorActionPreference = 'Stop'

$lines = [System.IO.File]::ReadAllLines((Resolve-Path $Source), [System.Text.Encoding]::UTF8)

$startLine = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'static const char s_index_html') { $startLine = $i; break }
}
if ($startLine -lt 0) { throw 's_index_html not found' }

$endLine = -1
for ($i = $startLine + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i].TrimEnd().EndsWith('";') -and $lines[$i] -match '</html>') { $endLine = $i; break }
}
if ($endLine -lt 0) { throw 'end of s_index_html not found' }

$sb = New-Object System.Text.StringBuilder
for ($i = $startLine + 1; $i -lt $endLine + 1; $i++) {
    $l = $lines[$i]
    if ($l.TrimStart().StartsWith('#')) { continue }
    $q1 = $l.IndexOf('"')
    $q2 = $l.LastIndexOf('"')
    if ($q1 -lt 0 -or $q2 -le $q1) { continue }
    $s = $l.Substring($q1 + 1, $q2 - $q1 - 1)
    $s = $s.Replace('\"', '"').Replace('\\', '\').Replace("\'", "'")
    [void]$sb.Append($s)
}

$html = $sb.ToString()
$si = $html.IndexOf('<script>')
$se = $html.LastIndexOf('</script>')
if ($si -lt 0 -or $se -le $si) { throw 'script block not found' }
$js = $html.Substring($si + 8, $se - $si - 8)

$enc = New-Object System.Text.UTF8Encoding($false)
$jsFile = Join-Path $env:TEMP 'dsh_web_check.js'
[System.IO.File]::WriteAllText($jsFile, $js, $enc)

$node = Get-Command node -ErrorAction SilentlyContinue
if ($node -eq $null) {
    throw 'node not found, cannot validate JS'
}

$errFile = Join-Path $env:TEMP 'dsh_web_check_err.txt'
if (Test-Path $errFile) { Remove-Item $errFile -Force }
cmd /c "node --check `"$jsFile`" 2> `"$errFile`""
$err = ''
if (Test-Path $errFile) { $err = [System.IO.File]::ReadAllText($errFile, [System.Text.Encoding]::UTF8) }
if ($err.Trim().Length -gt 0) {
    Write-Output 'JS SYNTAX FAIL'
    Write-Output $err
    exit 1
}

$defined = [regex]::Matches($html, 'id="([^"]+)"') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$used = [regex]::Matches($js, "getElementById\('([^']+)'\)") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$missing = @($used | Where-Object { $defined -notcontains $_ })
if ($missing.Count -gt 0) {
    Write-Output ('JS REFERENCES MISSING IDS: ' + ($missing -join ', '))
    exit 1
}

$htmlFile = Join-Path $env:TEMP 'dsh_web_check.html'
[System.IO.File]::WriteAllText($htmlFile, $html, $enc)

Write-Output ('web check: JS syntax ok, ids ok, html=' + $html.Length + ' js=' + $js.Length)
exit 0
