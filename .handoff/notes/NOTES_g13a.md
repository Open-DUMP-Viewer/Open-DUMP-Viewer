# NOTES_g13a — 群13a (DLL 側の SQL・CSV 出力: M-12/13/15/16/17/18/19/28 + 引継ぎ)

作業ツリー: /home/user/Open-DUMP-Viewer/.claude/worktrees/agent-a277fb330d5932f3f
(ブランチ worktree-agent-a277fb330d5932f3f。開始時 f8e13cb (古い main) → `git reset --hard 67133b1`)
R = /tmp/claude-0/-home-user-Open-DUMP-Viewer/6f7e1e96-1edb-5288-8b7c-135e457959e9/scratchpad/odvtest
タグ: 候補 g13a (fix/bin_g13a)、基準 g13a0 (67133b1 を git archive → fix/g13awork/src0、fix/bin_g13a0)
作業用: $R/fix/g13awork/ (runsq.py = コンテナの sqlplus でファイルをそのまま流す、mxml.py = マスター表の XML_CLOB を出す、x/ = 実験)
比較ツールの写し: $R/fix/compare_g13a.py (基準・候補とも ODV_COMPARE で同じ写しを使う)
実験用の DB ユーザー: ODV_G13A_X (11/21/23)。最後に消す

## ★ 型の対応表の仕様: fix/TYPE_MAPPING_SPEC.md を書いた (10/18 02:30、実装に合わせて 10/18 夕方まで更新済み:
##   0x00 の扱い (利用者の決定)・制約の並び・一意の索引・長い DDL のリテラル分割・BINARY_FLOAT の接尾辞)。群13b はこれに従う。
   (統括へ: 仕様の判断を含む点 — SQL Server の VARCHAR2(n CHAR) を NVARCHAR(2n) に、MySQL/SQL Server の精度なし
    NUMBER を DECIMAL(65,30) / DECIMAL(38,10) + NOTE に、FLOAT を DOUBLE / FLOAT(53) に、小数秒は切り捨て + NOTE、
    TSTZ は MySQL では UTC、CSV の「値を作れないもの」は空欄 + <csv>.notes.txt、expdp のコメントと索引は
    圧縮されたメタデータ (KGC) にしか無い — 下の「expdp のメタデータ」)

## 進捗
- [x] 読む: WORKER_GUIDE_M / brief_g13 / brief_g13_extra / triage g13 / NOTES_g8〜g14
- [x] 基準ビルド bin_g13a0 (警告 win 0 / native 5 / asan 1)
- [x] fix/TYPE_MAPPING_SPEC.md
- [x] 実装 1 回目 (10/18 03:40、未コミット。ビルド bin_g13a は警告 win 0 / native 5 / asan 1 = 基準と同じ)
  - odv_sql.c: 全面書き直し (型の写像 = 仕様 2 章、値 = 4 章、長い値の分割・PL/SQL、NOTE、制約・索引・コメントを
    ファイル末尾へ (FK は最後)、表ごとのハッシュ、INSERT 前置きは可変長 (M-28)、識別子の引用符の二重化、
    日付は SLASH に固定して SYYYY で書く (M-13)、MySQL の \ を \\ (M-16)、SQL Server の N'')
  - odv_csv.c: (ptr,len) で書く、full_values、印は空欄 + <csv>.notes.txt
  - odv_types.h: ODV_VALUE.flags (ODV_VFLAG_MARKER / CUT)、session.full_values / want_ddl_meta / ddl_meta / late_ddl
  - odv_record.c: set_value_marker、flags の初期化
  - odv_expdp.c: full_values の LOB 全体 (decode_lob_full)・LONG/本体の上限を外す、印に旗、TABLE_T から制約・
    NOT NULL・DEFAULT (meta_*、master_read_aside で先読み)、平文の COMMENT_T/INDEX_T、KGC は NOTE、
    s->table の memset 前に odv_table_free_text
  - odv_exp.c: full_values の LOB 全体 (read_one_lob_column → exp_lob_finish)、LONG の上限、印に旗、
    後置の FK・索引を s->late_ddl へ (EXP_TGT_CB)、表ごとの SQL は対象の表の後も末尾まで読む (tail_scan)
  - odv_datetime.c: 紀元前の年の復号 (-4712 が -4688 になっていた)
  - odv_tz.c: odv_tz_sql_parts (他方言の TSTZ/TSLTZ)
  - odv_api.c: odv_table_add_constraint
