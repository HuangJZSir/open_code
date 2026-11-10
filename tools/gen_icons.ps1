# gen_icons.ps1 - procedural 48x48 UI icon generator (LVGL 9.0, lv_img_* API)
# Renders each icon into a 4x oversized boolean coverage mask (192x192),
# box-averages 4x4 blocks down to 48x48 alpha, and emits icons.c / icons.h
# plus tools/icons_preview.png for eyeballing.
#
# Pixel layout of LV_IMG_CF_TRUE_COLOR_ALPHA at LV_COLOR_DEPTH 16 is
# INTERLEAVED: 3 bytes per pixel (RGB565 low byte, RGB565 high byte, alpha).
# LVGL reads it as px = (w*y + x) * LV_IMG_PX_SIZE_ALPHA_BYTE with
# LV_IMG_PX_SIZE_ALPHA_BYTE == 3, and the alpha of a pixel is byte 2 of it.
# Do NOT emit rgb-plane-then-alpha-plane, that renders as noise.
#
# Run from the project root:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen_icons.ps1

param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\main\ui'),
    [string]$PreviewFile = (Join-Path $PSScriptRoot 'icons_preview.png')
)

$ErrorActionPreference = 'Stop'

$Size = 48          # final icon edge in px
$SS = 4             # supersample factor
$Hi = $Size * $SS   # high-res mask edge (192)
$SampleCount = $SS * $SS
$IconRgb = 0xF4F7FF # pure white foreground (icon is tinted by the tile bg)

# ---------------------------------------------------------------- pixel buffer

$Mask = New-Object 'byte[]' ($Hi * $Hi)

function Clear-Mask {
    [System.Array]::Clear($script:Mask, 0, $script:Mask.Length)
}

function Get-MaskCount {
    $n = 0
    $m = $script:Mask
    for ($i = 0; $i -lt $m.Length; $i++) { if ($m[$i] -ne 0) { $n++ } }
    return $n
}

# ---------------------------------------------------------------- rasterisers
# All coordinates are given in 48x48 icon space (float px, y grows downwards).
# Each routine loops only its own bounding box in the 192x192 mask and ORs
# (Val = 1) or clears (Val = 0) coverage for the hi-res pixel centres.

function Fill-RoundRect {
    param([double]$X0, [double]$Y0, [double]$X1, [double]$Y1, [double]$R, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $ix0 = [int][math]::Floor($X0 * $S); if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor($Y0 * $S); if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling($X1 * $S); if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling($Y1 * $S); if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    if ($R -lt 0) { $R = 0 }
    $kx0 = $X0 + $R; $kx1 = $X1 - $R
    $ky0 = $Y0 + $R; $ky1 = $Y1 - $R
    if ($kx0 -gt $kx1) { $kx0 = ($X0 + $X1) / 2.0; $kx1 = $kx0 }
    if ($ky0 -gt $ky1) { $ky0 = ($Y0 + $Y1) / 2.0; $ky1 = $ky0 }
    $r2 = $R * $R
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $cy = $py
        if ($cy -lt $ky0) { $cy = $ky0 } elseif ($cy -gt $ky1) { $cy = $ky1 }
        $dy = $py - $cy
        $dy2 = $dy * $dy
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $cx = $px
            if ($cx -lt $kx0) { $cx = $kx0 } elseif ($cx -gt $kx1) { $cx = $kx1 }
            $dx = $px - $cx
            if (($dx * $dx + $dy2) -le $r2) { $Mask[$base + $ix] = $v }
        }
    }
}

function Fill-Circle {
    param([double]$CX, [double]$CY, [double]$R, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $ix0 = [int][math]::Floor(($CX - $R) * $S) - 1; if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor(($CY - $R) * $S) - 1; if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling(($CX + $R) * $S) + 1; if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling(($CY + $R) * $S) + 1; if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    $r2 = $R * $R
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $dy = $py - $CY
        $dy2 = $dy * $dy
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $dx = $px - $CX
            if (($dx * $dx + $dy2) -le $r2) { $Mask[$base + $ix] = $v }
        }
    }
}

