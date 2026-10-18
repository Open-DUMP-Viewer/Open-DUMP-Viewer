### M-12 expdp の CSV・SQL に LOB の先頭 4096 バイトだけを値として出し、BLOB を HEXTORAW なしの数値か文字列で書くので、CLOB が黙って切れ、BLOB を持つ表は SQL でほとんどの行が入らない
形式:expdp (CSV・SQL エクスポート)、exp (SQL エクスポートの BLOB)
症状:expdp の CSV では、5000 字の CLOB が 4096 字、3000 バイトの BLOB が 16 進 4096 桁 (2048 バイト分) で、切れた印も無く終わります。8192 字以上の CLOB も一律 4096 字です。Oracle 方言の SQL では、BLOB の 0012・00・01 が引用符なしの数値 (VALUES (1, 'z', 0012) など) になり、ORA-00932 で入りません。ABCD は 'ABCD' という文字列になります。4096 字のリテラルは ORA-01704 になります。CLOB の '00123' は数値 123 になります。4 行の T_BLOB は 1 行だけ、BLOB のほかに ID・FN・D・Z を持つ 8 行の T_LOBFN は 1 行も入りません。PostgreSQL 方言では、BLOB 'ABCD' が ASCII の 41424344 として黙って入り、5000 字の CLOB は 4096 字で入ります。exp のダンプの SQL は CLOB を '%CLOB%'、BLOB を '%BLOB%' と書きます。Oracle では '%BLOB%' が ORA-01465 になり、T_BLOB・T_LOBFN とも全行が入りません。PostgreSQL では '%BLOB%' がバイト列 25424c4f4225 として黙って入ります。EMPTY_CLOB() は exp では '%CLOB%'、expdp では空です。
条件:expdp の 11.2・21c・23.26 で、CLOB・NCLOB・BLOB の列を持つ表を CSV か SQL に出すとき。切れるのは CLOB が UTF-8 で 4096 バイト、BLOB が 2048 バイトを超える値です。BLOB の 16 進が 0〜9 だけ (0012、01 など) なら数値として、それ以外なら文字列として書かれます。exp では、BLOB の列を持つ表の SQL エクスポートで起きます。
影響:ヘルプは LOB を CSV・SQL では「スキップ」「SQL エクスポートに含まれません」としていますが、実際は切り詰めたプレビューが値として出ます。CSV の CLOB は完全に見えて、実は 4096 バイトで切れています。SQL で移行すると、BLOB を持つ表は LOB 以外の列も含めてほとんどの行が失敗するか、壊れた値で入ります。同じ表でも exp と expdp で出力が違います。
原因:expdp の LOB の列の値は、accumulate_lob_preview (odv_expdp.c:582-680) が作るプレビューです。CLOB は UTF-8 で 4096 バイト、BLOB は 16 進 4096 字 (ODV_LOB_PREVIEW_LEN、odv_types.h:303) で打ち切ります。CSV (odv_csv.c:123-128) と SQL (sql_row_callback、odv_sql.c:774-866) は、これを値全体として扱います。LOB の列を飛ばす処理も、切れた印もありません。SQL の 16 進リテラル (Oracle の HEXTORAW など) は COL_RAW と COL_LONG_RAW だけで (odv_sql.c:840-842)、COL_BLOB の分岐がありません。BLOB は odv_sql.c:861 の is_numeric_value (数字と '.' だけなら引用符なし) か、odv_sql.c:864 の文字列に落ちます。4000 バイトを超えるリテラルも分けずに書きます。exp の経路は LOB の値を '%CLOB%' / '%BLOB%' の印に置き換え (odv_exp.c:1059-1095)、SQL はこれを普通の文字列として書きます。
該当:- odv_expdp.c:582-680
- odv_types.h:303
- odv_csv.c:123-128
- odv_sql.c:840-842
- odv_sql.c:861-864
- odv_exp.c:1059-1095
再現:create table T_CLOB (ID NUMBER(4) PRIMARY KEY, Z CHAR(1) DEFAULT 'z' NOT NULL, C CLOB NOT NULL); に 'short clob'、to_clob(rpad('a', 4000, 'a')) || to_clob(rpad('b', 1000, 'b')) (5000 字)、'00123'、'あいう' を入れます。create table T_BLOB (ID NUMBER(4) PRIMARY KEY, Z CHAR(1) DEFAULT 'z' NOT NULL, B BLOB NOT NULL); に hextoraw('0012')、hextoraw('ABCD')、hextoraw('01')、3000 バイトの値を入れます。create table T_LOBFN (ID NUMBER(4) PRIMARY KEY, FN VARCHAR2(100), D DATE NOT NULL, Z CHAR(1) DEFAULT 'z' NOT NULL, B BLOB NOT NULL); に hextoraw('01')〜hextoraw('0808080808080808') の 8 行を入れます。expdp と exp で取り、CSV と Oracle 方言の表ごとの SQL を出して、別スキーマに流します。
証拠:cases/output/main/results/21/dumps/odv_c_out_21_dp.dmp、verify/output/case_v/results/21/dumps/odv_v_out_21_dp.dmp、verify/output/case_v/results/21/dumps/odv_v_out_21_e.dmp

