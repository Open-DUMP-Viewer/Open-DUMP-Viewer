# Changelog

## [4.6.2] - 2026-10-18

Oracle ダンプの解析で、表・行・値が黙って欠けたり化けたりする複数の不具合を修正しました。いずれも、エラーや警告を出さずに結果の不足・誤りが起きていた箇所です。

### セキュリティ
- **LOB をファイルに抽出するとき、指定した出力先の外にファイルを作れる問題を修正しました**
  - ファイル名に使う列の値 (ダンプ由来で信頼できません) を検査せずに出力先のパスへ連結していたため、値に `..\` やパス区切り・ドライブ文字などが含まれると、選んだ出力先フォルダの外にファイルを書き出したり、既存のファイルを上書きしたりできました
  - 列の値を安全な 1 つのファイル名に無害化し、組み立てたパスが出力先フォルダの中に収まることを確かめてから書き出すようにしました。ファイルの作成・書き込みに失敗したときは、黙って成功扱いにせず、抽出完了時に失敗件数を知らせます

### バグ修正 — EXPDP (DataPump) ダンプ
- **表が一覧から消える問題を修正しました**
  - XMLTYPE・ANYDATA・SDO_GEOMETRY などの列や表を、その型の所有者 (SYS・MDSYS など) の表と取り違えて捨てていました
  - 行データに `<?xml` を含む表を、表定義の始まりと取り違え、その表と以降の表を落としていました
- **列の値が 1 つずつずれる問題を修正しました**
  - 列の並びを、ダンプ内の格納位置に基づいて決めるようにしました。仮想列・オブジェクト表・入れ子表などを持つ表で、全列の値がずれていたのが直ります
- **行・LOB の値が壊れる・欠ける問題を修正しました**
  - 行の断片や列の長さを正しく読むようにしました。VARCHAR2(4000)・256 バイトを超える行・NULL で終わる行・256 列を超える表などが、0 行や壊れた行になっていたのが直ります
  - CLOB / NCLOB のプレビューの文字化け、複数の LOB 列を持つ表での列の取り違え、外部表形式 (QUERY= / EXTERNAL_TABLE) で取ったダンプの LOB / LONG がほぼ 1 行しか読めない問題もあわせて修正しました
- **区画名が別の区画に付く問題を修正しました**
- **0 行の表・0 行の区画が一覧に出ず、CONTENT=METADATA_ONLY のダンプでは表が 1 つも出ない問題を修正しました**
  - ダンプ末尾のマスター表から表定義・空の区画・区画名を補います。0 行の表も SQL エクスポートで CREATE TABLE を出します
- **LONG / LONG RAW / BFILE を持つ表で行が消えたり、値が空や別の行の内容になったりする問題を修正しました**
  - 64KB を超える LONG の続きが別の行として読まれ、でたらめな行が増えていた点もあわせて修正しました
- **型の情報が誤る問題を修正しました**
  - TIMESTAMP の小数秒の桁を常に 6 としていたため、7〜9 桁目が切り捨てられ、型も TIMESTAMP(6) になっていました
  - VARCHAR2(n CHAR) / CHAR(n CHAR) をバイト長の型として表示・出力していました
  - FLOAT(p) を NUMBER(p)、INTEGER と NUMBER(*,s) を NUMBER と読み、SQL で作り直すと値が丸められていました
  - INTERVAL の精度が落ち、3 桁以上の年・日の行が入りませんでした
  - NCLOB を CLOB と表示していました
- **表名・列名の `&` などが `&amp;` と表示され、前後の空白が削られる問題を修正しました**
  - "DUP" と "DUP " のように空白だけが違う表が 1 つにまとめられていた点も直ります
- **JSON・VECTOR・BOOLEAN 型の値が誤って表示され、行が欠ける問題を修正しました**
  - JSON は JSON_SERIALIZE、VECTOR は VECTOR_SERIALIZE と同じ表記で表示します。要素の型がダンプに無い VARRAY など、値を復元できないものは推測した値を出さず `%VARRAY%` などと表示します
- **外部表形式 (ACCESS_METHOD=EXTERNAL_TABLE など) で取った LONG だけの表で、値が空になる問題を修正しました**
- **列の並びが表の定義と違う順で表示される問題を修正しました** (LONG を持つ表など)

### バグ修正 — レガシー EXP ダンプ
- **クライアント文字セットと DB 文字セットが異なるダンプで、表名・列名・値が文字化けする問題を修正しました**
  - 名前・コメント・DDL はクライアント文字セット、データは DB 文字セットで復号するようにしました。US7ASCII・WE8MSWIN1252 などで従来型を GBK、DIRECT=Y を Shift_JIS と誤判定していた点もあわせて修正しました
  - 表名・列名が化けて製品の表の絞り込みと一致せず 0 行になっていた表が、正しく表示されます
- **実行ユーザー名によって従来型ダンプを DIRECT=Y と誤判定し、一部の表が壊れる問題を修正しました**
- **DDL・行データ解析の複数の不具合を修正しました**
  - 列名が CHECK・PRIMARY・CONSTRAINT などで始まる表を、制約と取り違えて 0 行にしていた問題
  - 1 つの LOB が約 6MB を超えると、その行から表の最後までが消えていた問題
  - LOB 抽出で、前の列が NULL の行で列の対応がずれ、別の列の中身を書いていた問題
  - オブジェクト型・VARRAY・入れ子表・REF の列を行データと取り違えて、後続の表を飲み込んでいた問題
  - DIRECT=Y で約 8KB を超える LONG / LONG RAW の分割された片の連結、行データの開始位置の判定、ALTER 文の連結範囲の誤り
  - 表を絞って解析するとき、データ直後で打ち切って COMMENT ON・CREATE INDEX が出力されなかった問題
- **CHECK 制約の条件・コメント・DEFAULT が崩れたり欠けたりする問題を修正しました**
  - 長い制約名・コメント・DEFAULT も全文を表示・出力します。PL/SQL やビューの本文の中の CREATE TABLE を表と取り違えて、行の無い幻の表が一覧に出る問題もあわせて修正しました
  - TablePreview からの SQL エクスポートで、DEFAULT 式や CHECK 条件の途中で切れて CREATE TABLE が構文誤りになる問題、コメントが出力されない問題も修正しました
- **制約・索引・コメントが別の表に付く問題を修正しました**
  - FOREIGN KEY や関数索引が最後の表に、一時表の制約・索引・コメントが直前の表に付き、ビューのコメントが表のコメントを上書きしていました。名前の無い (システム命名の) FOREIGN KEY・UNIQUE 制約が出ない問題もあわせて修正しました
- **オブジェクト表 (CREATE TABLE … OF 型) が一覧から消える問題を修正しました**
- **表と区画が合わせて 1,000 件を超えるダンプで、それ以降の区画が普通の表として並ぶ問題を修正しました**
  - 上限を 1,048,576 件に引き上げました (ヘルプの記載も更新)
- **区画の多い表 (CREATE TABLE が非常に長い表) で、全区画が 0 行になる問題を修正しました**
- **DIRECT=Y で LONG / LONG RAW を持つ表の値が、全行で別の列に入れ替わる問題を修正しました**
- **CHAR / NCHAR の末尾の空白を削っていたため、空白だけの値が NULL になる問題を修正しました**
- **LONG RAW の値が `%BLOB%` と表示される問題を修正しました**
  - LONG / LONG RAW と LOB を持つ表の LOB 抽出で、LONG の中身が LOB のファイルに混ざる問題もあわせて修正しました
- **約 64KB 以上の LONG / LONG RAW を持つ行から後ろが消える問題を修正しました**
- **21c の DIRECT=Y で、行の無い表の直後の表が一覧から消える問題を修正しました**
- **表や区画を絞って読むとき、値の中のバイト列の並びで読み飛ばしが止まり、後ろの表の行が欠けることがある問題を修正しました**
- **破損した部分や大きな LONG を含むダンプで、表一覧が出るまで数十秒止まる問題を修正しました**

### バグ修正 — 区画表・分割ダンプ
- **区画表の一覧で親の行を開いても、先頭の 1 区画の行しか出ない問題を修正しました**
  - 親の行を開く・親の行から CSV / SQL を出力する・一括エクスポートする、のいずれも全区画の行を対象にしました。一括エクスポートで同じ表の複数の区画を選んだときは、出力ファイルが互いに上書きしないよう区画名を付けます
  - expdp で、他の表をはさんで後ろにある区画の行が欠ける場合があったのもあわせて修正しました
- **FILESIZE などで複数ファイルに分割したダンプで、1 本目にある表・行しか読まない問題を修正しました** (expdp・exp)
  - 開いたダンプの名前 (末尾が `_01` など) から同じフォルダの続きのファイルを探し、形式と巻番号が整合することを確かめたうえで、全体を 1 つの連続したダンプとして読むようにしました。続きのファイルを選ぶ操作は要りません
  - 23ai の expdp を FILESIZE で分割したダンプは、ファイルごとに独自の構造を持つため完全には復元できず、1 本目の範囲までの読み取りになります (全表の一覧は出ます)
- **exp DIRECT=Y を FILESIZE で分割したダンプで、1 本目しか読まない問題を修正しました**

### バグ修正 — SQL / CSV / Excel / ODBC エクスポート
- **数字に見える文字列を引用符なしで出力する問題を修正しました**
  - `'007'` が `7` になったり、`'1.2.3'` や `'.'` の行が構文誤りで取り込めなかったりしていました。数値型の列だけを引用符なしにし、文字列型は常に引用符で囲むようにしました
- **Oracle 方言の出力を SQL*Plus で流すと、行が欠けたり値が変わったりする問題を修正しました**
  - 先頭に `SET DEFINE OFF` / `SET SQLBLANKLINES ON` を出し、値の中の改行・復帰を `CHR(10)` / `CHR(13)` の連結で書くようにしました
- **LOB・LONG・長い文字列の値が切れたり、行が入らなかったりする問題を修正しました**
  - expdp の CSV / SQL で CLOB が先頭 4096 バイトで切れ、BLOB を HEXTORAW なしの値で書いていたため、BLOB を持つ表はほとんどの行が入りませんでした。LOB・LONG の値を全体で出力します
  - 4000 バイトを超える値を 1 つのリテラルで書いていたため、長い LONG や全角 1334 字以上の NVARCHAR2 の行が ORA-01704 で入りませんでした。Oracle 方言では値を分けて連結します
  - EMPTY_CLOB() / EMPTY_BLOB() の値を NULL と区別して書きます
- **日付・時刻の値が入らない、時刻や小数秒が落ちる問題を修正しました**
  - 表示用の日付書式で値を書いていたため、日付書式によっては時刻・小数秒が落ち、独自の書式では INSERT の大半が失敗し、紀元前の日付の行が入りませんでした。表示の書式によらず、固定の書式で書きます
  - TablePreview から出した SQL は日付・時刻を TO_DATE / TO_TIMESTAMP なしで書いていたため、DATE 列を持つ表の行が Oracle に入りませんでした
  - 他の DBMS 向けでは、TIMESTAMP WITH TIME ZONE を PostgreSQL は timestamptz、SQL Server は datetimeoffset の時差付きの値で書きます。MySQL はタイムゾーン付きの型が無いため UTC の時刻で書き、その旨を注記します
- **制約・索引・コメント・DEFAULT が SQL に出ない問題を修正しました**
  - Workspace・一括エクスポートの SQL に主キー・一意・外部キー・CHECK 制約、NOT NULL、DEFAULT がなく、expdp のダンプでは制約・索引・コメントがまったく出ていませんでした。TablePreview からの SQL も制約・索引・コメントを出していませんでした
  - 外部キーはファイルの最後にまとめて書きます。exp で全表の後ろに置かれる外部キー・関数索引が、表ごとの SQL に出ていなかった点もあわせて修正しました
  - 制約ではない一意の索引を UNIQUE 制約として書いて主キーと重なると ORA-02261 になる点、expdp の CHECK 条件が ORA-01735 になる点も直します
  - expdp の既定 (COMPRESSION=METADATA_ONLY) のダンプでは、コメントと単独の索引は圧縮された形式で格納されていて読めないため、その旨を SQL に注記します
- **他の DBMS 向けの SQL で、CREATE TABLE が構文誤りになる・桁や値の範囲が失われる問題を修正しました**
  - NUMBER(*,0)・VARCHAR2(10 CHAR) などの括弧の中をそのまま写して、構文誤りになっていました
  - FLOAT を REAL / DOUBLE に、精度なしの NUMBER を DECIMAL(38,10) にしていたため、桁と値の範囲が失われていました。型の対応を見直し、表せない型や値は注記します
- **MySQL 向けの SQL で、値の `\` をそのまま書いていたため、値が変わったり文が閉じなかったりする問題を修正しました**
- **PostgreSQL 向けの SQL で、値の 0x00 (NUL) を扱えない問題に対応しました**
  - PostgreSQL の文字列は 0x00 を持てないため、0x00 を除いて書き、その旨を注記します (Oracle・MySQL・SQL Server 向けは 0x00 を保って書きます)
- **列の多い表・列名の長い表で、INSERT の列リストが途中で切れて全行が構文誤りになる問題を修正しました**
- **TablePreview からの SQL で、NOT NULL かつ DEFAULT 付きの列を持つ表の CREATE TABLE が ORA-00907 になる問題を修正しました**
- **表名に `'` を含む表で、Oracle 向けの DROP と SQL Server 向けの OBJECT_ID が構文誤りになる問題を修正しました**
- **長い CHECK 条件・DEFAULT・コメントを持つ表で、SQL*Plus の 1 行の長さの上限を超えて表が作られない問題を修正しました**
- **BINARY_FLOAT / BINARY_DOUBLE の大きな値が Oracle に入らない (ORA-01426) 問題を修正しました**
- **値を復元できない列 (`%VARRAY%` などと表示されるもの) に、その表示を値として書いていた問題を修正しました**
  - SQL では NULL にして注記を残し、CSV では空欄にして、該当の行と列を `<出力ファイル名>.notes.txt` に書きます