- [ ] 実装の確認 (smoke 21 e/dp は 4 方言とも見た目 OK。exp と expdp の差は型の表記と KGC の NOTE だけ)
  - 10/18 追加: Oracle の型は exp/expdp で同じ表記に (ora_type_canon。NUMBER(10, 0)→NUMBER(10)、TIMESTAMP (6)→
    TIMESTAMP(6))。EMPTY_CLOB()/EMPTY_BLOB() を NULL と区別 (set_value_empty。exp は LOB 区画が空、expdp は列長 0x00。
    行コールバックには "" で渡るので、SQL 側は s->record.values[i].is_null を見る)
  - lob/main: exp と expdp の SQL が 4 方言とも一致 (diff 無し)。CSV の差は RAW の表示 (exp は 0x 付き、expdp は無し)
    だけで、これは基準からの表示の差 (VB 経路と表示を揃えるため CSV では変えない)
  - lob/main を SQL*Plus でそのまま流す (fix/g13awork/sqp_rt.py: スキーマ名だけ書き換え、sqlplus に標準入力で渡し、
    sql_roundtrip.compare_tables で比べる): 21 e / 11 ed / 23 dp とも SQL*Plus のエラー 0 行、23 表すべて一致
    (EMPTY_CLOB の行も一致)。LONG・LONG RAW (sql_roundtrip は比べない) は fix/g13awork/longcmp.py
    (TO_LOB で写して DBMS_LOB.COMPARE) で 11 列すべて一致 (わざと 1 行変えると 1 件出ることも確認)
  - 10/18 午後: 往復 (SQL*Plus でそのまま流す) で見つけて直したもの
    - exp の CREATE UNIQUE INDEX (制約でない一意の索引) が UNIQUE 制約になり、同じ列の主キーと ORA-02261
      (m_ddl2 D_PKIDX) → ODV_CONSTRAINT.unique_index を足し、SQL は CREATE UNIQUE INDEX を制約より前に書く。
      exp で後から ALTER TABLE ADD CONSTRAINT 同名 UNIQUE が来たら制約に戻す (m_ddl の FK 先の UQ が ORA-02270 に
      なったのをこれで直した)。expdp の INDEX_T (平文のとき) も PROPERTY の bit0 で同じ扱い
    - expdp の CHECK の条件は外側の括弧が無い: `(A) OR (B)` に CHECK を直に付けて ORA-01735 (m_ddl2 C_CHKP)
      → 全体を囲む括弧を 1 組外してから必ず CHECK (...) にする (exp・expdp とも同じ SQL)
    - expdp で主キーの列に NOT NULL が付いて、往復後に NOT NULL 制約が 1 つ増える → TABLE_T の列の <CON> (CONTYPE 7)
      が無い主キー列は ODV_COLUMN.not_null_pk = 1 にして SQL では書かない (表示・tables.json の not_null は変えない)
    - 索引の列の並び: exp の `("A" , "B" )` を詰めて expdp と同じに
    - BINARY_FLOAT/DOUBLE の値に f / d の接尾辞 (1.0000000000000001e+300 が NUMBER として ORA-01426。dpopt dpnc T_F)
    - オブジェクト型・VARRAY の列 (K_UDT) は印でなくても NULL + NOTE (exp の VARRAY は生のバイトが値に来て
      文字化けした文字列リテラルになっていた。m_varr B_VARR)
  - 往復の結果 (21c、SQL*Plus でそのまま): m_ddl e 12 表・m_ddl2 e 8 表・dpopt e 6 表・dpopt dpnc 7 表 (コメント・
    索引も一致)・smoke e がすべて一致。expdp (既定の圧縮) は KGC にあるコメント・索引だけが欠ける (NOTE に書いている)
  - さらに直したもの: NUMBER(*,0) を NUMBER(*) に縮めていた (INTEGER が NUMBER になる。num/main T_INT) → * のときは
    ,0 を残す。他方言の CHECK の変換で `J IS JSON` の JSON を列名と見ていた (PostgreSQL で構文エラー。m_new
    D_JSONV) → IS の後は NULL / NOT NULL だけ認める。PostgreSQL の interval は月数が 32 ビット
    (178956970 年 7 か月まで、16 で実測) → 超える INTERVAL YEAR TO MONTH は NULL + 行の NOTE (dt T_IYM の ±999999999-11)
  - 往復 (SQL*Plus でそのまま、すべてエラー 0 行):
    | ケース | 版・形式 | 結果 |
    |---|---|---|
    | lob/main | 21 e/dp, 11 ed, 23 dp | 23 表一致 (EMPTY_CLOB/BLOB、PL/SQL の CLOB/NCLOB/BLOB、LONG/LONG RAW 11 列も longcmp で一致) |
    | verify/output/case_v (M-12) | 21 e/dp | 6 表一致 |
    | dt (M-13) | 11/21/23 × e/dp | 7 表一致 (INTERVAL の 2 表は sql_roundtrip の正規化が元表でも ORA-01877 になるので mcmp.py の MINUS で一致を確認) |
    | dt の日付書式 | 21 e、--date-fmt 1/2/3 (DD/MM/YYYY HH24:MI:SS・YYYY-MM-DDTHH24:MI:SS) | SQL・表ごと SQL が書式 0 と同一 (CSV は書式どおり) |
    | chr/long (M-15) | 11 e, 21 e/dp | 6 表一致 (T_NV2000 は mcmp、T_LONG は longcmp) |
    | chr/main | 21 e/dp | NUL を含む 6 行だけ違う (値が NUL で切れて来る。群12b の長さ付きの値の統合待ち)。実験で、長さを記録の data_len から取る写し (fix/bin_g13anul、作業ツリーには入れない) では 5 表と LONG まで一致 = 書き手の CHR(0) は正しい |
    | num/main (M-18/19) | 21 e/dp | 1E-130 等が 0 になる (NUMBER の復号、表示でも value)、expdp の BINARY_FLOAT の最大値が Inf になる (表示でも value) 以外は一致。どちらも基準から同じで範囲外 |
    | shape/cols (M-28) | 11 e, 21 e/dp | 11 表一致 (1000 列の表も。最長行 2399 バイト) |
    | smoke・meta/m_ddl・m_ddl2・opts/dpopt (e, dpnc)・verify/known_x・output/main・output/ident・chr/names dp (M-17) | 21 | 制約・索引・コメントまで一致。expdp の既定 (圧縮) はコメントと単独の索引だけ欠ける (NOTE) |
    | meta/m_new | 23 dp | 8 表一致 (JSON・BOOLEAN・VECTOR) |
    | meta/g9_types | 23 dp | 型 (VARRAY) を使う 4 表は CREATE TYPE が要るので作れない (表の NOTE どおり)。他の 10 表は一致 |
  - PostgreSQL 16 (コンテナ、tmpfs) に psql でそのまま流す: 上の 36 出力すべて ERROR 0、表ごとの行数 = INSERT 数
    (修正前は dt の interval 範囲外 2 件と m_new の IS JSON 1 件)
  - sqlglot (MySQL / T-SQL): 全出力で構文エラー 0。ただし sqlglot は緩い (末尾のカンマ・隣り合う文字列を通す) ので、
    確かめられるのは括弧の対応・リテラルの字句程度。MySQL の無名 `ADD CHECK`・`NOT ENFORCED`、T-SQL の
    `WITH NOCHECK` は sqlglot が解析できず素通し (Command) になる
  - 全件回帰 1 回目で見つけて直したもの (10/18 夕方):
    - compare の写し: '...'||CHR(10)||... が数百個続く連結で再帰が深すぎる (chr/long で compare が落ちた。基準も候補も)
      → 連結は繰り返しで読む。値が ')' だけの文字列片を閉じ括弧と数えて文の切れ目を誤る (m_long C_LDEF の
      sql_parse 13 件) → 括弧は記号のトークンだけ数える。写しを変えたので基準 g13a0 も最初から回し直した
    - m_long: DEFAULT の 3,000 バイトのリテラルを 1 行に書いて SQL*Plus が SP2-0341 → 表が作られず INSERT も失敗。
      DDL の中の 1,000 バイトを超えるリテラルを '...'||'...' の片に分けて 1 片 1 行にした (値は同じことを
      defcmp.py で式を評価して確認)。expdp の 9,396 バイトの DEFAULT は TABLE_T の DEFAULT_VALC にあった (読んでいな
      かった) → 読む
    - exp と expdp で制約の並び・後置の外部キーの並びが違った → 種類の順・名前の順に揃えた (外部キーはファイル末尾で
      表ごとに名前順。表の並びはダンプの順のまま)
  - exp/expdp の SQL の同一性 (fix/g13awork/samecheck.py: コメント・単独の索引・NOTE を除いて 4 方言を比べる、21c):
    smoke・m_ddl2・lob/main・case_v・dt・chr/long・chr/main・shape/cols・output/ident は 4 方言とも一致。
    違いが残るのは、ダンプの中身が違うもの (m_ddl の H_GTT は expdp だけにある、num/main・output/main・known_x は
    表の並び (0 行の表・BINARY 表の位置) が違うだけで、表ごとの文は同じ)
  - 見つけたが範囲外: exp で 128 バイトの列名 (chr/names の 列…bb) が 127 バイトで切れ、残りが型に入る
    (`b" NUMBER`)。SQL もこの型をそのまま書くので全表 SQL の字句が崩れる (基準から同じ。DDL 解析の不具合)