### M-13 SQL エクスポートの日付リテラルを表示用の文字列と固定の書式マスクで作るので、紀元前の行は入らず、日付書式 1 と 3 では時刻・小数秒が黙って落ち、独自書式では大半の INSERT が失敗する
形式:SQL エクスポート (Oracle 方言、表ごと・一括)。exp・expdp
症状:(a) 紀元前の日付は TO_DATE('-1000/06/15 12:00:00', 'YYYY/MM/DD HH24:MI:SS') のように書かれ、ORA-01841 で入りません。正しく読めている -0001 年・-1000 年の値も入りません。紀元前の値を持つ行 (T_DATE で 9 行、T_TS で 2 行) は、どの日付書式でも失敗します。(b) 日付書式 1 (YYYYMMDD) では TO_DATE('20240131', 'YYYYMMDD') と TO_TIMESTAMP('20240131', 'YYYY/MM/DD HH24:MI:SS.FF') になります。T_DATE の 11 行と T_TS の 10 行が、時刻 00:00:00 としてエラー無しに入ります。(c) 日付書式 3 (独自書式、既定は 'YYYY-MM-DD HH24:MI:SS') では小数秒が出ず、TO_TIMESTAMP('2024-01-31 13:45:59', 'YYYY/MM/DD HH24:MI:SS.FF') になります。T_TS の 9 行と T_TSLTZ の 7 行が、小数秒 0 で入ります。(d) 独自書式 'DD/MM/YYYY HH24:MI:SS' では TO_DATE('31/01/2024 13:45:59', 'YYYY/MM/DD HH24:MI:SS') になり、89 文中 67 文 (expdp のダンプでは 73 文) が ORA-01861・01847・01850・01830 で失敗します。'YYYY-MM-DDTHH24:MI:SS' でも 67 文が ORA-01861・01858 で失敗します。日付書式 0 (既定) と 2 は、紀元前を除いて Oracle の緩い解釈で正しく入ります。
条件:Workspace からの SQL エクスポート (表ごと・一括、Oracle 方言) で、DATE・TIMESTAMP・TIMESTAMP WITH (LOCAL) TIME ZONE の列があるとき。(a) はどの日付書式でも起きます。(b)〜(d) は、エクスポートの設定で日付書式を 1 か 3 にしたときに起きます。設定は保存され、次回以降も使われます。exp・expdp の 11.2・21c・23.26 で同じ SQL になります。TablePreview を開いているときの SQL は別の経路 (TO_DATE を使わない) で、この誤りは出ません。
影響:CSV やプレビューの見た目のために日付書式を変えると、SQL エクスポートも黙って変わります。書式 1 では時刻が、書式 3 では小数秒が、エラー無しに失われます。独自書式では、日付のある行のほとんどが入りません。紀元前の日付を含む行は、どの設定でも入りません。
原因:sql_row_callback の日付の分岐 (odv_sql.c:809-832) は、表示用に整形された値 (odv_set_date_format の書式のまま) に、固定の書式マスクを組み合わせます。DATE は書式 1 なら 'YYYYMMDD'、書式 2 なら 'YYYYMMDDHH24MISS'、それ以外 (0・3) は 'YYYY/MM/DD HH24:MI:SS' です (odv_sql.c:823-831)。TIMESTAMP 系はどの書式でも 'YYYY/MM/DD HH24:MI:SS.FF' です (odv_sql.c:816-821)。このため、値の書式とマスクが合いません。マスクの 'YYYY' は負の年を受け付けません ('SYYYY' なら '-1000' も '-099' も読めます)。また、日付の整形は %04d なので (odv_datetime.c:125 など)、-99 年を '-099' と出します。独自書式の整形 (odv_datetime.c:203-206) は小数秒を出しません。
該当:- odv_sql.c:809-832
- odv_sql.c:816-821
- odv_sql.c:823-831
- odv_datetime.c:203-206
- odv_datetime.c:125
再現:create table T_DATE (ID NUMBER, D DATE); に to_date('2024-01-31 13:45:59', 'YYYY-MM-DD HH24:MI:SS')、to_date('-1000-06-15 12:00:00', 'SYYYY-MM-DD HH24:MI:SS')、to_date('-0001-01-01', 'SYYYY-MM-DD') などを入れます。create table T_TS (ID NUMBER, TS3 TIMESTAMP(3), TS9 TIMESTAMP(9)); に小数秒のある値を入れます。exp か expdp で取ります。エクスポートの設定で日付書式を 0・1・2・3 (独自書式は既定の 'YYYY-MM-DD HH24:MI:SS' と 'DD/MM/YYYY HH24:MI:SS') に変えて、Workspace から Oracle 方言の表ごとの SQL を出し、別スキーマに流して元と比べます。
証拠:cases/dt/results/21/dumps/odv_c_dt_21_e.dmp、cases/dt/results/21/dumps/odv_c_dt_21_dp.dmp

