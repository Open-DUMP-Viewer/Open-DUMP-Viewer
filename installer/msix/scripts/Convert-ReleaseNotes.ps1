<#
.SYNOPSIS
    Microsoft Store の「このバージョンの新機能」欄用の平文を CHANGELOG.md から生成する。

.DESCRIPTION
    Store のこの欄 (提出 JSON の listings.<言語>.baseListing.releaseNotes) は
    **平文しか描画しない**。Markdown をそのまま渡すと `**` や `- ` が生のまま出る。
    インストーラーの使用許諾 (Convert-LicenseFiles.ps1) と同じ考え方で、
    原本 (CHANGELOG.md) から毎回平文を生成し、文面を二重管理しない。

    Store の上限は 1500 文字。超える場合は行の切れ目で打ち切り、末尾に
    GitHub Releases への案内を付ける。文字数で機械的に切ると文の途中で
    切れたまま公開されてしまうため、必ず行単位で切る。

    抽出に失敗したら例外で止める。呼び出し側 (publish-store ジョブ) は
    Store を触る前にこのスクリプトを実行するので、ここで止めれば
    中途半端な下書き提出が Partner Center に残らない。

.PARAMETER Version
    抽出するバージョン (例: 4.5.0)。CHANGELOG.md の "## [4.5.0] - ..." に対応する。

.PARAMETER OutputPath
    生成した平文の書き出し先。

.PARAMETER ChangelogPath
    CHANGELOG.md のパス。既定はリポジトリルートの CHANGELOG.md。

.PARAMETER ReleasesUrl
    打ち切り時に案内する URL。既定は当該バージョンのタグページ。

.PARAMETER MaxLength
    Store の上限文字数。既定 1500。
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$Version,
    [Parameter(Mandatory=$true)] [string]$OutputPath,
    [string]$ChangelogPath,
    [string]$ReleasesUrl,
    [int]$MaxLength = 1500
)

$ErrorActionPreference = 'Stop'

# installer/msix/scripts -> installer/msix -> installer -> リポジトリルート
if (-not $ChangelogPath) {
    $repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
    $ChangelogPath = Join-Path $repoRoot 'CHANGELOG.md'
}
if (-not $ReleasesUrl) {
    $ReleasesUrl = "https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/tag/v$Version"
}

if (-not (Test-Path $ChangelogPath)) { throw "CHANGELOG が見つかりません: $ChangelogPath" }

$content = Get-Content $ChangelogPath -Raw -Encoding UTF8

# "## [4.5.0] - 2026-08-10" と "## 4.5.0" のどちらにも対応する
# (build-and-release.yml の release ジョブが使う抽出と同じ形)
$escaped = [regex]::Escape($Version)
$match = [regex]::Match($content, "(?ms)^## \[?${escaped}\]?[^\n]*\n(.*?)(?=\n## |\Z)")
if (-not $match.Success) {
    throw "CHANGELOG.md に $Version のセクションがありません。Store 提出の前に停止します。"
}
$section = $match.Groups[1].Value

# ─────────────────────────────────────
# Markdown -> 平文
# ─────────────────────────────────────
$lines = New-Object System.Collections.Generic.List[string]

foreach ($line in ($section -split "\r?\n")) {
    # 表の区切り行 (|---|---|) は捨てる。水平線 (---) と誤認しないよう「|」を必須にする
    if ($line -match '^\s*\|[\s:|-]+\|\s*$') { continue }

    # 記号を落とす前にネストの深さを段数として取っておく (半角 2 つで 1 段)
    $depth = 0
    if ($line -match '^(\s+)') { $depth = [int][math]::Floor($Matches[1].Length / 2) }

    $text = $line.Trim()
    if ($text -eq '') { $lines.Add(''); continue }

    # 水平線は平文では意味を持たない
    if ($text -match '^[-*_]{3,}$') { continue }

    # 見出し (### 機能変更)
    $isHeading = $false
    if ($text -match '^#{1,6}\s*(.+)$') { $text = $Matches[1]; $isHeading = $true }

    # 箇条書きの記号。段数に応じて後で付け直す
    $isBullet = $false
    if (-not $isHeading -and $text -match '^[-*+]\s+(.*)$') { $text = $Matches[1]; $isBullet = $true }

    $text = $text -replace '<[^>]+>', ''                        # HTML タグ
    $text = [regex]::Replace($text, '\[([^\]]+)\]\(([^)]+)\)', {  # Markdown リンクは URL を残す
        param($m)
        $t = $m.Groups[1].Value; $u = $m.Groups[2].Value
        if ($t -eq $u) { $u } else { "$t ($u)" }
    })
    $text = $text -replace '\*\*', ''                           # 強調
    $text = $text -replace '`', ''                              # インラインコード
    $text = $text.Trim()
    if ($text -eq '') { continue }

    if ($isHeading) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ne '') { $lines.Add('') }
        $lines.Add("■ $text")
    } elseif ($isBullet -and $depth -le 0) {
        $lines.Add("・$text")
    } elseif ($isBullet) {
        $lines.Add(('  ' * $depth) + "- $text")
    } else {
        $lines.Add(('  ' * $depth) + $text)
    }
}