- **整数の推定を有効にした Oracle 向けの SQL で、列を NUMBER(4) などに狭めて 10000 以上の値が入らない問題を修正しました**
  - Oracle 向けでは型を変えません。他の DBMS 向けでは SMALLINT / INTEGER / BIGINT などに写します
- **Excel エクスポートで、数字だけの文字列が数値になり先頭の 0 が消える問題を修正しました**
  - 数値型の列だけを数値のセルにします。15 桁を超える数値、ミリ秒より細かい小数秒を持つ時刻、1900 年 3 月 1 日より前の日付は、値が変わらないよう文字列のセルにします
- **ODBC エクスポートで、FLOAT を VARCHAR(4000) に、NUMBER(p,s) を桁の合わない型にしていた問題を修正しました**
  - FLOAT・BINARY_FLOAT / BINARY_DOUBLE は DOUBLE、NUMBER(p,s) は DECIMAL(p,s)、DATE / TIMESTAMP は日時の型で書きます
- **区画の行から Excel・Access・SQL Server・ODBC へ出力すると、表全体の行を出力していた問題を修正しました**
- **SQL Server 向けで指定したデータベース名 (USE) が、Workspace・一括エクスポートの SQL に出ていなかった問題を修正しました**

### バグ修正 — 文字セット・日時
- **TIMESTAMP WITH TIME ZONE の時差・地域が落ち、UTC の時刻で表示される問題を修正しました**
  - 現地時刻に時差 (`+09:00`) または地域名と時差 (`Asia/Tokyo +09:00`) を付けて表示します。夏時間の切り替わりの重なりでも一意に読めます。地域の規則は IANA Time Zone Database に基づきます
  - TIMESTAMP WITH LOCAL TIME ZONE は、元の DB のタイムゾーン (DBTIMEZONE) での時刻に、そのタイムゾーンを付けて表示します
  - Oracle 方言の SQL エクスポートは、流すセッションのタイムゾーンによらず元の瞬間と時差・地域が保たれる形で書きます
- **DB 文字セットが JA16EUC / JA16SJIS 系のダンプで、一部の文字が化ける問題を修正しました**
  - JIS X 0212 の漢字 (鷗・丂 など) が別の字と見えない制御文字に化け、〜 ～ ￠ ￡ ￢ などが Oracle と違う字になり、YEN 版では ¥ が \ になっていました。Oracle と同じ対応で変換します (JA16SJIS / JA16SJISTILDE / JA16SJISYEN / JA16EUC / JA16EUCTILDE / JA16EUCYEN)
- **値の途中に NUL 文字 (0x00) があるとそこで値が切れ、先頭が NUL の値が NULL になる問題を修正しました**
  - プレビューでは NUL 文字を ␀ で表示します (値そのものは保持し、検索・エクスポートでは元の値を扱います)
- **紀元前の日付の年が誤って表示される問題を修正しました** (例: 紀元前 4712 年が 4688 年に)

### バグ修正 — 安定性・ファイル
- **途中で切れたダンプを、警告なしで読める範囲だけ表示していた問題を修正しました**
  - 読めた表と行を表示したうえで「途中で切れている」と警告します。FILESIZE などで分割したダンプの続きのファイルが無い場合も知らせます。expdp の先頭部分だけのファイルに「COMPRESSION=ALL は非対応」と誤って案内していたのも直します
- **日本語などを含むフォルダで、LOB 抽出・CSV / SQL エクスポート・ダンプの読み込みができない問題を修正しました**
  - 長いパス (260 文字以上) も扱えます