### M-14 TablePreview を開いた状態(VB の経路)の SQL エクスポートが、日付・時刻を TO_DATE / TO_TIMESTAMP なしの表示用の文字列で書くので、DATE 列を持つ表の行が Oracle に入らない
形式:SQL エクスポート(Oracle 方言、VB の経路)。exp・expdp
症状:T_DATE(ID, D DATE, Z)の 29 行の例です。
- VB の経路は INSERT … VALUES (1, '2024/01/31 13:45:59', 'Z'); と、日付を素の文字列で書きます。DLL の経路は、同じ行を TO_DATE('2024/01/31 13:45:59', 'YYYY/MM/DD HH24:MI:SS') と書きます。
- 21c に流すと、NLS が AMERICAN(NLS_DATE_FORMAT=DD-MON-RR)のセッションでも、nls_territory=JAPAN(RR-MM-DD)のセッションでも、D が NULL の 1 行しか入りません。残りの 28 行は、AMERICAN では ORA-01861 / ORA-01847、JAPAN では ORA-01861 / ORA-01841 で失敗します。
- 日付書式 1(コンパクト)では、'20240131' を数値とみなして VALUES (1, 20240131, 'Z') と引用符なしで書くので、28 行が ORA-00932(expected DATE got NUMBER)で失敗します。
- TIMESTAMP の表 T_TS(13 行)は、AMERICAN では全列 NULL の 1 行しか入りません(ORA-01843 / ORA-01847)。JAPAN では 11 行が正しい値で入り、紀元前の 2 行が ORA-01841 で失敗します。
- expdp のダンプからでも同じで、T_DATE は 1 行しか入りません。
条件:TablePreview を開いた状態で SQL エクスポート(Oracle 方言)を行う場合と、SqlInferInteger=True の場合(単表・一括)。DATE の値は、日付書式によらずセッションの NLS_DATE_FORMAT に合わないので入りません。TIMESTAMP は、セッションの NLS_TIMESTAMP_FORMAT が 'YYYY/MM/DD HH24:MI:SS.FF' の形を受け付けるとき(JAPAN など)だけ入ります。CREATE TABLE は NOT NULL と DEFAULT の順の誤り(M-32)でも失敗しますが、表を別に正しく作ってから INSERT だけを流しても、DATE の行は入りません。
影響:表を開いてからエクスポートするというごく普通の手順で、DATE 列を持つ表の行がほぼすべて入りません。エラーで止めない実行方法では、日付が NULL の行だけが入った表が残ります。
原因:SqlExportLogic.ExportFromData は値を 1 つずつ、空なら NULL、ExportHelper.IsNumericValue(Double.TryParse)が真ならそのまま、それ以外は ' で囲んで書きます(SqlExportLogic.vb:120-132、ExportHelper.vb:319-323)。列の型(columnTypes)を見ないので、DLL が表示用に整形した日付・時刻(odv_set_date_format の書式)が、そのまま文字列リテラルになります。Oracle はこれをセッションの NLS の書式で暗黙に変換するので、'YYYY/MM/DD HH24:MI:SS' の形は既定の書式に合いません。日付書式 1 の '20240131' は Double.TryParse を通るので、引用符も付きません(数字に見える文字列を数値として書く誤りは H-08 も参照)。DLL の経路の日付リテラルの誤りは M-13 です。
該当:- Logics/Export/SqlExportLogic.vb:120-132
- Logics/Export/ExportHelper.vb:319-323
- HMI/Open DUMP Viewer/Open DUMP Viewer.vb:829-848
- Logics/Export/BulkExportLogic.vb:50-56
再現:create table T_DATE (ID NUMBER(4) NOT NULL, D DATE, Z CHAR(1) DEFAULT 'Z' NOT NULL, CONSTRAINT PK_T_DATE PRIMARY KEY (ID)) に (1, to_date('2024-01-31 13:45:59', 'YYYY-MM-DD HH24:MI:SS'), 'Z') などを入れ、exp か expdp で取ります。製品で T_DATE を TablePreview で開き、Oracle 方言で SQL エクスポートします。同じ定義の表を作った別のスキーマに、出力の INSERT を sqlplus で流します。
証拠:cases/dt/results/21/dumps/odv_c_dt_21_e.dmp、cases/dt/results/21/dumps/odv_c_dt_21_dp.dmp、verify/vbsql/f0/T_DATE_noddl.sql、verify/vbsql/run/date_ins_am.out、verify/vbsql/run/date_ins_jp.out、verify/vbsql/run/date_ins_f1_am.out、verify/vbsql/run/ts_ins_jp.out