## 構文の検査の方法 (他方言)
- 外部ネットワークは使える (pip / docker pull が通る)。PostgreSQL は postgres:16-alpine を pull 済み
  (データは tmpfs で動かす)。MySQL・SQL Server の実 DB はディスクの空き (共有で 1GB 前後) が足りないので見送り、
  sqlglot (Python の多方言パーサ) で文ごとに構文を確かめる。venv: scratchpad/pyenv/v (pglast 8.6 = PostgreSQL の
  本物のパーサ libpg_query、sqlglot 30.23)
- [ ] compare_g13a.py の拡張 (|| / TO_CLOB / PL/SQL ブロック / NOTE + NULL)
- [ ] 回帰 (基準 g13a0・候補 g13a)・ASan・ゲート

## 実測 (Oracle 11.2 / 21c / 23.26、ODV_G13A_X)
- SQL*Plus の 1 行: 11.2 は 2,498 バイトまで (2,499 で SP2-0027)、21c・23 は 4,998 まで。1 文 1,202 行は通る
- 素のリテラルの || 連結は結果 4000 バイトまで (超えると ORA-01489)。NVARCHAR2(2000) に全角 2000 字は
  TO_NCLOB/TO_CLOB/N'' の連結で入る
- LONG: TO_CLOB の連結は ORA-22835 (4000 まで)。PL/SQL の LONG 変数で 32,760 バイトまで入る (40,000 は ORA-06502)
- BLOB: HEXTORAW||HEXTORAW は 2000 バイトまで。TO_BLOB(HEXTORAW())||… はエラー無しに NULL が入る (危険)。
  PL/SQL の DBMS_LOB.WRITEAPPEND は 1MB でも入る
