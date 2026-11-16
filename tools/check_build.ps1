param(
    [string]$BuildDir = "$PSScriptRoot\..\build",
    [string[]]$Files = @()
)

$ErrorActionPreference = 'Stop'

$cc = Join-Path $BuildDir 'compile_commands.json'
if (-not (Test-Path $cc)) {
    Write-Output "compile_commands.json not found: $cc"
    exit 1
}

$db = Get-Content $cc -Raw | ConvertFrom-Json
$sample = $db | Where-Object { $_.file -like '*app_player.c*' } | Select-Object -First 1
if ($sample -eq $null) {
    $sample = $db | Select-Object -First 1
}

$toks = $sample.command -split '\s+'
$compiler = $toks[0]
$flags = New-Object System.Collections.Generic.List[string]
$i = 1
while ($i -lt $toks.Count) {
    $t = $toks[$i]
    if ($t -eq '-o') { $i += 2; continue }
    if ($t -eq '-c') { $i += 1; continue }
    if ($t -like '*.c') { $i += 1; continue }
    $flags.Add($t); $i += 1
}

if ($Files.Count -eq 0) {
    $root = Split-Path $PSScriptRoot -Parent
    $Files = @(
        'main\app_codec.c', 'main\app_album.c', 'main\app_httpd.c', 'main\app_video.c',
        'main\app_audio.c', 'main\app_player.c', 'main\main.c',
        'main\ui\ui_album.c', 'main\ui\ui_video.c', 'main\ui\ui_nav.c', 'main\ui\launcher.c',
        'components\lvgl\porting\lv_port_disp.c', 'components\lvgl\porting\lv_port_indev.c'
    )
}

$outDir = Join-Path $env:TEMP 'dsh_build_check'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$failed = 0
foreach ($f in $Files) {
    $src = Join-Path (Split-Path $PSScriptRoot -Parent) $f
    if (-not (Test-Path $src)) {
        Write-Output ("MISSING  " + $f)
        $failed++
        continue
    }
    $obj = Join-Path $outDir ((Split-Path $f -Leaf) + '.o')
    if (Test-Path $obj) { Remove-Item $obj -Force }
    $log = Join-Path $outDir ((Split-Path $f -Leaf) + '.log')
    $rsp = Join-Path $outDir ((Split-Path $f -Leaf) + '.rsp')
    $lines = (@($flags) + @('-c', $src, '-o', $obj)) | ForEach-Object { $_.ToString().Replace('\', '/') }
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($rsp, ($lines -join "`n"), $enc)
    cmd /c "`"$compiler`" `"@$rsp`" 2> `"$log`""
    $text = ''
    if (Test-Path $log) { $text = [System.IO.File]::ReadAllText($log, [System.Text.Encoding]::UTF8) }
    $errors = @($text -split "`n" | Where-Object { $_ -match 'error:' })
    if ($errors.Count -gt 0) {
        Write-Output ("FAIL     " + $f)
        $errors | Select-Object -First 4 | ForEach-Object { Write-Output ('         ' + $_.Trim()) }
        $failed++
    } else {
        Write-Output ("OK       " + $f)
    }
}

if ($failed -gt 0) {
    Write-Output ("build check: $failed file(s) failed")
    exit 1
}
Write-Output 'build check: all clean'
exit 0