- **複数の表を同時に開いたりエクスポートしたりすると、別の表の名前が付いたり行が欠けたりする問題を修正しました**
- **解析中にアプリが異常終了することがある問題を修正しました**
- **LOB 抽出で存在しない列名を指定しても成功と表示される問題を修正しました**

### 変更
- TablePreview で絞り込みをしていない表の SQL / CSV エクスポートは、Workspace・一括エクスポートと同じ出力になります
  - SQL Server 向けの SQL で、スキーマを `dbo` に置き換えず、元のスキーマ名のまま書きます (Workspace・一括エクスポートと同じ)
- CSV の改行を、ヘルプの記載 (RFC 4180) どおり CRLF に揃えました。Workspace・一括エクスポートの CSV は LF でした
- 0 行の表の CSV にも見出し行 (設定が有効なら型の行も) を書くようにしました

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.6.1 → 4.6.2 に同期
- 解析エンジンに IANA Time Zone Database から生成したタイムゾーンの表と、Oracle の文字対応から生成した JA16 系の対応表を同梱しました (`THIRD-PARTY-NOTICES.txt` に tzdata を追記)
- 行のコールバックに長さ付きの形式 (`odv_set_row_callback_ex`) を追加しました
- 呼び出し側向けの API (`odv_set_value_mode` / `odv_read_table_ddl` / `odv_get_table_ddl_hint` / `odv_set_ddl_hint`) を追加しました

## [4.6.1] - 2026-10-08

### 依存関係
- Microsoft.Data.SqlClient 7.0.2 → 7.1.1
  - 推移的依存の Microsoft.Data.SqlClient.Extensions.Abstractions / Microsoft.Data.SqlClient.Internal.Logging も 7.1.1、Microsoft.Data.SqlClient.SNI.runtime は 6.0.2 → 7.1.0、Microsoft.Bcl.Cryptography / Microsoft.Extensions.Caching.Abstractions / Microsoft.Extensions.Caching.Memory / Microsoft.Extensions.Options / Microsoft.Extensions.Primitives は 9.0.13 → 9.0.18 になります
  - 推移的依存として System.Threading.RateLimiting 9.0.18 (MIT) が新たに同梱されるため、`THIRD-PARTY-NOTICES.txt` に追記しました
- Microsoft.Web.WebView2 1.0.4191.47 → 1.0.4258.31
- System.Data.Odbc / System.Data.OleDb 10.0.11 → 10.0.12
  - 推移的依存の System.Configuration.ConfigurationManager / System.Diagnostics.EventLog / System.Diagnostics.PerformanceCounter / System.Security.Cryptography.ProtectedData も 10.0.11 → 10.0.12 になります

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.6.0 → 4.6.1 に同期

## [4.6.0] - 2026-09-11

### セキュリティ
- **ライセンス認証の問い合わせで、ライセンスキーが URL に載らないようにしました**
  - これまでは起動時のライセンス確認が `https://api.odv.dev/licenses/check/(ライセンスキー)` の形式で、キーが URL の一部として送信されていました。URL は API サーバーや経路上の機器のアクセスログに平文で残るため、社内ネットワークの管理者やログを閲覧できる第三者にライセンスキーを取得されるおそれがありました
  - 問い合わせ方法を変更し、ライセンスキーをリクエスト本文に入れて送るようにしました。URL にキーが現れないため、アクセスログやブラウザ履歴には残りません
  - ライセンスキーはお客様ポータルへのログインにも使用できるため、実質的にパスワードと同等の情報として扱う必要があります。お取り扱いにはご注意ください
  - なお、社内に SSL 可視化 (TLS インスペクション) の仕組みがある環境では通信の中身自体が復号されうるため、キー本体を送らない方式への移行は引き続き検討します
  - **4.5.1 以前をお使いの方へ**: 旧方式 (URL にキーを載せる問い合わせ) のサーバー側の受け付けは **2027 年 9 月 30 日で終了**します。それ以降、4.5.1 以前のバージョンでは起動時にライセンスが解除され、ライセンスファイルの再登録が必要になります。期日までに 4.6.0 以降へ更新してください

### 機能変更
- **サーバー側の一時的な不調でライセンスが解除されないようにしました**
  - これまではライセンス確認の応答が「見つからない」だった場合、その場でライセンスを失効として扱い、登録済みのライセンスファイルを削除していました。API の不具合や経路上の遮断でも同じ応答になりうるため、正規のお客様がライセンスの再取得を強いられるおそれがありました
  - サーバーが「無効」と明示的に回答した場合のみ失効として扱い、さらに 2 回続けて同じ回答を受け取ったときに確定するようにしました
  - 通信できない場合・サーバーが応答しない場合は、これまでどおりそのままご利用いただけます (オフライン環境での動作に変更はありません)

### 内部
- ライセンス確認を `POST /licenses/check` に変更 (旧 `GET /licenses/check/:key` は、旧バージョンのご利用者のためにサーバー側へ残置)
- 失効の確認回数を `%APPDATA%\OpenDUMPViewer\license.check` に記録 (キー自体は書き込まず SHA-256 ハッシュで保持)
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.5.1 → 4.6.0 に同期

## [4.5.1] - 2026-08-30

### 依存関係
- Microsoft.Web.WebView2 1.0.4129.50 → 1.0.4191.47
- System.Data.Odbc / System.Data.OleDb 10.0.10 → 10.0.11
  - 推移的依存の System.Configuration.ConfigurationManager / System.Diagnostics.EventLog / System.Diagnostics.PerformanceCounter / System.Security.Cryptography.ProtectedData も 10.0.10 → 10.0.11 になります
  - 同梱コンポーネントの構成に変更はないため、`THIRD-PARTY-NOTICES.txt` の更新はありません

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.5.0 → 4.5.1 に同期

## [4.5.0] - 2026-08-10

### 機能変更
- **起動時のアップデート通知から、そのまま更新できるようにしました**
  - これまでは通知で「はい」を選んでもリリースページがブラウザで開くだけで、ダウンロードとインストールは手作業でした。バージョン情報ダイアログと同じく、インストーラーをダウンロードして更新するようになります
  - ダウンロード中は進捗ダイアログを表示し、途中でキャンセルできます。中断・失敗した場合に中途半端なファイルは残しません
  - 実行中のアーキテクチャ (x64 / arm64) 向けのインストーラーがリリース資産に見つからない場合に限り、従来どおりリリースページへ誘導します
- **バージョン情報ダイアログの更新で、確認ダイアログを挟まないようにしました**
  - 「最新バージョンに更新する」ボタンを押すこと自体が明示的な操作であるため、押した時点でダウンロードを開始します。進捗はダイアログ内の進捗バーに出ます

### 内部
- 最新リリースの取得・インストーラーのダウンロード・インストーラーの起動を `UpdateLogic` に集約し、バージョン情報ダイアログと起動時のアップデート通知が同じ経路を通るようにしました
- インストーラーのダウンロード元を https かつ GitHub のホストに限定しました
- 応答が `Content-Length` を超えた場合に進捗が 100% を超え、進捗バーが例外になりうる箇所を修正しました
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.4.1 → 4.5.0 に同期

### ドキュメント
- ヘルプの「自動更新」の説明を、10 言語とも新しい動作に更新しました
- ヘルプのインストール手順が「MSI インストーラー」のままだったため、実際の配布物 (Inno Setup の EXE インストーラー) に合わせました
- AGENTS.md に Windows 以外でのコンパイル確認方法を追記しました

## [4.4.1] - 2026-08-10

### 依存関係
- Microsoft.Web.WebView2 1.0.4078.44 → 1.0.4129.50

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.4.0 → 4.4.1 に同期

## [4.4.0] - 2026-07-28

### パフォーマンス
- **大規模ダンプのテーブル一覧取得を高速化しました**
  - EXPDP 形式では、一覧取得の後処理でパーティション名・制約名・索引名を探すために、ファイル全体を **4 回** 走査していました（パーティション用に 1 回、制約・参照制約・索引で各 1 回）。4 つとも単なるバイト列の検索であるため、**1 回の走査**にまとめました
  - あわせて、一覧取得中の列値のデコード（NUMBER / DATE の展開、文字セット変換）をやめました。一覧取得で必要なのは行数だけで、展開した値は破棄していました。EXP・EXPDP の両形式が対象です
  - 数十 GB 規模のダンプでは、一覧取得で読むデータ量が大きく減ります。取得できる情報（表・パーティション・行数・制約・索引）は従来と同一です
    - 制約・索引が 1 表あたりの上限 (20 件) を超える場合に、どれが残るかの順序も従来どおりです

