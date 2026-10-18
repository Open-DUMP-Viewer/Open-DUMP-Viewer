# SQL エクスポートの型の対応と値の書き方 (仕様) — 群13a 作成、群13b (VB 経路) と共通

対象: SQL エクスポートの 4 方言 (Oracle / PostgreSQL / MySQL / SQL Server)。DLL の経路 (odv_sql.c) と
VB の経路 (Logics/Export の SqlExportLogic / ExportHelper、ODBC の型) は、同じ Oracle の型・同じ値に対して
**この表どおりの同じ結果**を出す。CSV の値の書き方は 9 章。

根拠は各 DBMS の公式文書 (2026-10-18 に参照。末尾に URL)。Oracle の挙動のうち実 DB (11.2 XE / 21c XE /
23.26 Free、いずれも AL32UTF8) で確かめたものは「実測」と書く。確かめられないものは「未確認」と書く。

## 0. 前提と用語

- 型名は exp と expdp で同じ形で届く (群8 以降): `VARCHAR2(10)` `VARCHAR2(10 CHAR)` `CHAR(3 CHAR)` `NVARCHAR2(n)`
  `NUMBER` `NUMBER(p)` `NUMBER(p,s)` `NUMBER(*,s)` `FLOAT(p)` `TIMESTAMP(p)` `TIMESTAMP(p) WITH TIME ZONE`
  `TIMESTAMP(p) WITH LOCAL TIME ZONE` `INTERVAL YEAR(p) TO MONTH` `INTERVAL DAY(p) TO SECOND(s)` `RAW(n)` `UROWID(n)`
  `VECTOR(n, FMT[, SPARSE])` `"OWNER"."TYPE"` (VARRAY・オブジェクト型) など。exp の古い表記 `INTERVAL DAY (9) TO SECOND (9)`
  (括弧の前に空白) もある。**型名は大文字小文字を区別せず、括弧の前後・中の空白を無視して読む**。
- 括弧の中の `n BYTE` / `n CHAR` は長さの意味。`*` は精度 38 (Oracle の既定)。
- 「値」は解析エンジンが行コールバックで渡す文字列 (表示用の書式ではなく、SQL 出力用に固定した書式。6 章)。
- 「NOTE」= SQL ファイルに `-- NOTE: ...` の行を書くこと。値を書けないとき・型で表せないときは、**黙って別の値を
  入れず**、NOTE を残して NULL (値) または近い型 (型) にする。NOTE は英文 1 行で、本文は**切らずに全文**
  (長い DEFAULT の式も。以前の DLL は 511 バイトで切っていた、m_long C_LDEF)。本文に改行があれば、続きの行も
  `--   ` で始めて注釈のままにする (改行の後が SQL として読まれないように。10/18 追加)。
- 「行の NOTE」は、その INSERT (または PL/SQL ブロック) の直前に書く。「表の NOTE」は表の見出し (`-- Table:`) の
  直後に書く (CREATE TABLE を出さない設定でも書く)。

## 1. 識別子

| 方言 | 書き方 | 名前の中の引用符 |
|---|---|---|
| Oracle | `"NAME"` | `"` を `""` に |
| PostgreSQL | `"NAME"` | `"` を `""` に |
| MySQL | `` `NAME` `` | `` ` `` を ` `` ` に |
| SQL Server | `[NAME]` | `]` を `]]` に |

- 文字列リテラルの中に名前を書くとき (Oracle の `EXECUTE IMMEDIATE 'DROP TABLE "S"."T" ...'`、SQL Server の
  `OBJECT_ID(N'...')`) は、さらに `'` を `''` にする (表名 `it's` / `T'Q`)。
- 名前は Oracle の名前をそのまま使う (大文字小文字を変えない)。PostgreSQL では二重引用符で囲むので大文字のまま残る。

## 2. 型の対応 (CREATE TABLE の列の型)

### 2.1 文字

| Oracle | Oracle 方言 | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| VARCHAR2(n) / VARCHAR2(n BYTE) | そのまま | VARCHAR(n) | VARCHAR(n) | NVARCHAR(n)、n>4000 は NVARCHAR(MAX) |
| VARCHAR2(n CHAR) | そのまま | VARCHAR(n) | VARCHAR(n) | NVARCHAR(2n)、2n>4000 は NVARCHAR(MAX) |
| NVARCHAR2(n) | そのまま | VARCHAR(n) | VARCHAR(n) | NVARCHAR(n)、n>4000 は NVARCHAR(MAX) |
| CHAR(n) / CHAR(n BYTE) / CHAR(n CHAR) | そのまま | CHAR(n) | CHAR(n)、n>255 は VARCHAR(n) | NCHAR(n) |
| NCHAR(n) | そのまま | CHAR(n) | CHAR(n)、n>255 は VARCHAR(n) | NCHAR(n) |
| CLOB / NCLOB / LONG | そのまま | TEXT | LONGTEXT | NVARCHAR(MAX) |

長さの意味:
- Oracle の `n BYTE` は DB 文字セットでのバイト数、`n CHAR` と NVARCHAR2/NCHAR の n は文字数。
- PostgreSQL の VARCHAR(n)/CHAR(n) の n は文字数 (上限 10,485,760)。MySQL の VARCHAR(M)/CHAR(M) の M も文字数
  (CHAR は 255 まで)。どちらも Oracle の n BYTE 以上を必ず収める (1 文字は 1 バイト以上) ので n をそのまま使う。
