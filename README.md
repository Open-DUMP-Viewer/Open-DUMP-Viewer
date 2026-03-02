<p align="center">
  <h1 align="center">OraDB DUMP Viewer</h1>
  <p align="center">
    Oracle データベースの EXPORT ファイル (.dmp) を<br>
    <strong>Oracle 環境なしで解析・閲覧</strong>できる Windows デスクトップアプリケーション
  </p>
</p>

<p align="center">
  <a href="https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases"><img src="https://img.shields.io/github/v/release/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer?label=%E6%9C%80%E6%96%B0%E3%83%90%E3%83%BC%E3%82%B8%E3%83%A7%E3%83%B3&style=flat-square" alt="Release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20x64-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/runtime-.NET%2010.0-purple?style=flat-square" alt=".NET 10">
  <a href="https://www.odv.dev/"><img src="https://img.shields.io/badge/%E3%83%A9%E3%82%A4%E3%82%BB%E3%83%B3%E3%82%B9-www.odv.dev-green?style=flat-square" alt="License"></a>
</p>

---

## 特徴

<table>
<tr>
<td width="50%">

### Oracle 環境不要
データベースやクライアントのインストールなしで、.dmp ファイルの中身を直接閲覧できます。

</td>
<td width="50%">

### 高速な解析エンジン
C ネイティブ DLL による高速解析。テーブル一覧を瞬時に取得し、必要なテーブルだけをオンデマンドで解析します。

</td>
</tr>
<tr>
<td>

### 2 つの DUMP 形式に対応
- **EXP** (レガシー Oracle Export)
- **EXPDP** (Oracle DataPump)

</td>
<td>

### 主要なデータ型をサポート
NUMBER / DATE / TIMESTAMP / VARCHAR2 / CHAR / CLOB / BINARY_FLOAT / BINARY_DOUBLE

</td>
</tr>
</table>

---

## 機能一覧

| 機能 | 説明 | 状態 |
|---|---|:---:|
| **ダンプ解析** | EXP / EXPDP 形式の .dmp ファイルを解析 | 実装済み |
| **スキーマ・テーブル一覧** | ツリー表示でスキーマとテーブルを階層表示 | 実装済み |
| **データプレビュー** | テーブルデータを DataGridView で閲覧 (ページング対応) | 実装済み |
| **高度な検索** | 12 種類の演算子 / AND・OR 複合条件 / 大文字小文字区別 | 実装済み |
| **文字セット自動判定** | UTF-8 / Shift_JIS / EUC-JP を自動検出・変換 | 実装済み |
| **進捗表示** | 解析の進捗率・残り時間・処理速度をリアルタイム表示 | 実装済み |
| **CSV エクスポート** | RFC 4180 準拠の CSV 出力 (単一 / 一括) | 実装済み |
| **SQL スクリプト生成** | Oracle / PostgreSQL / MySQL / MSSQL 対応の INSERT 文生成 | 実装済み |
| **Excel エクスポート** | .xlsx 形式での出力 (ClosedXML) | 実装済み |
| **Access エクスポート** | .accdb 形式での出力 (OleDb) | 実装済み |
| **SQL Server 出力** | SQL Server への直接エクスポート (SqlBulkCopy) | 実装済み |
| **ODBC 出力** | ODBC 接続によるエクスポート | 実装済み |
| **LOB ファイル抽出** | BLOB / CLOB / NCLOB データを個別ファイルに抽出 | 実装済み |
| **ワークスペース保存** | 作業状態 (除外テーブル・フィルタ等) の保存・復元 (.odvw) | 実装済み |
| **テーブル除外** | 不要テーブルの非表示 / 元に戻す (Undo/Redo) | 実装済み |
| **最近使ったファイル** | ダンプファイル・ワークスペースの MRU リスト | 実装済み |
| **ヘルプ** | HTML ヘルプ (CHM 対応) / F1 キー | 実装済み |

---

## 動作環境

