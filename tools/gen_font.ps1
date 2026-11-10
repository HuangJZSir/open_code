param(
    [string]$OutDir = "$PSScriptRoot\..\main\fonts",
    [string]$Family = "Microsoft YaHei",
    [bool]$Cjk = $true
)

Add-Type -AssemblyName System.Drawing

function Get-Codes {
    $set = New-Object System.Collections.Generic.SortedSet[int]
    for ($c = 32; $c -lt 127; $c++) { [void]$set.Add($c) }
    $root = Join-Path $PSScriptRoot "..\main"
    if (Test-Path $root) {
        Get-ChildItem -Recurse -File $root -Include *.c,*.h | Where-Object { $_.FullName -notmatch "\\fonts\\" } | ForEach-Object {
            $text = [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8)
            foreach ($ch in $text.ToCharArray()) {
                $code = [int][char]$ch
                if ($code -ge 0x7F -and $code -le 0xFFFF) { [void]$set.Add($code) }
            }
        }
    }
    return $set
}

function Get-GbkLevel1 {
    $set = New-Object System.Collections.Generic.SortedSet[int]
    $enc = [System.Text.Encoding]::GetEncoding(936)
    for ($lead = 0xB0; $lead -le 0xD7; $lead++) {
        for ($trail = 0xA1; $trail -le 0xFE; $trail++) {
            $bytes = [byte[]]@([byte]$lead, [byte]$trail)
            $text = $enc.GetString($bytes)
            if ($text.Length -eq 1) {
                $code = [int][char]$text[0]
                if ($code -gt 0x7F) { [void]$set.Add($code) }
            }
        }
    }
    return $set
}