- SQL Server の NVARCHAR(n)/NCHAR(n) の n は UTF-16 の単位数 (byte-pair、4000 まで)。n バイトの UTF-8 / 1 バイト系 /
  2 バイト系の文字列は n 単位以下なので BYTE は n のまま。CHAR の文字数 n は補助文字 (U+10000 以上、2 単位) を
  含むと最大 2n 単位なので NVARCHAR は 2n にする。**NCHAR(n) は 2n にしない** (固定長なので 2n にすると空白が
  余分に付き、値が変わる)。補助文字を含む CHAR(n CHAR) の値は NCHAR(n) に入らず INSERT が失敗する (表の NOTE に書く)。
- NVARCHAR2/NCHAR の n: 各国語文字セットが AL16UTF16 なら n は UTF-16 の単位数で SQL Server と同じ。UTF8 の場合の
  補助文字の数え方は未確認 (AL16UTF16 として扱う)。
- MySQL の行の大きさの上限 (全列で 65,535 バイト。utf8mb4 の VARCHAR(n) は 4n バイトと数える) を超える表は
  CREATE TABLE が失敗する。DLL は表全体の見積りが上限を超えるとき、VARCHAR(n) (n > 255) を TEXT に替え、表の NOTE に書く。
- MySQL の文字セットは utf8mb4 を前提とする (DB・表の既定。CREATE TABLE に文字セットは書かない)。
- SQL Server の文字の列は N 型 (Unicode) にする。VARCHAR (コードページ) にはしない。

### 2.2 数値

| Oracle | Oracle 方言 | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| NUMBER (精度なし) | NUMBER | NUMERIC | DECIMAL(65,30) + 表の NOTE | DECIMAL(38,10) + 表の NOTE |
| NUMBER(p) / NUMBER(p,0) | そのまま | NUMERIC(p,0) | DECIMAL(p,0) | DECIMAL(p,0) |
| NUMBER(*,s) | そのまま | p=38 として下の規則 | 同 | 同 |
| NUMBER(p,s) 0<s≤p | そのまま | NUMERIC(p,s) | DECIMAL(p,s) (s≤30)、s>30 は下 | DECIMAL(p,s) |
| NUMBER(p,s) s<0 | そのまま | NUMERIC(p−s,0) | DECIMAL(p−s,0) (p−s≤65) | DECIMAL(p−s,0) (p−s≤38) |
| NUMBER(p,s) s>p | そのまま | NUMERIC(s,s) | DECIMAL(s,s) (s≤30) | DECIMAL(s,s) (s≤38) |
| FLOAT / FLOAT(p) | そのまま | NUMERIC | DOUBLE | FLOAT(53) |
| BINARY_FLOAT | そのまま | REAL | FLOAT | REAL |
| BINARY_DOUBLE | そのまま | DOUBLE PRECISION | DOUBLE | FLOAT(53) |

- Oracle の NUMBER は 10 進 38 桁、絶対値 1E-130 以上 1E+126 未満 (精度 1〜38、位取り -84〜127)。FLOAT(p) は
  NUMBER の一種で p は 2 進の精度 (1〜126、10 進で約 p×0.30103 桁)、値は 10 進で格納される。
- PostgreSQL の NUMERIC(p,s) は p≦1000。位取りが負・精度より大きい形は 15 以降でしか作れないので、どの版でも
  通る形 (上表) に直す。精度なし NUMERIC は小数点の前 131,072 桁・後 16,383 桁まで。Oracle の値は全部入る。
- MySQL の DECIMAL(M,D) は M≦65、D≦30、D≦M。収まらない位取り (s>30) は D=30、精度は min(p 相当, 65) にし、
  表の NOTE に書く (小数 31 桁目以降は MySQL が丸める)。精度なし NUMBER の DECIMAL(65,30) は整数部 35 桁・
  小数 30 桁まで。
- SQL Server の DECIMAL(p,s) は 1≦p≦38、0≦s≦p。精度なし NUMBER の DECIMAL(38,10) は整数部 28 桁・小数 10 桁まで。
- 収まらない**値**は行の NOTE に書く: 小数の桁が列の位取りを超える値 (DBMS が丸める) は「rounded」、整数部の桁が
  収まらない値 (INSERT が失敗する) は「out of range」。値はそのまま書く (丸めた値を自分で作らない)。
- FLOAT(p) を DOUBLE / FLOAT(53) にするのは、範囲 (1E-130〜1E+126) が IEEE 倍精度に収まるため。有効数字は 15 桁
  まで正確に往復する。p>49 (10 進 15 桁超) の列は 16 桁目以降が丸められうるので表の NOTE に書き、有効数字が 15 桁を
  超える値は行の NOTE に書く。PostgreSQL は NUMERIC なので丸めない。
- 整数の型 (INTEGER/BIGINT) には写さない (NUMBER(p) は DECIMAL(p,0) のまま)。

### 2.3 日付・時刻

| Oracle | Oracle 方言 | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| DATE | DATE | TIMESTAMP(0) | DATETIME | DATETIME2(0) |
| TIMESTAMP(p) | そのまま | TIMESTAMP(min(p,6)) | DATETIME(min(p,6)) | DATETIME2(min(p,7)) |
| TIMESTAMP(p) WITH TIME ZONE | そのまま | TIMESTAMPTZ(min(p,6)) | DATETIME(min(p,6)) (UTC) + 表の NOTE | DATETIMEOFFSET(min(p,7)) |
| TIMESTAMP(p) WITH LOCAL TIME ZONE | そのまま | TIMESTAMPTZ(min(p,6)) | DATETIME(min(p,6)) (UTC) + 表の NOTE | DATETIMEOFFSET(min(p,7)) |
| INTERVAL YEAR(p) TO MONTH | そのまま | INTERVAL YEAR TO MONTH | VARCHAR(64) + 表の NOTE | NVARCHAR(64) + 表の NOTE |
| INTERVAL DAY(p) TO SECOND(s) | そのまま | INTERVAL DAY TO SECOND(min(s,6)) | VARCHAR(64) + 表の NOTE | NVARCHAR(64) + 表の NOTE |

