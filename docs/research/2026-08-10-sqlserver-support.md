# SQL Server (.mdf / .bak) 対応の実現可能性調査

調査日: 2026-08-10 / 対象コミット: `e1a355d` (v4.4.1)

本書は 2 件の調査結果をまとめたものである。

1. SQL Server の MDF / BAK を Open DUMP Viewer で開けるようにする場合の技術的・法務的・工数的な見通し
2. 表示名を「Open DUMP Viewer for Oracle **D**atabase」へ変更する場合のコスト

いずれも実装前の判断材料であり、本書の時点で製品コードへの変更は行っていない。

---

## 1. 現行アーキテクチャが新形式をどれだけ受け入れられるか

### 1.1 C パーサ側 — 拡張点は 5 箇所に閉じている

`Logics/DumpParser` は 11 モジュール 8,945 行。形式ごとの分岐 (`s->dump_type` による dispatch) は次の **5 箇所しかない**。

| 位置 | 関数 |
|---|---|
| `odv_api.c:262` | `odv_list_tables` |
| `odv_api.c:369` | `odv_parse_dump` |
| `odv_api.c:604` | `odv_extract_lob` |
| `odv_csv.c:191`  | `write_csv_file` |
| `odv_sql.c:1015` | `write_sql_file` |

新形式の追加は「`odv_detect.c` に判定を足す」「上記 5 箇所に `case` を足す」「パーサモジュールを新規追加する」で完結し、**既存の EXP / EXPDP 実装には一切触れずに済む**。分岐が散らばっていないのは、この種の拡張にとって非常に良い状態である。

### 1.2 公開 API は形式非依存 — 変更不要

`odv_api.h` の `ODV_TABLE_CALLBACK` / `ODV_ROW_CALLBACK` は「スキーマ名・表名・列名・列値(文字列)」というモデルで、Oracle 固有の概念を含んでいない。SQL Server の `database → schema → table` はそのまま `schema → table` に写せる（DB 名はファイル 1 つにつき 1 個なのでウィンドウタイトルへ）。

**API シグネチャの変更は不要**。増えるのは dump kind 定数だけである。

```c
#define ODV_DUMP_MSSQL_MDF        20
#define ODV_DUMP_MSSQL_BAK        21
```

### 1.3 VB.NET 側の変更点は限定的

| ファイル | 変更内容 |
|---|---|
| `Logics/Workspace/OraDB_NativeParser.vb` | dump kind 定数の追加 |
| `Logics/Workspace/AnalyzeLogic.vb:232` | `.dmp` 決め打ちの拡張子チェックを拡張子集合へ |
| `Logics/Open DUMP Viewer/MenuStripLogics.vb:11` | `Dialog_DumpFilter`（Strings.resx × 10 言語） |
| `HMI/Open DUMP Viewer/Open DUMP Viewer.vb:46` | コマンドライン引数の拡張子判定 |
| `installer/setup.iss` / `installer/msix/Package.appxmanifest.template` | `.mdf` / `.bak` のファイル関連付け |

UI（Workspace のツリー、TablePreview、検索、各エクスポート）は形式を意識していないため、**そのまま流用できる**。

### 1.4 ただし既存の内部制約が 3 つ引っかかる

- **`ODV_MAX_TABLES = 1000`（固定長配列）** — Oracle のダンプは業務単位で切り出されることが多く 1,000 表で足りていたが、SQL Server の MDF は**データベース全体**であり、1,000 表超は珍しくない（ERP・パッケージ製品の DB）。動的確保への変更が事実上必須。
- **`ODV_SESSION` が静的に約 2MB** — `table_list[1000]`（1 件約 1.4KB）と `partition_list[1000]`、`read_buf[65536]` を構造体内に直に持っている。
- **read_buf 前提のストリーム設計** — .dmp は「先頭から流して読む」形式だが、MDF は 8KB ページのランダムアクセスである。既存の逐次読み込みバッファとは噛み合わないので、ページ単位の LRU キャッシュを別に用意する必要がある。

いずれも新規モジュール側で完結でき、既存パーサの挙動を変えずに対応できる。

---

## 2. MDF フォーマットの技術調査

### 2.1 構造