function Fill-Ellipse {
    param([double]$CX, [double]$CY, [double]$RX, [double]$RY, [double]$AngleDeg, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $rad = $AngleDeg * [math]::PI / 180.0
    $ca = [math]::Cos($rad)
    $sa = [math]::Sin($rad)
    $ex = [math]::Sqrt(($RX * $ca) * ($RX * $ca) + ($RY * $sa) * ($RY * $sa))
    $ey = [math]::Sqrt(($RX * $sa) * ($RX * $sa) + ($RY * $ca) * ($RY * $ca))
    $ix0 = [int][math]::Floor(($CX - $ex) * $S) - 1; if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor(($CY - $ey) * $S) - 1; if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling(($CX + $ex) * $S) + 1; if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling(($CY + $ey) * $S) + 1; if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    $rx2 = $RX * $RX
    $ry2 = $RY * $RY
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $dy = $py - $CY
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $dx = $px - $CX
            $u = $dx * $ca + $dy * $sa
            $w = -$dx * $sa + $dy * $ca
            if ((($u * $u) / $rx2) + (($w * $w) / $ry2) -le 1.0) { $Mask[$base + $ix] = $v }
        }
    }
}

function Fill-Convex {
    param([double[]]$Pts, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $n = [int]($Pts.Length / 2)
    if ($n -lt 3) { throw 'Fill-Convex needs at least 3 points' }
    $area = 0.0
    $minX = $Pts[0]; $maxX = $Pts[0]
    $minY = $Pts[1]; $maxY = $Pts[1]
    for ($i = 0; $i -lt $n; $i++) {
        $j = ($i + 1) % $n
        $xi = $Pts[2 * $i]; $yi = $Pts[2 * $i + 1]
        $xj = $Pts[2 * $j]; $yj = $Pts[2 * $j + 1]
        $area += ($xi * $yj) - ($xj * $yi)
        if ($xi -lt $minX) { $minX = $xi }
        if ($xi -gt $maxX) { $maxX = $xi }
        if ($yi -lt $minY) { $minY = $yi }
        if ($yi -gt $maxY) { $maxY = $yi }
    }
    $sgn = 1.0
    if ($area -lt 0) { $sgn = -1.0 }
    $ix0 = [int][math]::Floor($minX * $S) - 1; if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor($minY * $S) - 1; if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling($maxX * $S) + 1; if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling($maxY * $S) + 1; if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $inside = $true
            for ($k = 0; $k -lt $n; $k++) {
                $k2 = ($k + 1) % $n
                $ex = $Pts[2 * $k2] - $Pts[2 * $k]
                $ey = $Pts[2 * $k2 + 1] - $Pts[2 * $k + 1]
                $qx = $px - $Pts[2 * $k]
                $qy = $py - $Pts[2 * $k + 1]
                if (((($ex * $qy) - ($ey * $qx)) * $sgn) -lt -1e-9) { $inside = $false; break }
            }
            if ($inside) { $Mask[$base + $ix] = $v }
        }
    }
}

function Fill-Line {
    param([double]$X0, [double]$Y0, [double]$X1, [double]$Y1, [double]$W, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $h = $W / 2.0
    $minX = [math]::Min($X0, $X1) - $h
    $maxX = [math]::Max($X0, $X1) + $h
    $minY = [math]::Min($Y0, $Y1) - $h
    $maxY = [math]::Max($Y0, $Y1) + $h
    $ix0 = [int][math]::Floor($minX * $S) - 1; if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor($minY * $S) - 1; if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling($maxX * $S) + 1; if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling($maxY * $S) + 1; if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    $vx = $X1 - $X0
    $vy = $Y1 - $Y0
    $len2 = ($vx * $vx) + ($vy * $vy)
    if ($len2 -le 0.0) { $len2 = 1e-9 }
    $h2 = $h * $h
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $t = ((($px - $X0) * $vx) + (($py - $Y0) * $vy)) / $len2
            if ($t -lt 0.0) { $t = 0.0 } elseif ($t -gt 1.0) { $t = 1.0 }
            $qx = $X0 + $t * $vx
            $qy = $Y0 + $t * $vy
            $dx = $px - $qx
            $dy = $py - $qy
            if (($dx * $dx + $dy * $dy) -le $h2) { $Mask[$base + $ix] = $v }
        }
    }
}

