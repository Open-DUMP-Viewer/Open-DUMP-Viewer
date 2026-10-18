# NOTES_g13b — 群13b (VB 経路のエクスポート: M-14 / M-32 + VB に関わる引継ぎ)

作業ツリー: /home/user/Open-DUMP-Viewer/.claude/worktrees/agent-aa2dbe1d0a32d4f2c
(ブランチ worktree-agent-aa2dbe1d0a32d4f2c。開始時 f8e13cb → `git reset --hard e9b302b`)
R = /tmp/claude-0/-home-user-Open-DUMP-Viewer/6f7e1e96-1edb-5288-8b7c-135e457959e9/scratchpad/odvtest
作業用: $R/fix/g13bwork/
参照する DLL 側の実装: 群13a の作業ツリー /home/user/Open-DUMP-Viewer/.claude/worktrees/agent-a277fb330d5932f3f
(未コミット。odv_sql.c の map_type / put_value / render_constraint を VB に写す)

## 進捗
- [x] 読む: WORKER_GUIDE_M / TYPE_MAPPING_SPEC / brief_g13 (M-14, M-32, M-15/16/18/19/28 の VB 部分) /
      brief_g13_extra / triage g13 / NOTES_g9,g10,g12b,g13a,g14
- [x] 設計 (下)
- [x] 実装
- [x] VB 実行ハーネス (net10.0 コンソール + 実ソース + Linux の DLL)
- [x] DLL 経路と VB 経路の一致
- [x] Oracle への往復 (試験ユーザー ODV_C_G13B / ODV_S_G13B は 11/21/23 とも削除済み)
- [x] dotnet build 0/0、コミット

## 調べて分かったこと
- TablePreview の GetExportContext は DumpFilePath / DataOffset / Partition / DataOffsets / ConstraintsJson を
  持たない → TablePreview からの SQL は制約・索引・コメントを一切出していなかった (WriteConstraints は
  SqlInferInteger=True の Workspace 経路でだけ動いていた)
- プレビューの値は表示用: 日付は利用者の日付書式 (既定 SLASH)、LOB はプレビュー (expdp は先頭 4096 バイト、
  BLOB は 2048 バイト = 16 進 4096 字)、TSTZ は「現地時刻 [地域] 時差」の表示文字列、exp の RAW は 0x 付き
- DLL の全量 (full_values)・TSTZ の生値 (tz_raw_values) は DLL 内部の旗で、公開 API からは要求できない
- 群13a の expdp の制約・DEFAULT・コメント (TABLE_T) は SQL 出力のとき (want_ddl_meta) だけ読まれ、一覧 (list_only) の
  constraints_json には入らない → VB の書き手は expdp の制約を出せない (DLL 経路との差として残る)。
  exp の unique_index (制約でない一意の索引)・主キーだけの NOT NULL (not_null_pk) も JSON に無い
- 10/18 統括の連絡: 仕様 v2 (fix/g13bwork/spec_v2.md に写し)。差分は 0x00 の扱いだけ:
  PostgreSQL は 0x00 を除いて行の NOTE「N byte(s) 0x00 removed from the value (PostgreSQL text cannot hold U+0000)」、
  MySQL '\0'、SQL Server CONCAT(CAST(N'..' AS NVARCHAR(MAX)), NCHAR(0), N'..')
- 群13a の odv_sql.c の写し: fix/g13bwork/odv_sql_g13a_0434.c (04:34 版。長さ付きコールバック対応済み)

## 設計 (採用)
1. VB の書き手 (SqlExportLogic.ExportFromData) を群13a の odv_sql.c に合わせて書き直す (型 = map_type、値 = put_value、
   制約 = render_constraint、並び・ファイル末尾の DDL・FK、DROP、ファイル先頭、NOTE の文言まで同じ)。
   VB は型の文字列しか持たないので、列の種類は型名から決める (DLL は解析器の型コード)
2. 値は「SQL 用の固定の日付書式 (SLASH)」で解析し直したものを使う (公開 API odv_set_date_format で足りる)。
   プレビューを開いた時点の日付書式が SLASH ならプレビューの値をそのまま使う
