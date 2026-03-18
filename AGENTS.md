# AGENTS.md - OraDB DUMP Viewer

## Project Overview

OraDB DUMP Viewer は、Oracle データベースの EXPORT 形式（.dmp ファイル）を **Oracle 環境なしで** 解析・閲覧できる Windows デスクトップアプリケーションです。

**主な特徴:**
- EXP (レガシー) / EXPDP (DataPump) 両形式の .dmp ファイル解析
- C ネイティブ DLL による高速解析エンジン
- ライセンス認証機能（RSA-2048 署名検証）
- 大量データへの対応（ページング表示）
- 12種類の演算子による高度な検索機能
- 文字セット自動判定（UTF-8 / Shift_JIS / EUC-JP）

## Architecture & Data Flow

### アーキテクチャ概要

```
VB.NET UI (WinForms)
  ↓
AnalyzeLogic.vb (解析制御)
  ↓
OraDB_NativeParser.vb (P/Invoke + コールバック)
  ↓
OraDB_DumpParser.dll (C ネイティブ, x64)
```

### 2フェーズ解析

```
フェーズ1: ListTables（高速・メモリ軽量）
  → テーブル一覧・行数・DDLオフセットを取得
  → カラム名をキャッシュ（0行テーブルの列表示用）

フェーズ2: AnalyzeTable（オンデマンド）
  → DDLオフセットで高速シーク
  → 選択テーブルのみ解析・行データ取得
  → コールバックで VB.NET 側にデリバリ
```

### コールバック方式

```
ODV_ROW_CALLBACK(schema, table, col_count, col_names[], col_values[], user_data)
  → P/Invoke経由で VB.NET の OnRowCallback に到達
  → Dictionary(Of String, Object) として行データを蓄積
```

## Project Structure

```
OraDB DUMP Viewer/
├─ HMI/                                    # UI層
│  ├─ OraDB DUMP Viewer/
│  │  ├─ OraDB_DUMP_Viewer.vb              # メインフォーム（ライセンス認証）
│  │  └─ OraDB_DUMP_Viewer.Designer.vb
│  ├─ Workspace/
│  │  ├─ Workspace.vb                      # スキーマ・テーブル一覧表示
│  │  └─ Workspace.Designer.vb
│  └─ TablePreview/
│     ├─ TablePreview.vb                   # テーブルデータ表示（ページング）
│     ├─ TablePreview.Designer.vb
│     ├─ AdvancedSearchForm.vb             # 高度な検索ダイアログ
│     ├─ AdvancedSearchForm.Designer.vb
│     ├─ SearchConditionRow.vb             # 検索条件行コンポーネント
│     └─ SearchConditionRow.Designer.vb
├─ Logics/                                 # ロジック層
│  ├─ COMMON.vb                            # 共通ユーティリティ
│  ├─ LICENSE.vb                           # ライセンス検証（RSA-2048）
│  ├─ OraDB DUMP Viewer/
│  │  └─ MenuStripLogics.vb               # メニュー処理
│  ├─ Workspace/
│  │  ├─ AnalyzeLogic.vb                   # DLL呼出し制御（進捗表示）
│  │  └─ OraDB_NativeParser.vb             # P/Invoke + コールバック + GCHandle
│  ├─ TablePreview/
│  │  ├─ TablePreviewLogic.vb              # テーブル表示制御
│  │  └─ SearchCondition.vb                # 検索条件の定義・評価
│  └─ DumpParser/                          # C ネイティブ DLL ソース
│     ├─ odv_types.h                       # 内部型定義
│     ├─ odv_api.h / odv_api.c             # DLL公開API（セッション管理）
│     ├─ odv_detect.c                      # DUMP形式判定
│     ├─ odv_exp.c                         # レガシーEXP解析
│     ├─ odv_expdp.c                       # DataPump解析
│     ├─ odv_record.c                      # レコードバッファ管理
│     ├─ odv_number.c                      # Oracle NUMBER デコード
│     ├─ odv_datetime.c                    # DATE/TIMESTAMP デコード
│     ├─ odv_charset.c                     # 文字セット変換
│     ├─ odv_xml.c                         # XMLパーサ（EXPDP用）
│     ├─ odv_csv.c                         # CSV出力
│     ├─ odv_sql.c                         # SQL INSERT文生成
│     └─ _build.cmd                        # MSVCビルドスクリプト
├─ installer/                              # Inno Setup インストーラー
│  ├─ setup.iss                            # Inno Setup スクリプト（10言語対応）
│  ├─ ChineseSimplified.isl               # 中国語（簡体字）翻訳（CI で自動DL）
│  ├─ WizardImage.bmp                     # バナー画像（CI で自動生成）
│  └─ WizardSmallImage.bmp               # 小バナー画像（CI で自動生成）
├─ .github/workflows/
│  ├─ build-and-release.yml                # CI/CD 正式版（release ブランチ）
│  └─ build-and-release-beta.yml           # CI/CD ベータ版（beta ブランチ）
├─ README.md                               # プロジェクト説明
├─ CHANGELOG.md                            # リリースノート
├─ CLA.md                                  # コントリビューターライセンス同意書
├─ EULA.md                                 # エンドユーザー使用許諾契約書
├─ SECURITY.md                             # セキュリティポリシー
└─ OraDB DUMP Viewer.vbproj                # VB.NET プロジェクト
```