### メモリ
- **テーブルを閉じてもメモリが解放されない問題を修正しました**
  - BLOB 列を含む表のプレビューで、セルのサムネイル画像を**再描画のたびに生成し、破棄していませんでした**。スクロールやウィンドウ操作のたびに GDI+ のビットマップが積み上がり、閉じても解放されませんでした。生成済みの画像をキャッシュして使い回し、ページ切替・フォーム終了で明示的に破棄するようにしました
  - テーブルプレビューを閉じた時点で、保持していた全行データ・列情報・検索条件の参照を切るようにしました。あわせて、大きな表（5 万行以上）を閉じた場合は大きなオブジェクトヒープ (LOH) の圧縮を伴う回収を要求し、解放したメモリが OS に返るようにしました
  - ワークスペースを閉じた時点で、テーブル一覧のメタデータ（表エントリ・列名・列型・制約 JSON）を解放するようにしました
- **モーダルダイアログが破棄されずに残っていた箇所を修正しました**
  - `ShowDialog` はフォームを破棄しません。高度な検索・テーブルプロパティ・エクスポートオプション・バージョン情報・エラー報告・SQL エクスポート・DB 接続の各ダイアログが、開くたびにコントロール一式とウィンドウハンドルを残していました
  - BLOB / CLOB のポップアッププレビューでも、表示に使った画像とウィンドウを確実に破棄するようにしました

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.3.1 → 4.4.0 に同期

## [4.3.1] - 2026-07-26

### セキュリティ
- **不正な .dmp ファイルの解析でヒープバッファオーバーフローが起きる問題を修正しました**
  - レガシー EXP 形式の表定義を JSON へ組み立てる処理で、`snprintf` の戻り値（「書けたはずの長さ」）を無検査で書き込み位置に加算していました。切り詰めが起きた時点で位置がバッファ長を追い越し、以降の書き込みがヒープ領域の外へ出ます
  - 加えて、事前確保のサイズ見積りが最悪長に足りていませんでした。表・列コメントに引用符やバックスラッシュを多く含む .dmp を読ませると、確保済み領域を超えて書き込まれます
  - 影響を受けるのは信頼できない .dmp ファイルを開いた場合です。コメントの内容はファイル作成者が決められるため、細工したファイルによって到達できます
  - 対策として、固定の見積りをやめ必要に応じて伸長するバッファに置き換えました。あわせて同じ形の書き込み（制約 JSON の静的バッファ、カスタム日付書式の整形）も同時に直しています
- リリースワークフローに `permissions` 宣言を追加し、`GITHUB_TOKEN` の既定を read-only に絞りました（宣言のないジョブがリポジトリ既定の権限をそのまま受け取っていました）

### バグ修正
- **制約を持たない表のコメントが SQL エクスポートに出力されない問題を修正しました**
  - コメントは制約と同じ JSON に載る構造ですが、制約が 0 件の表では JSON の生成側・受け取り側の双方が「制約なし」として処理を打ち切っており、`COMMENT ON TABLE` / `COMMENT ON COLUMN` が丸ごと欠落していました
- **コメントに改行やタブを含む表で、制約・コメントの情報が読み取れなくなる問題を修正しました**
  - JSON 文字列内の制御文字をエスケープしていなかったため、生成される JSON が不正になり読み取りに失敗していました

### 依存関係
- ClosedXML を 0.105.0 → 0.105.1 へ更新（上流で SixLabors.Fonts の依存範囲に上限 `< 3.0.0` が設定されました。解決されるバージョンは従来どおり 1.0.0 のままで、同梱コンポーネントの構成と `THIRD-PARTY-NOTICES.txt` に変更はありません）

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.3.0 → 4.3.1 に同期

## [4.3.0] - 2026-07-24

### 配布・セキュリティ
- **リリース成果物に Authenticode コード署名を導入しました**
  - 署名対象: アプリ本体 (`Open DUMP Viewer.exe` / `Open DUMP Viewer.dll` / `Open_DumpParser.dll`) とインストーラー EXE。ポータブル版 ZIP の中身も署名済みです
  - 署名者: 柳井建人（Sectigo 発行の OV コード署名証明書。秘密鍵は Google Cloud KMS の HSM 内にあり、CI にも手元にも存在しません）
  - インストーラー実行時の SmartScreen / UAC で発行元が表示されるようになります（SmartScreen の評価が蓄積されるまでは警告が出る場合があります）
  - Microsoft Store 版 (MSIX) は従来どおり Store の署名で配布されます
  - 検証方法: ファイルのプロパティ →「デジタル署名」タブ、または PowerShell の `Get-AuthenticodeSignature`

### 開発・CI
- リリースワークフローに署名ステップと承認ゲート（GitHub Environment `release-signing`）を追加。実行前にオーナー承認が必要になりました
- 手動実行 (`workflow_dispatch`) の既定を「公開なしのビルド・署名確認のみ」に変更（`publish_release` 入力で明示した場合のみ公開）。従来の `skip_store_submit` 入力は未配線だったため置き換えました

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.2.1 → 4.3.0 に同期

## [4.2.1] - 2026-07-18

### バグ修正
- **インストーラーの使用許諾画面で、契約書末尾の HTML タグが生のまま表示される問題を修正** (v4.2.0-beta で発生)
  - Inno Setup の使用許諾画面は書式なしテキストのみを表示するため、Markdown と HTML を落として表示する必要がありますが、変換処理が見出し・強調・水平線しか処理しておらず、表・リンク・HTML タグが素通りしていました
  - あわせて表とリンクも読みやすい平文へ変換するようにしました

### 開発・CI
- 使用許諾テキストの変換を `installer/Convert-LicenseFiles.ps1` に切り出し、正式版・ベータ版のワークフローで共有するようにしました
- 変換後にマークアップの残存を検査し、残っていればビルドを失敗させるようにしました（表示崩れがリリースまで気付かれなかったため）
- `ChineseSimplified.isl` の取得先を上流の移動に追随（`Files/Languages/Unofficial/` → `Files/Languages/`）。旧 URL が 404 になりビルドが失敗していました

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.2.0 → 4.2.1 に同期

## [4.2.0] - 2026-07-18

### ライセンス・権利表示
- **第三者ライセンスの表示を追加**: 本製品が再配布しているすべての第三者コンポーネントの著作権表示とライセンス全文を `THIRD-PARTY-NOTICES.txt` に収録し、インストーラー版・ポータブル版の双方に同梱するようにしました
  - 対象は推移的依存を含む 29 パッケージ、self-contained 発行で同梱される .NET ランタイム、インストーラーに含まれる Inno Setup
  - MIT が大半ですが、SixLabors.Fonts は Apache-2.0、Microsoft.Web.WebView2 は BSD 系の独自条件、Microsoft.Data.SqlClient.SNI は Microsoft ソフトウェアライセンス条項と、条件が異なるものが混在します
- **インストール時に使用許諾契約の同意画面を表示**するようにしました（日本語は EULA、その他の言語は英語版 LICENSE）
- `LICENSE.txt` / `EULA.md` / `THIRD-PARTY-NOTICES.txt` をインストール先にも配置するようにしました
- 商標表示に「本製品は Oracle Corporation とは無関係であり、承認・提携・後援されていない」旨を明記しました

### 依存関係の更新
- Microsoft.Data.SqlClient 6.1.4 → 7.0.2
- Microsoft.Web.WebView2 1.0.3124.44 → 1.0.4078.44
- System.Data.Odbc / System.Data.OleDb 10.0.3 → 10.0.10

### 開発・CI
- 依存とコードの脆弱性検知を三層で導入: NuGet Audit（ビルド時）・Dependency Review（PR 時）・Dependabot（週次と CVE 公表時）
- CodeQL による静的解析を追加（C ネイティブ DLL と GitHub Actions ワークフローが対象。週次実行）
- PR / Issue テンプレートを日英併記で追加
- CLA の受付を外部サービスから自己ホスト方式へ移行（署名文書がリポジトリ内の `CLA.md` と乖離しない構成に変更）

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.1.3 → 4.2.0 に同期

## [4.1.3] - 2026-06-05