3. TablePreview で絞り込みが無いときの SQL / CSV は DLL の経路で出す (同じ表は同じ出力になる)。絞り込みがあるときと
   SqlInferInteger のときだけ VB の書き手 (統括の判断事項として報告)
4. VB の書き手に残る限界: LOB はプレビュー (expdp は先頭 4096 バイト) しか持たない → 切れている可能性がある値は
   NULL + 行の NOTE (黙って切った値を書かない)。全体を得るには DLL に full_values を公開する API が要る (C の変更、報告)

## 実装 1 回目 (dotnet build 0/0)
- 新規 Logics/Export/SqlDialect.vb: 型名の読み取り (ParseOraType / OraTypeCanon / KindOf)、型の写像 MapColumnType
  (= odv_sql.c map_type)、リテラル (OraPiece / OraText / OraClobConcat / OtherText)、TranslateExpr / PutWrapped /
  StripOuterParens / TrimParenWs / DefaultPortable、数値の判定、日付の読み取りと Oracle の暦 (TSTZ の UTC 換算)、SqlText (行のバイト数)
- Logics/Export/SqlExportLogic.vb: ExportFromData を SqlTableWriter (= write_sql_file の 1 表分) に置き換え
- Logics/Export/ExportHelper.vb: EscapeSqlString / MapOracleType / IsNumeric* は SqlDialect に委ねる。InferIntegerTypes は
  書き出し先の整数型を返す (Oracle 方言は推定しない)
- Logics/Workspace/OraDB_NativeParser.vb: ParseDump に sqlDateFormat (SLASH に固定) を追加
- 試験: fix/g13bwork/vbh (net10.0 + 実ソース)、mklib.sh (C を写して試験用の UTF-16/Latin-1→UTF-8 変換 shim_charset.py を
  足し Linux の .so に。Linux 版は文字コード変換が無く NCHAR が UTF-16 のまま届くため)、pair.sh (DLL の経路と VB の書き手の
  表ごとの SQL を作って diff)。DLL は群13a の作業ツリーの C の写し (fix/g13bwork/src_g13a、04:50 時点) を両方に使う
- 結果 (1 回目): dt 21 e / 11 e、num 21 e、chr 21 e は 4 方言とも DLL とバイト一致。差は (a) expdp の制約・DEFAULT・主キーだけの
  NOT NULL (一覧に無い)、(b) exp の CLOB はプレビューでは %CLOB% (VB は NULL + NOTE、DLL は全体)

## 実装 2 回目 (dotnet build 0/0)
- TablePreview: 表の U+0000 を ␀ で表示 (ForDisplay。CLOB のポップアップも)。Workspace の表の情報 (sourceContext) と
  解析した日付書式を受け取る。GetExportContext にダンプのパス・位置・区画・制約の JSON。ShowsAllRows、GetRowsInSqlDateFormat
- ExportHelper.RowsInSqlDateFormat: 表示用の書式で開いた行を、SLASH で解析し直した同じ位置の行に (参照で位置を引く。並べ替えの順を保つ)
- メイン画面: TablePreview の SQL は絞り込みが無く整数推定もなければ DLL の経路、絞り込みがあれば VB の書き手 (SLASH の行)。
  CSV は絞り込みがあるときだけ VB。Excel・ODBC は SLASH の行 (単表・一括)。Excel / Access / SQL Server / ODBC / 整数推定の
  ParseDump に RowCount と Partition も渡す (区画の子行で表全体を読んでいた)
- Excel: 列の型でセルの型を決める (数値の列だけ数値、15 桁超は文字列。DATE/TIMESTAMP は 1900-03-01 以降でミリ秒までなら日時、
  それ以外は文字列。印・プレビューで切れた LOB は空のセル + メモ。32767 字超は空 + メモ)。一括の Excel も同じ書き手
- ODBC: FLOAT・BINARY_* は DOUBLE (FLOAT が VARCHAR(4000) だった)、NUMBER(p,s) は DECIMAL(p,s) (SQL Server と同じ写し)、
  精度なし NUMBER は DOUBLE のまま、DATE/TIMESTAMP は DATETIME で値は DateTime、TSTZ 等は VARCHAR(4000)。印は NULL
