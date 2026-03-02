# ============================================================
#  OraDB DUMP Viewer - Store Icon Asset Generator
#
#  高解像度 PNG からストア必須サイズのアイコンを生成する。
#  ソース画像がない場合は app.ico の最大フレームから拡大 (フォールバック)。
#
#  Usage:
#    .\generate_assets.ps1 [-SourceImage ..\app_512.png]
#    .\generate_assets.ps1                              # app.ico から自動検出
# ============================================================

param(
    [string]$SourceImage = "",
    [string]$OutputDir = "$PSScriptRoot\Assets"
)

Add-Type -AssemblyName System.Drawing

# ソース画像の自動検出
if (-not $SourceImage -or -not (Test-Path $SourceImage)) {
    # Store/ ディレクトリ内の高解像度 PNG を探す
    $candidates = @(
        "$PSScriptRoot\app_512.png",
        "$PSScriptRoot\app_1024.png",
        "$PSScriptRoot\..\app_512.png"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) {
            $SourceImage = $c
            break
        }
    }
}

# 見つからない場合は app.ico からフォールバック
if (-not $SourceImage -or -not (Test-Path $SourceImage)) {
    $icoPath = "$PSScriptRoot\..\app.ico"
    if (-not (Test-Path $icoPath)) {
        Write-Error "No source image found. Place app_512.png in Store/ or provide -SourceImage."
        exit 1
    }
    Write-Host "WARNING: Using app.ico as fallback. Quality may be low for Store submission."
    Write-Host "         Recommend providing a 512x512+ PNG source image."
    $SourceImage = $icoPath
}

Write-Host "Source: $SourceImage"
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# 必須サイズ定義
$sizes = [ordered]@{
    "StoreLogo.png"          = @(50, 50)
    "Square44x44Logo.png"    = @(44, 44)
    "Square71x71Logo.png"    = @(71, 71)
    "Square150x150Logo.png"  = @(150, 150)
    "Wide310x150Logo.png"    = @(310, 150)
    "Square310x310Logo.png"  = @(310, 310)
}

# ソース画像を読み込み
$src = [System.Drawing.Image]::FromFile((Resolve-Path $SourceImage).Path)
Write-Host "Source size: $($src.Width)x$($src.Height)"

foreach ($entry in $sizes.GetEnumerator()) {
    $w = $entry.Value[0]
    $h = $entry.Value[1]
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    if ($w -ne $h) {
        # ワイドタイル: アイコンを中央配置
        $scale = [Math]::Min($w, $h) / [Math]::Max($src.Width, $src.Height)
        $drawW = [int]($src.Width * $scale * 0.6)
        $drawH = [int]($src.Height * $scale * 0.6)
        $x = [int](($w - $drawW) / 2)
        $y = [int](($h - $drawH) / 2)
        $g.DrawImage($src, $x, $y, $drawW, $drawH)
    } else {
        $g.DrawImage($src, 0, 0, $w, $h)
    }

    $g.Dispose()
    $outPath = Join-Path $OutputDir $entry.Key
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  Generated: $($entry.Key) (${w}x${h})"
}

$src.Dispose()
Write-Host ""
Write-Host "All assets generated in $OutputDir"
