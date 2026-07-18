<!-- PR タイトルは Conventional Commits 形式で(squash 時の最終コミットメッセージになります) -->

## 概要

<!-- 何を・なぜ。関連 Issue があれば #番号 で参照 -->

## チェックリスト

該当しない行は削除してください。正本は [AGENTS.md](AGENTS.md)。

- [ ] バージョンを上げた → **3ファイルすべて**を更新した(`Open DUMP Viewer.vbproj` の `<Version>` / `Logics/DumpParser/odv_api.c` の `ODV_VERSION_STRING` / `CHANGELOG.md`)
- [ ] C ネイティブ DLL(`Logics/DumpParser`)を変更した → Windows(`_build.cmd`)でビルドし、実際の .dmp ファイルで解析を確認した
- [ ] UI 文言を追加・変更した → `Strings.resx` と各言語版(en/zh/ko/de/fr/es/it/ru/pt-BR)を更新した
- [ ] インストーラーの挙動に影響 → `installer/setup.iss` を更新した
- [ ] 動作環境・機能の記述に影響 → `README.md` の該当箇所も更新した
- [ ] 外部依存の追加・更新 → 当日ライブ検証の記録を下に残した

<!-- 外部依存を追加・更新した場合のみ:
Vendored: <pkg> <ver> (verified <cmd> = <ver>, YYYY-MM-DD)
-->

## 検証

<!-- 何をどう確認したか。手元でのビルド・動作確認の結果を書いてください -->
