<p align="center">
  <h1 align="center">OraDB DUMP Viewer</h1>
  <p align="center">
    Oracle データベースの EXPORT ファイル (.dmp) を<br>
    <strong>Oracle 環境なしで解析・閲覧</strong>できる Windows デスクトップアプリケーション
  </p>
</p>

<p align="center">
  <a href="https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases"><img src="https://img.shields.io/github/v/tag/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer?label=%E6%9C%80%E6%96%B0%E3%83%90%E3%83%BC%E3%82%B8%E3%83%A7%E3%83%B3&style=flat-square" alt="Release"></a>
  <a href="https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases"><img src="https://img.shields.io/github/downloads/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/total?label=%E3%83%80%E3%82%A6%E3%83%B3%E3%83%AD%E3%83%BC%E3%83%89&style=flat-square" alt="Downloads"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-blue?style=flat-square" alt="Platform">
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

### 解析・閲覧

| 機能 | 説明 |
|---|---|
| **ダンプ解析** | EXP / EXPDP 形式の .dmp ファイルを解析 |
| **スキーマ・テーブル一覧** | ツリー表示でスキーマとテーブルを階層表示 |
| **データプレビュー** | テーブルデータを DataGridView で閲覧 (ページング対応) |
| **高度な検索** | 12 種類の演算子 / AND・OR 複合条件 / 大文字小文字区別 |
| **文字セット自動判定** | UTF-8 / Shift_JIS / EUC-JP を自動検出・変換 |
| **進捗表示** | 解析の進捗率・残り時間・処理速度をリアルタイム表示 |

### エクスポート

| 機能 | 説明 |
|---|---|
| **CSV** | RFC 4180 準拠の CSV 出力 (単一テーブル / 一括) |
| **SQL スクリプト** | Oracle / PostgreSQL / MySQL / SQL Server 対応の INSERT 文生成 |
| **Excel** | .xlsx 形式での出力 |
| **Access** | .accdb 形式での出力 |
| **SQL Server** | SQL Server への直接エクスポート |
| **ODBC** | ODBC 接続による任意のデータベースへの出力 |
| **LOB ファイル抽出** | BLOB / CLOB / NCLOB データを個別ファイルに保存 |

### ワークスペース管理

| 機能 | 説明 |
|---|---|
| **ワークスペース保存** | 作業状態 (除外テーブル・フィルタ等) の保存・復元 (.odvw) |
| **テーブル除外** | 不要テーブルの非表示 / 元に戻す (Undo/Redo) |
| **最近使ったファイル** | ダンプファイル・ワークスペースの MRU リスト |
| **ドラッグ & ドロップ** | .dmp ファイルをウィンドウにドロップして開く |
| **ファイル関連付け** | .dmp ファイルをダブルクリックで直接開く (インストーラー版) |
| **ヘルプ** | HTML ヘルプ (CHM) / F1 キー |

---

## 動作環境

| 項目 | 要件 |
|---|---|
| OS | Windows 10 以降 (x64 / ARM64) |
| ランタイム | 不要 (.NET ランタイム同梱済み) |
| 対応ファイル | Oracle .dmp (EXP / EXPDP) |

---

## インストール

[Releases](https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases) ページからダウンロードしてください。

| 配布形式 | ファイル名 | 説明 |
|---|---|---|
| **インストーラー** (推奨) | `OraDBDumpViewer_vX.X.X_installer_{arch}.exe` | ショートカット・ファイル関連付け (.dmp) 付き。多言語対応 |
| **ポータブル版** | `OraDBDumpViewer_vX.X.X_portable_{arch}.zip` | 解凍してすぐ使える。レジストリ変更なし |

> **winget でのインストール:**
> ```
> winget install OraDBDumpViewer.OraDBDumpViewer
> ```

---

## ライセンス

> **個人・教育機関は無料**でご利用いただけます。

ライセンスは **[https://www.odv.dev/](https://www.odv.dev/)** から取得できます。

| プラン | 価格 | 対象 |
|---|---|---|
| **パーソナル** | **無料** | 個人・学生の方 |
| **エデュケーション** | **無料** | 教育・研究機関 |
| **プロフェッショナル** | **4,900 円 / 年** | 個人事業主・フリーランス・非営利団体 |
| **ビジネス** | **9,800 円 / 年** | 法人・企業・チーム |

すべてのプランで全機能を利用できます。

### 認証手順

1. [https://www.odv.dev/](https://www.odv.dev/) でライセンスを申請
2. ライセンスファイル (.lic.json) をダウンロード
3. アプリケーションを起動 → 認証ダイアログで .lic.json ファイルを指定
4. 認証完了

再認証は **ヘルプ(H) > ライセンス認証(L)** から行えます。

---

## 使い方

1. アプリケーションを起動し、ライセンス認証を完了
2. **ファイル(F) > ダンプファイル(D)** から .dmp ファイルを選択 (ドラッグ & ドロップ、またはエクスプローラーからダブルクリックも可)
3. テーブル一覧が自動取得され、左側ツリーにスキーマが表示
4. スキーマを選択 → 右側にテーブル一覧を表示
5. テーブルをダブルクリック → データを解析・表示
6. 必要に応じて検索やエクスポートを利用

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

## 利用条件

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
| Pull Request による貢献 | **可** ([CLA](CLA.md) への署名が必要) |
| 改変して再配布 | **不可** |
| 類似の商用製品の作成 | **不可** |

### 関連ドキュメント

- [EULA](EULA.md) - エンドユーザー使用許諾契約書
- [CLA](CLA.md) - コントリビューターライセンス同意書
- [セキュリティポリシー](SECURITY.md) - 脆弱性報告の手順
- [利用規約・プライバシーポリシー](https://www.ta-yan.ai/rules)

### 免責事項

- 本ソフトウェアは「現状のまま」提供され、明示または黙示の保証はありません
- 本ソフトウェアの使用によって生じた損害について、開発者は一切の責任を負いません
- Oracle、Oracle DataPump は Oracle Corporation の商標です

---

<p align="center">
  <strong>YANAI Taketo</strong><br>
  <a href="https://www.odv.dev/">https://www.odv.dev/</a>
</p>