## Build

### C ネイティブ DLL

```bash
cd Logics/DumpParser
_build.cmd
```

- Visual Studio 2026 (MSVC, x64)
- コンパイルオプション: `/MT /utf-8 /std:c11`
- 定義: `WINDOWS WIN32 UTF8 ODV_DLL_MODE _CRT_SECURE_NO_WARNINGS`

### VB.NET アプリケーション

```bash
dotnet build "OraDB DUMP Viewer.vbproj"
```

- .NET 10.0 (net10.0-windows7.0)
- 言語: Visual Basic .NET
- UI: Windows Forms (WinForms)

### バージョンポリシー

**すべてのランタイム・SDK・ツールは常に最新バージョンを使用する。**

- .NET SDK: メジャー・マイナーともに最新版を追従（例: .NET 10.0 → 10.1 → 11.0）
- Visual Studio: 最新版を使用
- Node.js (CI): 最新 LTS を使用
- GitHub Actions: Node.js 24 対応版アクションを使用

バージョンアップ時の更新対象:
- `OraDB DUMP Viewer.vbproj` の `TargetFramework`
- CI/CD ワークフロー (`dotnet-version`, `node-version`, アクションバージョン)
- `README.md` の動作環境記載

### アプリバージョン管理

リリース時に以下の **3ファイル** を更新する:

| ファイル | 項目 | 例 |
|---|---|---|
| `OraDB DUMP Viewer.vbproj` | `<Version>` | `2.3.0` |
| `Logics/DumpParser/odv_api.c` | `ODV_VERSION_STRING` | `"2.3.0"` |
| `CHANGELOG.md` | 新バージョンセクション追加 | `## [2.3.0] - 2026-03-18` |

- アプリバージョンと DLL バージョンは **同一バージョン** を使用
- セマンティックバージョニング: 破壊的変更=メジャー、機能追加=マイナー、バグ修正=パッチ

## Release Flow

### ブランチ戦略

```
main (開発) → beta (ベータ公開) → release (正式公開)
```

| ブランチ | 用途 | トリガー | GitHub Release |
|---|---|---|---|
| `main` | 日常開発 | — | — |
| `beta` | ベータ版公開 (最短毎日) | push → GitHub Actions | Pre-release |
| `release` | 正式版公開 | push → GitHub Actions | Release |

### インストーラー

- **技術**: Inno Setup 6（EXE 形式、10言語自動検出）
- **スクリプト**: `installer/setup.iss`
- **機能**: OS言語自動検出、ファイル関連付け (.dmp)、インストール後起動、アンインストール時設定削除確認
- **配布**: インストーラー版 (EXE) + ポータブル版 (ZIP) を別々に配布

### リリース手順

**ベータ版公開:**
```bash
git push origin main:beta
```
→ GitHub Actions (`build-and-release-beta.yml`) が自動実行
→ タグ `v{version}-beta` で Pre-release を作成
→ Winget ID: `OraDBDumpViewer.OraDBDumpViewer.BETA`