function Fill-Arc {
    param([double]$CX, [double]$CY, [double]$R, [double]$T, [double]$A0, [double]$A1, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $ro = $R + ($T / 2.0)
    $ri = $R - ($T / 2.0)
    $ix0 = [int][math]::Floor(($CX - $ro - 1) * $S); if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor(($CY - $ro - 1) * $S); if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling(($CX + $ro + 1) * $S); if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling(($CY + $ro + 1) * $S); if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    $ro2 = $ro * $ro
    $ri2 = $ri * $ri
    if ($ri2 -lt 0) { $ri2 = 0 }
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $dy = $py - $CY
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $dx = $px - $CX
            $d2 = ($dx * $dx) + ($dy * $dy)
            if (($d2 -gt $ro2) -or ($d2 -lt $ri2)) { continue }
            $ang = [math]::Atan2($dy, $dx) * 180.0 / [math]::PI
            if (($ang -ge $A0) -and ($ang -le $A1)) { $Mask[$base + $ix] = $v }
        }
    }
}

function Fill-Tooth {
    param([double]$CX, [double]$CY, [double]$AngleDeg, [double]$U0, [double]$U1, [double]$HW, [int]$Val)
    $S = $script:SS
    $Hi = $script:Hi
    $Mask = $script:Mask
    $rad = $AngleDeg * [math]::PI / 180.0
    $ca = [math]::Cos($rad)
    $sa = [math]::Sin($rad)
    $ext = $U1 + $HW + 1.0
    $ix0 = [int][math]::Floor(($CX - $ext) * $S); if ($ix0 -lt 0) { $ix0 = 0 }
    $iy0 = [int][math]::Floor(($CY - $ext) * $S); if ($iy0 -lt 0) { $iy0 = 0 }
    $ix1 = [int][math]::Ceiling(($CX + $ext) * $S); if ($ix1 -gt $Hi - 1) { $ix1 = $Hi - 1 }
    $iy1 = [int][math]::Ceiling(($CY + $ext) * $S); if ($iy1 -gt $Hi - 1) { $iy1 = $Hi - 1 }
    $v = [byte]$Val
    for ($iy = $iy0; $iy -le $iy1; $iy++) {
        $py = ($iy + 0.5) / $S
        $dy = $py - $CY
        $base = $iy * $Hi
        for ($ix = $ix0; $ix -le $ix1; $ix++) {
            $px = ($ix + 0.5) / $S
            $dx = $px - $CX
            $u = $dx * $ca + $dy * $sa
            if (($u -lt $U0) -or ($u -gt $U1)) { continue }
            $w = -$dx * $sa + $dy * $ca
            if ([math]::Abs($w) -le $HW) { $Mask[$base + $ix] = $v }
        }
    }
}

function Stroke-RoundRect {
    param([double]$X0, [double]$Y0, [double]$X1, [double]$Y1, [double]$R, [double]$T, [int]$Val)
    Fill-RoundRect -X0 $X0 -Y0 $Y0 -X1 $X1 -Y1 $Y1 -R $R -Val $Val
    $ir = $R - $T
    if ($ir -lt 0) { $ir = 0 }
    Fill-RoundRect -X0 ($X0 + $T) -Y0 ($Y0 + $T) -X1 ($X1 - $T) -Y1 ($Y1 - $T) -R $ir -Val 0
}

# ------------------------------------------------------------------ the icons

function Draw-Music {
    # beamed pair of eighth notes
    Fill-Ellipse -CX 15.5 -CY 35.0 -RX 6.2 -RY 4.4 -AngleDeg -22 -Val 1
    Fill-Ellipse -CX 32.5 -CY 32.0 -RX 6.2 -RY 4.4 -AngleDeg -22 -Val 1
    Fill-RoundRect -X0 18.8 -Y0 9.5 -X1 21.5 -Y1 35.0 -R 0.6 -Val 1
    Fill-RoundRect -X0 35.8 -Y0 6.5 -X1 38.5 -Y1 32.0 -R 0.6 -Val 1
    Fill-Convex -Pts @(18.8, 9.5, 38.5, 6.5, 38.5, 12.6, 18.8, 15.6) -Val 1
}

function Draw-Video {
    Stroke-RoundRect -X0 5 -Y0 9 -X1 43 -Y1 39 -R 5 -T 3 -Val 1
    Fill-Convex -Pts @(20.5, 17, 20.5, 31, 32, 24) -Val 1
}

function Draw-Photo {
    Stroke-RoundRect -X0 5 -Y0 7 -X1 43 -Y1 41 -R 5 -T 3 -Val 1
    Fill-Circle -CX 15 -CY 16 -R 4.2 -Val 1
    Fill-Convex -Pts @(9.5, 36, 21, 21.5, 32.5, 36) -Val 1
}