- ページサイズ 8KB 固定、8 ページ = 1 エクステント (64KB)
- ページヘッダ 96 バイト、ページ末尾から逆向きにスロット配列（2 バイト/スロット）
- 固定位置のページ: 0 = ファイルヘッダ、1 = PFS、2 = GAM、3 = SGAM、6 = DCM、7 = BCM、**9 = ブートページ（プライマリファイルのみ）**
- ブートページの DBINFO に、DB 名・照合順序・作成した SQL Server の内部バージョン・`sysallocunits` の先頭ページ位置が入る

レコード形式:

```
statusA(1) statusB(1) 固定長データ終端オフセット(2)
固定長データ
列数(2) NULL ビットマップ(ceil(n/8))
可変長列数(2) 可変長列オフセット配列(2×n) 可変長データ
```

### 2.2 メタデータ取得のブートストラップ

ユーザー表の一覧を得るには、システム基底表を順に読む必要がある。

1. ブートページ (page 9) → `sysallocunits` の先頭ページ
2. `sysallocunits` (objid 7) → アロケーションユニットと先頭ページ / IAM ページ
3. `sysrowsets` (objid 5) → rowset ↔ object_id / index_id の対応
4. `sysschobjs` (objid 34) → object_id・表名・スキーマ・種別（`U` = ユーザー表）
5. `syscolpars` (objid 41) → 列定義（colid・名前・型・長さ・精度）
6. `sysscalartypes` (objid 50) → 型名の解決

**これらシステム基底表自身のスキーマはハードコードするしかなく、SQL Server のバージョンごとに列構成が違う**。ここが実装の一番の泥臭い部分になる。

内部バージョン（ブートページから読める）の既知の対応:

| SQL Server | 内部バージョン |
|---|---|
| 2005 | 611 / 612 |
| 2008 | 655 |
| 2008 R2 | 660 / 661 |
| 2012 | 706 |
| 2014 | 782 |
| 2016 | 852 |

2017 以降の値は公開情報での確認が取れなかったため、実ファイルでの採取が必要。

### 2.3 データ型

int / bigint / smallint / tinyint / bit / decimal / numeric / money / smallmoney / float / real / datetime / smalldatetime / date / time / datetime2 / datetimeoffset / char / varchar / nchar / nvarchar / binary / varbinary / uniqueidentifier / text / ntext / image / xml / sql_variant など **40 種弱**。Oracle 側が 20 種強で実装済みなので、デコーダの物量は既存実装と同等かそれ以上。

照合順序 (collation) → Windows コードページの対応表が新たに必要。ただし変換器そのものは `odv_charset.c` に既にある（SJIS / GBK / Windows-1252 等）ので流用できる。

### 2.4 初版で非対応にすべきもの

TDE 暗号化、行圧縮 / ページ圧縮、columnstore、メモリ最適化テーブル、sparse 列、FILESTREAM。これらは「対応形式・制限事項」ヘルプに明記する。

---

## 3. BAK フォーマットの技術調査

- `.bak` は **MTF (Microsoft Tape Format)** コンテナ。NTBackup と共通の形式で、仕様は公開されている。
- 構造は TAPE ディスクリプタブロック → SSET（バックアップセット）→ 各ブロックがストリームヘッダ + ストリームデータ。SQL Server は MSDA ストリームに **MDF のページイメージをほぼそのまま**格納する。
- したがって **BAK 対応 = MTF コンテナリーダ + MDF ページエンジンの再利用**。MDF を先に作れば追加コストは小さい。
- **逆順では作れない。MDF が先である。**
- MTF ヘッダだけを読めば `RESTORE HEADERONLY` / `RESTORE FILELISTONLY` 相当の情報（DB 名・作成日時・バックアップ種別・構成ファイル一覧）が SQL Server 無しで取れる。

### 非対応にすべきもの、およびその実務上の重さ

`BACKUP ... WITH COMPRESSION`（独自圧縮）、`WITH ENCRYPTION`、ストライプ / 複数メディアファミリ、差分・ログバックアップ。

**圧縮バックアップは実運用で標準的に使われている。** 「.bak 対応」と告知した結果、利用者の手元のファイルがことごとく圧縮済みで開けない、という事態は現実的に起こりうる。告知の書き方とヘルプの制限事項の明示が、機能そのものと同じくらい重要になる。

---

## 4. 法務・ライセンス

### 4.1 OrcaMDF は GPL-3.0 — 参照方法に注意が必要