- LONG RAW: PL/SQL の LONG RAW 変数 + UTL_RAW.CONCAT で 20,000 バイト入る
- CLOB: TO_CLOB の連結で 5MB 入る (13 秒)。PL/SQL の一時 CLOB へ追記なら 1MB 0.4 秒、20MB を 1 ブロックで 11 秒
- 日付: TO_DATE('-0099/06/15 12:00:00', 'SYYYY/MM/DD HH24:MI:SS') と TO_TIMESTAMP('-4712/...9 桁', 'SYYYY/.. .FF') が
  11/21 で通る
- 23ai: BOOLEAN は TRUE キーワードも 'TRUE' 文字列も入る。JSON・VECTOR の列は CLOB 変数のまま入る。XMLTYPE は
  CLOB 変数だと ORA-00932 なので XMLTYPE(v) が要る

## expdp のメタデータ (M-17)
- マスター表の OBJECT_TYPE='TABLE' の XML_CLOB (TABLE_T、圧縮されない) に CON1_LIST (CHECK=1 / PK=2 / UNIQUE=3、
  条件・列・ENABLED・FLAGS)、CON2_LIST (FK)、COL_LIST_ITEM の NOT_NULL・DEFAULT_VAL がある → これを読む
- コメント (COMMENT_T)・単独の索引 (INDEX_T)・REF_CONSTRAINT_T は別のメタデータ (DUMP_POSITION の位置) にあり、
  既定 (COMPRESSION=METADATA_ONLY) では "KGC\x80\x01HDR>T<?" で始まる Oracle 独自の圧縮。COMPRESSION=NONE の
  ダンプ (opts/dpopt dpnc) だけ平文の XML。KGC の展開は形式が非公開なので実装しない → 平文があれば読み、
  無ければ SQL に NOTE を書く

## 統括からの連絡 (10/18 夕方) への対応
1. e9b302b (群12b) の上へ積み直した: 作業ツリーのブランチを 1 コミットにまとめて `git rebase e9b302b`。衝突は
   odv_sql.c・odv_csv.c (書き手は自分の版を採り、群12b の長さ付きコールバック ODV_ROW_CALLBACK_EX に付け替え:
   NULL = -1・空 = 0 をそのまま使う。EMPTY_CLOB を見分けるために s->record を覗いていた細工は不要になったので消した)
   と odv_types.h (群12b の ODV_META_CACHE と自分の ODV_LATE_DDL_FN を両方残す)。odv_record.c・odv_api.c は自動
   マージ (群12b の deliver_row / meta_cache のセッション化 + 自分の set_value_marker / set_value_empty / flags)。
   新しい基準: タグ g13a0e (e9b302b を git archive → fix/g13awork/src0e、fix/bin_g13a0e。警告 0/5/1)
2. 比較ツールの写しは更新後の共有版 (群12b の連結の評価入り) から作り直した。自分の連結の読み (再帰) は共有版の
   繰り返しの読みに置き換え、評価は自分の版 (TO_CLOB / HEXTORAW / PL/SQL ブロック / NOTE) を先に通す。
   基準・候補ともこの写しで回し直す
3. PostgreSQL の 0x00: 0x00 を除いた値を書き、`-- NOTE: column "V" of the next row: 1 byte(s) 0x00 removed from the
   value (PostgreSQL text cannot hold U+0000)` を残す (以前は NULL + NOTE)。chr/main 21 e の PostgreSQL 出力を
   psql で流してエラー 0、NOTE 8 件。MySQL は `\0`、SQL Server は `CONCAT(CAST(N'..' AS NVARCHAR(MAX)), NCHAR(0), N'..')`
   のまま (根拠は TYPE_MAPPING_SPEC.md 4.2)