- Oracle の小数秒は 0〜9 桁 (既定 6)。PostgreSQL と MySQL は 6 桁、SQL Server は 7 桁まで。それより細かい桁は
  **値を書くときに切り捨て** (丸めない。繰り上がりで日付が変わらないように)、p が上限を超える列は表の NOTE に書く
  (例 `column "TS9" TIMESTAMP(9): PostgreSQL keeps 6 fractional digits; digits 7-9 are dropped`)。
- 範囲: Oracle DATE は 4712 BC〜9999 AD。PostgreSQL の timestamp は 4713 BC〜294276 AD (紀元前を書ける)。
  MySQL の DATETIME の公式の範囲は 1000-01-01〜9999-12-31 (それより前は「動くかもしれないが保証しない」)。
  SQL Server の DATETIME2 / DATETIMEOFFSET は 0001-01-01〜9999-12-31。**紀元前の値は MySQL・SQL Server では書けない**
  (NULL + 行の NOTE)。MySQL の 1000 年より前の値はそのまま書く (公式の範囲外である旨を表の NOTE に書くのは、
  そういう値が実際にあったときだけ: 行の NOTE)。
- 暦: Oracle は 1582-10-15 より前をユリウス暦で持つ。PostgreSQL・MySQL・SQL Server は先発グレゴリオ暦。年月日の
  表記をそのまま写す (同じ「日」ではなく同じ「表記」になる)。
- タイムゾーン: PostgreSQL の timestamptz は瞬間 (UTC) だけを持ち、元の時差・地域名は残らない。MySQL には
  タイムゾーン付きの型が無いので、UTC の時刻を DATETIME に入れる。SQL Server の datetimeoffset は時差 (±14:00
  まで) を持つが地域名は持たない (地域名の値はその瞬間の時差にする)。いずれも表の NOTE に書く。
- TSLTZ (WITH LOCAL TIME ZONE) は「瞬間」の型。値はダンプの DBTIMEZONE での時刻として届くので、DBTIMEZONE の
  時差を付けて瞬間として書く。DBTIMEZONE がダンプに無いときは、時差なしで書き (読み込み側のセッションの
  タイムゾーンで解釈される) 表の NOTE に書く。
- PostgreSQL の INTERVAL の秒の小数は 6 桁まで (切り捨て、上と同じ)。MySQL・SQL Server には INTERVAL が無いので
  Oracle の表記の文字列 (`+2-3`、`-3 04:05:06.123`) を入れる。

### 2.4 2 進・LOB・その他

| Oracle | Oracle 方言 | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| RAW(n) | そのまま | BYTEA | VARBINARY(n) | VARBINARY(n)、n>8000 は VARBINARY(MAX) |
| LONG RAW / BLOB | そのまま | BYTEA | LONGBLOB | VARBINARY(MAX) |
| BFILE | BFILE | VARCHAR(530) + 表の NOTE | VARCHAR(530) + 表の NOTE | NVARCHAR(530) + 表の NOTE |
| ROWID | ROWID | VARCHAR(18) | VARCHAR(18) | NVARCHAR(18) |
| UROWID(n) | そのまま | TEXT | TEXT | NVARCHAR(MAX) |
| XMLTYPE | XMLTYPE | XML | LONGTEXT | XML |
| JSON (21c 以降のネイティブ) | JSON | JSON | JSON + 表の NOTE | NVARCHAR(MAX) |
| VECTOR(...) (23ai) | そのまま | TEXT + 表の NOTE | LONGTEXT + 表の NOTE | NVARCHAR(MAX) + 表の NOTE |
| BOOLEAN (23ai) | BOOLEAN | BOOLEAN | BOOLEAN | BIT |
| "OWNER"."TYPE" (VARRAY・オブジェクト型)・その他 | そのまま | TEXT + 表の NOTE | LONGTEXT + 表の NOTE | NVARCHAR(MAX) + 表の NOTE |

- JSON: PostgreSQL は `JSON` (入力の文字列をそのまま保つ。`JSONB` はキーの順・重複を変える)。MySQL の JSON 型は
  キーを並べ替え・重複を除いて格納する (表の NOTE)。SQL Server はネイティブの json 型が 2025 版からなので
  NVARCHAR(MAX) にする。
- VECTOR は PostgreSQL・MySQL 8.4・SQL Server 2022 の標準の型に無い (pgvector は拡張、MySQL の VECTOR は 9.0 以降、
  SQL Server の vector は 2025 版)。`[1,2,3]` の文字列として入れる。
- UROWID の論理形は `*` + Base64 (最大約 5,400 字) なので可変長の最大にする。
- VARRAY・オブジェクト型の値は解析エンジンが値を作れない (`%VARRAY%` など) ので NULL (5 章)。印でない値
  (exp の VARRAY は生のバイトが表示に出る) も同じく NULL + 行の NOTE (文字列リテラルはオブジェクト型の列に
  入らない。Oracle では ORA-00932)。Oracle 方言の型は
  元の型名のまま (CREATE TYPE が先に要る。表の NOTE に書く)。

## 3. 列の定義・制約・索引・コメント

### 3.1 列の定義
`<名前> <型> [DEFAULT <式>] [NOT NULL]` の順 (**DEFAULT が先**。Oracle は NOT NULL を先に書くと ORA-00907、
M-32)。PostgreSQL・MySQL・SQL Server はどちらの順でも通るが同じ順にする。

DEFAULT の式:
- Oracle 方言: ダンプの式をそのまま (前後の空白と末尾の改行を除く)。
- 他方言: 移せるのは、数値リテラル (`0`、`-1.5`)、文字列リテラル (`'z'`、引用符は方言の規則で書き直す)、`NULL`
  だけ。それ以外の式 (SYSDATE、SYS_GUID()、関数・順序) は書かず、表の NOTE に元の式を残す
  (`DEFAULT of column "C" (SYSDATE) is an Oracle expression and was not written`)。
