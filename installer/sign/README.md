# コード署名(Sectigo + Google Cloud KMS)

リリース成果物(自前バイナリ・EXE インストーラー)を GCP KMS の HSM 鍵で Authenticode 署名する。
署名基盤(証明書・鍵・WIF)は [Yagura](https://github.com/Yanai-Taketo/Yagura) プロジェクトと共有
(同一オーナーの個人名義 Sectigo OV 証明書 1 枚を両プロジェクトで使う)。

## この配下

| ファイル | 役割 |
|---|---|
| `Invoke-OdvSign.ps1` | jsign(GCP KMS)で対象を署名し、Subject・タイムスタンプまで検証する |
| `codesign-chain.pem` | 証明書チェーン(leaf + 中間 CA 2 枚。USERTrust ルートへ到達するクロス署名版)。公開情報なので同梱する |

## 署名対象

- 署名する: `Open DUMP Viewer.exe`・`Open DUMP Viewer.dll`・`Open_DumpParser.dll`(publish 出力を
  portable ZIP / インストーラー / MSIX へ複製する**前**に署名する)、Inno Setup インストーラー EXE
- 署名しない: .NET ランタイム・第三者 DLL(上流の形のまま同梱)、**MSIX パッケージ本体**
  (Microsoft Store が Store 署名を付けるため。内包バイナリの Authenticode は問題にならない)

## ピン留め(供給網保護)

| 依存 | 版 | 検証 |
|---|---|---|
| jsign | 7.5 | jar SHA256 = `602a51c3545a6dc4fb99bd2ea7152b26d1345916d0c93ddfbd5936cb735af91c`(ワークフローで照合) |
| `google-github-actions/auth` | v3.0.0 | commit SHA `7c6bc770dae815cd3e89ee6cdf493a5fab2cc093` でピン |

TSA(タイムスタンプ)は主 `http://timestamp.sectigo.com` + 副 `http://timestamp.digicert.com` の
フォールバック。全滅時は fail-closed。

## GitHub の設定

署名ステップはリポジトリ変数 **`ODV_SIGNING` が `enabled` のとき**だけ動く(緊急時のオフスイッチ)。

- リポジトリ変数(`vars`。公開してよい値):
  - `ODV_SIGNING = enabled`
  - `GCP_WIF_PROVIDER = projects/275116585205/locations/global/workloadIdentityPools/github-pool/providers/github-provider`
  - `GCP_SIGNER_SA = odv-signer@code-signing-502513.iam.gserviceaccount.com`
  - `GCP_KEYRING = projects/code-signing-502513/locations/asia-northeast1/keyRings/codesign`
  - `GCP_KEY_ALIAS = codesign-rsa4096`
  - `SIGNER_SUBJECT = 柳井建人`(署名者 Subject の期待値。証明書の実際の Subject は
    `C=JP, ST=Yamaguchi, O=柳井建人, CN=柳井建人`)
- GitHub Environment **`release-signing`**(required reviewers = オーナー)= 承認ゲート。
  build ジョブ全体をこの Environment に載せているため、**リリース実行はビルド開始前に
  オーナー承認 1 回を要する**。WIF の信頼条件がこの Environment 名を要求するため、
  これが無いと GCP がトークン自体を発行しない
- Secrets は不要(WIF で短命トークンを取得するため長期秘密を置かない)

## 動作確認(公開せずに署名まで試す)

`build-and-release.yml` を手動実行(`workflow_dispatch`)すると、既定
(`publish_release=false`)では **GitHub Release 作成・Winget/Chocolatey/Store 公開を行わず**
ビルド + 署名 + 検証だけを実行する。

```
gh workflow run build-and-release.yml --ref <ブランチ> -f publish_release=false
```
