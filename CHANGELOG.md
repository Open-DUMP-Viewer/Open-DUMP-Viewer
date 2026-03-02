# Changelog

## [1.0.0] - 2026-03-02

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
- C ネイティブ DLL (`OraDB_DumpParser.dll`) による高速解析
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