# 連続する空行を 1 つにまとめ、前後の空行を落とす
$compact = New-Object System.Collections.Generic.List[string]
$prevBlank = $true
foreach ($l in $lines) {
    $isBlank = [string]::IsNullOrWhiteSpace($l)
    if ($isBlank -and $prevBlank) { continue }
    $compact.Add($l); $prevBlank = $isBlank
}
while ($compact.Count -gt 0 -and [string]::IsNullOrWhiteSpace($compact[$compact.Count - 1])) {
    $compact.RemoveAt($compact.Count - 1)
}

$body = ($compact -join "`n")

# ─────────────────────────────────────
# 上限超過時の打ち切り
# ─────────────────────────────────────
$suffix = "…（以下省略）`n続きは GitHub Releases をご覧ください:`n$ReleasesUrl"

if ($body.Length -gt $MaxLength) {
    $original = $body.Length

    # 本文と案内の間に空行を 1 つ挟むので、改行 2 文字分も予算から引く
    $budget = $MaxLength - ($suffix.Length + 2)
    if ($budget -lt 1) {
        throw "MaxLength ($MaxLength) が案内文 ($($suffix.Length) 文字) に足りません"
    }

    $kept = New-Object System.Collections.Generic.List[string]
    $len = 0
    foreach ($l in $compact) {
        $add = if ($kept.Count -eq 0) { $l.Length } else { $l.Length + 1 }  # 改行 1 文字
        if ($len + $add -gt $budget) { break }
        $kept.Add($l); $len += $add
    }

    if ($kept.Count -eq 0) {
        # 1 行目からして予算に収まらない場合だけ、やむを得ず文字単位で切る
        $body = $body.Substring(0, $budget)
    } else {
        # 末尾の空行と、中身が 1 行も残らなかった見出しを落とす
        while ($kept.Count -gt 0 -and
               ([string]::IsNullOrWhiteSpace($kept[$kept.Count - 1]) -or $kept[$kept.Count - 1].StartsWith('■'))) {
            $kept.RemoveAt($kept.Count - 1)
        }
        if ($kept.Count -eq 0) { throw "打ち切り後に本文が残りませんでした (v$Version)" }
        $body = ($kept -join "`n")
    }

    $body = "$body`n`n$suffix"
    Write-Host "上限 $MaxLength 文字を超えたため打ち切りました ($original -> $($body.Length) 文字)"
}

# ─────────────────────────────────────
# 書き出しと検証
# ─────────────────────────────────────
$outDir = Split-Path $OutputPath -Parent
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# msstore へは JSON の文字列値として渡すため BOM は付けない
[System.IO.File]::WriteAllText($OutputPath, $body, (New-Object System.Text.UTF8Encoding $false))

# 生成物の検証。マークアップが残っても Store 上で崩れて見えるだけでリリースは通ってしまい、
# 公開後まで気付けないため、ここで止める (Convert-LicenseFiles.ps1 と同じ考え方)
if ($body.Trim() -eq '') { throw "生成物が空です (v$Version)" }
if ($body.Length -gt $MaxLength) { throw "生成物が上限 $MaxLength 文字を超えています ($($body.Length) 文字)" }
$residual = [regex]::Match($body, '<[a-zA-Z/][^>]*>|\]\(|(?m)^\s*#{1,6}\s|\*\*|`')
if ($residual.Success) { throw "生成物にマークアップが残っています: $($residual.Value)" }

Write-Host "What's new for v$Version ($($body.Length)/$MaxLength chars) -> $OutputPath"
Write-Host ('-' * 70)
Write-Host $body
Write-Host ('-' * 70)