- MySQL の TEXT/BLOB/JSON の列の DEFAULT は式の形 `DEFAULT ('z')` (8.0.13 以降) で書く。
- 長い DEFAULT は他の長い DDL と同じく行を分ける (8 章)。

### 3.2 制約・索引の書き出しの順序
1 つの表について: `CREATE TABLE` → INSERT → 主キー・一意・CHECK の `ALTER TABLE ... ADD CONSTRAINT` →
`CREATE INDEX` → コメント。外部キーは、全表の SQL ではファイルの最後 (全表の後) にまとめ、表ごとの SQL では
その表のファイルの最後に書く (参照先の表が先に作られている必要があるため)。

| 制約 | Oracle | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| 主キー | `ALTER TABLE t ADD CONSTRAINT n PRIMARY KEY (c, ...)` | 同 | 同 | 同 |
| 一意 | `... UNIQUE (c, ...)` | 同 | 同 | 同 |
| CHECK | `... CHECK (<条件>)` | 条件を書き直せるときだけ (下) | 同左 | 同左 |
| 外部キー | `... FOREIGN KEY (c) REFERENCES p (c) [ON DELETE CASCADE / SET NULL]` | 同 | 同 | 同 |
| 索引 | `CREATE [UNIQUE] INDEX n ON t (c [DESC], ...)` / 関数索引は式のまま | 列の索引だけ | 同 | 同 |

- 名前の無い制約 (exp の無名 UNIQUE・FK、SYS_C... の名前) は `CONSTRAINT n` を付けずに書く (名前は DB が付ける)。
- 状態: Oracle 方言は元の状態を付ける (`DEFERRABLE`、`INITIALLY DEFERRED`、`DISABLE`、`NOVALIDATE`)。
  PostgreSQL は主キー・一意・外部キーの `DEFERRABLE [INITIALLY DEFERRED]` を付ける。MySQL・SQL Server に
  延期の指定は無い (表の NOTE)。無効 (DISABLE) の制約は、SQL Server は外部キー・CHECK を `WITH NOCHECK` で作って
  `NOCHECK CONSTRAINT` にする。PostgreSQL・MySQL は無効の制約を作れないので書かず表の NOTE に残す
  (MySQL の CHECK は `NOT ENFORCED` で書ける)。
- CHECK の条件・関数索引の式は Oracle の式。他方言には、識別子 (`"C"`)・数値・文字列リテラル・比較と論理の演算子
  (`= <> != < > <= >= AND OR NOT IN BETWEEN LIKE IS NULL`)・算術・括弧だけでできている式に限り、識別子の引用を方言に
  直して書く。関数呼び出しなどを含む式は書かず、表の NOTE に元の式を残す。`IS` の後は `NULL` / `NOT NULL` だけを認める
  (`J IS JSON` などは Oracle の述語なので書かない)。
- NOT NULL 制約は列の定義に書く (名前付きの NOT NULL 制約の名前は移さない)。主キーの列が主キーのためだけに
  NOT NULL のとき (自分の NOT NULL 制約が無い) は列に NOT NULL を書かない (元の表と同じく主キーに任せる。expdp は
  TABLE_T の列の `<CON>` (CONTYPE 7) の有無で見分ける。exp の DDL はもともとそう書いている)。
- 主キー・一意の制約が使う索引は制約が作る。同じ名前の索引を別に CREATE INDEX しない。
- 制約でない一意の索引 (`CREATE UNIQUE INDEX` だけで作ったもの) は `CREATE UNIQUE INDEX` で書き、表の制約より前に
  置く (同じ列の主キーは Oracle ではその索引を使う。後に置くと ORA-02261 / ORA-01408)。
- CHECK の条件は、全体を囲む括弧を 1 組外してから `CHECK (<条件>)` にする (exp は外側の括弧付き、expdp は括弧
  無しで持つので、同じ表が同じ SQL になる。`(A) OR (B)` のように全体を囲まない括弧は外さない)。
- 索引の列の並びは括弧の内側とカンマの前の空白を詰める (exp は `("A" , "B" )` と書く)。
- exp が全表の後に置く関数索引・DESC 索引 (と外部キー) も、その表の制約・索引と同じ並び (8.1) に入れる。コメントより
  前、同じ種類の中は名前の順 (10/18 統括の決定 (g)。それまで DLL は表の DDL の後にダンプの順で足していた)。
- MySQL で TEXT / BLOB の列の索引・一意制約は長さの指定が要るので書かず、表の NOTE に残す。

### 3.3 コメント

| | Oracle | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| 表 | `COMMENT ON TABLE t IS '...'` | 同 | `ALTER TABLE t COMMENT = '...'` | `EXEC sp_addextendedproperty N'MS_Description', N'...', N'SCHEMA', N's', N'TABLE', N't'` |
| 列 | `COMMENT ON COLUMN t.c IS '...'` | 同 | `ALTER TABLE t MODIFY COLUMN c <列の定義> COMMENT '...'` | 上に `N'COLUMN', N'c'` を足す |

- Oracle の `COMMENT ON ... IS` はリテラルしか取らない。改行を含むコメント・4000 バイトを超えるコメントは
  `EXECUTE IMMEDIATE` で組み立てる (群10 の書き方)。
- MySQL のコメントの上限は表 2,048 字、列 1,024 字。超えるコメントは書かず表の NOTE に残す。