4. exp の VARRAY: odv_exp.c の decode_exp_column で、型が "OWNER"."TYPE" の列 (exp は VARCHAR 扱い) は値を復号せず
   印にする (集合の像 88 01 で始まれば %VARRAY%、それ以外は %OBJECT%。ODV_VFLAG_MARKER 付き)。SQL は NULL + NOTE、
   CSV は空欄 + notes。m_varr 21 e で表示 %VARRAY%・SQL NULL + NOTE を確認。SQL の書き手も "OWNER"."TYPE" の列を
   K_UDT として扱う (以前は exp では文字列型扱い)
- 積み直し後の確認: chr/main 21 e/dp の往復が 5 表と LONG まで一致 (0x00 を含む行も。以前は群12b 待ちで 6 行違った)

## 統括への判断事項 (選択肢つき)
1. exp の表ごとの SQL (sqltab) は、対象の表の後もファイルの末尾まで読む (exp は外部キーと関数索引を全表の後に
   まとめて書くので、そこまで読まないと M-17 の「正しい表の SQL に」が満たせない)。対象の表より後の表の行は
   読み飛ばす (decode しない) が、ファイルの残りは走査する。
   - 案 A (採用): 常に末尾まで読む。大きな exp で表ごとの SQL が遅くなる (回帰の odv_sec で見る)
   - 案 B: 表ごとの SQL では後置の DDL を諦め、NOTE だけ書く
   - 案 C: 一覧 (list) の段階で後置の DDL の位置を記録して、そこへシークする (解析エンジンの一覧の作りに手が入る)
2. CSV の「値を作れないもの」(印) は空欄 + `<csv>.notes.txt`。行ごとの印を CSV の中に残さないので、
   空の値と区別したい利用者には notes.txt が要る。代わりに、見出しに印を付ける案 / 別の列を足す案もある
3. expdp の既定 (COMPRESSION=METADATA_ONLY) ではコメント・単独の索引が KGC 圧縮で読めない。SQL に NOTE を書くだけ。
   (展開の形式は非公開。解析するなら別の群)
4. ODV_FULL_VALUE_MAX = 512 MiB (1 値)。超えたら NULL + NOTE (CSV は空欄 + notes)

## 群13b (VB 経路) への引継ぎ
- 仕様は fix/TYPE_MAPPING_SPEC.md。DLL 経路はこの通りに実装済み (odv_sql.c の map_type / put_value / render_constraint)
- VB 経路が同じ結果になるために要るもの:
  - 日付は表示の書式に関係なく SYYYY/MM/DD の固定形で書く (M-13/M-14)。VB は表示用に整形済みの値しか持たないので、
    SQL 用に再解析するか、DLL 経路に寄せる必要がある (triage の M-14 の fixer_notes のとおり)
  - 印 (%VARRAY% など) の判定: DLL は旗 (ODV_VFLAG_MARKER)。VB は「型が VARRAY / オブジェクト型、または値が印の形で
    列の型が BLOB/CLOB/NCLOB/BFILE/JSON/VECTOR/BOOLEAN/LONG」(仕様 5 章)。オブジェクト型・VARRAY の列は常に NULL + NOTE
  - exp の RAW の値は表示が `0x...` (expdp は 0x 無し)。SQL では 0x を外す
  - BINARY_FLOAT/DOUBLE は Oracle 方言で f / d の接尾辞
  - 一意の索引は CREATE UNIQUE INDEX で制約より前、CHECK は外側の括弧を 1 組外して CHECK (...)、主キーだけの NOT NULL は
    書かない (VB 経路は制約・索引を出していないなら対象外)
  - NUL (0x00) を含む値: Oracle は `'a'||CHR(0)||'b'`、PostgreSQL は NULL + NOTE、MySQL は `\0`、SQL Server は
    `CONCAT(CAST(N'a' AS NVARCHAR(MAX)), NCHAR(0), N'b')`
5. MySQL / SQL Server の DECIMAL に入らない値 (NUMBER(2,-84) の 1.2E+85 等) は、値をそのまま書いて行の NOTE に
   「the INSERT fails」と書く (採用)。案: NULL + NOTE にしてスクリプトを最後まで通す。値をファイルに残せば
   利用者が型を広げて入れ直せるので、値を残す方を選んだ
6. BINARY_FLOAT / DOUBLE の Oracle の値に f / d を付けた (1E+126 を超える値が NUMBER として ORA-01426 になるため)。
   「通常の値の INSERT は変える理由が無ければ同じ形」の例外 (理由: 値が入らない)

## 統括からの追加 (10/18 05:20〜) — 作業中
- [ ] m_bigpart/21/dp の sqltab の遅延 (回帰 43.5→795 s)。ネイティブで sqltab 5,208 回: 基準 2.8 s → 候補 323.5 s
  (1 回 62 ms 増。全表の SQL 4 方言は 0.94→1.08 s で問題なし) = 表ごとのエクスポートのたびにマスター表のメタデータを
  集め直すのが原因。調査中
