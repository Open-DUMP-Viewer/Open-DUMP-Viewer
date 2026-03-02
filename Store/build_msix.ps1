# ============================================================
#  OraDB DUMP Viewer - MSIX Package Builder
#
#  ローカルおよび CI 環境で MSIX パッケージを生成する。
#
#  Usage:
#    .\build_msix.ps1 -PublishDir ..\publish\app -Version 1.0.0 -OutputPath ..\publish\OraDBDumpViewer.msix
#
#  Prerequisites:
#    - Windows SDK (makeappx.exe)
#    - Published self-contained app in $PublishDir
#    - Store/Assets/*.png (run generate_assets.ps1 first)
# ============================================================

param(
    [Parameter(Mandatory)][string]$PublishDir,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$OutputPath,
    [string]$ManifestTemplate = "$PSScriptRoot\AppxManifest.xml",
    [string]$AssetsDir = "$PSScriptRoot\Assets",
    [string]$IdentityName = "",
    [string]$Publisher = "",
    [string]$CertPath = "",
    [string]$CertPassword = ""
)

$ErrorActionPreference = "Stop"

# バージョンを 4 桁に正規化
$version4 = if ($Version -match '^\d+\.\d+\.\d+$') { "$Version.0" } else { $Version }
Write-Host "Building MSIX package v$version4"

# --- 1. makeappx.exe を検索 ---
$sdkDir = "C:\Program Files (x86)\Windows Kits\10\bin"
$makeappx = Get-ChildItem -Path $sdkDir -Recurse -Filter "makeappx.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "x64" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $makeappx) {
    throw "makeappx.exe not found in Windows SDK ($sdkDir)"
}
Write-Host "Using: $($makeappx.FullName)"

# --- 2. ステージングディレクトリ準備 ---
$stageDir = Join-Path ([System.IO.Path]::GetTempPath()) "msix_stage_$(Get-Random)"
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

try {
    # アプリファイルをコピー
    Write-Host "Copying app files from $PublishDir ..."
    Copy-Item -Path "$PublishDir\*" -Destination $stageDir -Recurse -Force

    # --- 3. マニフェストをコピーしてパッチ ---
    Write-Host "Patching AppxManifest.xml ..."
    $manifest = Get-Content $ManifestTemplate -Raw -Encoding UTF8

    # バージョン注入
    $manifest = $manifest -replace '(?<=Identity[^>]*Version=")[^"]*', $version4

    # Identity Name/Publisher の差し替え (指定された場合のみ)
    if ($IdentityName) {
        $manifest = $manifest -replace '(?<=Identity[^>]*Name=")[^"]*', $IdentityName
    }
    if ($Publisher) {
        $manifest = $manifest -replace '(?<=Identity[^>]*Publisher=")[^"]*', $Publisher
    }

    Set-Content -Path (Join-Path $stageDir "AppxManifest.xml") -Value $manifest -Encoding UTF8

    # --- 4. アイコンアセットをコピー ---
    $assetsOut = Join-Path $stageDir "Assets"
    New-Item -ItemType Directory -Path $assetsOut -Force | Out-Null
    if (Test-Path $AssetsDir) {
        Copy-Item -Path "$AssetsDir\*" -Destination $assetsOut -Force
        Write-Host "Copied icon assets from $AssetsDir"
    } else {
        Write-Warning "Assets directory not found: $AssetsDir"
        Write-Warning "MSIX package will not have proper icons."
    }

    # --- 5. MSIX パッケージ生成 ---
    Write-Host "Packing MSIX ..."
    & $makeappx.FullName pack /d $stageDir /p $OutputPath /o
    if ($LASTEXITCODE -ne 0) { throw "makeappx pack failed with exit code $LASTEXITCODE" }

    # --- 6. 署名 (オプション) ---
    if ($CertPath -and (Test-Path $CertPath)) {
        $signtool = Get-ChildItem -Path $sdkDir -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "x64" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($signtool) {
            Write-Host "Signing with $CertPath ..."
            $signArgs = @("sign", "/fd", "SHA256", "/f", $CertPath)
            if ($CertPassword) { $signArgs += @("/p", $CertPassword) }
            $signArgs += $OutputPath
            & $signtool.FullName @signArgs
            if ($LASTEXITCODE -ne 0) { Write-Warning "Signing failed (non-fatal for Store submission)" }
        }
    }

    Write-Host ""
    Write-Host "MSIX BUILD SUCCEEDED: $OutputPath"
    Write-Host "File size: $([math]::Round((Get-Item $OutputPath).Length / 1MB, 1)) MB"
}
finally {
    Remove-Item -Path $stageDir -Recurse -Force -ErrorAction SilentlyContinue
}