### 3.4 DROP
| Oracle | `BEGIN EXECUTE IMMEDIATE 'DROP TABLE "S"."T" CASCADE CONSTRAINTS PURGE'; EXCEPTION WHEN OTHERS THEN NULL; END;` + `/` (名前の `'` は `''`) |
|---|---|
| PostgreSQL | `DROP TABLE IF EXISTS "S"."T" CASCADE;` |
| MySQL | ``DROP TABLE IF EXISTS `S`.`T`;`` |
| SQL Server | `IF OBJECT_ID(N'[S].[T]', N'U') IS NOT NULL DROP TABLE [S].[T];` |

## 4. 値の書き方 (INSERT の VALUES)

### 4.1 共通
- NULL は `NULL`。Oracle では空文字列 `''` も NULL (Oracle の VARCHAR2 に空文字列は無い)。LOB の「空の値」
  (EMPTY_CLOB() など、長さ 0 で NULL でない値) は Oracle 方言で `EMPTY_CLOB()` / `EMPTY_BLOB()`、PostgreSQL・MySQL・
  SQL Server で空文字列 / 空のバイト列。DLL 経路は群12b の長さ付きコールバック (ODV_ROW_CALLBACK_EX: NULL は長さ -1、
  空は 0) で区別する。
- 数値の列 (NUMBER・FLOAT・BINARY_*) の値は引用符なし。それ以外は値が数字だけでも引用符付き (H-08)。

### 4.2 文字列リテラル

