<#
.SYNOPSIS
    Open DUMP Viewer の MSIX パッケージをビルドする。

.DESCRIPTION
    `dotnet publish` で生成された self-contained 出力に Package.appxmanifest と
    Visual Assets を加え、MakeAppx.exe で .msix を作成する。

    識別子はパラメータ経由で受け取り、テンプレートを置換する。

.PARAMETER PublishDir
    `dotnet publish` の出力ディレクトリ (例: ./publish/app)

.PARAMETER OutputMsix
    生成する .msix ファイルのパス

.PARAMETER Version
    パッケージバージョン (例: 4.0.0)。MSIX は 4 セグメント必須なので ".0" を補完する

.PARAMETER Architecture
    x64 / arm64

.PARAMETER IdentityName
    Package/Identity/Name (例: YANAITaketo.OpenDUMPViewer)

.PARAMETER IdentityPublisher
    Package/Identity/Publisher (例: CN=22377CE5-...)

.PARAMETER PublisherDisplayName
    Package/Properties/PublisherDisplayName (例: YANAITaketo)

.PARAMETER ManifestTemplate
    appxmanifest テンプレートのパス

.PARAMETER ImagesDir
    Visual Assets ディレクトリ
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$PublishDir,
    [Parameter(Mandatory=$true)] [string]$OutputMsix,
    [Parameter(Mandatory=$true)] [string]$Version,
    [Parameter(Mandatory=$true)] [ValidateSet('x64','arm64')] [string]$Architecture,
    [Parameter(Mandatory=$true)] [string]$IdentityName,
    [Parameter(Mandatory=$true)] [string]$IdentityPublisher,
    [Parameter(Mandatory=$true)] [string]$PublisherDisplayName,
    [string]$ManifestTemplate = "$PSScriptRoot/../Package.appxmanifest.template",
    [string]$ImagesDir = "$PSScriptRoot/../Images"
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $PublishDir)) { throw "PublishDir not found: $PublishDir" }
if (-not (Test-Path $ManifestTemplate)) { throw "Manifest template not found: $ManifestTemplate" }
if (-not (Test-Path $ImagesDir)) { throw "ImagesDir not found: $ImagesDir" }

$PublishDir = (Resolve-Path $PublishDir).Path
$ManifestTemplate = (Resolve-Path $ManifestTemplate).Path
$ImagesDir = (Resolve-Path $ImagesDir).Path

# MakeAppx.exe を Windows SDK から探索
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
if (-not (Test-Path $sdkRoot)) {
    throw "Windows SDK not found at $sdkRoot. Install Windows 10/11 SDK."
}
$makeAppx = Get-ChildItem $sdkRoot -Recurse -Filter MakeAppx.exe |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
if (-not $makeAppx) { throw "MakeAppx.exe not found under $sdkRoot" }
Write-Host "Using MakeAppx: $($makeAppx.FullName)"

# 4 セグメントバージョン強制 (MSIX 仕様)
$verSegments = $Version.Split('.')
while ($verSegments.Count -lt 4) { $verSegments += '0' }
$msixVersion = ($verSegments[0..3] -join '.')
Write-Host "MSIX Version : $msixVersion"

# レイアウト用一時ディレクトリ
$layoutRoot = Join-Path ([System.IO.Path]::GetTempPath()) "odv-msix-layout-$Architecture-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $layoutRoot -Force | Out-Null
Write-Host "Layout root  : $layoutRoot"

try {
    # 1. publish の中身を layout にコピー
    Write-Host "Copying publish output..."
    Copy-Item -Path "$PublishDir/*" -Destination $layoutRoot -Recurse -Force

    # 2. Visual Assets を Images/ 配下にコピー
    Write-Host "Copying Visual Assets..."
    $imagesDest = Join-Path $layoutRoot 'Images'
    New-Item -ItemType Directory -Path $imagesDest -Force | Out-Null
    Copy-Item -Path "$ImagesDir/*.png" -Destination $imagesDest -Force

    # 3. Manifest テンプレートを置換して書き出し
    Write-Host "Generating manifest..."
    $manifest = Get-Content $ManifestTemplate -Raw
    $manifest = $manifest.Replace('{{IDENTITY_NAME}}',          $IdentityName)
    $manifest = $manifest.Replace('{{IDENTITY_PUBLISHER}}',     $IdentityPublisher)
    $manifest = $manifest.Replace('{{IDENTITY_VERSION}}',       $msixVersion)
    $manifest = $manifest.Replace('{{PUBLISHER_DISPLAY_NAME}}', $PublisherDisplayName)
    $manifest = $manifest.Replace('{{PROCESSOR_ARCHITECTURE}}', $Architecture)
    $manifestOut = Join-Path $layoutRoot 'AppxManifest.xml'
    Set-Content -Path $manifestOut -Value $manifest -Encoding utf8

    # 4. .msix にパック
    Write-Host "Packing MSIX..."
    $outDir = Split-Path $OutputMsix -Parent
    if ($outDir -and -not (Test-Path $outDir)) {
        New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    }
    & "$($makeAppx.FullName)" pack /d $layoutRoot /p $OutputMsix /o
    if ($LASTEXITCODE -ne 0) {
        throw "MakeAppx pack failed with exit code $LASTEXITCODE"
    }

    Write-Host ""
    Write-Host "MSIX built : $OutputMsix" -ForegroundColor Green
    Get-Item $OutputMsix | Select-Object Name, @{N='SizeMB';E={[math]::Round($_.Length/1MB, 2)}}
}
finally {
    if (Test-Path $layoutRoot) {
        Remove-Item $layoutRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
