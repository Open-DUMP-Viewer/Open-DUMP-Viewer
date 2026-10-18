# 群13 への引継ぎ事項(群8〜14・群12a の作業者と統括から)

brief_g13.md の 10 件(M-12〜M-19・M-28・M-32)に加え、次を同じ群で直す。いずれも SQL・CSV 出力の書き手側の問題で、
他の群の修正で値・表が正しく読めるようになって見えるようになったもの。

## 方針(統括)
- プレースホルダ(%BLOB% / %VARRAY% / %JSON% / %LONG% など、値を復号できなかった印)を SQL・CSV に「値として」書かない。
  SQL では NULL にして、その旨をコメント(-- NOTE:)で残す。CSV では空にするか、ヘッダ・別ファイルで分かる形にする。
  どちらも黙って誤った値を入れないこと。
- 値は欠けずに全量を出す(LOB・LONG・長い文字列)。Oracle の 1 リテラル 4000 バイト、SQL*Plus の 1 行の長さの上限、
  他 DBMS のリテラル・型の制約に合わせて分割・連結・関数呼び出しで書く。
- DLL の経路(odv_sql.c / odv_csv.c)と VB の経路(Logics/Export/*.vb の TablePreview からの出力・ODBC)で、
  同じ値・同じ型が同じ結果になること。型の対応表は 1 か所に仕様として書き、両方がそれに従う。

## 型の写し(他 DBMS 方言)— M-18 / M-19 の範囲を exp・expdp の両方に
- 群8 の修正で expdp の型名が exp と同じ正しい形(VARCHAR2(10 CHAR)、NUMBER(*,0)、FLOAT(p)、INTERVAL YEAR(p) TO MONTH 等)
  になった。そのため、他 DBMS 方言への既存の写しの誤りが expdp でも出る(リリースまでに必ず直す)。
  - DLL(odv_sql.c): PG / MySQL で VARCHAR(10 CHAR)、NUMERIC(*,0) / DECIMAL(*,2) の構文誤り。FLOAT が REAL / DOUBLE。
  - VB ExportHelper: NUMBER(*,0) を SMALLINT、n CHAR をそのまま写す、FLOAT を TEXT。
  - VB OdbcExportLogic: FLOAT を VARCHAR(4000)。
- PG の TIMESTAMP(9) は 6 桁までしか持てない。丸めるなら黙らず注記する(どう扱うかは群13 が決め、NOTES と最終報告に)。
- TSTZ / TSLTZ(群12a): Oracle 方言は DLL で正しく出る(FROM_TZ ... AT TIME ZONE)。他方言(PG timestamptz、
  SQL Server datetimeoffset、MySQL は TZ を持たない)の書き方を決めて直す。VB 経路(TablePreview の SQL・Excel)は
  表示文字列(「時刻 地域名 時差」「時刻 +HH:MM」、TSLTZ は末尾に DBTIMEZONE)を使っているので、型として正しく書く。
- JSON / VECTOR / BOOLEAN(群9): Oracle 方言(JSON・VECTOR・BOOLEAN の 23ai リテラル)と他方言の書き方。
- VARRAY: 値は %VARRAY%(要素の型がダンプに無い)。INSERT に '%VARRAY%' を書いている → 上の方針で NULL + 注記。

## LOB・LONG の全量(M-12・M-15 の延長)
- 群9: 外部表形式の CSV/SQL も直接パスと同じく LOB の先頭 4096 バイトで切れる → 全量に。
- 群11: exp の LONG / LONG RAW は 6,144,000 バイトまで行バッファに持つ(超過は last_error)。全量出力ではストリームで出す。
  4000 バイトを超える LONG の SQL が ORA-01704。VB は LONG RAW の 16 進を文字列のまま書く。
- 群10: 長い DDL 行(CHECK・DEFAULT・コメント)が SQL*Plus の 1 行上限(2499/3000/4999 バイト)で SP2-0027 になる。

## DDL・制約(M-17 の延長)
- expdp はコメント・DEFAULT・制約・索引を出さない(群8・群9 で照合に見えるようになった)。M-17 の根本として、
  expdp のメタデータ(制約・索引・コメント・DEFAULT の XML)を読み、DLL の SQL に出す。
- 群10: 全表の後ろに置かれた FK・関数索引・DESC 索引が DLL の表ごとの SQL に出ない(誤った表に付くのは止めた)。
  正しい表の SQL に出す。VB の WriteConstraints は FK の delete_rule / deferrable / deferred / status を使っていない。
- 群8: Oracle 方言の DROP(BEGIN EXECUTE IMMEDIATE 'DROP TABLE "..."')で表名の ' を二重化していない。
  SQL Server の OBJECT_ID('...') も同じ。表名 it's / T'Q で PLS エラー。

## 確認の方法
- SQL は DB へ流し戻して元の表と比べる(tools/sql_roundtrip.sh / .py)。11/21/23 の Oracle と、他方言は構文の検査
  (PG / MySQL / SQL Server の実 DB はこの環境に無い。構文検査の方法を決めて NOTES に)。
- VB 経路は AGENTS.md の方法(net10.0 のコンソールに実ソースをリンクし、WinForms・Loc・DLL 呼び出しだけ差し替え)で実行して確かめる。