### バグ修正
- **通常の EXPDP ダンプが「データベースのスキーマが見つかりません」となる問題を修正** ([#31](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/31))
  - expdp の `COMPRESSION` 既定値は `METADATA_ONLY` で、DDL (CREATE TABLE 文) は KGC ブロックに圧縮されますが、テーブル定義ストリーム (`<?xml ... STRMTABLE_T>`) と行データは非圧縮で残るため、本来は解析可能です
  - しかし形式判定 (`detect_dump_kind`) が先頭 **1MB** までしか `<?xml` を探索しておらず、オブジェクト数の多い大きなスキーマエクスポートでは先頭の圧縮メタデータ領域が 1MB を超えて最初の可読 XML がその先に位置するため、`COMPRESSION=ALL` の完全圧縮ダンプと誤判定し「非対応」として弾いていました
  - 修正後は EOF まで（最初の `<?xml` を見つけ次第早期終了）走査するようにし、既定圧縮 (`METADATA_ONLY`) のダンプを正しく `DUMP_EXPDP` として解析します。実際にデータ部が圧縮された `COMPRESSION=ALL` のダンプのみ非対応と判定します
  - あわせて、解析不能なダンプ時に VB 側が `odv_list_tables` の戻り値を無視していたため一律「データベースのスキーマが見つかりません」と表示されていた点を修正し、DLL のエラーメッセージ（例: 圧縮非対応の案内）を表示するようにしました
  - 1,000 を超えるテーブルを含むダンプで発生し得た `parse_expdp_dump` 内のバッファオーバーフロー（マスターテーブル走査の `scan_keys`）も修正
  - 影響箇所: `odv_detect.c::detect_dump_kind()`, `odv_expdp.c::parse_expdp_dump()`, `OraDB_NativeParser.vb::ListTables()`

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.1.2 → 4.1.3 に同期

## [4.1.2] - 2026-05-18

### バグ修正
- **中国語(簡体字)dumpの文字化けを修正** ([#30](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/30))
  - レガシーEXP形式のヘッダ文字セットバイト判定に GBK レンジ (`0x50-0x5f`) が無く、`ZHS16GBK` / `ZHS16CGB231280` / `ZHS32GB18030` の dump が ASCII 扱いにフォールバックして Latin-1 として表示される問題を修正
  - 修正後は Windows CP936 (GBK) として正しく UTF-8 に変換されます
  - 影響箇所: `odv_exp.c::byte_to_charset()`, `odv_detect.c::detect_dump_kind()` の EXP 初期判定
  - EXPDP 形式は元から `ZHS16GBK` の文字列判定で対応済みのため影響なし

### 内部
- DLL バージョン文字列 (`ODV_VERSION_STRING`) を 4.1.0 → 4.1.2 に同期（v4.1.1 で更新漏れがあったため）

## [4.1.1] - 2026-05-02

### 開発・CI
- Microsoft Store 提出向けに MSIX Package Identity を新ストア用アプリ (`YANAITaketo.ODVforOracleDB`) に切替。これに伴いストア版とそれ以外 (GitHub Release / Winget / Chocolatey) の MSIX が OS 上別アプリ扱いとなるため、両方をインストールしている場合は更新は別系統となる
- DisplayName / PublisherDisplayName / Capabilities など他のメタデータは v4.1.0 から変更なし

## [4.1.0] - 2026-05-01

### 新機能
- **Microsoft Store 配布対応**: MSIX パッケージング対応により Microsoft Store からのインストール・自動更新が可能になりました
  - 配布チャネル: Microsoft Store / Inno Setup インストーラー / Portable ZIP / winget / Chocolatey の **5 チャネル並列配布**
  - x64 / ARM64 両アーキテクチャを `.msixbundle` で 1 パッケージ提供
  - MSIX 環境での実行時はアプリ内アップデートチェックを自動的に無効化（Store が更新を担当）
  - `.dmp` ファイル関連付けは Store 版でも対応
  - ライセンス保存先 (`%APPDATA%\OpenDUMPViewer`) は他配布チャネルと共有のため、ライセンスはチャネルをまたいで有効
- **GitHub Sponsors リンク**: バージョン情報ダイアログに GitHub Sponsors への寄付リンクを追加（10 言語対応）

### 開発・CI
- GitHub Actions ワークフローを再構築: 各配布チャネル（GitHub Release / Winget / Chocolatey / Microsoft Store）を独立した並列ジョブに分離。1 つの配布失敗が他チャネルを巻き込まなくなり、UI 上でどのチャネルが成功・失敗したか一目で分かるようになりました
- `workflow_dispatch` トリガーを追加（任意ブランチからの手動実行に対応）
- MSIX 用 Visual Assets を `app.ico` から自動生成するスクリプトを追加（`installer/msix/scripts/generate-assets.ps1`）
- MSIX マニフェストテンプレート + ビルドスクリプトを追加（`installer/msix/`）

## [4.0.0] - 2026-04-16

### 破壊的変更 — 商標準拠のためのリネーム
- **ツール名を「OraDB DUMP Viewer」から「Open DUMP Viewer for Oracle database」に変更**
  - Oracle Corporation のトレードマークポリシーへの準拠のため
  - 製品名・ブランド表記を全面刷新、既存のドメイン (`odv.dev`) とライセンスキー形式 (`ODV-XXXX-XXXX-XXXX`) は維持
- **Winget パッケージID**: `OraDBDumpViewer.OraDBDumpViewer` → `OpenDumpViewer.OpenDumpViewer`
  - 旧パッケージは deprecated 化。ユーザーは `winget uninstall OraDBDumpViewer.OraDBDumpViewer` 後に `winget install OpenDumpViewer.OpenDumpViewer` を実行
- **Chocolatey パッケージID**: `oradb-dump-viewer` → `open-dump-viewer`（同様に旧パッケージは deprecated 化）
- **インストーラー AppId (Inno Setup GUID) 刷新**
  - 新規インストール時、既存の旧バージョン (v3.x 以前) を自動的にサイレントアンインストールしてから新規インストール
  - **再ライセンス認証が必要**: 既存ライセンスキーを再度入力してください（ライセンスキー自体は有効）
- **ファイル関連付け ProgID**: `OraDBDumpViewer.dmp` → `OpenDumpViewer.dmp`
- **AppData 設定フォルダ**: `%APPDATA%\OraDBDUMPViewer` → `%APPDATA%\OpenDUMPViewer`（旧フォルダはアンインストール時に既定で削除）
- **インストール先ディレクトリ**: `C:\Program Files\OraDB DUMP Viewer` → `C:\Program Files\Open DUMP Viewer`
- **DLL 名**: `OraDB_DumpParser.dll` → `Open_DumpParser.dll`
- **VB.NET RootNamespace / メインフォームクラス**: `OraDB_DUMP_Viewer` → `Open_DUMP_Viewer`
- **ソリューション/プロジェクトファイル名**: `OraDB DUMP Viewer.{slnx,vbproj}` → `Open DUMP Viewer.{slnx,vbproj}`
- **GitHub 組織/リポジトリ**: `OraDB-DUMP-Viewer/OraDB-DUMP-Viewer` → `Open-DUMP-Viewer/Open-DUMP-Viewer`
- **メールアドレス**: `oradb_dump_viewer@sent.at` → `open_dump_viewer@sent.at`
- **配布バイナリ命名**: `OraDBDumpViewer_v*_installer_*.exe` → `OpenDumpViewer_v*_installer_*.exe`

### 保持される項目（変更なし）
- ドメイン `odv.dev` / `api.odv.dev` / `report.odv.dev` / `status.odv.dev`
- ライセンスキー形式 `ODV-XXXX-XXXX-XXXX`
- C API 関数名プレフィックス (`odv_*`, `ODV_*`)
- 既存の支払い・サブスクリプション（Stripe側では過去の請求書の商品名は凍結される）

## [3.3.0] - 2026-04-12

### 新機能
- **SQL エクスポート API をビットフラグ方式に刷新** ([#25](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/25))
  - `odv_set_sql_flags()` による `ODV_SQL_CREATE_TABLE` / `ODV_SQL_CREATE_INDEX` / `ODV_SQL_WRITE_COMMENTS` / `ODV_SQL_WRITE_INSERTS` の個別制御
  - DDL のみ出力モード（INSERT 文を省略可能）
  - 全テーブル一括出力（`table_name=NULL` で全テーブルを 1 パスでエクスポート）
  - 空テーブル（0 行）の DDL 出力に対応

### 修正
- **Oracle 向け SQL エクスポートで DATE / TIMESTAMP / RAW 型の値が正しくフォーマットされない問題を修正**
  - DATE: `TO_DATE()` でユーザー指定の日付フォーマットに対応
  - TIMESTAMP: `TO_TIMESTAMP()` でマイクロ秒を含むフォーマットに対応
  - RAW: `HEXTORAW()` で正しいバイナリリテラルを出力
- **MySQL 向け SQL エクスポートで CREATE INDEX の識別子クォートが正しくない問題を修正**
  - Oracle 形式のダブルクォートからバッククォートに変換
- **Linux (GCC) ビルド時の警告を全て修正** (25 件 → 0 件)

### 破壊的変更
- `odv_set_sql_options()` を `odv_set_sql_flags()` に置き換え（C API 利用者のみ影響）

## [3.2.0] - 2026-04-11

### 新機能
- **SQL エクスポートの DDL 選択出力** ([#25](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/25))
  - CREATE TABLE / CREATE INDEX / COMMENT ON を個別に ON/OFF 可能に
  - SQL エクスポートダイアログとエクスポートオプションダイアログの両方に対応
  - デフォルトは全て ON（完全な SQL スクリプトを出力）
- **C パーサーライブラリのクロスプラットフォーム対応** ([#27](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/27))
  - Linux (.so) / macOS (.dylib) でのビルドをサポート
  - Makefile を追加（`make` で共有ライブラリをビルド可能）
  - Windows 固有 API (`__stdcall`, `_stricmp` 等) をポータブルマクロに抽象化

### ドキュメント
- README に [odv-testdump](https://github.com/Open-DUMP-Viewer/odv-testdump) リポジトリへのリンクを追加 ([#26](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/26))

## [3.1.2] - 2026-04-09

### 修正
- **EXP 形式の BLOB / CLOB を含むテーブルが 0 行と表示される問題を修正** ([#24](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues/24))
  - 従来は LOB 列を含むテーブルのレコードを強制スキップしていたため、行数が 0、データが空で表示されていた
  - LOB 列のロケータ + チャンクストリーム形式 (単一チャンク・マルチチャンク) を完全にサポート
  - 同一行内に NULL / 非NULL の BLOB が混在するケースにも対応
  - 200KB クラスのマルチチャンク BLOB も byte-perfect に抽出可能

## [3.1.1] - 2026-04-03

### 修正
- ハートビート・ライセンスチェックの API エンドポイントを `api.odv.dev` に移行

## [3.1.0] - 2026-04-02

### 新機能
- **試用版モード**: ライセンス未登録でも基本機能（ダンプ閲覧・テーブル一覧・検索）が利用可能に
  - エクスポート・LOB抽出・ワークスペース保存はライセンス認証後に利用可能
  - テーブルプレビューは検索結果の先頭100行まで表示（検索自体は全データ対象）
  - タイトルバー・ステータスバーに試用版表示
  - 試用版中のライセンス認証で即座に全機能解除
- **readme.txt 同梱**: Vector 掲載基準を満たす利用者向け説明書をインストーラーに同梱

### 改善
- ヘルプ「バージョン情報」に作者連絡先（メール・Web）を追加（全10言語）

## [3.0.1] - 2026-03-20
### セキュリティ
- ライセンス認証サーバを変更

## [3.0.0] - 2026-03-20

### 新機能
- **パーティションテーブル完全対応**: EXP / EXPDP 両形式でパーティション検出・階層表示・パーティション単位プレビュー・エクスポートに対応
- **BLOB/CLOB プレビュー表示**: LOB カラムの内容をテーブルプレビュー内に表示（BLOB は hex、CLOB は UTF-8 テキスト、最大 4KB）
- **CHECK 制約エクスポート**: CHECK 制約を `ALTER TABLE ... ADD CONSTRAINT` として DDL に反映
- **INDEX 定義エクスポート**: 通常 INDEX・複合 INDEX・関数 INDEX の `CREATE INDEX` 文を生成
- **テーブル・カラムコメントエクスポート**: `COMMENT ON TABLE/COLUMN` 文を SQL エクスポートに含める
- **カスタム CSV デリミタ**: タブ、セミコロン、パイプ等のデリミタを選択可能に
- **EXPDP メタデータ高信頼化**: マスターテーブルスキャンによる制約名・INDEX 名の自動取得
- **impdp SQLFILE 連携**: Oracle Client 環境で impdp を使った完全な DDL 生成（オプション機能）
- **複数テーブル選択エクスポート**: 複数テーブルを一括で CSV/SQL/Excel/Access にエクスポート
- **エクスポート進捗表示**: テーブル数・処理行数・経過時間をリアルタイム表示
- **Access エクスポート**: Microsoft Access (.accdb) 形式への一括エクスポート
- **起動時アップデートチェック**: 新バージョンの自動検出と通知
- **ライセンス認証サーバー検証**: オンラインでのライセンス有効性確認

### 改善
- **EXPDP パーサ全面書き直し**: ステートマシン方式に刷新し、LOB カラムの正確な解析を実現
- **文字セット対応拡充**: WE8MSWIN1252、WE8ISO8859P15、ZHS16GBK を追加
- **SYS_IME_* 内部カラムフィルタリング**: OSON 内部カラムを自動除外
- **26ai EXP 形式対応**: DDL 行分割・COMMENT ON・ALTER TABLE MODIFY DEFAULT の蓄積処理

### セキュリティ
- **C DLL**: バッファオーバーフロー修正（odv_number.c）、整数オーバーフローガード（LOB バッファ）、realloc メモリリーク修正、境界チェック追加
- **VB.NET**: パストラバーサル防止（BulkExportLogic）、コマンドインジェクション対策（AboutDialog, ImpdpHelper）、SQL インジェクション軽減（DEFAULT 値）、TLS 暗号化強制（SQL Server 接続）

## [2.3.1] - 2026-03-18

### 新機能
- **SQL エクスポート: 制約 (PRIMARY KEY / UNIQUE / FOREIGN KEY) を出力**: EXP 形式のダンプファイルから制約情報を抽出し、`ALTER TABLE ... ADD CONSTRAINT` として DDL に反映

### バグ修正
- **EXP パーサー: スキーマ名の先頭に余分な文字が付く問題を修正**: ヘッダのモードフラグバイトがスキーマ名に含まれていた問題を修正

## [2.3.0] - 2026-03-18

### 新機能
- **SQL エクスポート: NOT NULL 制約を出力**: EXP / EXPDP 両形式のダンプファイルから NOT NULL 制約を取得し、CREATE TABLE DDL に反映
- **SQL エクスポート: DEFAULT 値を出力 (EXP 形式)**: EXP ダンプの `ALTER TABLE ... MODIFY DEFAULT` から DEFAULT 値を抽出し、CREATE TABLE DDL に反映
- **SQL Server 向けフォーマット強化**: `DROP TABLE IF EXISTS` + `GO` 区切り + `ON [PRIMARY]` + `USE [DB名]` 入力欄を追加
- **SQL Server 向けスキーマを `dbo` に自動変換**: Oracle のスキーマ名を SQL Server の標準スキーマ `dbo` に変換
- **裸の NUMBER を実データから整数型に推定するオプション**: 精度なしの `NUMBER` カラムの実データと DEFAULT 値を走査し、値の範囲に応じて `SMALLINT` / `INT` / `BIGINT` にマッピング

### 改善
- **C DLL: ODV_TABLE_CALLBACK 拡張**: NOT NULL フラグと DEFAULT 値をコールバック経由で VB.NET に伝達
- **ローカライズ: 不足キー13件を修正**: 8言語の `Menu_Help_Language_*` キーと英語の `LobExtract_*` キーを追加
- **ローカライズ: resx フォーマット統一**: de/es/fr/it の複数行形式を1行形式に統一

### バグ修正
- **エラー報告: 内容欄で Enter キーが改行にならない問題を修正**
- **エラー報告: テーブルプレビュー表示中にダンプファイルが未開扱いになる問題を修正**
- **SQL エクスポート: NUMBER(p,0) を整数型にマッピング**: 精度に応じて `SMALLINT` / `INT` / `BIGINT` に適切にマッピング

## [2.2.1] - 2026-03-18

### バグ修正
- **エラー報告: 内容欄で Enter キーが改行にならない問題を修正**: 内容欄で Enter を押すとフォームが送信されてしまう問題を修正。`AcceptsReturn` を有効化し、改行を正しく入力可能に
- **エラー報告: テーブルプレビュー表示中にダンプファイルが未開扱いになる問題を修正**: TablePreview がアクティブな場合に Workspace が見つからず、ダンプファイル情報が取得できなかった問題を修正

### 改善
- **SQL エクスポート: NUMBER(p,0) を整数型にマッピング**: Oracle の `NUMBER(p,0)` / `INTEGER` / `SMALLINT` が一律 `DECIMAL` に変換されていた問題を修正。精度に応じて `SMALLINT` / `INT` (`INTEGER`) / `BIGINT` に適切にマッピング (PostgreSQL / MySQL / SQL Server 全対応)

## [2.2.0] - 2026-03-17

### 改善
- **SQL エクスポートの型マッピングを大幅改善**: Oracle 固有型を各 DBMS (PostgreSQL / MySQL / SQL Server) に正しく変換。Oracle 23ai・PostgreSQL 17・MySQL・SQL Server の4環境で実機検証済み
  - `NUMBER(>38)` (EXPDP の FLOAT) を `DOUBLE PRECISION` / `DOUBLE` / `FLOAT` に正しく変換
  - `NUMBER(p, 負スケール)` を `NUMERIC(p+|s|, 0)` に変換
  - `TIMESTAMP WITH LOCAL TIME ZONE` を PostgreSQL `TIMESTAMPTZ` / MySQL `DATETIME` / SQL Server `DATETIMEOFFSET` に変換
  - `FLOAT(n)` のバイナリ精度を IEEE 浮動小数点型に正しくマッピング
  - `INTERVAL` / `LONG RAW` / `XMLTYPE` / `ROWID` / `BFILE` 等の不足型を追加
- **Oracle 向け DROP TABLE を PL/SQL ブロックに変更**: `IF EXISTS` 非対応の Oracle 21c 以前でもエラーなく実行可能
- **NaN / Infinity の DBMS 別出力**: Oracle (`BINARY_FLOAT_NAN` 等) / PostgreSQL (`'NaN'::REAL` 等) / MySQL・SQL Server (`NULL`)
- **EXPDP パーサーで負スケールを保持**: `NUMBER(5,-3)` 等の型情報が正しく type_str に反映

## [2.1.0] - 2026-03-17

### 改善
- **EXPDP パーサー大幅改善**: TYPE_NUM マッピング拡充・レコード overread 修正

## [2.0.0] - 2026-03-16

### 破壊的変更
- **インストーラーを WiX (MSI) から Inno Setup (EXE) に移行**: v1.x の MSI 版をご利用の方は、手動でアンインストール後に v2.0.0 をインストールしてください

### 新機能
- **インストーラーの多言語対応 (10言語)**: OS の言語設定に応じてインストーラー UI が自動的に切り替わります。対応言語: 日本語・英語・ドイツ語・スペイン語・フランス語・イタリア語・韓国語・ポルトガル語（BR）・ロシア語・中国語（簡体）
- **インストール完了後にアプリを起動**: インストール完了画面からアプリを直接起動可能
- **アンインストール時の設定削除確認**: ユーザー設定（ライセンス情報等）の削除を確認するダイアログを表示
- **アンインストール後の最新版案内**: アンインストール完了後に最新リリースページへの誘導を表示
- **実行中アプリの自動検出**: インストール時にアプリが実行中の場合、終了を促す
- **.dmp ファイル関連付け**: インストーラー版でエクスプローラーから .dmp ファイルをダブルクリックして直接開けるように
- **コマンドライン引数対応**: コマンドラインから .dmp ファイルパスを指定して起動可能

## [1.3.3] - 2026-03-16

### バグ修正
- **NUMBER型の末尾ゼロ欠落を修正**: Oracle NUMBER のバイナリデコードで、末尾がゼロの整数値（例: 2700, 40000, 100）が正しく表示されない問題を修正。Oracle NUMBER は base-100 可変長形式で末尾のゼロペアを省略して格納するが、省略分の補完処理が欠落していた。正・負の両方で修正

### 改善
- **GitHub Actions を Node.js 24 対応に更新**: Node.js 20 非推奨化に伴い、CI/CD ワークフローの全アクションを Node.js 24 対応版に更新。ilammy/msvc-dev-cmd → vcvarsall.bat 直接呼出し、softprops/action-gh-release → gh CLI に置換

## [1.3.1] - 2026-03-09

### 新機能
- **ヘルプの多言語対応**: ヘルプドキュメント (14ページ) を10言語に翻訳。言語設定に応じたヘルプが自動表示される

## [1.3.0] - 2026-03-09

### 新機能
- **多言語対応 (10言語)**: 日本語・英語に加え、中国語（簡体）・韓国語・ドイツ語・フランス語・スペイン語・イタリア語・ロシア語・ポルトガル語（BR）の8言語を追加。ヘルプメニューの「言語」から切り替え可能

---

## [1.2.12] - 2026-03-09

### 改善
- **SQL エクスポートダイアログに DROP TABLE + CREATE TABLE の ON/OFF チェックボックスを追加**: エクスポート時に DDL 出力の有無を切り替え可能に
- **ODBC エクスポートに DROP TABLE IF EXISTS を追加**: 既存テーブルがあれば自動削除してから CREATE TABLE を実行。全エクスポート形式で既存テーブル対応が完了

---

## [1.2.11] - 2026-03-09

### 改善
- **SQL エクスポートに DROP TABLE IF EXISTS を追加**: CREATE TABLE の前に DROP TABLE IF EXISTS を出力し、既存テーブルがあれば自動削除。Oracle/PostgreSQL/MySQL/SQL Server 各 DBMS の構文に対応
- **SQL エクスポートの CREATE TABLE デフォルトを ON に変更**: DROP TABLE + CREATE TABLE + INSERT INTO を標準出力に

---

## [1.2.10] - 2026-03-09

### 新機能
- **アプリ内ヘルプビューアー**: WebView2 ベースのヘルプビューアーをアプリ内に統合。左側に目次ツリー、右側にHTMLコンテンツを表示。戻る/進む/ページ内検索に対応
- **ヘルプコンテンツ大幅更新**: 高度な検索 (12演算子)、全6種エクスポート (Excel/Access/SQL Server/ODBC)、INTERVAL型、自動更新、ライセンス認証、エラー報告等の機能をドキュメントに反映

---

## [1.2.9] - 2026-03-08

### バグ修正
- **EXPDP 8列以上テーブルの解析修正**: 0x08/0x09/0x0cバイトが3cセグメント内で列数として正しく処理されるよう修正。8〜9列テーブル（受注データ、社員マスタ等）が解析不能だった問題を解消
- **EXPDP フィルタ付きパースのシークオフセット修正**: DDLオフセットをブロック境界 (4096バイト) にアラインメントし、テーブル指定解析が正しく動作するよう修正

### 新機能
- **ライセンス有効性オンラインチェック**: 起動時にサーバーへライセンスキーの有効性を問い合わせ。オフライン環境では従来通り許容
- **EXP NCHAR/NVARCHAR2 文字セット対応**: EXP形式のNCHAR/NVARCHAR2カラムをUTF-16BE→UTF-8に正しく変換
- **INTERVAL型デコード対応**: INTERVAL YEAR TO MONTH / INTERVAL DAY TO SECOND のバイナリデコードをEXP・EXPDP両形式で実装
- **EXPDP INTERVAL型の自動認識**: DataPump DDLのTYPE_NUM 182/183をINTERVAL型として認識

---

## [1.2.3] - 2026-03-07

### 改善
- **エラー報告の環境情報を拡充**: DLL版、CPU、ロケール、DPI、メモリ、画面解像度、ダンプ情報を含む全項目をIssueに反映
- **ダンプファイル添付機能**: エラー報告時にユーザーの許可でダンプファイルを添付可能 (50 MB以下)

---

## [1.2.2] - 2026-03-07

### バグ修正
- **ライセンス検証エンドポイントの修正**: Heartbeat APIのURLを正しいエンドポイントに修正

---

## [1.2.1] - 2026-03-07

### 新機能
- **ライセンス有効性チェック**: アプリ起動時にライセンスの有効性を検証し、通知を表示

---

## [1.2.0] - 2026-03-06

### 新機能
- **多言語対応 (i18n)**: 日本語 / 英語の切替に対応
  - `ILocalizable` インターフェース + `LocaleManager` による統一的なローカライズ基盤
  - `Strings.resx` / `Strings.en.resx` によるリソースファイル管理
  - メニュー・ツールバー・ダイアログ・メッセージ・エラー報告を全面ローカライズ
  - ヘルプメニューから言語切替可能

### 改善
- **UI レイアウト修正**: ラベル重なり問題を修正

---

## [1.1.1] - 2026-03-05

### 改善
- **TIMESTAMP 表示を Oracle 準拠に改善**: カラム定義の精度 (TIMESTAMP(6) など) に基づく固定桁数表示。デフォルトは Oracle 標準の 6 桁 (マイクロ秒)
- **SQL 出力にバージョン情報を表示**: エクスポートした SQL ファイルのコメントにアプリバージョンを記載

### バグ修正

## [1.1.0] - 2026-03-03

### 新機能
- **TablePreview からのエクスポート対応**: テーブルデータビュー表示中に全6形式 (CSV / SQL / Excel / Access / SQL Server / ODBC) でエクスポート可能
- **クリップボードコピー**: DataGridView で Ctrl+C (コピー) / Ctrl+A (全選択) に対応。ヘッダー付きタブ区切りテキストで Excel にそのまま貼り付け可能
- **ドラッグ＆ドロップ対応**: ダンプファイルをウィンドウにドロップして解析開始
- **ARM64 ネイティブサポート**: x64 + ARM64 並列ビルド対応

### 改善
- **テーブル解析の高速化**: 非同期化 + ListView 描画最適化 + List 容量予約
- **エラー報告の強化**: 詳細な環境情報・ダンプファイル情報を含むレポート生成
- **ステータスラベル自動リセット**: 処理完了後5秒でステータス表示を自動クリア
- **README を製品版として整理**
- **ToolStripButton の名前整理**: コード可読性の向上

## [1.0.0] - 2026-03-03

### 新機能: エクスポート
- **CSV 出力**: RFC 4180 準拠。C DLL ストリーミング (全行) + VB.NET (フィルタ後) の2経路
- **SQL スクリプト**: INSERT 文生成。Oracle / PostgreSQL / MySQL / SQL Server の4 DB 対応
- **Excel 出力**: ClosedXML による .xlsx 生成。型別セル書式、ヘッダスタイル、列幅自動調整
- **Access 出力**: ADOX + OleDb による .accdb 生成。1000行バッチトランザクション
- **SQL Server 出力**: SqlBulkCopy による高速一括挿入。接続テスト・認証方式選択対応
- **ODBC 出力**: 汎用 ODBC ドライバ経由で任意の DB に出力。DSN 選択 / 接続文字列直接入力
- **進捗ダイアログ**: 全エクスポートで進捗表示 + キャンセル対応 (BackgroundWorker)
- **TablePreview からの CSV 出力**: フィルタ/検索後のデータをそのまま CSV に出力

### 共通基盤
- ExportHelper: Oracle → 各 DB の型マッピング辞書、SQL/CSV エスケープヘルパー
- ExportProgressDialog: ProgressBar + 行カウンタ + キャンセル機能
- DatabaseConnectionDialog: SQL Server / ODBC の2タブ構成、接続テスト機能
- C DLL API 拡張: `odv_export_sql()` (SQL INSERT 文ストリーミング生成)

### エクスポートオプション
- 日付フォーマット選択: YYYY/MM/DD HH:MI:SS / YYYYMMDD / YYYYMMDDHHMMSS / カスタム書式
- CSV: カラム名ヘッダ出力 ON/OFF、カラム型行出力 ON/OFF
- SQL スクリプト: CREATE TABLE DDL 出力 ON/OFF (Oracle/PG/MySQL/MSSQL 対応)
- 設定の永続化 (アプリ再起動後も保持)
- ツール → オプション メニューから設定

### テーブル管理
- テーブル除外機能: ListView で選択したテーブルをエクスポート対象から除外
- 一括エクスポート: 全可視テーブルを一度にエクスポート (CSV/SQL/Excel/Access/SQL Server/ODBC)
- 「選択を反転」機能

### 新機能: LOB ファイル抽出
- BLOB / CLOB / NCLOB カラムのデータを個別ファイルとして抽出
- C DLL: `odv_extract_lob()` API 追加 (EXPDP / EXP 両形式対応)
- LOB チャンク蓄積・ファイル書き出し機構
- ファイル名方式: 連番 / カラム値
- LobExtractDialog: LOB カラム選択・出力先・拡張子設定

### 新機能: ワークスペース保存/読込
- `.odvw` 形式で作業状態を JSON 保存・復元
- 保存項目: ダンプファイルパス、除外テーブル、検索フィルタ、スキーマ、ツリー展開状態
- ワークスペースを開く / 保存 / 名前を付けて保存 / 閉じる (保存確認付き)
- 最近使ったワークスペース / ダンプファイルの MRU リスト (最大5件)

### 新機能: メニュー整理・編集操作・オブジェクト操作
- テーブル除外の元に戻す (Ctrl+Z) / やり直し (Ctrl+Y) — Undo/Redo スタック
- 切り取り / コピー / 貼り付け / すべて選択の編集メニュー
- オブジェクト操作メニュー: 開く / 削除 (除外) / フィルタクリア / エクスポート / レポート出力
- 不要メニュー項目の削除 (最新の情報に更新、レポート定義)

### 新機能: ヘルプ
- HTML ヘルプコンテンツ 14ページ (操作説明、エクスポート、LOB抽出、ショートカットキー等)
- CHM (コンパイル済みヘルプ) プロジェクト + ビルドスクリプト
- 目次メニュー (F1 キー): CHM 優先、HTML フォールバック対応
- ライセンス認証メニューのハンドラ接続

### メモリ安全性改善
- odv_datetime.c: `sprintf` → `snprintf` (9箇所)
- odv_exp.c: 4byte/2byte 長デコードの符号拡張修正
- odv_sql.c: SAFE_APPEND マクロでバッファ追跡
- odv_detect.c: find_bytes 境界チェック追加
- odv_api.c: `strcat` → `snprintf`, `goto cleanup` パターン
- 単体テスト: LOB 46件 + 安全性 24件 = 全70件通過

### 改善
- ツールバーボタンの整理 (テキスト出力ボタンを CSV に統合、ボタン名を分かりやすく変更)

### バグ修正
- バージョンアップ機能のエンコーディングエラーを修正 (Encoding 932 → UTF-8)
- エクスポートダイアログのフリーズを修正
- CSV/SQL エクスポートで dump_type 未検出により 0 行になるバグを修正

### NuGet パッケージ追加
- ClosedXML 0.105.0 (Excel 出力)
- Microsoft.Data.SqlClient 6.1.4 (SQL Server 接続)
- System.Data.OleDb 10.0.3 (Access 出力)
- System.Data.Odbc 10.0.3 (ODBC 接続)

## [0.1.3] - 2026-03-02

### 新機能
- エラー報告機能: アプリ画面から直接エラー報告を送信できるダイアログを追加
  - GitHub アカウント不要 (Cloudflare Workers 経由で GitHub Issue を自動作成)
  - 件名・内容・連絡先 (任意) を入力して送信
  - アプリバージョン・OS情報・.NETバージョンを自動付加
  - レート制限 (5件/IP/時) によるスパム防止

## [0.1.2] - 2026-02-27

### 新機能
 スキーマ名、テーブル名、カラム数、行数、カラム一覧（名前+型）を表示するダイアログを追加
- バージョン情報: 現在のバージョン表示とGitHub最新リリースの自動チェック機能を追加
- 自動更新: 新バージョン検出時にMSIインストーラーをダウンロードして更新する機能を追加
- Winget (Windows Package Manager) 対応: リリース時に自動でマニフェストPRを送信

### 改善
- アプリケーションアイコンを追加


## [0.1.1] - 2026-02-27

### バグ修正
- RTABLES (単一テーブル) モードの EXP ダンプファイルが解析できない問題を修正
- Step 1 ステートマシンに METRIC マーカー認識を追加し、DDL 領域への遷移を改善
- RTABLES モード時に CONNECT 文がなくてもスキーマ名をヘッダから自動設定
- テーブルプレビューの1ページ表示件数の上限 (100件) を解除し、任意の件数を指定可能に
- 列ヘッダクリック時のソートを全データ対象に修正（ページ内のみ→全ページ横断）

### パフォーマンス改善
- DataGridView を VirtualMode 化（大量ページでも高速描画）
- ソートキーの事前計算で列ソートを高速化
- 行データを Dictionary → String配列に変更しメモリ使用量を約40%削減
- CellValueNeeded の辞書ルックアップを排除し表示速度を向上

## [0.1.0] - 2026-02-27

初回リリース

### ダンプファイル解析
- Oracle EXP (レガシー) 形式の解析に対応
- Oracle EXPDP (DataPump) 形式の解析に対応
- C ネイティブ DLL (`Open_DumpParser.dll`) による高速解析
- 2フェーズ解析: フェーズ1でテーブル一覧を高速取得、フェーズ2で選択テーブルのみオンデマンド解析
- テーブル位置キャッシュ (DDLオフセット) による高速シーク
- フィルタテーブル処理完了後の早期終了最適化

### Oracle データ型デコーダ
- NUMBER (任意精度) バイナリデコード
- DATE / TIMESTAMP デコード
- BINARY_FLOAT / BINARY_DOUBLE デコード

### 文字セット
- UTF-8 / Shift_JIS (SJIS) / EUC-JP の自動判定・変換
- ダンプファイルのヘッダからエンコーディングを自動検出

### ワークスペース画面
- MDI (Multiple Document Interface) ベースのメインウィンドウ
- TreeView でスキーマ一覧を階層表示
- ListView でテーブル一覧 (テーブル名・所有者・種類・行数) を表示
- テーブルダブルクリックでデータプレビューを表示

### テーブルプレビュー
- DataGridView によるテーブルデータの表示
- 高度な検索機能 (AND/OR 複合条件)
  - 12種類の比較演算子 (含む / 等しい / 前方一致 / 後方一致 / 大小比較 / NULL判定 など)
  - 大文字小文字の区別オプション
  - 前回の検索条件を保持

### エクスポート
- CSV 出力 (RFC 4180 準拠)
- SQL スクリプト出力 (Oracle / PostgreSQL / MySQL / MSSQL 対応)
- Excel (.xlsx) / Access (.mdb, .accdb) / テキスト出力
- SQL Server / ODBC 接続による直接エクスポート
- オブジェクト一覧レポート / テーブル定義レポート

### UI / UX
- ツールバーによるエクスポート操作
- ステータスバーに解析進捗を表示 (パーセンテージ・経過時間・残り時間・処理速度)
- DDLスキャン中とレコード読取中で進捗メッセージを区別

### ライセンス認証
- RSA-2048 公開鍵暗号による署名検証
- ライセンスキー (ODV-XXXX-XXXX-XXXX-XXXX) 形式
- ステータスバーにライセンス保有者名を表示

### ビルド / CI
- GitHub Actions による自動ビルド・リリース (`release` ブランチ push 時)
- C ネイティブ DLL の MSVC 自動ビルド (GitHub Actions)
- WiX v5 による MSI インストーラー生成
- ポータブル ZIP 配布
- .NET 10.0 (LTS) 対応
