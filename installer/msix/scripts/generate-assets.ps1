<#
.SYNOPSIS
    Generates MSIX Visual Assets from app.ico.

.DESCRIPTION
    Microsoft Store / MSIX packages require multiple logo sizes.
    This script reads app.ico and emits PNGs in the sizes Windows expects.

    Output files:
      Square44x44Logo.png       (44x44, app list / taskbar)
      Square71x71Logo.png       (71x71, small tile)
      Square150x150Logo.png     (150x150, default medium tile)
      Square310x310Logo.png     (310x310, large tile)
      Wide310x150Logo.png       (310x150, wide tile)
      StoreLogo.png             (50x50, Store listing)
      SplashScreen.png          (620x300, launch splash)
      Square44x44Logo.targetsize-{16,24,32,48,256}.png

.PARAMETER IconPath
    Source .ico file (default: ../../app.ico)

.PARAMETER OutputDir
    Output directory (default: ../Images)

.PARAMETER BackgroundColor
    Tile background color as R,G,B. Default: dark red (120,0,0).
#>

[CmdletBinding()]
param(
    [string]$IconPath = "$PSScriptRoot/../../../app.ico",
    [string]$OutputDir = "$PSScriptRoot/../Images",
    [int[]]$BackgroundColor = @(120, 0, 0)
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $IconPath)) {
    Write-Error "Icon file not found: $IconPath"
    exit 1
}

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$IconPath = (Resolve-Path $IconPath).Path
$OutputDir = (Resolve-Path $OutputDir).Path
Write-Host "Source icon : $IconPath"
Write-Host "Output dir  : $OutputDir"

$bgColor = [System.Drawing.Color]::FromArgb($BackgroundColor[0], $BackgroundColor[1], $BackgroundColor[2])

# Load the source image. Prefer a high-resolution PNG companion (app.png) if present,
# otherwise extract the largest frame from the .ico file.
$ext = [System.IO.Path]::GetExtension($IconPath).ToLowerInvariant()
$pngSibling = [System.IO.Path]::ChangeExtension($IconPath, '.png')

if (Test-Path $pngSibling) {
    Write-Host "Using PNG source: $pngSibling"
    $srcBmp = [System.Drawing.Image]::FromFile($pngSibling)
} elseif ($ext -eq '.ico') {
    # Walk the .ico header to find the largest frame, then decode it directly.
    $bytes = [System.IO.File]::ReadAllBytes($IconPath)
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $largestSize = 0
    $largestOffset = 0
    $largestLength = 0
    for ($i = 0; $i -lt $count; $i++) {
        $entry = 6 + ($i * 16)
        $w = $bytes[$entry];     if ($w -eq 0) { $w = 256 }
        $h = $bytes[$entry + 1]; if ($h -eq 0) { $h = 256 }
        $size = [Math]::Max($w, $h)
        $len = [BitConverter]::ToUInt32($bytes, $entry + 8)
        $off = [BitConverter]::ToUInt32($bytes, $entry + 12)
        if ($size -gt $largestSize) {
            $largestSize = $size
            $largestOffset = $off
            $largestLength = $len
        }
    }
    Write-Host "Selected frame: ${largestSize}x${largestSize} (offset=$largestOffset, len=$largestLength)"

    # The frame may be either a PNG blob (Vista+) or a DIB. Try PNG first (starts with 0x89 50 4E 47).
    $frameBytes = New-Object byte[] $largestLength
    [Array]::Copy($bytes, $largestOffset, $frameBytes, 0, $largestLength)
    $isPng = ($frameBytes[0] -eq 0x89 -and $frameBytes[1] -eq 0x50 -and $frameBytes[2] -eq 0x4E -and $frameBytes[3] -eq 0x47)
    if ($isPng) {
        $ms = New-Object System.IO.MemoryStream(,$frameBytes)
        $srcBmp = [System.Drawing.Image]::FromStream($ms)
    } else {
        # Legacy DIB frame — let .NET's Icon class handle decoding at its natural size.
        $iconStream = New-Object System.IO.MemoryStream(,$bytes)
        $ico = New-Object System.Drawing.Icon($iconStream, $largestSize, $largestSize)
        $srcBmp = $ico.ToBitmap()
        $ico.Dispose()
    }
} else {
    $srcBmp = [System.Drawing.Image]::FromFile($IconPath)
}

Write-Host "Source size : $($srcBmp.Width)x$($srcBmp.Height)"
if ($srcBmp.Width -lt 256) {
    Write-Warning "Source resolution is only $($srcBmp.Width)x$($srcBmp.Height). For Store-quality assets, provide a 256x256+ source as app.png."
}

function New-LogoBitmap {
    param(
        [int]$Width,
        [int]$Height,
        [System.Drawing.Bitmap]$SourceBitmap,
        [System.Drawing.Color]$Background,
        [bool]$Transparent = $false,
        [double]$LogoScale = 0.7
    )

    $bmp = New-Object System.Drawing.Bitmap $Width, $Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    if ($Transparent) {
        $g.Clear([System.Drawing.Color]::Transparent)
    } else {
        $g.Clear($Background)
    }

    # Center the logo (LogoScale ratio of the shorter side)
    $logoSize = [Math]::Min($Width, $Height) * $LogoScale
    $logoSize = [int]$logoSize
    $offsetX = [int](($Width - $logoSize) / 2)
    $offsetY = [int](($Height - $logoSize) / 2)
    $g.DrawImage($SourceBitmap, $offsetX, $offsetY, $logoSize, $logoSize)
    $g.Dispose()
    return $bmp
}

# Asset definitions: name, width, height, transparent, logoScale
$assets = @(
    @{ Name = "Square44x44Logo.png";   Width =  44; Height =  44; Transparent = $true;  LogoScale = 1.0 },
    @{ Name = "Square71x71Logo.png";   Width =  71; Height =  71; Transparent = $false; LogoScale = 0.7 },
    @{ Name = "Square150x150Logo.png"; Width = 150; Height = 150; Transparent = $false; LogoScale = 0.7 },
    @{ Name = "Square310x310Logo.png"; Width = 310; Height = 310; Transparent = $false; LogoScale = 0.7 },
    @{ Name = "Wide310x150Logo.png";   Width = 310; Height = 150; Transparent = $false; LogoScale = 0.7 },
    @{ Name = "StoreLogo.png";         Width =  50; Height =  50; Transparent = $true;  LogoScale = 1.0 },
    @{ Name = "SplashScreen.png";      Width = 620; Height = 300; Transparent = $false; LogoScale = 0.5 }
)

# Per-target-size icons for the app list / taskbar / start menu
$targetSizes = @(16, 24, 32, 48, 256)

foreach ($asset in $assets) {
    $outPath = Join-Path $OutputDir $asset.Name
    $bmp = New-LogoBitmap -Width $asset.Width -Height $asset.Height `
                          -SourceBitmap $srcBmp -Background $bgColor `
                          -Transparent $asset.Transparent -LogoScale $asset.LogoScale
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  Generated: $($asset.Name) ($($asset.Width)x$($asset.Height))"
}

foreach ($s in $targetSizes) {
    $outPath = Join-Path $OutputDir "Square44x44Logo.targetsize-$s.png"
    $bmp = New-LogoBitmap -Width $s -Height $s -SourceBitmap $srcBmp `
                          -Background $bgColor -Transparent $true -LogoScale 1.0
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  Generated: Square44x44Logo.targetsize-$s.png"
}

$srcBmp.Dispose()

$count = (Get-ChildItem $OutputDir -Filter *.png).Count
Write-Host ""
Write-Host "Generated $count PNG files in $OutputDir" -ForegroundColor Green