| 項目 | 要件 |
|---|---|
| OS | Windows 7 以降 (x64) |
| ランタイム | ポータブル版: 不要（同梱済み） / MSI 版: [.NET 10.0](https://dotnet.microsoft.com/ja-jp/download/dotnet/10.0) |
| 対応ファイル | Oracle .dmp (EXP / EXPDP) |

---

## インストール

[Releases](https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases) ページからダウンロードしてください。

| 配布形式 | ファイル名 | .NET ランタイム | 説明 |
|---|---|:---:|---|
| **MSI インストーラー** | `OraDBDumpViewer_vX.X.X_installer.msi` | 別途必要 | 軽量。ショートカット自動作成 |
| **ポータブル版** (推奨) | `OraDBDumpViewer_vX.X.X_portable.zip` | **同梱済み** | 解凍してすぐ使える |

---

## ライセンス認証

> **本ソフトウェアはライセンス認証が必要です。**
> 認証が完了するまでアプリケーションを使用できません。

### 料金プラン

ライセンスは **[https://www.odv.dev/](https://www.odv.dev/)** から取得できます。

| プラン | 価格 | 対象 |
|---|---|---|
| **個人利用** | **無料** | 個人の方 |
| **教育機関** | **無料** | 学校・教育機関 |
| **法人利用** | **9,800 円 / ライセンス / 年** (税別) | 企業・商用利用 |

> すべてのプランで全機能を利用できます。

### 認証手順

```
1. https://www.odv.dev/ でライセンスを申請
2. ライセンスファイル (.lic.json) をダウンロード
3. アプリケーションを起動 → 認証ダイアログが表示
4. 「はい」を選択 → .lic.json ファイルを指定
5. 認証完了 → すべての機能が利用可能に
```

再認証は **ヘルプ(H) > ライセンス認証(L)** から行えます。

### 認証の仕組み

| 項目 | 内容 |
|---|---|
| 方式 | RSA-2048 公開鍵暗号による署名検証 |
| 保管場所 | `%APPDATA%\OraDBDUMPViewer\license.status` |
| 有効期限 | ライセンス発行時に設定 (自動チェック) |
| 改ざん検出 | 署名不一致の場合は認証失敗 |

---

## 使い方

```
1. アプリケーションを起動し、ライセンス認証を完了
2. ファイル(F) > ダンプファイル(D) から .dmp ファイルを選択
3. テーブル一覧が自動取得 → 左側ツリーにスキーマ表示
4. スキーマを選択 → 右側にテーブル一覧を表示
5. テーブルをダブルクリック → データを解析・表示
6. 検索やエクスポートを必要に応じて利用
```

---

## ソースコードからのビルド

### 必要な環境

| ツール | バージョン |
|---|---|
| Visual Studio | 2026 (MSVC C++ ツールチェーン含む) |
| .NET SDK | 10.0 |

### ビルド手順

```bash
# 1. C ネイティブ DLL のビルド
cd Logics/DumpParser
build_dll.bat

# 2. VB.NET アプリケーションのビルド
cd ../..
dotnet build "OraDB DUMP Viewer.vbproj"
```

---

## ライセンスと利用条件

### ソフトウェアの利用

| 条件 | 内容 |
|---|---|
| 利用条件 | 有効なライセンスが必要 ([取得はこちら](https://www.odv.dev/)) |
| 利用範囲 | ライセンス取得者本人 (または取得法人) のみ |
| 譲渡・再配布 | 禁止 |

### ソースコードの取り扱い

本リポジトリのソースコードは **参照目的** で公開されています。

| 利用形態 | 可否 |
|---|---|
| ソースコードの閲覧・学習 | **可** |
| Pull Request による貢献 | **可** ([CLA](CLA.md) への署名が必要・著作権はプロジェクトに譲渡) |
| 改変して再配布 | **不可** |
| 類似の商用製品の作成 | **不可** |

### 関連ドキュメント

| ドキュメント | 内容 |
|---|---|
| [EULA](EULA.md) | エンドユーザー使用許諾契約書 |
| [CLA](CLA.md) | コントリビューターライセンス同意書 |
| [セキュリティポリシー](SECURITY.md) | 脆弱性報告の手順 |
| [利用規約・プライバシーポリシー](https://www.ta-yan.ai/rules) | Web版 |

### 免責事項

- 本ソフトウェアは「現状のまま」提供され、明示または黙示の保証はありません
- 本ソフトウェアの使用によって生じた損害について、開発者は一切の責任を負いません
- Oracle、Oracle DataPump は Oracle Corporation の商標です

---

<p align="center">
  <strong>YANAI Taketo</strong><br>
  <a href="https://www.odv.dev/">https://www.odv.dev/</a>
</p>