**正式版公開:**
```bash
git push origin main:release
```
→ GitHub Actions (`build-and-release.yml`) が自動実行
→ タグ `v{version}` で Release を作成
→ Winget ID: `OraDBDumpViewer.OraDBDumpViewer`

### ベータ版と安定版の並存

| 項目 | 安定版 | Beta版 |
|---|---|---|
| インストール先 | `Program Files\OraDB DUMP Viewer` | `Program Files\OraDB DUMP Viewer Beta` |
| ショートカット名 | `OraDB DUMP Viewer` | `OraDB DUMP Viewer (Beta)` |
| Inno Setup AppId | `cd3b541d-5df6-4737-9bcc-16a4329a8a54` | `a6f03b60-f9ab-4182-a1fc-aa9801428121` |
| Winget ID | `OraDBDumpViewer.OraDBDumpViewer` | `OraDBDumpViewer.OraDBDumpViewer.BETA` |

同一 PC に両方インストール可能。

## License & Authentication

### ライセンス仕様

| 項目 | 内容 |
|---|---|
| 方式 | RSA-2048 公開鍵暗号による署名検証 |
| ファイル形式 | JSON (`*.lic.json`) |
| 保存場所 | `%APPDATA%\OraDBDUMPViewer\license.status` |
| 検証時期 | アプリ起動時（認証完了まで使用不可） |

### ライセンスの種類

| プラン | 料金 | 対象 |
|---|---|---|
| 個人利用 | 無料 | 個人の方 |
| 教育機関 | 無料 | 学校・教育機関 |
| 法人利用 | 9,800円/ライセンス/年（税別） | 企業・商用利用 |

ライセンス取得: [https://www.odv.dev/](https://www.odv.dev/)

**実装場所**: `Logics/LICENSE.vb`, `HMI/OraDB DUMP Viewer/OraDB_DUMP_Viewer.vb`

## Advanced Search Feature

### 検索演算子（12種類）

| 演算子 | 説明 |
|---|---|
| 含む / 含まない | 部分一致 / 部分一致除外 |
| 等しい / 等しくない | 完全一致 / 完全不一致 |
| で始まる / で終わる | 前方一致 / 後方一致 |
| > / < / >= / <= | 数値比較 |
| Null / Not Null | NULL判定 |

- AND/OR による複合条件
- 大文字小文字区別オプション
- 検索条件の保持（再検索時に前回条件を復元）

## Code Style & Conventions

### VB.NET コーディング規約

- クラス・メソッド: PascalCase
- プライベートフィールド: `_camelCase`
- ローカル変数: camelCase
- `#Region` / `#End Region` でグループ化
- XML Documentation (`'''`) を public メソッドに記述
- デザイナーパターン: UI定義は `*.Designer.vb` に完全隔離

### C コーディング規約

- 関数名: `odv_` プレフィクス（例: `odv_open_session`）
- 型名: `ODV_` プレフィクス（例: `ODV_SESSION`）
- 定数: `ODV_` プレフィクス + 大文字（例: `ODV_OK`）
- C11 標準準拠

## Performance Considerations

- **2フェーズ解析**: テーブル一覧取得は高速、データ解析はオンデマンド
- **DDLオフセットキャッシュ**: 選択テーブルへの高速シーク
- **早期終了**: フィルタ対象テーブル処理完了で即座に解析終了
- **ページング**: 1ページ100行でUI描画負荷を軽減
- **短絡評価**: AND/OR 検索の最適化

## Legal Documents

| ドキュメント | 内容 |
|---|---|
| [EULA.md](EULA.md) | エンドユーザー使用許諾契約書 |
| [CLA.md](CLA.md) | コントリビューターライセンス同意書 |
| [SECURITY.md](SECURITY.md) | セキュリティポリシー |
| [利用規約・プライバシーポリシー](https://www.ta-yan.ai/rules) | Web版 |

## Contact & Support

- ライセンス: [https://www.odv.dev/](https://www.odv.dev/)
- セキュリティ報告: inquiry@ta-yan.ai
- バグレポート・機能リクエスト: [GitHub Issues](https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/issues)