function Draw-Folder {
    Fill-RoundRect -X0 6 -Y0 8.5 -X1 23 -Y1 22 -R 3 -Val 1
    Fill-RoundRect -X0 5 -Y0 14 -X1 43 -Y1 40 -R 4 -Val 1
}

function Draw-Clock {
    Fill-Circle -CX 24 -CY 24 -R 20 -Val 1
    Fill-Circle -CX 24 -CY 24 -R 16.6 -Val 0
    Fill-Line -X0 24 -Y0 24 -X1 24 -Y1 13.5 -W 3.2 -Val 1
    Fill-Line -X0 24 -Y0 24 -X1 31.8 -Y1 28.5 -W 3.4 -Val 1
}

function Draw-Calc {
    Stroke-RoundRect -X0 7 -Y0 4 -X1 41 -Y1 44 -R 5 -T 3 -Val 1
    Fill-RoundRect -X0 12 -Y0 9.5 -X1 36 -Y1 17 -R 1.5 -Val 1
    foreach ($col in @(15.5, 24.0, 32.5)) {
        foreach ($row in @(25.5, 34.0)) {
            Fill-Circle -CX $col -CY $row -R 2.7 -Val 1
        }
    }
}

function Draw-Settings {
    Fill-Circle -CX 24 -CY 24 -R 13.5 -Val 1
    for ($k = 0; $k -lt 8; $k++) {
        Fill-Tooth -CX 24 -CY 24 -AngleDeg ($k * 45.0) -U0 11 -U1 18.6 -HW 3.1 -Val 1
    }
    Fill-Circle -CX 24 -CY 24 -R 5.2 -Val 0
}

function Draw-Info {
    Fill-Circle -CX 24 -CY 24 -R 19.5 -Val 1
    Fill-Circle -CX 24 -CY 15 -R 2.7 -Val 0
    Fill-RoundRect -X0 21.9 -Y0 19.5 -X1 26.1 -Y1 32.5 -R 2.1 -Val 0
}

function Draw-Play {
    Fill-Convex -Pts @(15.5, 10.5, 15.5, 37.5, 36.5, 24) -Val 1
}

function Draw-Pause {
    Fill-RoundRect -X0 14.5 -Y0 10 -X1 21 -Y1 38 -R 2.6 -Val 1
    Fill-RoundRect -X0 27 -Y0 10 -X1 33.5 -Y1 38 -R 2.6 -Val 1
}

function Draw-Prev {
    Fill-RoundRect -X0 10.5 -Y0 11 -X1 14.5 -Y1 37 -R 1.6 -Val 1
    Fill-Convex -Pts @(34, 11, 34, 37, 17.5, 24) -Val 1
}

function Draw-Next {
    Fill-RoundRect -X0 33.5 -Y0 11 -X1 37.5 -Y1 37 -R 1.6 -Val 1
    Fill-Convex -Pts @(14, 11, 14, 37, 30.5, 24) -Val 1
}

function Draw-Sd {
    # card body outline: the inner contour is the outer one offset inwards by 3.2 px
    # on every edge (including the 45-ish degree chamfer), so the wall has even weight
    Fill-Convex -Pts @(8, 4, 30, 4, 41, 14, 41, 44, 8, 44) -Val 1
    Fill-Convex -Pts @(11.2, 7.2, 28.76, 7.2, 37.8, 15.42, 37.8, 40.8, 11.2, 40.8) -Val 0
    Fill-RoundRect -X0 14.6 -Y0 28.5 -X1 19.0 -Y1 36.5 -R 1.0 -Val 1
    Fill-RoundRect -X0 22.3 -Y0 28.5 -X1 26.7 -Y1 36.5 -R 1.0 -Val 1
    Fill-RoundRect -X0 30.0 -Y0 28.5 -X1 34.4 -Y1 36.5 -R 1.0 -Val 1
}

function Draw-Speaker {
    Fill-RoundRect -X0 6 -Y0 18.5 -X1 14.5 -Y1 29.5 -R 1.2 -Val 1
    Fill-Convex -Pts @(14.5, 18.5, 14.5, 29.5, 24, 34.5, 24, 13.5) -Val 1
    Fill-Arc -CX 24 -CY 24 -R 5.0 -T 2.4 -A0 -58 -A1 58 -Val 1
    Fill-Arc -CX 24 -CY 24 -R 9.0 -T 2.4 -A0 -58 -A1 58 -Val 1
}