function Render-Font {
    param([int]$Size, [System.Collections.Generic.SortedSet[int]]$Codes, [string]$Name, [string]$Path)

    $style = [System.Drawing.FontStyle]::Regular
    $family = New-Object System.Drawing.FontFamily($Family)
    $font = New-Object System.Drawing.Font($family, [float]$Size, $style, [System.Drawing.GraphicsUnit]::Pixel)

    $canvasW = 96
    $canvasH = 96
    $bmp = New-Object System.Drawing.Bitmap($canvasW, $canvasH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.Clear([System.Drawing.Color]::Black)
    $gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $sf = [System.Drawing.StringFormat]::GenericTypographic
    $sf.FormatFlags = $sf.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

    $em = [double]$family.GetEmHeight($style)
    $asc = [double]$family.GetCellAscent($style)
    $desc = [double]$family.GetCellDescent($style)
    $ascentPx = $asc * $Size / $em
    $lineHeight = [int][math]::Round(($asc + $desc) * $Size / $em)
    $baseLine = $lineHeight - [int][math]::Round($ascentPx)

    $originX = 32
    $originY = 32

    $bitmaps = New-Object System.Collections.Generic.List[byte]
    $glyphs = New-Object System.Collections.Generic.List[string]
    $codesOut = New-Object System.Collections.Generic.List[int]

    $glyphs.Add("{.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}")

    foreach ($code in $Codes) {
        $ch = [string][char]$code
        $gfx.Clear([System.Drawing.Color]::Black)
        $gfx.DrawString($ch, $font, $brush, [float]$originX, [float]$originY, $sf)

        $minX = 999999; $minY = 999999; $maxX = -1; $maxY = -1
        $data = $bmp.LockBits((New-Object System.Drawing.Rectangle(0, 0, $canvasW, $canvasH)),
                              [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = $data.Stride
        $bytes = New-Object byte[] ($stride * $canvasH)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        $bmp.UnlockBits($data)

        for ($y = 0; $y -lt $canvasH; $y++) {
            $rowOff = $y * $stride
            for ($x = 0; $x -lt $canvasW; $x++) {
                $v = $bytes[$rowOff + $x * 4 + 2]
                if ($v -gt 8) {
                    if ($x -lt $minX) { $minX = $x }
                    if ($x -gt $maxX) { $maxX = $x }
                    if ($y -lt $minY) { $minY = $y }
                    if ($y -gt $maxY) { $maxY = $y }
                }
            }
        }

        $adv = 0.0
        if ($code -ge 0x7F) {
            $adv = [math]::Round([double]$Size)
        } else {
            $m = $gfx.MeasureString($ch, $font, (New-Object System.Drawing.PointF(0, 0)), $sf)
            $adv = [math]::Round([double]$m.Width)
            if ($adv -lt 1) { $adv = [math]::Round([double]$Size * 0.5) }
        }
        $advW = [int]$adv * 16

        if ($maxX -lt 0) {
            $glyphs.Add("{.bitmap_index = $($bitmaps.Count), .adv_w = $advW, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}")
            [void]$codesOut.Add($code)
            continue
        }

        $boxW = $maxX - $minX + 1
        $boxH = $maxY - $minY + 1
        $ofsX = $minX - $originX
        $boxBottom = ($minY - $originY) + $boxH
        $ofsY = [int][math]::Round($ascentPx) - $boxBottom

        $start = $bitmaps.Count
        $glyphBytes = New-Object System.Collections.Generic.List[byte]
        $pending = -1
        for ($y = 0; $y -lt $boxH; $y++) {
            for ($x = 0; $x -lt $boxW; $x++) {
                $v = $bytes[($minY + $y) * $stride + ($minX + $x) * 4 + 2]
                $nib = [int]([math]::Floor($v / 17.0))
                if ($nib -gt 15) { $nib = 15 }
                if ($pending -lt 0) {
                    $pending = $nib
                } else {
                    [void]$glyphBytes.Add([byte](($pending -shl 4) -bor $nib))
                    $pending = -1
                }
            }
        }
        if ($pending -ge 0) { [void]$glyphBytes.Add([byte]($pending -shl 4)) }
        foreach ($b in $glyphBytes) { [void]$bitmaps.Add($b) }

        $glyphs.Add("{.bitmap_index = $start, .adv_w = $advW, .box_w = $boxW, .box_h = $boxH, .ofs_x = $ofsX, .ofs_y = $ofsY}")
        [void]$codesOut.Add($code)
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("#include ""lvgl.h""")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("static const uint8_t glyph_bitmap[] = {")
    $line = "    "
    $count = 0
    foreach ($b in $bitmaps) {
        $line += ("0x{0:x2}," -f $b)
        $count++
        if ($count % 20 -eq 0) { [void]$sb.AppendLine($line); $line = "    " }
    }
    if ($line.Trim().Length -gt 0) { [void]$sb.AppendLine($line) }
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    foreach ($gd in $glyphs) { [void]$sb.AppendLine("    $gd,") }
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")

    $cjk = @()
    foreach ($c in $codesOut) { if ($c -gt 127) { $cjk += $c } }
    $cjkMin = ($cjk | Measure-Object -Minimum).Minimum
    $cjkMax = ($cjk | Measure-Object -Maximum).Maximum
    if (($cjkMax - $cjkMin) -gt 65535) { throw "sparse cmap range exceeds uint16" }

    [void]$sb.AppendLine("static const uint16_t unicode_list_1[] = {")
    $line = "    "
    $count = 0
    foreach ($c in $cjk) {
        $line += ("0x" + ([int]($c - $cjkMin)).ToString("x") + ",")
        $count++
        if ($count % 12 -eq 0) { [void]$sb.AppendLine($line); $line = "    " }
    }
    if ($line.Trim().Length -gt 0) { [void]$sb.AppendLine($line) }
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    $asciiCount = 0
    foreach ($c0 in $codesOut) { if ($c0 -lt 127) { $asciiCount++ } }
    $cjkStart = 1 + $asciiCount
    [void]$sb.AppendLine("    {.range_start = 32, .range_length = $($asciiCount - 1), .glyph_id_start = 1, .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},")
    [void]$sb.AppendLine("    {.range_start = $cjkMin, .range_length = $($cjkMax - $cjkMin), .glyph_id_start = $cjkStart, .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = $($cjk.Count), .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}")
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    [void]$sb.AppendLine("    .glyph_bitmap = glyph_bitmap,")
    [void]$sb.AppendLine("    .glyph_dsc = glyph_dsc,")
    [void]$sb.AppendLine("    .cmaps = cmaps,")
    [void]$sb.AppendLine("    .kern_dsc = NULL,")
    [void]$sb.AppendLine("    .kern_scale = 0,")
    [void]$sb.AppendLine("    .cmap_num = 2,")
    [void]$sb.AppendLine("    .bpp = 4,")
    [void]$sb.AppendLine("    .kern_classes = 0,")
    [void]$sb.AppendLine("    .bitmap_format = 0,")
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("const lv_font_t $Name = {")
    [void]$sb.AppendLine("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    [void]$sb.AppendLine("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    [void]$sb.AppendLine("    .line_height = $lineHeight,")
    [void]$sb.AppendLine("    .base_line = $baseLine,")
    [void]$sb.AppendLine("    .subpx = LV_FONT_SUBPX_NONE,")
    [void]$sb.AppendLine("    .underline_position = -2,")
    [void]$sb.AppendLine("    .underline_thickness = 1,")
    [void]$sb.AppendLine("    .dsc = &font_dsc,")
    [void]$sb.AppendLine("};")

    $expect = 0
    foreach ($gd in $glyphs) {
        if ($gd -match '\.box_w = (\d+), \.box_h = (\d+)') {
            $expect += [int][math]::Floor(([int]$Matches[1] * [int]$Matches[2] + 1) / 2)
        }
    }
    if ($expect -ne $bitmaps.Count) { Write-Output ("BITMAP SIZE CHECK FAILED expect=" + $expect + " got=" + $bitmaps.Count) }
    Set-Content -Path $Path -Value $sb.ToString() -Encoding ascii
    $gfx.Dispose()
    $bmp.Dispose()
    $font.Dispose()

    Write-Output ("{0}: size={1} glyphs={2} bitmap={3} bytes lineHeight={4} baseLine={5} cjk={6}" -f $Name, $Size, $codesOut.Count, $bitmaps.Count, $lineHeight, $baseLine, $cjk.Count)
}

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$codes = Get-Codes
Write-Output ("ui codes: " + $codes.Count)

Render-Font -Size 16 -Codes $codes -Name "ui_font_16" -Path (Join-Path $OutDir "ui_font_16.c")
Render-Font -Size 24 -Codes $codes -Name "ui_font_24" -Path (Join-Path $OutDir "ui_font_24.c")
Render-Font -Size 32 -Codes $codes -Name "ui_font_32" -Path (Join-Path $OutDir "ui_font_32.c")

if ($Cjk) {
    $gbk = Get-GbkLevel1
    $codesCjk = New-Object System.Collections.Generic.SortedSet[int]
    foreach ($c in $codes) { [void]$codesCjk.Add($c) }
    foreach ($c in $gbk) { [void]$codesCjk.Add($c) }
    Write-Output ("cjk codes: " + $codesCjk.Count)
    Render-Font -Size 24 -Codes $codesCjk -Name "ui_font_cjk24" -Path (Join-Path $OutDir "ui_font_cjk24.c")
}
