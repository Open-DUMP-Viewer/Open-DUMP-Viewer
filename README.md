# CLA 署名台帳 / CLA signature ledger

このブランチは **CLA（コントリビューターライセンス同意書）への署名記録を保管する専用ブランチ**です。
製品のソースコードは含みません（orphan ブランチ。`main` とは履歴を共有しません）。

This is a dedicated **orphan** branch that stores the ledger of CLA signatures.
It contains no product source code and shares no history with `main`.

## 仕組み / How it works

- 署名は `.github/workflows/cla.yml`（[contributor-assistant/github-action](https://github.com/contributor-assistant/github-action)）が記録します
- 署名者が Pull Request に所定の定型文をコメントすると、その記録が `signatures/version1/cla.json` に追記されます
- 署名対象の文書は `main` ブランチの [CLA.md](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/blob/main/CLA.md) です

署名記録は**第三者のホスト型サービスへは送信されず**、本リポジトリ内に留まります。

## 手で編集しないこと / Do not edit by hand

`signatures/version1/cla.json` は Action が自動で更新します。手動編集は署名記録の
一貫性を損なうため行わないでください。