- CSV (VB): 行の区切り LF、型の行をヘッダと独立に、印・プレビューで切れた LOB は空欄 + <出力>.notes.txt (DLL と同じ文言)
- 確認: vbh preview (TablePreview の流れ: 日付書式 0/1/3 で開き、偶数行を逆順に絞り込み → SQL 用に解析し直し → 4 方言)。
  dt 21 e T_TS / T_DATE で解析し直した行が SLASH で解析した行と一致 (mismatch 0)、INSERT と行の NOTE はすべて DLL の出力に含まれる

## 確認の結果 (DLL = 群13a の C の写し fix/g13bwork/src_g13a (odv_sql.c sha256 5b5ed969…、05:20 に再確認して変更なし)、試験用 shim 付き Linux .so、-fwrapv)
- 試験用 DLL の注意: -fwrapv を付けないと負の INTERVAL の復号が化ける (odv_datetime.c の符号付きの桁あふれ。群13a の
  ビルドは -fwrapv)。mklib.sh に付けた
- pair (fix/g13bwork/pB (最終。1 回目の pA と分類が同じことを確認して pA は消した)、26 ダンプ × 表 × 4 方言 = 1,019 ファイル、classify.py で分類):
  - バイト一致: dt 21 e / 11 e、num 21 e、chr 21 e、chr/long 21 e / dp、m_ddl 21 e、m_varr 21 e、shape/cols 21 e (1000 列) など
  - 違いは 4 種類だけ (どれも VB の経路が DLL から受け取るデータの違い。書き手の違いは無い):
    (a) expdp の制約・DEFAULT・主キーだけの NOT NULL (tail / coldef。一覧の JSON に無い。群13a は SQL 出力のときだけ読む)
    (b) LOB: exp はプレビューで %CLOB% / %BLOB% (VB は NULL + NOTE)、expdp は先頭 4096 バイト (切れているかもしれない値は
        NULL + NOTE)。ODV_TEST_FULL (試験用に odv_parse_dump でも全体を渡す) では lob/main・output/main・case_v・smoke・
        m_new・g9_types の値が DLL とバイト一致 = 書き手は同じ
    (c) exp の CREATE UNIQUE INDEX (制約でない一意の索引): 一覧の JSON に unique_index が無いので VB は UNIQUE 制約にする
        (m_ddl2 D_PKIDX、Oracle で ORA-02261)。JSON に "unique_index":1 を足せば VB も DLL と同じ (ODV_TEST_UIDX で確認済み。
        VB はこの欄を読むようにしてある)
    (d) m_long C_LDEF: DLL の表の NOTE (note_col) は 511 バイトで切れる (長い DEFAULT の式が途中で終わり、"is an Oracle
        expression and was not written" が消える)。VB は全文。→ 群13a に報告 (note_col の 512 バイトの msg)
- Oracle への往復 (VB の書き手の出力をつないで SQL*Plus でそのまま、sqp_rt.py / mcmp.py。RT ユーザー ODV_C_G13B / ODV_S_G13B):
  | ダンプ | 結果 |
  |---|---|
  | dt 21 e | SQL*Plus エラー 0、7 表一致 (INTERVAL は mcmp で一致)。M-14 (T_DATE 29 行・T_TS 13 行) と M-32 (DEFAULT 'Z' NOT NULL) が直った |
  | dt 11 e (11.2) | エラー 0、7 表一致 (INTERVAL は mcmp) |
  | dt 23 dp (23) | エラー 0、7 表の行はすべて一致。主キー・DEFAULT が無い ((a)) |
  | chr/main 21 e | エラー 0、5 表一致 (0x00・改行・引用符の値) |
  | num/main 21 e | エラー 0。差は 1E-130 付近が 0 になる NUMBER の復号 (DLL と同じ、範囲外) と sql_roundtrip の表示 '#######' |
  | m_ddl 21 e | エラー 0、12 表一致 (制約・索引・コメントまで) |
  | m_ddl2 21 e | ORA-02261 1 件 (D_PKIDX、(c))。他 7 表一致 |
  | output/main 21 e | T_BLOB / T_CLOB が ORA-01400 ((b): NOT NULL の LOB 列に NULL)。full なら 9 表一致 |
  | smoke 21 e full / lob/main 21 e full | エラー 0、2 表 / 23 表一致 |
- 他方言の構文 (syncheck.sh: PostgreSQL は pglast = libpg_query、MySQL / SQL Server は sqlglot = 群13a の glotcheck.py):
  pA (= 最終の pB) の VB の出力 209 + 209 + 209 ファイルすべて構文エラー 0
- TablePreview の流れ (vbh preview): 日付書式 0/1/3 で開いて絞り込み・逆順 → 解析し直した行は SLASH で解析した同じ行と一致。
  INSERT と行の NOTE はすべて DLL の出力に含まれる。CSV は表示の書式のまま、LOB の切れた値は空欄 + notes.txt。
  Excel: VARCHAR2 の '00123' は文字列 (以前は数値 123)、NUMBER は数値、DATE/TIMESTAMP(≤3 桁) は日時、TIMESTAMP(4〜9) は文字列
- 整数推定 (ODV_TEST_INFER): Oracle 方言は型を変えない (以前は NUMBER(4) にして 10000 以上が入らなかった)。他方言は
  SMALLINT / INTEGER(INT) / BIGINT / NUMERIC(38,0)(DECIMAL)。推定した列には精度なし NUMBER の NOTE を書かない
- ODBC (vbh odbc): FLOAT → DOUBLE、NUMBER(p,s) → DECIMAL(p,s)、DATE/TIMESTAMP → DATETIME (値は DateTime、7 桁まで)、
  TSTZ / INTERVAL → VARCHAR(4000)

## 追加 (確認の後)
- SQL Server の USE [DB]: 以前は VB の書き手だけが書き、DLL の経路 (Workspace・一括) は無視していた。絞り込みの無い
  TablePreview を DLL の経路にしたので、SqlExportLogic.ExportFromDump でも DLL の出力の前に USE [DB] / GO を付ける
  (一時ファイル <出力>.part に書いてから写す)。vbh dll (ODV_TEST_DBNAME) で確認
- Excel のメモの作成者を "Open DUMP Viewer" に (既定は OS のユーザー名がファイルに残る)

## 統括への判断事項・報告 (選択肢つき)
1. TablePreview の SQL / CSV で絞り込みが無いときは DLL の経路で出す (案 A、採用)。案 B: 常に VB の書き手 (expdp の制約・
   DEFAULT・コメント、exp の LOB、長い LOB が DLL と違う)。採用理由: 表を開いてから出すふつうの手順で Workspace と同じ出力
2. VB の書き手が DLL と完全に同じになるのに要る C の変更 (群13a / 統括へ。VB 側は受け取る準備済み、または数行):
   (a) 一覧の constraints_json に "unique_index":1 (制約でない一意の索引)。VB は既に読む
   (b) 行の値を SQL 出力と同じ形で受け取る口: 例 odv_set_value_mode(s, ODV_VALUES_EXPORT) = full_values (LOB / LONG の全体) と
       印の旗。今は VB の経路は LOB がプレビュー (exp は %CLOB%) なので NULL + NOTE にしている (切れた値は書かない)
   (c) expdp の制約・DEFAULT・コメント (TABLE_T) を一覧 (list_only) でも constraints_json / col_defaults に入れる、
       主キーだけの NOT NULL を区別できる値 (col_not_nulls に別の値など)
   (d) odv_sql.c の note_col は 512 バイトで NOTE を切る (長い DEFAULT の式で文が途中で終わる)。VB は全文
3. CSV の VB の経路も DLL と同じく、印・切れた LOB は空欄 + <出力>.notes.txt にした (仕様 9 章「VB 経路も同じにするかは統括の判断」)
4. SQL Server 方言のスキーマ: VB の書き手は以前 dbo に置き換えていた。DLL と仕様に合わせて元のスキーマのまま ([S].[T])
5. 整数推定 (SqlInferInteger) は Oracle 方言では型を変えない。他方言は SMALLINT/INTEGER/BIGINT/DECIMAL(38,0) (仕様 2.2
   「整数の型には写さない」はオプション無しのときの規則と解釈。オプションの意味を保つ)
6. Excel: TIMESTAMP の小数秒が 4 桁以上 (0 以外) の値・1900-03-01 より前の日付は日時でなく文字列 (Excel の日時はミリ秒まで・
   1900 年から)。15 桁を超える数値も文字列。ODBC の DATE/TIMESTAMP は DateTime (100 ナノ秒まで。8〜9 桁目は落ちる)
7. ヘルプ (Help/*/export_sql.html) の DBMS 別の説明・LOB の注意は古い (DLL・VB とも)。直していない

## コミット

- 5cfcbbc (worktree-agent-aa2dbe1d0a32d4f2c、e9b302b の上に 1 件。push なし)。直前の dotnet build --no-incremental: 0 warning / 0 error

## 統括からの追加 (10/18 06:20〜): 群13a の C の (a)〜(e) を VB に繋ぐ (5cfcbbc の上)
API の正: 群13a の作業ツリーの odv_api.h と NOTES_g13a「統括からの追加」、仕様 12 章。

### 試験用 DLL の C の写し
- 06:22:20 UTC に群13a の作業ツリー (5fcd3f6 + 未コミット) の Logics/DumpParser/*.c *.h を fix/g13bwork/src_g13a2 へ写した
  (sha256 は src_g13a2/SHA256SUMS。odv_api.c 967b5947…、odv_sql.c 16fbdaf7…、odv_exp.c c5cf9e29…、odv_expdp.c e504b72d…、
  odv_csv.c 850bea46… (CRLF 済み)、odv_record.c 64cf06b0…、odv_api.h 7c153603…)。mklib.sh で lib_g13a2 (-fwrapv)。
  ODV_TEST_FULL の細工は不要になった (shim は当たらない = 正規の odv_set_value_mode を使う)

### VB の変更
- (b) Open_NativeParser.ParseDump(exportValues:=True, marks, wantedRows): odv_set_value_mode(ODV_VALUES_EXPORT)。長さ -2 は
  印の文字列を値の欄に置き ValueMarks に Marker、-3 は Nothing + TooLong。ValueMarks は行の配列 (参照) → 列ごとの種類 (印の
  ある行だけ)。wantedRows: 表示中の行だけ値を読み、他は Nothing (番号を保つ。絞り込んだ一部の LOB だけを全体で持つ)
  - ExportHelper.LoadExportRows (全行) / RowsForExport (TablePreview の表示中の行を読み直し、参照で同じ位置の行を取る)
  - 書き手 (SQL・CSV・Excel・ODBC・Access・SQL Server 直接) は marks で印・上限超えを見る。値の形 (%CLOB% など) で推測する
    IsMarkerValue / IsPreviewCut と「プレビューで切れているかもしれない LOB は NULL + NOTE」をやめた (SqlDialect から削除)
  - NOTE の文は DLL と同じ: SQL「the value could not be decoded (<印>); written as NULL」(UDT は "of type … cannot be decoded
    (its type is not in the dump)")、「the value is longer than 512 MiB and was not kept whole; written as NULL」。CSV は
    notes.txt に「the value could not be decoded (<印>)」「the value is longer than the export keeps whole (512 MiB)」
  - 経路: TablePreview の絞り込みのある SQL/CSV、Excel・ODBC (SQL 用の日付)、Access・SQL Server 直接 (利用者の日付書式、
    これまでどおり)、整数推定 (SqlExportLogic.ExportTableViaWriter)、一括 (BulkExportLogic.LoadTableData) をすべて
    エクスポートの値に。TablePreview は常に読み直す (以前は日付書式が SLASH なら読み直さなかった) ので、表を開いたときの
    日付書式を持ち回す引数 (dataDateFormat) は消した。表示 (プレビュー) の経路は変えていない
  - Access / SQL Server 直接は NOTE を書く所が無いので、印・上限超えは NULL (DBNull)
- (c) Open_NativeParser.ReadTableDdl (odv_read_table_ddl。exp は ODV_ERROR_UNSUPPORTED → Nothing で一覧の DDL)。
  ExportHelper.TableDdlForSql(ctx) → TableDdl (NOT NULL 0/1/2・DEFAULT・JSON)。SqlTableWriter は NOT NULL を Integer で持ち、
  2 (主キーだけ) は CREATE TABLE に書かない。ExportFromData は ddl As TableDdl を受け取る (列ごとの 3 引数をまとめた)
- (a) unique_index: VB は前から読む。m_ddl2 D_PKIDX が CREATE UNIQUE INDEX (Oracle で往復一致、下)
- (e) TableEntry.DdlHint (ListTables で odv_get_table_ddl_hint)。Workspace の文脈 (BuildExportContext・GetVisibleTableContexts・
  区画表の親行は区画のどれかのヒント) → TableExportContext.DdlHint → ExportSql と ReadTableDdl の前に odv_set_ddl_hint
  (空なら渡さない)。TablePreview の GetExportContext も元の文脈のヒントを持つ
- CSV (VB) の改行を CRLF に戻した (notes.txt も CRLF、10 万行で打ち切って件数)。値の中の改行はそのまま (引用符の中)
- SQL の NOTE の本文の改行は "--   " で続ける (DLL の put_note と同じ。SqlDialect.PutNote)
- 型の種類: 「型名に引用符を含む → ユーザー定義型」を「先頭が引用符」に (DLL の col_kind と同じ。exp の REF "T" は文字の列扱い)

### 確認 (試験用 DLL = src_g13a2)
- dotnet build (EnableWindowsTargeting) 0 warning / 0 error
- pair (pair.sh、DLL の経路 ExportFromDump (ヒント付き) と VB の書き手 ExportTableViaWriter、4 方言): 63 ダンプ (listF =
  前回の 27 + 区画・制約・ADT・入れ子・XML・オブジェクト表・ANY・ROWID・列・空・型・g9_extlong・finf・lob/mixd・
  chr/names・dt/lobx・output/ident・m_long dp の 36)、1,760 ファイル、異なるのは 4 (= 1 表 × 4 方言) だけ:
  - m_cols 21 e D_FBI: exp の後置の索引 (関数索引 IX_D_FBI_UP・DESC 索引 IX_D_FBI_DESC) の並び。DLL は後置の DDL を表の DDL
    (コメント・NOTE を含む) の後にファイルの順で足す (odv_sql.c sql_late_ddl → tabs[].late)。VB は一覧の JSON (同じ 2 件が
    入っている) を仕様 8.1 どおり種類・名前の順に並べる (DESC → UP)。仕様に合っているのは VB。→ 群13a へ (C の並び)
  - 前回あった差 (a) expdp の制約・DEFAULT、(b) LOB、(c) unique_index、(d) NOTE の 511 バイトはすべて消えた
- ヒントなし (ODV_TEST_NOHINT=1) で DLL の経路・VB の書き手とも、ヒントありと同じ出力 (14 ダンプ・全ファイル)。
  ヒントあり 435 表のうち 434 表にヒント (無いのは入れ子表の格納表 B_NEST_ITEMS)
- 区画の子行 (区画名・その位置) で ReadTableDdl → 表全体と同じ DDL (m_part dp P_RANGE・P_RH の全区画)
- CSV (csvpair.sh: DLL odv_export_csv と VB CsvExportLogic.ExportFromData、全行): 63 ダンプ 440 ファイル (notes.txt 7 を含む)、
  異なるのは 11 で、すべて 0 行の表: DLL は最初の行で見出しを書くので 0 行の表は空のファイル、VB は見出しを書く
  (絞り込みで 0 行になったときに列名が残る方がよいので VB は変えていない。→ 統括の判断)
- TablePreview の流れ (vbh preview、pvrun.sh): 日付書式 0/1/3、絞り込み (偶数番目) + 逆順。読み直した行と印は、
  全行をエクスポートの値で読んだ同じ行と一致 (不一致 0)。表示より長い値 (LOB の全体・小数秒) を持つ: dt T_DATE fmt1 14、
  T_TS fmt1 66、lob/main dp T_B 6・T_C 3・T_LC 1、output T_CLOB 2。印: m_varr B_VARR %VARRAY% (Excel は空のセル + メモ)。
  出力した SQL の全行が DLL の全行の出力に含まれる (4 方言、8 表)。CSV のレコードも DLL の CSV に含まれ、CRLF で終わる
- 他方言の構文 (syncheck.sh pF): PostgreSQL 435/435、MySQL 435/435、SQL Server 435/435 エラー 0
- Oracle への往復 (rtrun.sh = VB の書き手の出力をつないで SQL*Plus、RT ユーザー ODV_C_G13B):
  | ダンプ | 結果 |
  |---|---|
  | output/main 21 e | エラー 0、9 表一致 (以前は T_BLOB / T_CLOB が ORA-01400) |
  | dt 23 dp (23) | エラー 0、7 表一致 (主キー・NOT NULL・DEFAULT も。T_IYM / T_IDS は sql_roundtrip の正規化が元表でも ORA-01877 になるので mcmp.py の MINUS で 8/8・9/9 一致) |
  | m_ddl2 21 e | エラー 0、8 表一致 (D_PKIDX の CREATE UNIQUE INDEX。以前は ORA-02261) |
  | lob/main 21 dp | エラー 0、23 表一致 (expdp の LOB の全体) |
  | output/main 21 dp | エラー 0、8 表一致。T_EMPTY は表のコメントと索引が無い (expdp の圧縮されたメタデータにしか無い。DLL も同じ NOTE。群13a の判断事項 3) |

### 写し直し (最終の確認)
- 06:47:27 UTC に群13a の作業ツリーを fix/g13bwork/src_g13a3 へ写し直した (src_g13a3/SHA256SUMS。06:22 の写しとの違いは
  odv_exp.c f28b2cbe… (06:25: 読み進めた別の表のエラーを last_error に残さない) と odv_sql.c 4ff21d74… (06:33: 表ごとの
  SQL で同じ名前の別スキーマの表を除く) だけ)。lib_g13a3 で pair・CSV・構文・往復・TablePreview をやり直した:
  - pair 63 ダンプ 1,760 ファイル、異なるのは m_cols D_FBI の 4 だけ (上と同じ。出力はすべて 06:22 の写しのときとバイト一致)
  - CSV 440 ファイル、異なるのは 0 行の表の 11 だけ (同上)
  - 構文 PostgreSQL / MySQL / SQL Server 435 ファイルずつエラー 0
  - 往復: output/main 21 e 9 表、dt 23 dp 7 表 (INTERVAL 2 表は mcmp)、m_ddl2 21 e 8 表、lob/main 21 dp 23 表、すべて一致・エラー 0
  - TablePreview の流れ 16 件 (pvrun.sh) 不一致 0、出力した SQL の行は DLL の出力に含まれる
- 往復の試験ユーザー ODV_C_G13B は 11/21/23 とも削除済み
- ParseDump(exportValues:=True) で marks を渡さないと ArgumentNullException (印を値と見分けられなくなるのを防ぐ)

### 統括・群13a への報告 (この回)
1. (C、群13a へ) exp の後置の索引 (関数索引・DESC 索引) の並び: DLL は表の DDL (コメント・NOTE まで) の後にファイルの順で
   足す (odv_sql.c sql_late_ddl → tabs[].late → write_tail)。仕様 3.2 (索引 → コメント)・8.1 (同じ種類は名前の順) と違い、
   VB の書き手 (一覧の JSON を仕様どおりに並べる) と m_cols 21 e D_FBI で違う。案 A: late の索引も render_table_ddl の並び
   に入れる (一覧と同じく表の制約に足してから並べる)。案 B: 仕様を「exp の後置の索引はコメントの後にダンプの順」に直し、
   VB もそうする (VB は JSON のどれが後置かを知らないので、JSON に印が要る)。案 A を勧める
2. (C、統括の判断) CSV の 0 行の表: DLL は最初の行で見出しを書くので空のファイル (見出しも無い)。VB は見出しを書く。
   0 行の表の CSV に列名が無いのは不便なので DLL も見出しを書く方がよいと考える (VB は変えていない)
3. Access / SQL Server 直接は NOTE を書く所が無いので、印・上限超えの値は NULL。日付は利用者の日付書式のまま (従来どおり)
4. TablePreview から VB の書き手で出すときは常に表を読み直す (表示中の行だけ値を持つ)。ダンプのファイルが動かされて
   いると、以前は SLASH の日付なら出せたのが、今はエラーになる (DLL の経路と同じ)
- コミット 0105d8b (5cfcbbc の上に 1 件、仮メッセージ。push なし)。直前の dotnet build --no-incremental: 0 warning / 0 error
