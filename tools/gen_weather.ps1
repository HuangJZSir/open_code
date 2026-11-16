param(
    [string]$SrcDir = "G:\10-ESP32\espsparkbot\factory_demo_v1\weather",
    [string]$OutDir = "$PSScriptRoot\..\main\ui",
    [int]$Size = 48
)

Add-Type -AssemblyName System.Drawing

$codes = @(
    '100', '101', '102', '103', '104',
    '150', '151', '154',
    '300', '301', '302', '305', '306', '307', '308', '309', '313', '314', '315',
    '400', '404', '407',
    '501', '503',
    '399'
)

if (-not (Test-Path $SrcDir)) { throw "source dir not found: $SrcDir" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("#include ""wx_icons.h""")
[void]$sb.AppendLine("")

$entries = New-Object System.Collections.Generic.List[string]
$total = 0

foreach ($code in $codes) {
    $path = Join-Path $SrcDir "$code.png"
    if (-not (Test-Path $path)) { throw "missing icon: $path" }

    $src = [System.Drawing.Bitmap]::FromFile($path)
    $bmp = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $Size, $Size)))
    $g.Dispose()
    $src.Dispose()

    $rect = New-Object System.Drawing.Rectangle(0, 0, $Size, $Size)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $raw = New-Object byte[] ($stride * $Size)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $raw, 0, $raw.Length)
    $bmp.UnlockBits($data)

    $bytes = New-Object System.Collections.Generic.List[byte]
    for ($y = 0; $y -lt $Size; $y++) {
        $off = $y * $stride
        for ($x = 0; $x -lt $Size; $x++) {
            $b = $raw[$off + $x * 4 + 0]
            $gg = $raw[$off + $x * 4 + 1]
            $r = $raw[$off + $x * 4 + 2]
            $a = $raw[$off + $x * 4 + 3]
            if ($a -lt 8) {
                [void]$bytes.Add(0)
                [void]$bytes.Add(0)
                [void]$bytes.Add(0)
                continue
            }
            $v = ((([int]($r -band 0xF8)) -shl 8) -bor (([int]($gg -band 0xFC)) -shl 3) -bor ([int]($b -shr 3)))
            [void]$bytes.Add([byte]($v -band 0xFF))
            [void]$bytes.Add([byte](($v -shr 8) -band 0xFF))
            [void]$bytes.Add([byte]$a)
        }
    }
    $bmp.Dispose()

    [void]$sb.AppendLine("static const uint8_t wx_map_$code[] = {")
    $line = "    "
    $count = 0
    foreach ($by in $bytes) {
        $line += ("0x{0:x2}," -f $by)
        $count++
        if ($count % 24 -eq 0) { [void]$sb.AppendLine($line); $line = "    " }
    }
    if ($line.Trim().Length -gt 0) { [void]$sb.AppendLine($line) }
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("static const lv_img_dsc_t wx_icon_$code = {")
    [void]$sb.AppendLine("    .header.always_zero = 0,")
    [void]$sb.AppendLine("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
    [void]$sb.AppendLine("    .header.w = $Size,")
    [void]$sb.AppendLine("    .header.h = $Size,")
    [void]$sb.AppendLine("    .data_size = $($bytes.Count),")
    [void]$sb.AppendLine("    .data = wx_map_$code,")
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")
    $entries.Add("{""$code"", &wx_icon_$code}")
    $total += $bytes.Count
}

$tableName = "wx_icon_table"
[void]$sb.AppendLine("typedef struct {")
[void]$sb.AppendLine("    const char *code;")
[void]$sb.AppendLine("    const lv_img_dsc_t *icon;")
[void]$sb.AppendLine("} wx_icon_entry_t;")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("static const wx_icon_entry_t $tableName[] = {")
foreach ($e in $entries) { [void]$sb.AppendLine("    $e,") }
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("const lv_img_dsc_t *wx_icon_for(const char *code)")
[void]$sb.AppendLine("{")
[void]$sb.AppendLine("    size_t i;")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("    if (code != NULL) {")
[void]$sb.AppendLine("        for (i = 0; i < sizeof($tableName) / sizeof($tableName[0]); i++) {")
[void]$sb.AppendLine("            if (strcmp(code, $tableName[i].code) == 0) {")
[void]$sb.AppendLine("                return $tableName[i].icon;")
[void]$sb.AppendLine("            }")
[void]$sb.AppendLine("        }")
[void]$sb.AppendLine("    }")
[void]$sb.AppendLine("    return &wx_icon_399;")
[void]$sb.AppendLine("}")

$cPath = Join-Path $OutDir "wx_icons.c"
Set-Content -Path $cPath -Value $sb.ToString() -Encoding ascii

$hPath = Join-Path $OutDir "wx_icons.h"
$h = @(
    "#pragma once",
    "",
    "#include <stddef.h>",
    "",
    "#include ""lvgl.h""",
    "",
    "const lv_img_dsc_t *wx_icon_for(const char *code);"
)
Set-Content -Path $hPath -Value ($h -join "`r`n") -Encoding ascii

Write-Output ("wx icons: {0} icons at {1}px, {2} bytes total" -f $codes.Count, $Size, $total)