MDF パーサの事実上唯一の公開実装である [OrcaMDF](https://github.com/improvedk/OrcaMDF)（C#）は **GPL-3.0** である。本製品はプロプライエタリなので、コードの流用は当然できない。それだけでなく、実装を読みながら書くこと自体が派生物性の議論を招く。

したがって:

- **公開文献のみを根拠としたクリーンルーム実装とする。** Microsoft Learn のページ / エクステント アーキテクチャ解説、`DBCC PAGE` の出力、書籍『SQL Server Internals』、Paul Randal のストレージエンジン解説記事など。
- **参照した資料を `AGENTS.md` に記録する。** Oracle の .dmp 実装のときには同種の GPL 実装が存在しなかったため、この論点は本件で新たに生じたものである。

### 4.2 フォーマットのリバースエンジニアリング自体

MDF は非公開仕様だが、これを読むツールは多数が商用販売されており、実務上の法的障害はない。MTF は公開仕様。

### 4.3 商標

Microsoft の商標ガイドラインに従い、「Microsoft SQL Server」を名詞的用法（`for Microsoft SQL Server`）で用い、初出で帰属表示を行う。Oracle 商標と同じ扱いになる。

---

## 5. 市場・プロダクトとしての妥当性（最大の論点）

Oracle .dmp と SQL Server MDF/BAK は、**価値の出方が構造的に違う**。

| | Oracle .dmp | SQL Server .mdf/.bak |
|---|---|---|
| 中身を見る正規の手段 | impdp — Oracle 環境が必要（実質有償・重い） | SSMS で ATTACH / RESTORE — **Express / Developer は無償** |
| 「DB 無しで見る」の価値 | 非常に高い | 相対的に低い |
| 競合 | ほぼ皆無 | **多数**（Aryson / RecoveryTools / Nucleus / Stellar / Kernel / Univik 等。無償を謳うものもある） |

つまり ODV が Oracle 側で持っている「ほぼ唯一の選択肢」というポジションは、SQL Server 側では取れない。

### それでも残る本物の需要

1. **バージョンの壁** — 新しい SQL Server は古すぎる DB を attach / restore できない（SQL Server 2000 以前は現行版では不可、2005 世代も版により不可）。逆方向（新しい .bak を古いサーバへ）も不可。ODV は版に依存せず読める。**ここが最も強い差別化。**
2. **破損・孤立した MDF** — attach できないファイルから中身を取り出す。
3. **SQL Server を導入できない端末** — 受領データの検証、監査、顧客先の PC。
4. **既存の出口との相乗** — CSV / Excel / SQL / ODBC エクスポート、LOB 抽出、12 演算子の検索という既存資産が、そのまま SQL Server 側にも効く。競合の多くは閲覧のみ、またはエクスポートが有償。

### 結論

**やる価値はあるが、Oracle ほどの独占的差別化にはならない。** 同じ 3〜4 か月を Oracle 側の機能改善に充てた場合と比べて、投資対効果が上回る保証はない。したがって「全部作ってから出す」のではなく、**小さく出して反応を見る**進め方を推奨する（§7）。

---

## 6. 工数見積り

| 段階 | 内容 | C 新規行数 | 期間 |
|---|---|---|---|
| Phase 0 | 形式判定のみ。`.mdf`/`.bak` を認識し、DB 名・作成バージョン・照合順序・構成ファイル一覧を表示（表は開けない） | 400–600 | 2–3 日 |
| Phase 1 | MDF メタデータ。システム基底表のブートストラップ → 表一覧・列定義・行数 | 1,500–2,000 | 2–3 週 |
| Phase 2 | MDF 行データ。heap / clustered 走査、主要 25 型のデコード、照合順序 → コードページ | 1,500–2,500 | 3–4 週 |
| Phase 3 | BAK (MTF) コンテナ → Phase 1/2 へ委譲 | 800–1,200 | 1–2 週 |
| Phase 4 | LOB (text/image/row-overflow)、CSV/SQL/Excel 出口の接続 | 800–1,200 | 2–3 週 |
| 付随 | `ODV_MAX_TABLES` 動的化、ページキャッシュ、ヘルプ 10 言語、Strings 10 言語、インストーラ関連付け、テスト基盤 | — | 2–3 週 |

**合計 5,000–7,500 行の C、実働 3〜4 か月。** 既存の Oracle 実装が 8,945 行であることを踏まえた規模感である。

### テスト基盤

`odv-testdump` と同じ発想で、Docker + `mcr.microsoft.com/mssql/server` から SQL Server 2017 / 2019 / 2022 を立て、全データ型を含む DB の MDF / BAK を自動生成する `odv-testmssql` 相当が要る。

**ただし、古い版（2000 / 2005 / 2008）の MDF は Docker では作れない。** 実機または既存検体の入手が別途必要であり、**最大の差別化要因である「版の壁」の検証が、最も手当てしにくい**という構造になっている。ここは着手前に検体入手の目処を立てておくべき。

### 本リポジトリの開発環境について（確認済み）

`Logics/DumpParser/Makefile` により、**C ライブラリは Linux / gcc でそのままビルドが通る**（本調査で確認済み）。新規パーサモジュールの単体テストは Windows / MSVC を待たずに回せる。CI に Linux ビルド + テストのジョブを足す価値がある。

---

## 7. 推奨する進め方

**Phase 0 を単独で先にリリースする。**

- 2〜3 日で出せる。「`.mdf` / `.bak` をドロップすると、SQL Server 無しで DB 名・作成バージョン・照合順序・ファイル構成が判る」＝ `RESTORE HEADERONLY` / `FILELISTONLY` 相当で、単体で使い道がある（受領した `.bak` が何版のどの DB か、SQL Server を立てずに判定できる）。
- ページ / レコード構造の読み取りコードがそのまま Phase 1 の土台になり、捨てにならない。
- 実利用者の反応を見てから Phase 1 以降の投資を判断できる。3〜4 か月を先に払うより桁違いに安全。

Phase 0 の段階ではヘルプ・リリースノートに「実験的機能」と明記し、名称は据え置く（§8 参照）。

---

## 8. 名称変更「Open DUMP Viewer for Oracle **D**atabase」のコスト

### 8.1 まず事実確認

現行の表示名は既に **`Open DUMP Viewer for Oracle database`**（`database` が小文字）である。ご指定の `Open DUMP Viewer for Oracle Database`（`Database` が大文字）は**リポジトリ内に 1 件も存在しない**。したがって本件は「`d` → `D` の大文字化」である。

これは単なる表記ゆれの是正ではなく根拠がある。**Oracle の製品正式名称は "Oracle Database"（D は大文字）**であり、Oracle の商標使用ガイドラインは商標を Oracle の表記どおりに用いることを求める。`AGENTS.md` が記録している「OraDB DUMP Viewer からの改称」と同じ商標コンプライアンス上の是正にあたる。

### 8.2 実測コスト

**出現箇所: 310 箇所 / 193 ファイル**

| 面 | ファイル | 箇所 | 性質 |
|---|---:|---:|---|
| Help HTML (10 言語) | 144 | 204 | 機械置換。全言語とも英語表記のまま |
| `Strings.*.resx` (10 言語) | 10 | 40 | 機械置換。4 キー × 10 言語 (`Status_Unlicensed` / `Status_Licensed` / `About_UpdateBatchMessage` / `Help_FormTitle`) |
| ルート文書 | 9 | 27 | README 7 / AGENTS 7 / CLA 3 / EULA 3 / readme.txt 3 / CHANGELOG 1 / SECURITY 1 / LICENSE 1 / THIRD-PARTY 1 |
| C ソース | 16 | 18 | すべてファイル先頭コメント。機能影響ゼロ |
| VB.NET | 6 | 8 | **うち 6 箇所がハードコードのユーザー可視文字列** |
| installer | 3 | 7 | `setup.iss` の `MyAppName`、MSIX の `DisplayName` |
| chocolatey | 2 | 3 | nuspec の title / description |
| GitHub Actions | 3 | 3 | winget の `PackageName`（正式 / Beta）、`cla.yml` のコメント |

**約 93%（289/310）は機械置換で完了する。** 手当てが要るのは VB.NET のハードコード箇所である。

```
HMI/Open DUMP Viewer/AboutDialog.Designer.vb:75      lblProductName.Text
HMI/Open DUMP Viewer/SplashScreen.Designer.vb:47     lblAppName.Text
HMI/Open DUMP Viewer/Open DUMP Viewer.Designer.vb:507 ToolStripStatusLabel.Text
HMI/Open DUMP Viewer/Open DUMP Viewer.Designer.vb:614 フォーム Text
HMI/Open DUMP Viewer/Open DUMP Viewer.vb:21 / :564   実行時のフォーム Text
Logics/Export/SqlExportLogic.vb:71                   SQL 出力の先頭コメント
```

製品名が Designer に直書きされており、`Strings.resx` に集約されていない。**大文字化のついでに `Loc.S("App_ProductName")` へ集約しておくと、次に名称を触るとき（§8.4）のコストが 6 箇所 → 1 箇所になる。**

### 8.3 変わらないもの（重要）

識別子は表示名と分離されているため、大文字化しても以下は**一切変わらない**。既存利用者の更新経路は壊れない。

- Inno Setup AppId (`25f04e6a…` / Beta `2e7f53d3…`) — 既存インストールの上書き更新
- winget `PackageIdentifier` = `OpenDumpViewer.OpenDumpViewer` — winget 経由の更新
- MSIX Identity Name (`vars.MSIX_IDENTITY_NAME`) — Store のパッケージ同一性
- インストール先 `Program Files\Open DUMP Viewer`、EXE 名 `Open DUMP Viewer.exe`
- `RootNamespace` = `Open_DUMP_Viewer`、ライセンス保存先 `%APPDATA%\OpenDUMPViewer`
- ファイル関連付けの ProgID `OpenDumpViewer.dmp`
- コード署名の発行者名（「柳井建人」。製品名と無関係）

### 8.4 リポジトリ外に波及する作業

| 対象 | 内容 |
|---|---|
| winget | `PackageName` の変更。次回リリースの自動 PR に乗るが、winget-pkgs 側で意図を問われることがある |
| Microsoft Store | リスティング表示名の変更 → **再審査が必要**（数日） |
| Chocolatey | nuspec title の変更 → 次回提出時にモデレーション再通過 |
| odv.dev サイト・ブログ 9 本 | OGP / SEO タイトル。実作業の主。大文字化のみなら検索順位への影響は実質ゼロ |
| `docs/screenshots/*.png` (4 枚) | タイトルバーに旧表記が写っている。撮り直しが望ましい |

### 8.5 見積り

- **リポジトリ内: 0.5 日**（置換 + Designer の確認 + ビルド確認。`App_ProductName` への集約を含めて 1 日）
- **リポジトリ外: 1〜2 日**（Store 再審査の待ち時間は別途数日）
- スクリーンショット撮り直しを含めて **合計 2〜3 日**

### 8.6 ただし、SQL Server 対応を入れるなら今やるべきではない

Oracle 専用名を丁寧にする作業と、Oracle 専用名から離れる作業は逆方向である。両方やると同じ 193 ファイルを 2 回触ることになる。

SQL Server 対応時の名称の選択肢:

| 案 | 名称 | 影響 |
|---|---|---|
| **A. 中立化** | `Open DUMP Viewer`（対応 DB はサブタイトルで列挙） | 193 ファイル × 1 回。Oracle 系検索流入の資産を一部失う。商標リスクは最小 |
| B. 併記 | `Open DUMP Viewer for Oracle Database & SQL Server` | 長すぎる。Store / winget の表示で切れる。商標を 2 社分背負う |
| C. 別 SKU | Oracle 版は据え置き、`Open DUMP Viewer for SQL Server` を別 AppId / 別 winget ID で配布（コードベースは共通） | 名称変更コスト 0。ただし配布・ライセンス・サポートが 2 本になる恒常コスト |
| D. 据え置き | 名称は Oracle のまま、SQL Server 対応は実験的機能扱い | 誤認を招く。Store 審査でリスティングと機能の不一致を指摘されうる |

**推奨**: Phase 0 の間は **D**（実験的機能と明記して据え置き）で走らせ、Phase 1 で表データが読めるようになった時点で **A** へ一度だけ改称する。大文字化は A の作業に自然に吸収される（`for Oracle Database` という表記自体が無くなるため）。

**SQL Server 対応を見送る場合は、大文字化を単独で実施する価値がある**（商標コンプライアンス、2〜3 日）。

---

## 参考資料

- [OrcaMDF (improvedk/OrcaMDF) — GPL-3.0](https://github.com/improvedk/OrcaMDF)
- [Microsoft Tape Format — Wikipedia](https://en.wikipedia.org/wiki/Microsoft_Tape_Format)
- [Media Sets, Media Families, and Backup Sets — Microsoft Learn](https://learn.microsoft.com/en-us/sql/relational-databases/backup-restore/media-sets-media-families-and-backup-sets-sql-server)
- [View or Change the Compatibility Level of a Database — Microsoft Learn](https://learn.microsoft.com/en-us/sql/relational-databases/databases/view-or-change-the-compatibility-level-of-a-database)
- [SQL Server Internal Database Versions and Compatibility Levels](https://sqlserverbuilds.blogspot.com/2014/01/sql-server-internal-database-versions.html)