$IconList = @(
    @{ Name = 'ic_music'; Draw = { Draw-Music } },
    @{ Name = 'ic_video'; Draw = { Draw-Video } },
    @{ Name = 'ic_photo'; Draw = { Draw-Photo } },
    @{ Name = 'ic_folder'; Draw = { Draw-Folder } },
    @{ Name = 'ic_clock'; Draw = { Draw-Clock } },
    @{ Name = 'ic_calc'; Draw = { Draw-Calc } },
    @{ Name = 'ic_settings'; Draw = { Draw-Settings } },
    @{ Name = 'ic_info'; Draw = { Draw-Info } },
    @{ Name = 'ic_play'; Draw = { Draw-Play } },
    @{ Name = 'ic_pause'; Draw = { Draw-Pause } },
    @{ Name = 'ic_prev'; Draw = { Draw-Prev } },
    @{ Name = 'ic_next'; Draw = { Draw-Next } },
    @{ Name = 'ic_sd'; Draw = { Draw-Sd } },
    @{ Name = 'ic_speaker'; Draw = { Draw-Speaker } }
)

# -------------------------------------------------------------- colour / hexes

function Convert-To565 {
    param([int]$Rgb)
    $r = ($Rgb -shr 16) -band 0xFF
    $g = ($Rgb -shr 8) -band 0xFF
    $b = $Rgb -band 0xFF
    return ((($r -band 0xF8) -shl 8) -bor (($g -band 0xFC) -shl 3) -bor ($b -shr 3))
}

$Ico565 = Convert-To565 $IconRgb
$RgbLo = [byte]($Ico565 -band 0xFF)
$RgbHi = [byte](($Ico565 -shr 8) -band 0xFF)

$HexTab = New-Object 'string[]' 256
for ($i = 0; $i -lt 256; $i++) { $HexTab[$i] = ('0x{0:x2},' -f $i) }

# -------------------------------------------------------------------- emitters

$HdrText = New-Object System.Text.StringBuilder
$SrcText = New-Object System.Text.StringBuilder
[void]$SrcText.Append('#include "icons.h"' + "`n" + "`n")

$AlphaSets = New-Object System.Collections.Generic.List[object]
$Report = New-Object System.Collections.Generic.List[string]
$TotalBytes = 0