- [ ] (a) constraints_json に "unique_index":1 (exp の CREATE UNIQUE INDEX。expdp も区別があれば)
- [ ] (b) 公開 API で行の値を SQL 出力と同じ形で受け取る口 (odv_set_value_mode 等。印の旗。既定は不変)
- [ ] (c) expdp の制約・DEFAULT・コメントを一覧 (list_only) の constraints_json / col_defaults にも。主キーだけの NOT NULL を区別
- [ ] (d) note_col の 511 バイト切れ (m_long C_LDEF)
- [ ] (e) exp の sqltab を案 C に: 一覧で後置 DDL の開始位置を記録して返す、新 API で渡す、sqltab は対象表の後そこへシーク。
  渡されないときは案 A (末尾まで)。分割ダンプでも確認。m_bigpart/21/e の sqltab が基準並みに戻ることを示す。
  できれば 300MB 程度の exp を作って 1 表の sqltab を基準・案 A・案 C で比べる (計測後に消す)
- 確認: 既定の出力 (プレビュー・CSV・SQL) 不変を回帰で、ASan、群13b の pair (fix/g13bwork/pair.sh) で (a)(d) の差が消えること
- 10/18 06:00 途中経過 (未ビルド・未回帰。作業ツリーは 5fcd3f6 の上に未コミットの変更):
  - dp の遅延の原因: 表ごとの SQL のたびにマスター表を全行読んでメタデータを集める (5fcd3f6 の M-17)。
    1 回 0.8 ms → 65 ms (ネイティブ、m_bigpart dp)。gprof/callgrind: 1 バイトずつの odv_vread (62%)・
    マスター表の約 100 列の復号・全表の TABLE_T の UTF-16 変換と XML 解析
  - 対処 1: マスター表は master_row が読む 14 列だけ復号 (MASTER_CTX.need、一覧でも効く)。
    対処 2: 表ごとのエクスポートは対象の表の TABLE_T / COMMENT / INDEX だけ変換・解析 (EXPDP_META.filtered)。
    → 41〜55 ms。残りはマスター表を歩くこと自体
  - 対処 3 (= (e) と同じ仕組み): 一覧で「表の DDL が離れた所にある位置」のヒントを記録し、新 API で渡す
    odv_get_table_ddl_hint(s, i) / odv_set_ddl_hint(s, hint)。expdp: マスター表のその表の行の位置
    ("P<大きさ>:<位置>,...")、exp: 後置の DDL の始まり ("L<大きさ>:<位置>:<スキーマの16進>" / "L<大きさ>:-")
  - (b) odv_set_value_mode(s, ODV_VALUES_EXPORT): odv_parse_dump で full_values、長さ付きコールバックで印は -2
    (値ポインタは印の文字列)、上限超えは -3
  - (c) 案 A (要求時に表単位): odv_read_table_ddl(s)。表コールバック 1 回、col_not_nulls は 2 = 主キーだけの NOT NULL、
    constraints_json は exp の一覧と同じ形 (odv_serialize_constraints_json を共用)。一覧は変えない
  - (a) jbuf_put_constraint に "unique_index":1。(d) note_col / row_note を動的な長さに (put_note)、改行は "--   " で続ける
  - 確認済み: m_ddl 21 e/dp で sqltab がヒントの有無で同一 (48/52 ファイル)。m_ddl2 21 e・m_long 21 e の pair
    (群13b の VB 試験ハーネスの写し x/vbh、DLL はこの作業ツリー) が differ 4→0 / 3→0
- 10/18 06:05 実装済み・確認中 (ビルド bin_g13a2 = 警告 0/5/1。その後ヒント関係だけ 2 点修正: expdp は TABLE_T を持つ
  TABLE 行だけ記録 (区画表は区画ごとに TABLE 行が 1 つずつあり、B_BIG で 1301 行になっていた)、exp の一覧は rc == OK の
  ときだけヒントを出す)
  - 測定 (1 表の SQL 1 回あたり。perfdrv = 製品の ExportSql と同じ呼び方、x/pb):
    | ダンプ・表 | 基準 e9b302b | ヒントなし (5fcd3f6 の方式) | ヒントあり |
    |---|---|---|---|
    | m_bigpart dp A_PLAIN (ネイティブ) | 0.9 ms | 34.7 ms | 2.0 ms |
    | m_bigpart dp B_BIG (1300 区画) | 0.7 ms | 44.0 ms | 11.0 ms (TABLE_T が大きい: 1300 区画の XML の変換と解析) |
    | m_bigpart e A_PLAIN (ネイティブ) | 6.4 ms | 10.4 ms | 5.5 ms |
    | m_bigpart e B_BIG | 10.7 ms | 10.7 ms | 10.5 ms |
    | m_bigpart e A_PLAIN (wine, 20 回 × 2) | 11.6〜13.6 ms | 26.0〜26.5 ms | 10.9〜11.0 ms |
    harness の sqltab 全 5,208 回 (ネイティブ): dp 基準 2.8 s / 候補 (修正前) 323.5 s / 修正後ヒントなし 227 s / ヒントあり 97.5 s
    (B_BIG の 1301 行を読んでいた時点。TABLE 行の絞り込み後は B_BIG 11 ms × 5,200 ≈ 57 s の見込み)
  - 出力の同一性: m_ddl 21 e/dp・m_bigpart 21 e/dp の sqltab 全ファイルがヒントの有無で同一。全ダンプでの確認を実行中
    (x/hint_all.log、cmpnative.py: 同じハーネスの写しを ODV_HINT あり / なしで比べる。m_bigpart は出力が大きいので別)
  - 全件回帰 g13a2 を実行中 (reg_cand2.log)
  - 注意: 10/18 05:58 に perf2.sh の出力 (m_bigpart e の sqltab 538MB) でディスクが一時的に満杯になった。すぐ消した。
    以後、出力は 1 本ずつ消す