### M-15 SQL エクスポートが 4000 バイトを超える値を 1 つの文字列リテラルで書くので、長い LONG や全角 1334 字以上の NVARCHAR2 の行が ORA-01704 で入らない
形式:SQL エクスポート (Oracle 方言)。exp・expdp
症状:8000・31998・32000・32760 バイトの LONG は、INSERT … VALUES (2, 'z', 'bbbb…') のように 1 つのリテラルで書かれ、ORA-01704: string literal too long で入りません。NVARCHAR2(2000) に 'あ' を 2000 字入れた値 (UTF-8 で 6000 バイト) も同じです。4000 バイトちょうどの LONG・NVARCHAR2・VARCHAR2(4000) と、引用符 4000 個の値 (リテラルは 8002 字) は入ります。上限はエスケープ後のリテラルの字数ではなく、値のバイト数で決まります。
条件:Oracle 方言の SQL で、文字列の値が DB 文字セット (AL32UTF8) で 4000 バイトを超えるとき。4001 バイト以上の LONG、全角 1334 字以上の NVARCHAR2 (UTF-16 では 4000 バイト以内でも、UTF-8 では 4000 バイトを超えます)、expdp の CLOB のプレビュー (4096 バイト) で起きます。exp・expdp の 11.2・21c・23.26 で同じです。
影響:4000 バイトを超える LONG と、全角 1334 字を超える NVARCHAR2 の行は、SQL エクスポートで移行できません。その行の INSERT だけが失敗し、他の行は入ります。ヘルプで「対応」とされている NVARCHAR2 の、正当な長さの値でも起きます。
原因:sql_row_callback (odv_sql.c:774-866) は、値の長さに関係なく、sql_write_string (odv_sql.c:863-864) で 1 つの '…' リテラルにします。Oracle の SQL の文字列リテラルは、既定の MAX_STRING_SIZE=STANDARD では 4000 バイトまでです。それを超える値は TO_CLOB('…') || TO_CLOB('…') のように分けて書く必要があります。TablePreview 経由の SQL (Logics/Export/SqlExportLogic.vb:126-131) も、ExportHelper.EscapeSqlString (Logics/Export/ExportHelper.vb:71-74) で 1 つのリテラルにします。
該当:- odv_sql.c:774-866
- odv_sql.c:863-864
- Logics/Export/SqlExportLogic.vb:126-131
- Logics/Export/ExportHelper.vb:71-74
再現:create table T_LONG (ID NUMBER(4) NOT NULL, Z VARCHAR2(1) NOT NULL, L LONG); に 4000・8000・32000 バイトの値と 'short long' を入れます。create table T_NV2000 (ID NUMBER(4) NOT NULL, NV NVARCHAR2(2000), Z VARCHAR2(1) NOT NULL); に、unistr('\3042') を 2000 回つないだ値を PL/SQL で入れます。exp で取り、Oracle 方言の SQL を出して、別スキーマに流します。
証拠:cases/chr/long/results/21/dumps/odv_c_chr_l_21_e.dmp、cases/chr/long/results/11/dumps/odv_c_chr_l_11_e.dmp、cases/lob/main/results/21/dumps/odv_c_lob_m_21_e.dmp

