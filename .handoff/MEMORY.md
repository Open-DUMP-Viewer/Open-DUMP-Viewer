# MEMORY — 4.6.2「中程度の誤り」修正の引継ぎ (2026-10-18 停止時点)

このフォルダ (`.handoff/`) は作業の引継ぎ用。**PR を作る前に削除すること。**
ブランチ `claude/ecstatic-lovelace-i5is9k` のコードは e9b302b まで検証済み (この退避コミットはコードを変えていない)。

## 取り組んだこと
- 依頼「中程度まで修正して公開します」: 中程度 43 件を、群 8〜14 に分けて根本修正する (案A)
- 公開の手順: ベータ (`main:beta`) → 利用者が Windows で確認 → 正式版 (`main:release`)。PR を作り、CI と承認が通ったら Claude がマージする

## 完了したこと (push 済み・検証済み)
- 群8 ef4f2fb / 群10 0168333 / 群11 53200ca, f6e8122 / 群14 affdd20, 67133b1 / 群12a 3dfe1fb / 群9 65bf28b / 群12b e9b302b
- 統合ごとの検証: 全件回帰の新しい差がすべて群ごとの説明に含まれること、health 0、ビルド警告が増えていないこと (win 0 / native 5 / asan 1)、dotnet build 0/0

## 進行中 (未統合。`patches/` に退避)
- **群13a (C: SQL/CSV 出力、M-12/13/15/16/17/18/19/28)**
  - `patches/g13a/0001-*.patch`: 本体 (5fcd3f6。コミットメッセージは仮なので書き直す)
  - `patches/g13a/9999-uncommitted-wip.diff`: 追加分 (a)〜(f)。実装は済み、確認の途中。全件回帰は終わったが、差の分析は未了
    - (a) constraints_json に `"unique_index":1`
    - (b) `odv_set_value_mode(s, ODV_VALUES_EXPORT)`: 長さ -2 = 印、-3 = 上限 512MiB 超え
    - (c) `odv_read_table_ddl(s)`: expdp の制約・DEFAULT・コメント。col_not_nulls = 2 は主キーだけによる NOT NULL
    - (d) NOTE の 511 バイト切れ
    - (e) DDL ヒント `odv_get_table_ddl_hint(s, i)` / `odv_set_ddl_hint(s, hint)`: 表ごと SQL の遅延対策
    - (f) CSV を CRLF に
  - **未着手**: (g) exp の後置の索引 (関数索引・DESC 索引) を仕様どおりの並び (種類の順・名前の順) にする (m_cols 21 e の D_FBI)
  - **未着手**: (h) 0 行の表の CSV にも見出し行を書く
- **群13b (VB: エクスポート経路、M-14/M-32 + 繋ぎ込み)**
  - `patches/g13b/0001`, `0002` (5cfcbbc, 0105d8b。メッセージは仮)。確認済み:
    - dotnet build 0/0
    - DLL の経路との比較 (63 ダンプ・1,760 ファイル) で、差は (g) の 1 表だけ
    - Oracle への往復で 5 ダンプの全表が一致
  - (b)(c)(e) の繋ぎ込み済み。g13a の API が変わったら合わせ直す
- **ヘルプの改稿 (途中)**
  - `patches/help-partial.diff`: 日本語の 7 頁だけ。事実の反証は済んだが、改稿後の見直しと 9 言語への反映は未実施
- **CHANGELOG**: `notes/changelog_462_medium_draft.md` が 4.6.2 節への追記の下書き。【】の箇所は確定待ち
- **仕様・記録**
  - `notes/TYPE_MAPPING_SPEC.md`: 型の対応・値の書き方・CSV の仕様。DLL と VB で共通
  - `notes/NOTES_g13a.md`, `notes/NOTES_g13b.md`: 作業記録と判断事項

## 決定事項 (利用者の決定、統括の判断)
- **利用者の決定**
  - TSTZ は時差を常に併記する
  - TSLTZ は DBTIMEZONE を付ける
  - PostgreSQL では 0x00 を除いて注記する
  - プレビューでは 0x00 を ␀ で表示する
  - M-09 は IANA tzdata を同梱する
  - M-24 は Oracle の実測から表を作る
- **統括の判断 (利用者は未確認)**
  - SQL Server 方言はスキーマを dbo に置き換えない
  - CSV はすべての経路で CRLF、BOM なし。ヘルプの「BOM 付き」は誤りとして直す
  - TablePreview で絞り込みの無い SQL / CSV は DLL の経路で出す
  - exp の表ごとの SQL は、一覧で記録した位置へシークして後置の DDL を読む
  - 値を復元できないもの (印) は、SQL では NULL と `-- NOTE:`、CSV では空欄と `<出力>.notes.txt` にする
- **範囲外として残すもの (低)**
  - L-18: exp DIRECT で 128 バイトの名前が切れる
  - L-24: 負の INTERVAL の UBSan。MSVC のビルドでは正しい値が出る

## 次回拾うべき項目 (順に)
1. **環境の再構築** (コンテナが回収されると失われる)
   - 試験一式 (`$S/odvtest`: 3 版のダンプ約 2GB・照合ツール・回帰スクリプト) と、Oracle 11/21/23 の Docker コンテナ
   - 失われていた場合は作り直しが要る。または、検証の範囲を利用者と相談する
2. g13a のパッチを当てる
   - `git am patches/g13a/0001*.patch`
   - `git apply patches/g13a/9999-uncommitted-wip.diff`
3. (g)(h) を実装し、全件回帰・ASan・Oracle への往復を確認する
4. g13b のパッチを当てる (`git am patches/g13b/*.patch`)。C と VB でファイルは重ならない
5. 統合の検証
   - DLL の経路と VB の書き手を突き合わせて、差 0
   - コミットメッセージを読み手向けに書き直す (M-/H- 番号や工程の数値は書かない)
6. ヘルプ (export_sql / export_csv など) を仕上げ、10 言語へ反映する
7. CHANGELOG 4.6.2 に下書きを反映し、日付を公開日にする
8. 最終検証
   - HEAD から全件回帰、全件 ASan、SQL の流し戻し
9. PR を作り、CI が通ったらマージする
   - `.handoff/` を削除してから PR を作る
   - PR テンプレートに従い、末尾に帰属表示を付ける
10. ベータを公開する (`git push origin main:beta`)
    - 利用者が Windows で確認したら、`main:release` で正式版を公開する