## 10/18 06:10 コンテナ再起動 → 06:15 再開
- 途中で切れたもの (回帰 g13a2・hint_all・m_bigpart e の後始末) は捨てた。残っていた odv_g13a2 (m_bigpart 21 e/dp・m_part 21 ed)・
  x/cn・x/t4.sql を消した
- /root/.wine (690MB) を作ったのは自分の perfw.sh (05:59、WINEPREFIX を付けずに wine を動かした)。perfw.sh に
  WINEPREFIX=$S/wineprefix を入れた
- (f) CSV の行の区切りを CRLF に (odv_csv.c: CSV_EOL。見出し・型・データの行と <csv>.notes.txt の行。値の中の改行はそのまま)。
  TYPE_MAPPING_SPEC.md 9 章に書いた。compare の写しの read_csv は csv.reader(newline='') なので CRLF でも同じに読む
- ヒントの全件確認の途中で見つけたもの: exp の表ごとの SQL (ヒントなし = 末尾まで読む) で、対象より後の表の DDL の
  注意 (m_long ja16sjis の C_LDEF の DEFAULT 4000 バイト切れ) が last_error に入り、別の表の run.json の last_error に出ていた
  (ヒントありでは飛ばすので出ない = 差)。→ exp_note は「絞り込みで出さない表 (EXP_DDLCTX.pend_other)」の注意を
  last_error に入れない (表の notes には入れる)。m_long ja16sjis 11/21 の 4 ダンプでヒントの有無の差が 0 に
- ビルド bin_g13a3 (警告 0/5/1 = 基準 g13a0e と同じ)。全件回帰 g13a3 を実行中 (reg_cand3.log)。最初の 162 件は
  pend_other の前のビルドで走った (止めて再ビルドしたが regress.py が続きから走った) → 終わったらその 162 件だけ消して走らせ直す
- 群13b の pair (x/vbh の写し、DLL = 最終の作業ツリー、13b の listA.txt の 26 ダンプ): m_ddl2 21 e differ 4→0 (a)、
  m_long 21 e 3→0 (d)。他の 24 ダンプの differ の数は 13b の pB.log と同じ
- ヒントの全件確認でもう 1 件: opts/multi・multi2 の exp (複数スキーマ、同じ名前の表 T1/T3 が A と B にある) で、
  ヒントなし (末尾まで読む) の A.T1 の表ごとの SQL に B.T1 の CREATE TABLE・制約が入っていた (読み進めた先の表の
  表コールバックを SQL の書き手が表名だけで受けていた)。基準は対象の表の次で止まるので出ない = 5fcd3f6 からの退行。
  → odv_sql.c same_table_as_target: 絞り込みにスキーマがあればスキーマも比べる (odv_stricmp、パーサの絞り込みと同じ)。
  opts/multi* の 32 ダンプでヒントの有無の差 0。回帰 g13a3 は止めて、この修正の後で最初から回す
- 大きな exp での比較 ((e) の「できれば」): 21c に ODV_G13A_BIG (A_FIRST 100 行 + PK + FK→C_PARENT + 関数索引 UPPER(NAME)、
  B_BIG 650,000 行 × 約 490 バイト、C_PARENT 10 行) を作り exp (318,504,960 バイト)。A_FIRST の表ごとの SQL 1 回 (perfdrv、
  data_offset あり、ファイルはキャッシュ済み、3 回の平均):
  | | 基準 e9b302b | 案 A (ヒントなし) | 案 C (ヒントあり) |
  |---|---|---|---|
  | ネイティブ | 1.2 ms | 126.1 ms | 1.6 ms |
  | wine | 4.7 ms | 161.5 ms | 3.8 ms |
  基準の出力は PK・FK・関数索引が無い (後置の DDL を読まない)。案 A・C は PK・関数索引・FK まで同じ出力。
  ヒントは "L318504960:318498416:<ODV_G13A_BIG の 16 進>" (B_BIG の後、後置の DDL の始まり)。
  計測後にダンプを消し、ユーザーを DROP した