### M-16 MySQL 方言の SQL エクスポートが値の \ を書き換えないので、\ を含む値が MySQL で変わるか、文が閉じなくなる
形式:SQL エクスポート(MySQL 方言)。exp・expdp。DLL の経路と VB の経路の両方
症状:'a\' は VALUES (14, 'a\', NULL, 'z'); のまま出ます。MySQL の既定の sql_mode では \' が引用符のエスケープになり、文が閉じません。'back\slash' は 'backslash' として入り、'C:\temp\new' の \t と \n はタブと改行になります。ファイルの先頭にも SET sql_mode='NO_BACKSLASH_ESCAPES' は出ません。文字の表 T_VAL の '\'、'a\'、'\n\t\\' も、そのまま出ます。TablePreview を開いているときの SQL(VB の経路)も、'a\' をそのまま書きます。
条件:MySQL 方言で、値に \ を含むとき(Windows のパス、正規表現など)。MySQL の sql_mode に NO_BACKSLASH_ESCAPES が無い既定の状態で起きます。exp・expdp のどちらのダンプでも同じです。
影響:MySQL に流すと、\ を含む値が黙って変わるか、文が壊れて以降の文が取り込めません。Windows のパスのような普通の値で起きます。
原因:sql_write_string(odv_sql.c:47-64)の \ の書き換えが `else if (*val == '\\' && 0)`(odv_sql.c:54)で無効になっています。ファイルに SET sql_mode='NO_BACKSLASH_ESCAPES' も出しません。VB の経路の ExportHelper.EscapeSqlString(Logics/Export/ExportHelper.vb:71-74)も、' を二重にするだけです。名前の中の引用符と、SQL Server の N 接頭辞は別の原因です(L-16)。
該当:- odv_sql.c:47-64
- odv_sql.c:54
- Logics/Export/ExportHelper.vb:71-74
再現:create table T_SQL (ID NUMBER(4) PRIMARY KEY, S VARCHAR2(100), NV NVARCHAR2(20), Z CHAR(1) DEFAULT 'z' NOT NULL); に (13, 'back\slash', null, 'z')、(14, 'a\', null, 'z')、(15, '\', null, 'z') を入れます。exp か expdp で取り、MySQL 方言の表ごとの SQL を出します。
証拠:cases/output/main/results/21/dumps/odv_c_out_21_e.dmp、cases/chr/main/results/21/dumps/odv_c_chr_21_dp.dmp、verify/vbsql/ident/TSQL_mysql.sql

### M-17 SQL エクスポートの DLL 経路が PK / UNIQUE / FK / CHECK・NOT NULL・DEFAULT を出さず、expdp 由来の SQL には制約・索引・コメントが一切無いので、同じ表でも経路によって作り直した表が食い違う
形式:SQL エクスポート(全方言)。exp 従来型・DIRECT=Y、expdp
症状:smoke の 21c のダンプの例です。T_SMOKE1 は主キーと表・列のコメントを、T_SMOKE2 は主キー・外部キー・CHECK・索引 IX_T_SMOKE2_DT・K の NOT NULL・PRICE の DEFAULT 0 を持ちます。
- DLL の経路の全表 SQL(exp)は、CREATE INDEX 1 件と COMMENT ON 3 件だけを出します。PK_T_SMOKE1・PK_T_SMOKE2・FK_T_SMOKE2_T1・CK_T_SMOKE2_FLAG の ALTER TABLE と、NOT NULL・DEFAULT は 0 件です。Oracle・PostgreSQL・MySQL・SQL Server のどの方言も同じです。製品が使う表ごとの SQL では、CREATE INDEX と COMMENT ON も 0 件です(H-13)。
- 同じ exp の表を TablePreview で開いて出す(VB の経路)と、NOT NULL、DEFAULT 0、主キー・外部キー・CHECK の ALTER TABLE、CREATE INDEX が出ます(CHECK の条件には制御文字が混じります。M-08)。COMMENT ON は出ません。
- expdp では、DLL の経路は ALTER TABLE・CREATE INDEX・COMMENT ON・NOT NULL がすべて 0 件です。VB の経路でも NOT NULL だけが出て、制約・索引・コメント・DEFAULT は出ません。
11g / 23ai の exp 従来型と expdp でも同じです。
条件:SQL エクスポートの経路で結果が変わります。
- DLL の経路: Workspace から TablePreview を開かずに単表で出す場合と、一括 SQL 出力(どちらも既定の SqlInferInteger=False)。製品の既定の手順です。
- VB の経路: TablePreview を開いた状態で出す場合と、SqlInferInteger=True の場合。
expdp のダンプでは、どちらの経路でも制約・索引・コメントが出ません。「CREATE INDEX 出力」「COMMENT ON 出力」の設定は既定で有効です。
影響:SQL で表を作り直すと、主キー・一意・外部キー・CHECK・NOT NULL・DEFAULT の無い表ができます。重複する行や NULL も入るようになり、元の表と同じ制約では使えません。expdp のダンプでは、索引とコメントも失われます。表を開いてから出すかどうかで出力が変わるので、欠落に気づきにくくなります。
原因:原因は経路ごとに 3 つあります。
(1) DLL の経路(odv_sql.c)は、CREATE TABLE に列名と型だけを書きます(odv_sql.c:459-471)。表コールバックに届く col_not_nulls・col_defaults・constraints_json は使わずに捨てます(odv_sql.c:894-895)。制約のうち書くのは CONSTRAINT_INDEX の CREATE INDEX だけで(odv_sql.c:490-492)、主キー・一意・外部キー・CHECK を書く処理がありません。
(2) expdp の解析は、表の制約・索引・コメントを s->table に入れず、表コールバックに constraint_count=0 と "[]" を渡します(odv_expdp.c:405-409)。列の DEFAULT も読まないので、col_defaults は空です。マスター表から名前だけを取る odv_get_table_constraints_json(odv_api.c:303)は製品が呼ばず、中身も不完全です(L-01)。
(3) VB の経路の SqlExportLogic.WriteConstraints は、要素ごとに c.columns から列リストを作ってから種類で分けます(SqlExportLogic.vb:281-282)。コメントの要素(type 5)は JSON の最後に置かれ(odv_exp.c:495-516)、columns を持ちません。そのため例外になり、Catch(SqlExportLogic.vb:366-368)が黙って捨てるので、COMMENT ON が出ません。コメントの要素に "columns":[] を足すと出ます。
該当:- odv_sql.c:459-471
- odv_sql.c:490-492
- odv_sql.c:894-895
- odv_expdp.c:405-409
- odv_exp.c:495-516
- Logics/Export/SqlExportLogic.vb:281-282
- Logics/Export/SqlExportLogic.vb:366-368
再現:create table T_SMOKE1 (ID NUMBER(10) NOT NULL, NAME VARCHAR2(40 CHAR), …, CONSTRAINT PK_T_SMOKE1 PRIMARY KEY (ID)) と create table T_SMOKE2 (K VARCHAR2(10) NOT NULL, T1_ID NUMBER(10), PRICE NUMBER(8,2) DEFAULT 0, FLAG CHAR(1), DT DATE, CONSTRAINT PK_T_SMOKE2 PRIMARY KEY (K), CONSTRAINT FK_T_SMOKE2_T1 FOREIGN KEY (T1_ID) REFERENCES T_SMOKE1 (ID), CONSTRAINT CK_T_SMOKE2_FLAG CHECK (FLAG IN ('Y', 'N'))) を作り、create index IX_T_SMOKE2_DT on T_SMOKE2 (DT) と comment on table / column を付けて行を入れます。exp と expdp で取り、製品の Workspace から TablePreview を開かずに SQL を出した場合と、TablePreview で開いてから出した場合を比べます。
証拠:cases/smoke/results/21/dumps/odv_s_smoke_21_e.dmp、cases/smoke/results/21/dumps/odv_s_smoke_21_dp.dmp、verify/known_x/case_kx/results/21/dumps/odv_v_kx_21_e.dmp、verify/vbsql/smoke/T_SMOKE2_e.sql、verify/vbsql/smoke/T_SMOKE2_dp.sql

### M-18 exp のダンプから他 DBMS 向けの SQL を出すと、NUMBER(*,0) や VARCHAR2(10 CHAR) の括弧の中身がそのまま写り、CREATE TABLE が構文誤りになってその表の行が入らない
形式:SQL エクスポート (PostgreSQL / MySQL / SQL Server)、exp 従来型・DIRECT=Y
症状:exp の DDL は INTEGER・SMALLINT・INT・DECIMAL・NUMERIC (精度なし) を NUMBER(*,0) と書き、NUMBER(*,2) は NUMBER(*,2) のまま書きます。これが PostgreSQL では "I" NUMERIC(*,0)・"NS2" NUMERIC(*,2)、MySQL では `I` DECIMAL(*,0)、SQL Server では [I] DECIMAL(*,0) になります。CHAR セマンティクスの列も同じです。VARCHAR2(10 CHAR) は PostgreSQL・MySQL で VARCHAR(10 CHAR)、SQL Server で NVARCHAR(10 CHAR) になります。CHAR(3 CHAR) は CHAR(3 CHAR) / NCHAR(3 CHAR)、VARCHAR2(4000 CHAR) は VARCHAR(4000 CHAR) になります。PostgreSQL 16 に流すと、T_INT と T_NUMPS2 の CREATE TABLE が syntax error at or near "*" で失敗し、続く INSERT 12 文 (各表 6 文) がすべて relation does not exist になります。'*' も 'n CHAR' も MySQL・SQL Server の文法に無いので、同じく表を作れません。
条件:exp (従来型・DIRECT=Y) のダンプで、ANSI の数値型 (INTEGER・SMALLINT・INT・DECIMAL・NUMERIC) か NUMBER(*,s) の列、または CHAR セマンティクス (VARCHAR2(n CHAR)・CHAR(n CHAR)) の列を持つ表。11.2 / 21c / 23.26 の exp で起きます。CHAR セマンティクスは AL32UTF8 の DB でよく使われます。expdp のダンプでは型名が NUMBER や VARCHAR2(40) のようなバイト長に変わる別の誤りがあるため、この構文誤りは起きません。Oracle 方言の SQL は型をそのまま使うので通ります。
影響:SQL エクスポート (全表・表ごと) で PostgreSQL・MySQL・SQL Server を選ぶと、該当する表の CREATE TABLE が失敗し、その表の INSERT もすべて失敗します。エクスポート自体は成功と表示されるので、流して初めて気づきます。プレビューと CSV には影響しません。
原因:exp の列の type_str は DDL の型文字列そのままで (odv_exp.c:749)、'NUMBER(*,0)' や 'VARCHAR2(10 CHAR)' が入ります。map_oracle_to_target_type は基本名だけを見て、括弧の部分を文字列のまま後ろに付けます (NUMBER は odv_sql.c:210 / 273 / 328、VARCHAR2・CHAR は odv_sql.c:200, 244, 264, 301, 319, 359)。parse_number_prec_scale (odv_sql.c:110-129) は '*' を atoi で 0 と読むだけです。'*' を桁数に直す処理も、' CHAR' / ' BYTE' を取り除く処理もありません。
該当:- odv_exp.c:749
- odv_sql.c:110-129
- odv_sql.c:210
- odv_sql.c:273
- odv_sql.c:328
- odv_sql.c:200
- odv_sql.c:244
- odv_sql.c:264
- odv_sql.c:301
- odv_sql.c:319
- odv_sql.c:359
再現:AL32UTF8 の DB で次の表を作り、数行ずつ入れます。create table T_INT (ID NUMBER(6) primary key, I INTEGER, S SMALLINT, IT INT, DC DECIMAL, DN8 NUMERIC(8), DD DEC(10,2), DNP NUMERIC); create table T_NUMPS2 (ID NUMBER(6) primary key, NS2 NUMBER(*,2)); create table T_TYP (ID NUMBER(4), VC VARCHAR2(10 CHAR), CC CHAR(3 CHAR), V4K VARCHAR2(4000 CHAR)); exp user/pass owner=<スキーマ> file=x.dmp で取ります (DIRECT=Y でも同じ)。製品の SQL エクスポートで PostgreSQL・MySQL・SQL Server を選び、出力の CREATE TABLE を見るか、PostgreSQL に psql -f で流します。
証拠:cases/num/main/results/21/dumps/odv_c_num_21_e.dmp、cases/num/main/results/21/dumps/odv_c_num_21_ed.dmp、cases/chr/main/results/21/dumps/odv_c_chr_21_e.dmp

### M-19 他 DBMS 向けの SQL で、FLOAT が REAL / DOUBLE に、精度なしの NUMBER が DECIMAL(38,10) になり、桁と値の範囲が失われるうえ、位取りによっては CREATE TABLE が通らない
形式:SQL エクスポート (PostgreSQL / MySQL / SQL Server)、exp・expdp
症状:PostgreSQL では、exp の FLOAT(1)〜FLOAT(24) が REAL、FLOAT(25)〜FLOAT(126) が DOUBLE PRECISION になります。T_FLT1 は 18 行中 5 行 (1E+125、5E+125、-5E+125、1E+40、-1E-130) が out of range for type real で入りません。入った行も、F24 の 0.33333333 が 0.33333334、1234.5678 が 1234.5677 になります。FLOAT(126) の 1/3 (38 桁) は 0.3333333333333333 になります。expdp でも、FLOAT(39)〜FLOAT(126) と REAL・DOUBLE PRECISION の列は型名が NUMBER(39)〜NUMBER(126) になり、精度が 38 を超えるので同じく DOUBLE PRECISION になります。MySQL の FLOAT / DOUBLE、SQL Server の FLOAT(24) / FLOAT(53) (expdp では FLOAT) も同じ幅です。MySQL・SQL Server では、精度指定の無い NUMBER が DECIMAL(38,10) になります。小数 11 桁目以降は丸められ (0.00000000001 は 0)、1E+28 以上の値は範囲外になります。位取りも写し方が誤っています。NUMBER(2,-84) は DECIMAL(86,0)、NUMBER(3,5) は DECIMAL(3, 5)、NUMBER(38,38) は DECIMAL(38, 38)、NUMBER(2,127) は DECIMAL(2, 127) になります。MySQL (精度 65 まで、位取り 30 まで、位取り≦精度) では 4 つとも作れません。SQL Server (位取り≦精度≦38) では DECIMAL(38,38) だけが有効です。PostgreSQL 16 は NUMERIC(3,5) も作れます。
条件:PostgreSQL・MySQL・SQL Server 方言の SQL エクスポートで、次の列を持つ表。FLOAT / REAL / DOUBLE PRECISION の列 (exp はすべての精度。expdp は FLOAT(39) 以上と REAL・DOUBLE PRECISION。expdp の FLOAT(1)〜FLOAT(38) は NUMBER(p) になる別の誤りがあります)。精度指定の無い NUMBER の列 (exp・expdp、MySQL・SQL Server)。位取りが負、精度より大きい、または 30 を超える NUMBER の列 (exp・expdp)。版によりません。
影響:他 DBMS へ移した数値が黙って丸められます。範囲外の行は INSERT が失敗して欠けます。位取りが範囲外の列を持つ表は、MySQL・SQL Server で CREATE TABLE が失敗し、その表の行がすべて入りません。プレビュー・CSV・Oracle 方言の SQL には影響しません。
原因:map_oracle_to_target_type の数値型の写像が、Oracle の NUMBER (10 進 38 桁、1E-130〜1E+126) を表せない型を選んでいます。FLOAT(p) は p≦24 なら REAL / FLOAT / FLOAT(24)、それ以外は DOUBLE PRECISION / DOUBLE / FLOAT(53) にします (odv_sql.c:214-220 / 276-282 / 331-338)。expdp では FLOAT(p) の型名が NUMBER(p) になります。このため p が 38 を超える列は、NUMBER の分岐の『精度 38 超・位取り 0 なら浮動小数点』で DOUBLE PRECISION / DOUBLE / FLOAT になります (odv_sql.c:205 / 268 / 323)。Oracle の FLOAT(p) は 2 進の精度を指定するだけで、値は 10 進の NUMBER として格納されます。そのため範囲も桁も 2 進浮動小数点と一致しません。精度指定の無い NUMBER は、MySQL / SQL Server で DECIMAL(38,10) に固定されます (odv_sql.c:274 / 329)。負の位取りは精度に足して DECIMAL(p+|s|,0) にし (odv_sql.c:207 / 270 / 325)、それ以外は括弧をそのまま写します (odv_sql.c:210 / 273 / 328)。各 DBMS の精度・位取りの上限は見ていません。
該当:- odv_sql.c:214-220
- odv_sql.c:276-282
- odv_sql.c:331-338
- odv_sql.c:205
- odv_sql.c:268
- odv_sql.c:323
- odv_sql.c:274
- odv_sql.c:329
- odv_sql.c:207
- odv_sql.c:270
- odv_sql.c:325
- odv_sql.c:210
- odv_sql.c:273
- odv_sql.c:328
再現:create table T_FLT1 (ID NUMBER(6) primary key, F1 FLOAT(1), F24 FLOAT(24), F38 FLOAT(38)); に 1/3、1234.5678、1E+40、1E+125、-1E-130 を入れます。create table T_FLT2 (ID NUMBER(6) primary key, F126 FLOAT(126), FR REAL, FDP DOUBLE PRECISION); に 1/3 を入れます。create table T_NUM (ID NUMBER(6) primary key, V NUMBER); に 0.00000000001 と 1E+30 を入れます。create table T_NUMPS (ID NUMBER(6) primary key, N3_5 NUMBER(3,5), N38_38 NUMBER(38,38), N2_M84 NUMBER(2,-84), N2_127 NUMBER(2,127)); も作ります。exp と expdp で取り、PostgreSQL・MySQL・SQL Server 向けに SQL をエクスポートして、CREATE TABLE の型と流した結果を見ます。
証拠:cases/num/main/results/21/dumps/odv_c_num_21_e.dmp、cases/num/main/results/21/dumps/odv_c_num_21_dp.dmp

### M-28 SQL エクスポートで、INSERT の列リストが固定長バッファ (4096 バイト) で途中で切れ、列の多い表・列名の長い表では INSERT が全行構文誤りになる
形式:SQL エクスポート (全方言、exp・expdp 共通)
症状:1000 列の表 T_C1000 (列名 C0001〜C1000) の INSERT は INSERT INTO "ODV_C_SHP_CO"."T_C1000" ("C0001", …, "C0450", "C0451" で切れ、) VALUES ( が無いまま値が続きます (… "C0451"4, 1002, 1003, …)。30 文字の列名 148 個を持つ T_WIDE (ID と Z を合わせて 150 列) では、INSERT の前半がちょうど 4095 バイトの … "C120_XXX で切れます。Oracle に流すと ORA-00917・ORA-01747・ORA-00972 で全行が入りません。PostgreSQL でも 0 行です。4 方言とも同じ位置で切れ、全表の SQL・表ごとの SQL のどちらでも起きます。ファイルの作成はエラーにならず、正常に終わります。
条件:INSERT INTO "スキーマ"."表" (列リスト) VALUES ( の長さが 4096 バイト以上になる表。1 列あたり「列名 + 4 バイト」なので、5 文字の列名で約 450 列、30 文字で約 120 列、12.2 以降の 128 バイトの列名なら約 31 列で溢れます。100 列・列リスト約 3440 バイトの表は正常です。exp (従来型・DIRECT=Y)・expdp、全版、4 方言 (Oracle / PostgreSQL / MySQL / SQL Server) のすべてで起きます。
影響:SQL エクスポートのうち DLL が書く経路 (既定の SqlInferInteger=False で、プレビューを開いていない状態の単表エクスポートと一括エクスポート)。その表の INSERT が全行使えないファイルになります。TablePreview を開いた状態や SqlInferInteger=True のときは VB の SqlExportLogic.BuildInsertPrefix (StringBuilder) が書くので起きません。
原因:SQL_CONTEXT の insert_prefix は char[4096] の固定長です (odv_sql.c:30)。build_insert_prefix は SAFE_APPEND (odv_sql.c:690-694) で INSERT の前半を組み立てます。snprintf が切り詰めると remain = 0 にするだけで、以後の列名も ) VALUES ( も書きません (odv_sql.c:716-725)。切れたことは呼び出し元に伝わらず、sql_row_callback が切れた接頭辞をそのまま各行の前に書きます (odv_sql.c:772)。
該当:- odv_sql.c:30
- odv_sql.c:690-694
- odv_sql.c:716-725
- odv_sql.c:772
- Open DUMP Viewer.vb:829-851
- BulkExportLogic.vb:50-60
再現:PL/SQL で create table T_C1000 (C0001 NUMBER, C0002 NUMBER, …, C1000 NUMBER) を作り、(1, 2, …, 1000) などを 5 行入れます。30 文字の列名なら、ID と 30 文字の列 148 個と Z の表に 2 行でも起きます。exp (従来型・DIRECT=Y) または expdp でスキーマを出力し、製品でその表をプレビューで開かずに SQL エクスポートします。出力の INSERT 文に ) VALUES ( がありません。
証拠:cases/shape/cols/results/21/dumps/odv_c_shp_co_21_e.dmp、cases/shape/cols/results/21/dumps/odv_c_shp_co_21_dp.dmp、cases/output/main/results/21/dumps/odv_c_out_21_e.dmp (T_WIDE)

### M-32 TablePreview を開いた状態(VB の経路)の SQL エクスポートが列定義を「NOT NULL DEFAULT 値」の順で書くので、NOT NULL かつ DEFAULT 付きの列を持つ表の CREATE TABLE が ORA-00907 になる
形式:SQL エクスポート(Oracle 方言、VB の経路)。exp 従来型・DIRECT=Y
症状:T_DATE(ID NUMBER(4) NOT NULL, D DATE, Z CHAR(1) DEFAULT 'Z' NOT NULL)の VB の経路の CREATE TABLE は、"Z" CHAR(1) NOT NULL DEFAULT 'Z' と書きます。21c に流すと、この行で ORA-00907(missing right parenthesis)になり、表ができません。続く ALTER TABLE ADD CONSTRAINT と 29 行の INSERT は、すべて ORA-00942 で失敗します。DEFAULT 'Z' NOT NULL の順なら作れます。DEFAULT の切り詰め(M-31)を直した T_MIX の DDL でも、Z の行で ORA-00907 になります。dt の 7 表はすべて同じ Z 列を持つので、どれもこの誤りに当たります。
条件:TablePreview を開いた状態で SQL エクスポート(Oracle 方言)を行う場合と、SqlInferInteger=True の場合(単表・一括)。「DROP TABLE + CREATE TABLE 出力」が有効(既定)のとき。exp のダンプで、NOT NULL と DEFAULT の両方を持つ列がある表が対象です。expdp のダンプは DEFAULT を取らない(M-17)ので、DEFAULT が書かれず、この誤りには当たりません。
影響:NOT NULL と DEFAULT を両方持つ列(状態フラグ、作成日時など)はよくあるので、表を開いてからエクスポートするふつうの手順で、その表の SQL が丸ごと流せません。表ができないので、制約と INSERT もすべて失敗します。
原因:SqlExportLogic.WriteCreateTable は、列ごとに型の後ろへ " NOT NULL" を足し(SqlExportLogic.vb:215-218)、その後ろに " DEFAULT 値" を足します(SqlExportLogic.vb:220-227)。Oracle の列定義では、DEFAULT を NOT NULL などの列制約より前に書く必要があるので、この順では構文誤りになります。DLL の経路の CREATE TABLE は NOT NULL も DEFAULT も書かない(M-17)ので、この誤りはありません。
該当:- Logics/Export/SqlExportLogic.vb:215-218
- Logics/Export/SqlExportLogic.vb:220-227
再現:create table T_DATE (ID NUMBER(4) NOT NULL, D DATE, Z CHAR(1) DEFAULT 'Z' NOT NULL, CONSTRAINT PK_T_DATE PRIMARY KEY (ID)) に行を入れて exp で取ります。製品で T_DATE を TablePreview で開き、Oracle 方言で SQL エクスポートして、出力を sqlplus で流します。
証拠:cases/dt/results/21/dumps/odv_c_dt_21_e.dmp、verify/vbsql/f0/T_DATE.sql、verify/vbsql/run/date_full_am.out、verify/dt/vb_run/vb_create_mix_fixD.out、verify/dt/vb_run/vb_order.out

