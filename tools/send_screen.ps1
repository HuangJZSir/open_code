param(
    [Parameter(Mandatory = $true)][string]$Device,
    [int]$Width = 320,
    [double]$Fps = 8,
    [int]$Quality = 70,
    [string]$Region = '',
    [switch]$Stop
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$base = "http://$Device"

if ($Stop) {
    $r = Invoke-WebRequest -Uri "$base/api/cast/stop" -Method Post -TimeoutSec 5 -UseBasicParsing
    Write-Output ("stop: " + $r.StatusCode + " " + $r.Content)
    return
}

$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($Region) {
    $v = $Region -split ','
    if ($v.Count -ne 4) { throw 'Region 需要 left,top,width,height' }
    $bounds = [System.Drawing.Rectangle]::new([int]$v[0], [int]$v[1], [int]$v[2], [int]$v[3])
}

$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' } | Select-Object -First 1
$params = [System.Drawing.Imaging.EncoderParameters]::new(1)
$params.Param[0] = [System.Drawing.Imaging.EncoderParameter]::new([System.Drawing.Imaging.Encoder]::Quality, [long]$Quality)

$period = 0
if ($Fps -gt 0) { $period = 1000.0 / $Fps }

$full = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
$grab = [System.Drawing.Graphics]::FromImage($full)
$sent = 0
$fail = 0
$report = 20
$clock = [System.Diagnostics.Stopwatch]::StartNew()

Write-Output ("发送到 " + $base + "/api/cast 区域 " + $bounds.Width + "x" + $bounds.Height + " 宽度 " + $Width + " 目标 " + $Fps + " fps")

try {
    while ($true) {
        $tick = [System.Diagnostics.Stopwatch]::StartNew()
        $grab.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $full.Size)

        $height = [int][Math]::Max(1, [Math]::Round($Width * $full.Height / $full.Width))
        $small = [System.Drawing.Bitmap]::new($Width, $height)
        $scale = [System.Drawing.Graphics]::FromImage($small)
        $scale.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
        $scale.DrawImage($full, 0, 0, $Width, $height)
        $scale.Dispose()

        $stream = [System.IO.MemoryStream]::new()
        $small.Save($stream, $codec, $params)
        $bytes = $stream.ToArray()
        $stream.Dispose()
        $small.Dispose()

        try {
            Invoke-WebRequest -Uri ($base + "/api/cast") -Method Post -Body $bytes -ContentType 'image/jpeg' -TimeoutSec 5 -UseBasicParsing | Out-Null
            $sent++
        } catch {
            $fail++
            Write-Output ("发送失败: " + $_.Exception.Message)
        }

        if ($sent -ge $report) {
            $report = $report + 20
            $elapsed = [Math]::Max($clock.Elapsed.TotalSeconds, 0.001)
            $fpsNow = [Math]::Round($sent / $elapsed, 1)
            Write-Output ("成功 " + $sent + " 帧 失败 " + $fail + " 帧 平均 " + $fpsNow + " fps 单帧 " + $bytes.Length + " 字节")
        }

        $wait = $period - $tick.Elapsed.TotalMilliseconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
    }
} finally {
    $grab.Dispose()
    $full.Dispose()
    try {
        Invoke-WebRequest -Uri ($base + "/api/cast/stop") -Method Post -TimeoutSec 3 -UseBasicParsing | Out-Null
    } catch {
    }
    Write-Output ("结束: 成功 " + $sent + " 帧 失败 " + $fail + " 帧")
}