## 10/18 06:50 統括から追加 (g)(h)
- (g) exp の後置の索引 (関数索引・DESC 索引) の並び: odv_sql.c の表の DDL を「部品」(SQL_CPIECE: 種類の順位・システム名か・
  名前・番号・文・NOTE) で持ち、後置の分 (sql_late_ddl) も部品として足して、write_tail で一緒に並べる (piece_cmp =
  旧 con_order_cmp と同じ順)。出力は [制約・索引の文 (並べた順)] → [コメント] → [制約・索引の NOTE (同じ順)] → [コメントの
  NOTE・表の NOTE]。後置の外部キーは別の束 (lfk) に入れ、ファイル末尾で表の外部キーと一緒に名前の順。後置の一意の索引は
  表の分と同じく create_index で判定 (以前は create_table)
- (h) 0 行の表の CSV: write_csv_file で表コールバックを差し替え (csv_table_callback)、対象の表 (名前 + 絞り込みのスキーマ)
  の表コールバックで見出し (と型の行) を書く (まだなら)。CRLF。exp の 0 行の区画 (m_part 21 e P_HASH SYS_P409) も見出しが出る。
  expdp の 0 行の区画は data_offset がマスター表の位置で、その区画のデータが無いので表コールバックが来なかった (CSV は空、
  表ごとの SQL は CREATE TABLE も無し。基準も同じ) → odv_expdp.c: 絞り込みの全体解析で対象の行を 1 つも読まなかったとき
  (MASTER_CTX.target_missing) は master_apply がマスター表の TABLE_T からその表の表コールバックを 0 行で出す
  (CSV は見出し、SQL は CREATE TABLE と DDL。m_part 21 dp P_HASH SYS_P389 で確認)
- 群13b の pair (13b の VB 試験ハーネス x/vbh13 = 13b が 06:48 に作ったもの、DLL = 最終の作業ツリー、listF の 63 ダンプ):
  SQL 1,760 ファイル differ 0 (m_cols D_FBI の 4 が消えた)、CSV 440 ファイル (notes.txt 7) differ 0 (0 行の表の 11 が消えた)
- 回帰 g13a3・ヒントの全件確認をこの版で最初から実行中
- (h) の追加: odv_api.h の odv_export_csv の説明に CRLF・0 行の見出し・notes.txt を書いた
- 全件回帰 g13a3 (最終の C、618 件、警告 0/5/1) vs 基準 g13a0e:
  - 1 回目: 新しいキーに csv_rows 401 (144 ダンプ) = (h) で 0 行の表の CSV に見出し行が付き、compare の写しがそれを
    データの行と数えた (基準は 0 行の表の CSV が空なので見出しを数えない) → compare_g13a.py の compare_csv に
    「真値が 0 行で CSV に行があれば、1 行目を見出しとして名前を比べて外す」を足した (csv_header で名前の違いは拾う)。
    該当の 144 件だけ走らせ直し (regress.py の続きから。1 回目の結果は x/regress_g13a3.before_csvfix.jsonl)
  - CRLF による崩れは無い (csv_parse・csv_header・csv_value の新規 0。read_csv は csv.reader(newline=''))
  - 最終: 新しいキーは csv_value_noted 190・sql_value_noted 214 (値を作れない印を空欄 + notes / NULL + NOTE にしたもの:
    g9_types TV1〜4、m_adt B_ADT、m_rowid E_BFILE、m_varr B_VARR、lob/mixd)、lob_placeholder 12 (m_varr B_VARR: exp の
    VARRAY が表示で %VARRAY% に)、value_udt 12 (m_adt B_ADT: exp の ADT が %OBJECT% に)、sqltab_mismatch 3 (chr/names
    21 e/ed・23 e の it's: exp の 128 バイトの列名が切れる既知の不具合で全表 SQL の字句が崩れ、compare が全表側の INSERT を
    拾えない。基準は同じ理由で 13 件)。消えたキー: sql_lob_placeholder 1312・sql_dialect_risk 757・sql_value 456・
    sql_literal_too_long 385・sql_parse 222・value 208 (dt の紀元前の年)・sql_row_missing 35・sqltab_mismatch 25・format 24
  - 5fcd3f6 の回 (regress_g13a) とキーの集合が全件で同じ (newdiffs g13a g13a3 が空) = (a)〜(h) と再起動後の修正で
    回帰の比較の結果は変わっていない
  - 時間 (odv_sec、共有ハーネスはヒントを渡さない): m_bigpart 21 dp 43.5→554.5 s、m_part 21/23 dp 14→52 / 18→56 s、
    m_bigpart 21 e 426.9→419.9、bigddl e 187→148 / 193→152。合計 1838→2442 s (5fcd3f6 の回は 3059)
- Oracle への往復 (rtall3.sh = rtall.sh を bin_g13a3 で): 22 ケース・40 出力の要約が rtall2.log (04:57) と同じ
  (エラー 0。g9_types 23 dp の 42 行は型を作れない 4 表で前と同じ。DIFF の行の違いは制約の数の辞書の並びだけ)
