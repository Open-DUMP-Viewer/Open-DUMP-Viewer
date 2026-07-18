<#
.SYNOPSIS
    インストーラーの使用許諾画面 (Inno Setup の LicenseFile) 用の平文を生成する。

.DESCRIPTION
    Inno Setup の LicenseFile は **RTF か平文しか解釈せず、Markdown も HTML も描画しない**。
    そのため EULA.md をそのまま渡すと、表・リンク・HTML タグが生のまま表示される
    (v4.2.0-beta で実際に HTML フッタが崩れて表示された)。

    本スクリプトは原本から平文を生成する:
      EULA.md -> installer/EULA-ja.txt    (日本語。詳細な正本)
      LICENSE -> installer/LICENSE-en.txt (英語。元から平文のため複写のみ)

    生成物は commit しない (.gitignore 済み)。法的文書の文面を二重管理すると
    原本と乖離するため、ビルドのたびに原本から作り直す。

    生成後にマークアップの残存を検査し、残っていればエラーで停止する。
    残っても表示が崩れるだけでビルドは通ってしまい、リリースまで気付けないため。
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$eulaSrc = Join-Path $RepoRoot 'EULA.md'
$licSrc  = Join-Path $RepoRoot 'LICENSE'
$eulaDst = Join-Path $PSScriptRoot 'EULA-ja.txt'
$licDst  = Join-Path $PSScriptRoot 'LICENSE-en.txt'

foreach ($f in @($eulaSrc, $licSrc)) {
    if (-not (Test-Path $f)) { throw "原本が見つかりません: $f" }
}

$src = Get-Content $eulaSrc -Encoding UTF8
$out = New-Object System.Collections.Generic.List[string]

foreach ($line in $src) {
    $l = $line

    # 表の区切り行 (|---|---|) は捨てる。水平線 (---) と誤認しないよう「|」を必須にする
    if ($l -match '^\s*\|[\s:|-]+\|\s*$') { continue }

    # <a href="url">text</a> は URL を残す
    $l = [regex]::Replace($l, '<a\s+href="([^"]+)"\s*>(.*?)</a>', {
        param($m)
        $u = $m.Groups[1].Value; $t = $m.Groups[2].Value
        if ($t -eq $u) { $u } else { "$t ($u)" }
    })

    # 残りの HTML タグを除去
    $l = $l -replace '<[^>]+>', ''

    # Markdown リンク [text](url)
    $l = [regex]::Replace($l, '\[([^\]]+)\]\(([^)]+)\)', {
        param($m)
        $t = $m.Groups[1].Value; $u = $m.Groups[2].Value
        if ($t -eq $u) { $u } else { "$t ($u)" }
    })

    # 見出し記号・強調
    $l = $l -replace '^#{1,6}\s*', ''
    $l = $l -replace '\*\*', ''

    # 水平線
    if ($l -match '^\s*-{3,}\s*$') { $out.Add('-' * 70); continue }

    # 表の本体行: 外側のパイプを落として整形
    if ($l -match '^\s*\|.*\|\s*$') {
        $cells = ($l.Trim().TrimStart('|').TrimEnd('|')) -split '\|' | ForEach-Object { $_.Trim() }
        $l = '  ' + ($cells -join '  |  ')
    }

    # HTML 除去で行末に残る孤立したパイプ
    $l = $l -replace '\s+\|\s*$', ''

    $out.Add($l.TrimEnd())
}

# 連続する空行を 1 つにまとめる
$compact = New-Object System.Collections.Generic.List[string]
$prevBlank = $false
foreach ($l in $out) {
    $isBlank = [string]::IsNullOrWhiteSpace($l)
    if ($isBlank -and $prevBlank) { continue }
    $compact.Add($l); $prevBlank = $isBlank
}

# Inno Setup に非 ASCII を正しく読ませるため BOM 付き UTF-8 で書く。
# Set-Content -Encoding UTF8 は PowerShell のバージョンで BOM の有無が変わる
# (5.1 は BOM あり / 7 は BOM なし) ため、.NET で明示的に BOM 付きで書く。
[System.IO.File]::WriteAllLines($eulaDst, $compact, (New-Object System.Text.UTF8Encoding $true))
Copy-Item $licSrc $licDst -Force

# 生成物の検証: マークアップが残っていたら停止する
foreach ($f in @($eulaDst, $licDst)) {
    if (-not (Test-Path $f)) { throw "$f の生成に失敗しました" }
    $residual = Select-String -Path $f -Pattern '<[a-zA-Z/][^>]*>|\]\(|^\s*#{1,6}\s|\*\*'
    if ($residual) {
        throw "$f にマークアップが残っています (L$($residual[0].LineNumber)): $($residual[0].Line)"
    }
    Write-Host ("{0} ({1:N0} bytes) - OK" -f (Split-Path -Leaf $f), (Get-Item $f).Length)
}
