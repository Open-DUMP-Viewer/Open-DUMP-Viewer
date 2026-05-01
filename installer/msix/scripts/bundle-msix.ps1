<#
.SYNOPSIS
    複数アーキテクチャの .msix を 1 つの .msixbundle に統合する。

.PARAMETER InputDir
    入力 .msix が置かれたディレクトリ (フラットに *.msix が並んでいる前提)

.PARAMETER OutputBundle
    出力 .msixbundle のパス

.PARAMETER Version
    バンドルバージョン (例: 4.0.0)
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$InputDir,
    [Parameter(Mandatory=$true)] [string]$OutputBundle,
    [Parameter(Mandatory=$true)] [string]$Version
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $InputDir)) { throw "InputDir not found: $InputDir" }

$InputDir = (Resolve-Path $InputDir).Path

# MakeAppx.exe を Windows SDK から探索
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$makeAppx = Get-ChildItem $sdkRoot -Recurse -Filter MakeAppx.exe |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
if (-not $makeAppx) { throw "MakeAppx.exe not found under $sdkRoot" }

# バージョン正規化 (4 セグメント)
$verSegments = $Version.Split('.')
while ($verSegments.Count -lt 4) { $verSegments += '0' }
$bundleVersion = ($verSegments[0..3] -join '.')

# 入力 .msix を確認
$msixFiles = Get-ChildItem $InputDir -Filter *.msix
if ($msixFiles.Count -eq 0) { throw "No .msix files found in $InputDir" }
Write-Host "Bundling $($msixFiles.Count) MSIX file(s):"
$msixFiles | ForEach-Object { Write-Host "  - $($_.Name)" }

$outDir = Split-Path $OutputBundle -Parent
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

& "$($makeAppx.FullName)" bundle /d $InputDir /p $OutputBundle /bv $bundleVersion /o
if ($LASTEXITCODE -ne 0) {
    throw "MakeAppx bundle failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "MSIX Bundle built : $OutputBundle" -ForegroundColor Green
Get-Item $OutputBundle | Select-Object Name, @{N='SizeMB';E={[math]::Round($_.Length/1MB, 2)}}