| 方言 | 書き方 | 引用符 | その他 |
|---|---|---|---|
| Oracle | `'...'` | `''` | CR・LF・NUL は `'...'||CHR(13)||'...'` (H-20、M-22)。& は `SET DEFINE OFF` |
| PostgreSQL | `'...'` | `''` | `\` はそのまま (standard_conforming_strings=on。ファイル先頭で `SET standard_conforming_strings = on;`)。NUL は text に入らない (PostgreSQL の仕様。`CHR(0)` も「null character not permitted」) → **0x00 を除いた値を書き、どの行・列で何バイト除いたかを行の NOTE に書く** (利用者の決定 10/18。黙って値を変えない) |
| MySQL | `'...'` | `''` | `\` は `\\` (既定の sql_mode では `\` がエスケープ。M-16)。NUL は `\0` (MySQL の文字列リテラルの NUL のエスケープ。`\` を `\\` に書くのと同じく既定の sql_mode (NO_BACKSLASH_ESCAPES なし) が前提。`CONCAT('a', CHAR(0), 'b')` でも入るが、`\` の書き方と前提を揃えて 1 つのリテラルにする) |
| SQL Server | `N'...'` | `''` | NUL は `NCHAR(0)` を `CONCAT(CAST(N'...' AS NVARCHAR(MAX)), NCHAR(0), N'...')` で連結 (T-SQL の文字列リテラルに NUL のエスケープは無い。`+` や CAST 無しの CONCAT は nvarchar(4000) で切れるので、先頭を NVARCHAR(MAX) にする。列は N 型なので NCHAR(0)。群12b の `CHAR(0)` でも nvarchar に暗黙変換されて入る) |

### 4.3 長い値 (Oracle)
Oracle の文字列リテラルは 1 つ 4000 バイトまで (MAX_STRING_SIZE=STANDARD、超えると ORA-01704)。素のリテラルを
`||` でつないでも結果が 4000 バイトを超えると ORA-01489 (実測)。SQL*Plus の 1 行は 11.2 で 2,498 バイト、21c・23ai で
4,998 バイトまで (超えると SP2-0027、実測)。そこで:

- 1 行は 2,000 バイト以内に保つ。行に収まらない INSERT は、値の区切り (`, `) の後で改行し、長い文字列は
  `'...'||` + 改行 + `'...'` に分ける (分け目は UTF-8 の文字の境界)。
- 列の型と値の大きさ (UTF-8 のバイト数) で書き方を決める:

| 列 | 〜4000 バイト (2 進は 〜2000 バイト) | それを超える | 上限を超える |
|---|---|---|---|
| CHAR・VARCHAR2・NCHAR・NVARCHAR2・ROWID・INTERVAL | リテラル (分けるときは `||`) | `TO_CLOB('…')||TO_CLOB('…')` (N 型は TO_NCLOB。暗黙の変換で列に入る、実測) | — |
| CLOB・NCLOB・XMLTYPE・JSON・VECTOR | リテラル | PL/SQL ブロック: 一時 LOB に `DBMS_LOB.APPEND(v, TO_CLOB('…'))` を繰り返し、INSERT は変数で (XMLTYPE は `XMLTYPE(v)`) | — |
| LONG | リテラル | PL/SQL ブロック: `v LONG;` に `v := v || '…';` を繰り返し、INSERT は変数で | 32,760 バイト超は書けない (PL/SQL の LONG の上限) → NULL + 行の NOTE |
| BLOB | `HEXTORAW('…')` (分けるときは `||`) | PL/SQL ブロック: 一時 BLOB に `DBMS_LOB.WRITEAPPEND(v, n, HEXTORAW('…'))` | — |
| RAW・LONG RAW | `HEXTORAW('…')` | PL/SQL ブロック: `v LONG RAW;` に `v := UTL_RAW.CONCAT(v, HEXTORAW('…'));` | 32,760 バイト超 → NULL + 行の NOTE |

  PL/SQL ブロックの形 (1 行に 1 つ以上の長い値があるとき、その行の INSERT をブロックにする):
  ```
  DECLARE
    v3 CLOB;
  BEGIN
    DBMS_LOB.CREATETEMPORARY(v3, TRUE);
    DBMS_LOB.APPEND(v3, TO_CLOB('…'));
    …
    INSERT INTO "S"."T" ("ID", "C") VALUES (1, v3);
    DBMS_LOB.FREETEMPORARY(v3);
  END;
  /
  ```
  実測: BLOB を `TO_BLOB(HEXTORAW())||…` でつなぐとエラー無しに NULL が入る (使わない)。LONG に TO_CLOB の連結を
  入れると ORA-22835。20MB の CLOB を 1 ブロック (1 万文) で入れられる (21c、11 秒)。
- 他方言は 1 つのリテラルで書く (長さの上限は PostgreSQL 1GB、MySQL は max_allowed_packet (既定 64MB)、
  SQL Server は文字列の上限 2GB・バッチの上限。行の長さに上限のあるクライアントは未確認)。

### 4.4 型ごとの値

| 型 | Oracle | PostgreSQL | MySQL | SQL Server |
|---|---|---|---|---|
| NUMBER・FLOAT | `123.45`、`-1E+125` | 同 | 同 | 同 |
| BINARY_FLOAT/DOUBLE | 数値 + 接尾辞 `f` / `d` (`1.5f`、`1.0000000000000001e+300d`。接尾辞が無いと NUMBER として読まれ、1E126 を超える値は ORA-01426。21c で実測)。NaN・±Inf は `BINARY_FLOAT_NAN` / `BINARY_DOUBLE_INFINITY` / `-BINARY_..._INFINITY` | `'NaN'::REAL`・`'Infinity'::DOUBLE PRECISION` | NaN・±Inf は NULL + 行の NOTE | 同左 |
| DATE | `TO_DATE('2024/01/31 13:45:59', 'SYYYY/MM/DD HH24:MI:SS')`、紀元前は年を `-0099` | `'2024-01-31 13:45:59'`、紀元前 `'0099-06-15 12:00:00 BC'` | `'2024-01-31 13:45:59'` | `N'2024-01-31T13:45:59'` |
| TIMESTAMP | `TO_TIMESTAMP('2024/01/31 13:45:59.123456', 'SYYYY/MM/DD HH24:MI:SS.FF')` (小数は列の精度の桁) | `'2024-01-31 13:45:59.123456'` (6 桁まで) | 同 | `N'2024-01-31T13:45:59.1234567'` (7 桁まで) |
| TSTZ (時差) | `FROM_TZ(TO_TIMESTAMP('<現地>', 'SYYYY-MM-DD HH24:MI:SS.FF'), '+09:00')` (群12a) | `'2024-01-31 13:45:59.123456+09:00'` (列が TIMESTAMPTZ なので型の指定は要らない) | UTC の `'2024-01-31 04:45:59.123456'` | `N'2024-01-31T13:45:59.1234567+09:00'` |
| TSTZ (地域) | `FROM_TZ(TO_TIMESTAMP('<UTC>', …), 'UTC') AT TIME ZONE 'Asia/Tokyo'` (群12a) | 現地時刻 + その瞬間の時差 (上と同じ形) | UTC | 現地時刻 + その瞬間の時差 |
| TSLTZ | `FROM_TZ(TO_TIMESTAMP('<DB の時刻>', …), '<DBTIMEZONE>')` (群12a) | DB の時刻 + DBTIMEZONE の時差 | UTC | DB の時刻 + DBTIMEZONE の時差 |
| INTERVAL YM | `'+2-3'` (暗黙の変換、実測済み) | `'2 years 3 months'` (負は各項に `-`。PostgreSQL の interval は月数を 32 ビットで持ち、178956970 年 7 か月 (負は 8 か月) を超える値は NULL + 行の NOTE。16 で実測) | `'+2-3'` | `N'+2-3'` |
| INTERVAL DS | `'+3 04:05:06.123'` | `'3 days 4 hours 5 minutes 6.123 seconds'` | `'+3 04:05:06.123'` | `N'+3 04:05:06.123'` |
| RAW・LONG RAW・BLOB | `HEXTORAW('DEADBEEF')` | `'\xDEADBEEF'::BYTEA` | `X'DEADBEEF'` | `0xDEADBEEF` |
| ROWID・UROWID | `'AAAR3sAAEAAAACXAAA'` | 文字列 | 文字列 | `N'...'` |
| BFILE | `BFILENAME('DIR', 'file')` (式として。ディレクトリ・ファイル名の `'` は `''`) | 文字列 `'BFILENAME(...)'` | 同 | 同 |
| BOOLEAN | `TRUE` / `FALSE` | `TRUE` / `FALSE` | `TRUE` / `FALSE` | `1` / `0` |
| JSON・XMLTYPE・VECTOR | 文字列 (4000 バイト超は 4.3 の PL/SQL) | 文字列 | 文字列 | `N'...'` |

- 日付・時刻の値は**エクスポートの日付書式の設定 (表示用) に関係なく**上の固定の形で書く (M-13 / M-14)。
  Oracle の `SYYYY` は紀元前の年を読む (`YYYY` は負の年で ORA-01841、実測)。
- 紀元前の DATE / TIMESTAMP は MySQL・SQL Server では NULL + 行の NOTE (2.3)。

## 5. 値を作れないもの

解析エンジンが値を復号できなかった印 (`%VARRAY%` `%JSON%` `%VECTOR%` `%BOOLEAN%` `%BFILE%` `%LONG%`、exp の
`%BLOB%` `%CLOB%` `%NCLOB%`、上限を超えて全体を持てなかった LOB / LONG) は、**値として書かない**:
- SQL: `NULL` を書き、行の NOTE に列名と理由を書く
  (`-- NOTE: column "V" of the next row: VARRAY value cannot be decoded (element type not in the dump); written as NULL`)。
- CSV: 空の欄にする (9 章)。
- 判定は印の文字列ではなく解析エンジンの「印である」旗で行う (`%BLOB%` という文字列の値は普通の値)。
  VB の経路は、`odv_set_value_mode(s, ODV_VALUES_EXPORT)` で受け取れば長さ -2 (印) / -3 (上限超え) で分かる
  (12.1)。プレビューの値しか持たないときは、型が VARRAY / オブジェクト型、または値が印の形で列の型が
  BLOB/CLOB/NCLOB/BFILE/JSON/VECTOR/BOOLEAN/LONG のときに印とみなす。
