<#
.SYNOPSIS
  Open DUMP Viewer のリリース成果物(自前バイナリとインストーラー)を Google Cloud KMS の鍵で
  Authenticode 署名する。

.DESCRIPTION
  CI(build-and-release(-beta).yml の build ジョブ)から呼ぶ。署名基盤(Sectigo OV 証明書 +
  GCP KMS HSM 鍵 + WIF)は Yagura プロジェクトと共有する。

  - 秘密鍵は GCP KMS の HSM にあり手元にも CI にも存在しない。jsign(--storetype GOOGLECLOUD)経由で
    署名する。ジョブは WIF から得た短命アクセストークンを渡すだけで、長期秘密は持たない。
  - 署名対象は「自前バイナリのみ」。上流(.NET ランタイム・第三者 DLL)は署名しない。
    呼び出し側が対象ファイルを渡す。
  - タイムスタンプは主+副 TSA でフォールバックする。全滅時は jsign が失敗し本スクリプトも失敗する
    (fail-closed)。
  - 署名後、各ファイルの Authenticode 有効性・署名者 Subject の期待値一致・タイムスタンプの存在を
    検証する。1 つでも満たさなければ throw する。
#>
[CmdletBinding()]
param(
  # 検証済み jsign-<ver>.jar のパス(呼び出し側が SHA256 を照合してから渡す)
  [Parameter(Mandatory)][string]$JsignJar,
  # 鍵リングのリソース名(鍵ではなくリング)。例:
  #   projects/code-signing-502513/locations/asia-northeast1/keyRings/codesign
  [Parameter(Mandatory)][string]$KeyRingResource,
  # KMS の鍵名(エイリアス)。例: codesign-rsa4096
  [Parameter(Mandatory)][string]$KeyAlias,
  # 証明書チェーン PEM(leaf + 中間 CA)。公開情報でリポジトリ同梱可
  [Parameter(Mandatory)][string]$CertFile,
  # WIF から取得した GCP アクセストークン(短命)
  [Parameter(Mandatory)][string]$AccessToken,
  # 期待する署名者 Subject の部分文字列。偽物を弾くための期待値照合
  [Parameter(Mandatory)][string]$ExpectedSubject,
  # 署名する PE のパス(複数可)。呼び出し側が自前バイナリのみに絞って渡す
  [Parameter(Mandatory)][string[]]$Files,
  # RFC 3161 タイムスタンプ機関(主, 副…)。フォールバック順
  [string[]]$TsaUrls = @('http://timestamp.sectigo.com', 'http://timestamp.digicert.com')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $JsignJar)) { throw "jsign jar が見つからない: $JsignJar" }
if (-not (Test-Path -LiteralPath $CertFile)) { throw "証明書チェーンが見つからない: $CertFile" }
if ($Files.Count -eq 0) { throw '署名対象が 1 つも指定されていない' }

foreach ($file in $Files) {
  if (-not (Test-Path -LiteralPath $file)) { throw "署名対象が見つからない: $file" }

  # jsign 引数。--storepass にアクセストークンを渡す(GOOGLECLOUD storetype)。
  $jsignArgs = @(
    '-jar', $JsignJar,
    '--storetype', 'GOOGLECLOUD',
    '--keystore', $KeyRingResource,
    '--storepass', $AccessToken,
    '--alias', $KeyAlias,
    '--certfile', $CertFile,
    '--tsmode', 'RFC3161'
  )
  foreach ($tsa in $TsaUrls) { $jsignArgs += @('--tsaurl', $tsa) }
  $jsignArgs += $file

  Write-Host "==> 署名: $file"
  & java @jsignArgs
  if ($LASTEXITCODE -ne 0) { throw "jsign 失敗: $file (exit $LASTEXITCODE)" }

  # --- 署名検証: Valid だけでなく Subject と TSA まで確認する ---
  $sig = Get-AuthenticodeSignature -LiteralPath $file
  if ($sig.Status -ne 'Valid') {
    throw "署名検証失敗: $file の Status=$($sig.Status)(Valid を期待)。詳細: $($sig.StatusMessage)"
  }
  $subject = $sig.SignerCertificate.Subject
  if ($subject -notlike "*$ExpectedSubject*") {
    # 攻撃者が自分の正規証明書で署名した偽物も Status=Valid になりうるため、Subject 期待値を必須で照合する。
    throw "署名者 Subject 不一致: $file は '$subject'(期待値 '*$ExpectedSubject*' を含むこと)"
  }
  if ($null -eq $sig.TimeStamperCertificate) {
    throw "タイムスタンプ無し: $file (RFC 3161 タイムスタンプが必須)"
  }
  Write-Host "    OK  Subject='$subject'"
  Write-Host "        TSA='$($sig.TimeStamperCertificate.Subject)'  Thumbprint=$($sig.SignerCertificate.Thumbprint)"
}

Write-Host "全 $($Files.Count) ファイルの署名・検証が完了しました。"