function Emit-Icon {
    param([string]$Name, [byte[]]$Alpha, [int]$MaskCount)
    $w = $script:Size
    $h = $script:Size
    $px = $w * $h
    $byteCount = $px * 3

    $rgbPlane = $px * 2
    if (($rgbPlane + $px) -ne $byteCount) { throw ("{0}: layout size mismatch" -f $Name) }

    $data = New-Object 'byte[]' $byteCount
    $p = 0
    for ($i = 0; $i -lt $px; $i++) {
        $data[$p] = $script:RgbLo; $p++
        $data[$p] = $script:RgbHi; $p++
        $data[$p] = $Alpha[$i]; $p++
    }

    if ($p -ne $byteCount) { throw ("{0}: wrote {1} bytes, expected {2}" -f $Name, $p, $byteCount) }

    [void]$script:HdrText.Append(('extern const lv_img_dsc_t {0};' -f $Name) + "`n")

    [void]$script:SrcText.Append(('static const uint8_t {0}_data[{1}] = {{' -f $Name, $byteCount) + "`n")
    $line = '    '
    $n = 0
    $tab = $script:HexTab
    for ($i = 0; $i -lt $byteCount; $i++) {
        $line += $tab[[int]$data[$i]]
        $n++
        if (($n % 24) -eq 0) { [void]$script:SrcText.Append($line + "`n"); $line = '    ' }
    }
    if ($line.Trim().Length -gt 0) { [void]$script:SrcText.Append($line + "`n") }
    [void]$script:SrcText.Append('};' + "`n" + "`n")
    [void]$script:SrcText.Append(('const lv_img_dsc_t {0} = {{' -f $Name) + "`n")
    [void]$script:SrcText.Append(('    .header = {{.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .w = {0}, .h = {1}}},' -f $w, $h) + "`n")
    [void]$script:SrcText.Append(('    .data_size = {0},' -f $byteCount) + "`n")
    [void]$script:SrcText.Append(('    .data = {0}_data,' -f $Name) + "`n")
    [void]$script:SrcText.Append('};' + "`n" + "`n")

    $nz = 0
    $op = 0
    $minX = $w; $maxX = -1; $minY = $h; $maxY = -1
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $a = $Alpha[$y * $w + $x]
            if ($a -gt 0) {
                $nz++
                if ($a -eq 255) { $op++ }
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($nz -eq 0) { throw ("{0}: empty icon mask (nothing drawn)" -f $Name) }
    if ($MaskCount -lt 64) { throw ("{0}: hi-res mask covers only {1} subpixels" -f $Name, $MaskCount) }
    if ($byteCount -ne 6912) { throw ("{0}: byte count {1} != 6912" -f $Name, $byteCount) }
    if ($maxX -lt 0) { throw ("{0}: alpha bounding box is empty" -f $Name) }

    $script:Report.Add(('{0,-12} {1}x{2} bytes={3} nonzero_alpha={4} opaque={5} bbox={6},{7}-{8},{9}' -f `
                $Name, $w, $h, $byteCount, $nz, $op, $minX, $minY, $maxX, $maxY))
    $script:AlphaSets.Add($Alpha)
    $script:TotalBytes += $byteCount
}

# ------------------------------------------------------------------ main build

foreach ($ic in $IconList) {
    Clear-Mask
    & $ic.Draw
    $maskCount = Get-MaskCount

    $alpha = New-Object 'byte[]' ($Size * $Size)
    for ($y = 0; $y -lt $Size; $y++) {
        for ($x = 0; $x -lt $Size; $x++) {
            $sum = 0
            $rowBase = ($y * $SS) * $Hi + ($x * $SS)
            for ($sy = 0; $sy -lt $SS; $sy++) {
                $b = $rowBase + ($sy * $Hi)
                for ($sx = 0; $sx -lt $SS; $sx++) {
                    $sum += $Mask[$b + $sx]
                }
            }
            $alpha[$y * $Size + $x] = [byte][int][math]::Round(($sum * 255.0) / $SampleCount)
        }
    }

    Emit-Icon -Name $ic.Name -Alpha $alpha -MaskCount $maskCount
}

$hdrText = "#pragma once`n`n#include `"lvgl.h`"`n`n" + $HdrText.ToString()
$srcText = $SrcText.ToString()

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$utf8 = New-Object System.Text.UTF8Encoding($false)
$hdrPath = Join-Path $OutDir 'icons.h'
$srcPath = Join-Path $OutDir 'icons.c'
[System.IO.File]::WriteAllText($hdrPath, $hdrText, $utf8)
[System.IO.File]::WriteAllText($srcPath, $srcText, $utf8)

# -------------------------------------------------------------------- preview

$prevW = $Size * $AlphaSets.Count
$prevH = $Size
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $prevW, $prevH
$rect = New-Object System.Drawing.Rectangle 0, 0, $prevW, $prevH
$locked = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $locked.Stride
$buf = New-Object 'byte[]' ($stride * $prevH)
$bgR = 0x17; $bgG = 0x1A; $bgB = 0x21
$fgR = ($IconRgb -shr 16) -band 0xFF
$fgG = ($IconRgb -shr 8) -band 0xFF
$fgB = $IconRgb -band 0xFF
for ($i = 0; $i -lt $AlphaSets.Count; $i++) {
    $a = $AlphaSets[$i]
    for ($y = 0; $y -lt $Size; $y++) {
        $o = $y * $stride + $i * $Size * 4
        for ($x = 0; $x -lt $Size; $x++) {
            $av = $a[$y * $Size + $x]
            $t = $av / 255.0
            $r = [int][math]::Round($bgR + ($fgR - $bgR) * $t)
            $g = [int][math]::Round($bgG + ($fgG - $bgG) * $t)
            $b = [int][math]::Round($bgB + ($fgB - $bgB) * $t)
            $buf[$o + 0] = [byte]$b
            $buf[$o + 1] = [byte]$g
            $buf[$o + 2] = [byte]$r
            $buf[$o + 3] = 255
            $o += 4
        }
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $locked.Scan0, $buf.Length)
$bmp.UnlockBits($locked)
$bmp.Save($PreviewFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

# --------------------------------------------------------------------- report

foreach ($r in $Report) { Write-Output $r }
Write-Output ('icons: {0}  total bytes: {1}  rgb565: 0x{2:x4}' -f $AlphaSets.Count, $TotalBytes, $Ico565)
Write-Output ('wrote {0}' -f (Resolve-Path $hdrPath))
Write-Output ('wrote {0}' -f (Resolve-Path $srcPath))
Write-Output ('wrote {0}' -f (Resolve-Path $PreviewFile))