- exp の VARRAY / オブジェクト型の列 (型が `"OWNER"."TYPE"`) の値は印 (`%VARRAY%` / `%OBJECT%`、旗付き)。
  型だけで決まるので (2.4)、どちらの経路でも常に NULL + 行の NOTE。

## 6. 解析エンジンが SQL 出力に渡す値の書式 (DLL の内部)
- DATE: `SYYYY/MM/DD HH24:MI:SS` (年は 4 桁、負は `-0099`)。TIMESTAMP: 同 + `.` + 列の精度の桁。
  `odv_set_date_format` の設定はエクスポート中だけ無視する (CSV・プレビューは設定どおり)。
- TSTZ / TSLTZ: 生のバイト列 (`#...`、群12a) を odv_sql.c が方言ごとに書く。
- LOB / LONG: 全体 (プレビューの 4096 バイトではない)。上限は 512MiB (2 進) で、超えたら 5 章の扱い。

## 7. ファイルの先頭
- Oracle: `SET DEFINE OFF` / `SET SQLBLANKLINES ON` (H-20。既存)。
- PostgreSQL: `SET standard_conforming_strings = on;`
- MySQL: sql_mode は変えない。`\` を `\\` に書くので、既定 (NO_BACKSLASH_ESCAPES なし) のセッションで正しく入る。
  NO_BACKSLASH_ESCAPES を有効にしたセッションで流すと `\\` が 2 文字のまま入る (ファイル先頭のコメントにその旨を書く)。
- SQL Server: `SET QUOTED_IDENTIFIER ON;` は書かない ([] で囲むので不要)。

## 8. 長い DDL の行 (Oracle)
CHECK・DEFAULT・索引の式・コメントで 1 行が 2,200 バイトを超えるときは、字句の切れ目 (空白) で
改行する。文字列リテラル・引用符付き識別子の中では切らない。リテラル自体が 1,000 バイトを超えるときは
`'…'||'…'` に分け (N'…' は各片に N)、1 片ずつ行を分ける (同じ値。DEFAULT の 3,000 バイトのリテラルを 1 行に書くと
SQL*Plus 21c で SP2-0341。cases/meta/m_long C_LDEF)。そのため往復後の DATA_DEFAULT の文字列は元と違う
(値は同じ。式を評価して確認)。行に入りきらない短いリテラルは前で改行する。COMMENT ON は `EXECUTE IMMEDIATE`。
- expdp の DEFAULT は 4,000 バイトまで TABLE_T の DEFAULT_VAL、超えると DEFAULT_VALC (CLOB) にある (21c で実測)。

## 8.1 制約・索引の並び
1 つの表の中では種類の順 (一意の索引 → 主キー → 一意 → CHECK → 外部キー → 索引)、同じ種類の中は名前の順
(システム名 SYS_C… は後ろにダンプの順)。exp (後置の外部キーを含む) と expdp で並びが違っても同じ SQL になる。

## 9. CSV
- RFC 4180 のとおり、行 (見出し行・型の行・データの行) の終わりは CRLF (`\r\n`)。最後の行の後にも CRLF を付ける。
  値の中の改行 (LF / CR / CRLF) は変えずにそのまま書く (その値は引用符で囲む)。文字コードは UTF-8 (BOM なし)。
  `<ファイル名>.notes.txt` の行の終わりも CRLF。DLL の経路 (Workspace・一括) と VB の経路 (TablePreview) で同じ
  (10/18 統括の決定 (f)。それまで DLL は LF、VB は CRLF で食い違っていた。ヘルプと README は「RFC 4180 準拠・改行 CRLF」)。
- 見出し行 (と型の行) は、行が 1 つも無い表 (ダンプに行が無い・区画や絞り込みで 0 行) でも、設定が有効なら書く
  (10/18 統括の決定 (h)。それまで DLL は最初の行で見出しを書いたので、0 行の表は空のファイルだった)。
- 値は SQL と同じく全体 (LOB の全体、LONG の全体)。BLOB / RAW / LONG RAW は 16 進 (大文字、接頭辞なし)。
- 日付・時刻はエクスポートの日付書式の設定どおり (表示用。SQL とは違う)。
- 5 章の「値を作れないもの」は空の欄にし、どの行のどの列かを、出力ファイルと同じ場所の
  `<ファイル名>.notes.txt` に 1 行ずつ書く (無ければ作らない)。※ DLL の経路で採用。VB 経路も同じにするかは統括の判断。

## 12. 呼び出し側 (VB) が DLL から受け取るもの (10/18 追加、群13b の依頼 (a)〜(c)・統括の (e))

### 12.1 値のモード `odv_set_value_mode(s, mode)` (odv_parse_dump の値)
- `ODV_VALUES_PREVIEW` (0、既定): 今までどおり (LOB は先頭 4096 バイト、exp の LOB は `%CLOB%` 等の印、LONG は 64KB まで)。
- `ODV_VALUES_EXPORT` (1): エクスポートの書き手 (odv_export_sql / _csv) が受け取るのと同じ値。LOB / LONG / JSON /
  VECTOR は全体 (1 値 512MiB まで)。長さ付きコールバック (`odv_set_row_callback_ex`) では:
  | col_lens[i] | 意味 | col_values[i] |
  |---|---|---|
  | ≥ 0 | 値 (全体) | 値 |
  | -1 | NULL | NULL |
  | -2 | 値の形が無い (5 章の印) | プレビューで出る印の文字列 (NUL 終端。`%BLOB%` `%VARRAY%` 等)。値ではない |
  | -3 | 512MiB を超えて持てなかった | NULL |
  長さの無い旧コールバックには印の文字列のまま渡る (区別できない)。日付は odv_set_date_format のとおり
  (SQL 用には SLASH = 0 を指定する。6 章と同じ形になる)。TSTZ / TSLTZ は表示の文字列のまま (DLL 内部の生の形は渡さない)。
- SQL の書き手は -2 を「NULL + 行の NOTE `the value could not be decoded (<印>); written as NULL`」、-3 を
  「NULL + 行の NOTE `the value is longer than 512 MiB and was not kept whole; written as NULL`」にする
  (DLL の put_value と同じ文)。CSV は空欄 + notes.txt (9 章)。

### 12.2 表の DDL (制約・NOT NULL・DEFAULT・コメント)
- 一覧の表コールバックの constraints_json (exp): 制約ごとに `{"type":T,"name":..,"columns":[..], ...}`。
  T: 0 主キー / 1 一意 / 2 外部キー / 3 CHECK / 4 索引、5 コメント、6 NOTE。**制約でない一意の索引
  (CREATE UNIQUE INDEX) は type 1 に `"unique_index":1` が付く** (SQL は `CREATE UNIQUE INDEX` で、制約より前に書く。3.2)。
- expdp の一覧は TABLE_T を読まない (制約・DEFAULT・コメントは入らない。一覧の時間を増やさないため)。表ごとに
  `odv_read_table_ddl(s)` で取る: odv_set_table_filter (+ odv_set_data_offset、odv_set_ddl_hint) をした
  セッションで呼ぶと、表コールバックが 1 回、一覧と同じ引数の形で呼ばれる。違いは:
  - col_not_nulls[i]: 0 = NULL 可、1 = NOT NULL、**2 = 主キーの列なので NOT NULL (それ自体の NOT NULL 制約は無い)**。
    2 の列には CREATE TABLE で NOT NULL を書かない (3.1、往復で NOT NULL 制約が増えないように)
  - col_defaults[i]: DEFAULT の式 (TABLE_T の DEFAULT_VAL / DEFAULT_VALC)
  - constraints_json: 上と同じ形 (主キー・一意・CHECK・外部キー、平文のメタデータにある索引 (一意なら
    `"unique_index":1`)・コメント、圧縮されたメタデータにしか無いコメント・索引は type 6 の NOTE)
  - row_count: -1 (行は読まない)
  exp のダンプには ODV_ERROR_UNSUPPORTED を返す (一覧のコールバックが既に持つ)。
- SQL の出力 (odv_export_sql) は TABLE_T を自分で読むので、この関数を呼ばなくても同じ DDL を書く。

### 12.3 DDL の位置のヒント `odv_get_table_ddl_hint(s, index)` / `odv_set_ddl_hint(s, hint)`
- 表ごとの SQL (odv_export_sql に表の絞り込み) は、表から離れた所にある DDL も読む:
  exp は全表の後に置かれる外部キー・関数索引・DESC 索引 (統括の判断で案 C)、expdp はマスター表の
  その表の行 (TABLE_T、COMMENT / INDEX の文書の位置)。ヒントが無いとダンプの末尾まで (exp) /
  マスター表の全行 (expdp) を読む (正しいが、ダンプの大きさに比例して遅い)。
- 一覧の後に `odv_get_table_ddl_hint(s, i)` (i は odv_get_table_entry と同じ) で取り、表ごとのエクスポートの
  前に `odv_set_ddl_hint(s, hint)` で渡す。出力はヒントの有無で変わらない (全件で確認、NOTES)。
- 中身は ASCII の不透明な文字列 (呼び出し側は解釈しない)。参考: exp は `L<ダンプの大きさ>:<位置>:<スキーマ名の16進>`
  (後置の DDL が無いダンプは `L<大きさ>:-`、どの項目も同じ)、expdp は `P<大きさ>:<マスター表の行の位置>,...`。
  大きさが違うダンプ・合わないヒントは使わない (無いときと同じに読む)。分割ダンプは論理位置。
- 空文字列 = ヒントなし (その表の DDL 行がマスター表に無い・後置の DDL の始まりを安全に決められない、など)。

## 10. 参照した公式文書
- Oracle 23ai SQL Language Reference: Data Types (https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/Data-Types.html)、
  Literals (…/sqlrf/Literals.html)、SQL*Plus User's Guide: SQL*Plus Limits (…/sqpug/SQL-Plus-limits.html)
- PostgreSQL 16: Numeric Types / Character Types / Binary Data Types / Date/Time Types (https://www.postgresql.org/docs/16/datatype-*.html)
- MySQL 8.4: Fixed-Point Types、String Data Type Syntax、The DATE, DATETIME, and TIMESTAMP Types、Date and Time Literals、
  String Literals (https://dev.mysql.com/doc/refman/8.4/en/)
- SQL Server (Microsoft Learn): decimal and numeric、nchar and nvarchar、datetime2、datetimeoffset
  (https://learn.microsoft.com/en-us/sql/t-sql/data-types/)

## 11. 未確認のもの
- PostgreSQL・MySQL・SQL Server の実 DB での流し込み (この環境に無い)。構文の検査は群13a の NOTES に書く方法で行う。
- NVARCHAR2 が UTF8 の各国語文字セットのときの長さの数え方。
- Oracle の 1 バイト文字セットの DB での exp の CLOB の符号化 (この環境の DB はすべて AL32UTF8。UTF-16BE として読む)。
- sqlcmd・mysql・psql の 1 行の長さの上限。
- SQL Server が datetime2 の文字列の 8 桁以上の小数秒をどう扱うか (7 桁に切り捨てて書くので影響しない)。
