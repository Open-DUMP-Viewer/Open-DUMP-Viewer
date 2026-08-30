# AGENTS.md - Open DUMP Viewer for Oracle database

## Project Overview

Open DUMP Viewer for Oracle database は、Oracle データベースの EXPORT 形式（.dmp ファイル）を **Oracle 環境なしで** 解析・閲覧できる Windows デスクトップアプリケーションです。

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
Open_NativeParser.vb (P/Invoke + コールバック)
  ↓
Open_DumpParser.dll (C ネイティブ, x64)
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
Open DUMP Viewer for Oracle database/
├─ HMI/                                    # UI層
│  ├─ Open DUMP Viewer for Oracle database/
│  │  ├─ Open_DUMP_Viewer.vb              # メインフォーム（ライセンス認証）
│  │  └─ Open_DUMP_Viewer.Designer.vb
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
│  ├─ Open DUMP Viewer for Oracle database/
│  │  └─ MenuStripLogics.vb               # メニュー処理
│  ├─ Workspace/
│  │  ├─ AnalyzeLogic.vb                   # DLL呼出し制御（進捗表示）
│  │  └─ Open_NativeParser.vb             # P/Invoke + コールバック + GCHandle
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
├─ .github/
│  ├─ dependabot.yml                       # 依存の継続監視（NuGet / GitHub Actions）
│  ├─ PULL_REQUEST_TEMPLATE.md             # PR テンプレート
│  ├─ ISSUE_TEMPLATE/                      # Issue テンプレート（バグ報告・機能要望）
│  └─ workflows/
│     ├─ build-and-release.yml             # CI/CD 正式版（release ブランチ）
│     ├─ build-and-release-beta.yml        # CI/CD ベータ版（beta ブランチ）
│     ├─ codeql.yml                        # 静的解析（C ネイティブ + Actions・週次）
│     └─ dependency-review.yml             # PR 差分の依存審査
├─ README.md                               # プロジェクト説明
├─ CHANGELOG.md                            # リリースノート
├─ CLA.md                                  # コントリビューターライセンス同意書
├─ EULA.md                                 # エンドユーザー使用許諾契約書
├─ SECURITY.md                             # セキュリティポリシー
└─ Open DUMP Viewer.vbproj                # VB.NET プロジェクト
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
dotnet build "Open DUMP Viewer.vbproj"
```

- .NET 10.0 (net10.0-windows7.0)
- 言語: Visual Basic .NET
- UI: Windows Forms (WinForms)

#### Windows 以外でのコンパイル確認

Linux / macOS でも**ビルドだけ**は通せる。Windows Desktop の参照アセンブリを NuGet から取る指示が要る。

```bash
dotnet build "Open DUMP Viewer.vbproj" -p:EnableWindowsTargeting=true
```

実行はできない（WinForms のランタイムが Windows にしか無い）。ロジック層の動作を確認したい場合は、対象の `.vb` を `net10.0` のコンソールプロジェクトへ `<Compile Include>` でリンクし、WinForms に触れる箇所（`Application.Exit` など）と `Loc` だけをスタブに差し替えれば、実ソースのまま実行して検証できる。

### バージョンポリシー

**すべてのランタイム・SDK・ツールは常に最新バージョンを使用する。**

- .NET SDK: メジャー・マイナーともに最新版を追従（例: .NET 10.0 → 10.1 → 11.0）
- Visual Studio: 最新版を使用
- Node.js (CI): 最新 LTS を使用
- GitHub Actions: Node.js 24 対応版アクションを使用

バージョンアップ時の更新対象:
- `Open DUMP Viewer.vbproj` の `TargetFramework`
- CI/CD ワークフロー (`dotnet-version`, `node-version`, アクションバージョン)
- `README.md` の動作環境記載

### アプリバージョン管理

リリース時に以下の **3ファイル** を更新する:

| ファイル | 項目 | 例 |
|---|---|---|
| `Open DUMP Viewer.vbproj` | `<Version>` | `2.3.0` |
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
→ Winget ID: `OpenDumpViewer.OpenDumpViewer.BETA`

**正式版公開:**
```bash
git push origin main:release
```
→ GitHub Actions (`build-and-release.yml`) が自動実行
→ タグ `v{version}` で Release を作成
→ Winget ID: `OpenDumpViewer.OpenDumpViewer`

### ベータ版と安定版の並存

| 項目 | 安定版 | Beta版 |
|---|---|---|
| インストール先 | `Program Files\Open DUMP Viewer` | `Program Files\Open DUMP Viewer Beta` |
| ショートカット名 | `Open DUMP Viewer for Oracle database` | `Open DUMP Viewer for Oracle database (Beta)` |
| Inno Setup AppId | `25f04e6a-ad47-47aa-9a66-74f64c772bac` | `2e7f53d3-f0d6-4000-8d40-253b4ae66c63` |
| Winget ID | `OpenDumpViewer.OpenDumpViewer` | `OpenDumpViewer.OpenDumpViewer.BETA` |

同一 PC に両方インストール可能。

### Microsoft Store の「このバージョンの新機能」

**現状は Partner Center で手入力する。** CHANGELOG からの自動生成を試みたが、`msstore` CLI 側の問題で公開そのものが通らなくなったため差し戻した（後述）。

この欄の実体は提出 JSON の `listings.<言語>.baseListing.releaseNotes`。MSIX パッケージ側（`Package.appxmanifest`）には対応する要素が無く、**提出メタデータを更新する以外に設定する方法はない**。

#### 自動化を試みて差し戻した経緯（v4.5.1）

`msstore publish` は既定でコミットまで進み、コミット済みの提出はメタデータを差し替えられない。そこで `publish-store` ジョブを **3 段構え**に組み替えた（745dcc9）:

| # | ステップ | コマンド |
|---|---|---|
| 1 | パッケージを上げ、下書きで止める | `msstore publish <bundle> -id <id> --noCommit` |
| 2 | `releaseNotes` を差し替える | `msstore submission get` → 差し替え → `msstore submission updateMetadata` |
| 3 | コミット（認定へ） | `msstore submission publish` |

**ステップ 1 が必ず失敗する。** v4.5.1 のリリース（ラン #78）で 3 回試して 3 回とも同一:

```
✅ Zip Bundle is configured and ready to be uploaded!
Uploading Bundle to Azure blob: 0%
Error while uploading the application package.
```

切り分けで分かっていること:

- **フラグ無しなら通る。** 同じ AppId・同じ資格情報・同種の msixbundle で、単発の `msstore publish`（v4.5.0 / ラン #77）は成功している
- **`--noCommit` を付けると CLI の経路が変わる。** #77 のログにある `Deleting existing Submission` / `✅ Existing submission deleted!` が #78 には出ない。Store API はアプリごとに pending 提出を 1 つしか持てないため最初はこれを疑ったが、**Partner Center で pending 提出を削除してから再実行しても同じ場所で落ちた**ので、これは原因ではない
- **パッケージサイズは無関係。** msixbundle は 112 MB で Store の上限に遠い
- 残る容疑は `--noCommit` 自体、3 段構えで追加した `Checkout repository`（msstore は CWD からプロジェクト種別を判定する。#77 には checkout が無かった）、`setup-msstore-cli` が `version: latest` のため 8/10 と 8/30 で CLI 版が変わっている可能性、の 3 つ

再挑戦する場合の順序:

1. `msstore publish` に **`--verbose` を付けて実際のエラーを出す**。`Error while uploading the application package.` は汎用メッセージで、この下に本当の原因がある。ここを見ずに直そうとしないこと
2. `setup-msstore-cli` の `version` を latest から固定版へ
3. `Checkout repository` を外した状態（#77 と同じ CWD）で試す

`installer/msix/scripts/Convert-ReleaseNotes.ps1` は**ワークフローから外したうえで残してある**。CHANGELOG → 平文の変換自体は動いており（v4.5.1 の節で 426/1500 文字、マークアップ残留なしを確認済み）、原因が判明したときにそのまま再接続できる。スクリプト側の仕様:

- **平文しか描画しない。** Markdown を落とす（`### 見出し` → `■ 見出し`、`- ` → `・`）。生成物にマークアップが残っていたら停止する
- **上限 1500 文字。** 超える場合は行の切れ目で打ち切り、末尾に GitHub Releases への案内を付ける。全 44 セクション中、超えるのは 1.0.0 と 4.0.0 の 2 件
- **本文の生成は Store を触る前に行う。** CHANGELOG に当該バージョンの節が無ければそこで停止し、Partner Center に中途半端な下書きを残さない
- **`updateMetadata` は差分マージではなく提出全体の置換。** `get` した JSON をそのまま返す必要があるため、`ConvertFrom-Json`/`ConvertTo-Json` ではなく `JsonNode` を使う（前者は日付に見える文字列を `[datetime]` に変換し、書き戻すと書式が変わる）
- **`releaseNotes` は listing（言語）ごと。** 本製品は説明文を含め **10 言語すべてに翻訳済みの listing** があり、この欄も v4.1.1 までは言語ごとに訳していた。CHANGELOG が日本語のみのため、**全 listing に同じ日本語を入れる**方針だった（意図的）

  > 日本語以外の利用者には、訳された説明文の下に日本語の更新履歴が並ぶ。手作業での 10 言語更新が続かず、v4.2.0 以降この欄が v4.1.1 のまま止まっていたため、内容が最新であることを優先した。インストーラーの使用許諾（日本語は EULA、他 9 言語は英語 LICENSE）と同じく、日本語を正、他言語をベストエフォートとする扱い

ベータ版（`build-and-release-beta.yml`）は Store へ提出しないため、この件は正式版ワークフローにのみ関係する。

## CLA（コントリビューターライセンス同意書）

署名は **CLA Assistant Lite**（`.github/workflows/cla.yml`。自己ホスト型の GitHub Action）で受け付ける。

- **署名文書**: リポジトリルートの `CLA.md`（著作権譲渡型）。Action が直接この URL を参照する
- **署名方法**: PR に `I have read the CLA Document and I hereby sign the CLA` とコメント
- **署名台帳**: `cla-signatures` ブランチ（orphan）の `signatures/version1/cla.json`。第三者サービスへは送信しない
- **除外**: `Yanai-Taketo` / `dependabot[bot]` / `github-actions[bot]`
- **再チェック**: PR に `recheck` とコメント

### なぜ自己ホストなのか（cla-assistant.io から移行した理由）

cla-assistant.io は署名対象の文書として **Gist しか指定できない**。そのためリポジトリの `CLA.md` とは別に必ず写しを持つ構造になり、実際にその写しが腐った——`CLA.md` を Oracle 商標対応で「Open DUMP Viewer for Oracle database」へ改称した後も、**Gist は旧名称「OraDB DUMP Viewer」のまま署名を集め続けていた**（2026-07-18 に発覚。外部署名者ゼロのため実害はなし）。

自己ホスト方式は `path-to-document` がリポジトリ内のファイルを直接指すため、この乖離が構造的に起きない。あわせて署名者の情報が第三者サービスへ渡らず、記録が自分の管理下に残る。

**`CLA.md` を改訂したら、それがそのまま署名対象になる**（同期作業は不要）。

## 権利・ライセンスの扱い

### 第三者ライセンスの表示

`THIRD-PARTY-NOTICES.txt`（リポジトリルート）が、**実際に再配布しているすべての第三者コンポーネント**の著作権表示とライセンス全文を保持する。MIT・Apache-2.0 いずれも「複製物に著作権表示と許諾表示を含めること」を条件にしているため、これは任意の親切ではなく**再配布の条件**である。

- 対象は**推移的依存を含む**（`dotnet list package --include-transitive` が正）。self-contained 発行で同梱される .NET ランタイム、インストーラーに含まれる Inno Setup 自身のコードも対象
- MIT が大半だが、**SixLabors.Fonts は Apache-2.0**、**Microsoft.Web.WebView2 は BSD 系の独自条件**、**Microsoft.Data.SqlClient.SNI は Microsoft ソフトウェアライセンス条項**と、条件が異なるものが混在する。一括で「MIT です」と書かないこと
- **依存を追加・更新したら `THIRD-PARTY-NOTICES.txt` を更新する**。Dependabot の PR も例外ではない（新規パッケージが増えたら追記が要る）。ライセンス種別は推測せず、NuGet キャッシュの `.nuspec`（`<license>` / `<copyright>`）と同梱の LICENSE ファイルから確認する
- Oracle Instant Client は**同梱していない**（利用者が明示的に選択したときのみ取得）。ただし配布経路が自社ドメインのため OTN ライセンス条件の確認が別途必要

#### SixLabors.Fonts を直接参照にしてはならない（2026-07-26 調査）

SixLabors.Fonts は **2.0.0 でライセンスが変わっている**。1.0.x は nuspec が `Apache-2.0` を表明する素の Apache-2.0 だが、2.0.0 以降は同梱 LICENSE ファイルの **Six Labors Split License Version 1.0** になり、Apache-2.0 が適用されるのは条文 2 条の付与要件を満たす場合のみになる。

本製品で Apache-2.0 が適用される根拠は、要件のうち次の一項である。

> - You are consuming the Work as a **Transitive Package Dependency**.
>   （"Transitive Package Dependency" = Six Labors と無関係な第三者依存によって間接的にインストールされる Work）

SixLabors.Fonts は **ClosedXML 経由の推移的依存**であり、直接参照していない。この形である限りバージョンが 2.x に上がっても Apache-2.0 のままで、売上要件は掛からない。`THIRD-PARTY-NOTICES.txt` が Apache-2.0 の節に置いているのはこの根拠による。

**したがって `Open DUMP Viewer.vbproj` に `SixLabors.Fonts` の `PackageReference` を足してはならない。** 直接参照（Direct Package Dependency）にすると付与要件が変わり、2.x では「年間総収入 100 万 USD 未満の営利」または「非営利・登録慈善団体」に該当しない限り Six Labors 商用ライセンスの購入が必要になる。バージョン固定の目的であっても、推移的依存のまま置くこと。

- ClosedXML 0.105.1 時点で依存範囲は `[1.0.0, 3.0.0)`。NuGet は既定で条件を満たす最小版を採るため実際の解決は **1.0.0**（他に SixLabors.Fonts を要求する依存はない）
- 3.0.0 以降は ClosedXML 側の上限で入らない。上限が緩む更新が来たら、推移的依存のままかを再確認する

配布物への同梱は `Open DUMP Viewer.vbproj` の `None Include` で行う。publish 出力がインストーラー版・ポータブル版の共通の元になるため、ここに入れれば両方に入る。

### インストーラーでの使用許諾の表示

`installer/setup.iss` の `[Languages]` で言語ごとに `LicenseFile` を指定し、インストール時に同意画面を出す。

| 言語 | 表示する文書 |
|---|---|
| 日本語 | `EULA-ja.txt`（`EULA.md` を平文化。詳細な正本） |
| その他 9 言語 | `LICENSE-en.txt`（`LICENSE` の写し。英語） |

**この 2 ファイルは commit しない。** 法的文書の文面を二重管理すると原本と乖離するため、CI（`build-and-release*.yml` の `Generate license files for installer`）が `EULA.md` / `LICENSE` から毎回生成する（`.gitignore` 済み）。

> 未対応: 日本語以外の利用者には、より簡潔な英語 `LICENSE` が表示される。EULA 本文（13 条）の多言語版は未整備。

## Security Automation

依存とコードの脆弱性を**三層**で検知する。

| 層 | 仕組み | 対象 | いつ効くか |
|---|---|---|---|
| 1. NuGet Audit | `Open DUMP Viewer.vbproj` の `NuGetAuditMode=all` + NU1900-1904 の warning-as-error | NuGet 依存（推移的含む） | restore / build のたび |
| 2. Dependency Review | `.github/workflows/dependency-review.yml` | PR 差分で入る依存（GitHub Actions 含む） | PR ごと |
| 3. Dependabot | `.github/dependabot.yml` + リポジトリの脆弱性アラート / 自動セキュリティ更新 | NuGet・GitHub Actions | 週次 + CVE 公表時に随時 |

コードの静的解析は `.github/workflows/codeql.yml`（push / PR / **週次 cron**）。

### CodeQL の対象範囲（重要）

**CodeQL は VB.NET を解析できない**（対応言語は C/C++・C#・GitHub Actions・Go・Java・Kotlin・JavaScript・Python・Ruby・Rust・Swift・TypeScript。`csharp` 解析も `.vb` は対象外。検証日 2026-07-18）。

- **対象**: `Logics/DumpParser` の C 実装（`c-cpp`、build-mode `manual` で Makefile を実ビルド）、ワークフロー自身（`actions`）
- **対象外**: `HMI/` と `Logics/` の VB.NET コード全般

C を対象に含める理由は、**信頼できない .dmp ファイルを直接パースする最も攻撃面の大きい層**であり、SECURITY.md が「対象となる脆弱性」の筆頭に挙げる「.dmp ファイル解析時のバッファオーバーフロー」がまさにここで起きるため。VB.NET 側は層 1 の NuGet Audit が依存の脆弱性を担当する。

### リリースワークフローの concurrency

`build-and-release*.yml` は `cancel-in-progress: false`。実リリース作成と Microsoft Store 提出まで進むため、後続 push で**割り込みキャンセルしてはならない**（待たせる）。

## License & Authentication

### ライセンス仕様

| 項目 | 内容 |
|---|---|
| 方式 | RSA-2048 公開鍵暗号による署名検証 |
| ファイル形式 | JSON (`*.lic.json`) |
| 保存場所 | `%APPDATA%\OpenDUMPViewer\license.status` |
| 検証時期 | アプリ起動時（認証完了まで使用不可） |

### ライセンスの種類

| プラン | 料金 | 対象 |
|---|---|---|
| パーソナル | 無料 | 個人・学生の方 |
| エデュケーション | 無料 | 教育・研究機関 |
| プロフェッショナル | 4,900円/年 | 個人事業主・フリーランス・非営利団体 |
| ビジネス | 9,800円/年 | 法人・企業・チーム |

ライセンス取得: [https://www.odv.dev/](https://www.odv.dev/)

**実装場所**: `Logics/LICENSE.vb`, `HMI/Open DUMP Viewer/Open_DUMP_Viewer.vb`

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

### テーブル一覧取得 (`list_only`) で「やらない」こと

`list_only` は行数だけが要る経路である。ここで余計な仕事をすると、大規模ダンプの初回表示がそのまま遅くなる。

- **列値をデコードしない**: `decode_column_value()` / `decode_exp_column()` は `list_only` では呼ばない。展開した値は誰にも渡らず捨てられる。レコードの構造は従来どおり辿るので行数は変わらない
- **マスタ表の走査は 1 回だけ**: EXPDP のパーティション名・制約名・参照制約名・索引名は、いずれも `SCHEMA_EXPORT/...` のバイト列検索で見つかる。かつては 4 つ別々にファイル全体を読んでいた。現在は `scan_master_metadata()` が 1 回の走査で 4 種類すべてを拾う

`scan_master_metadata()` を触るときの注意: 見つけた制約・索引は**その場で `meta_constraints[]` に書かず、いったん `matches[]` に貯めてから、マーカー種別ごとにまとめて書き込む**。`meta_constraints[]` は 1 表 20 件 (`ODV_MAX_META_CONSTRAINTS`) で打ち切られるため、ファイル出現順に詰めると打ち切られる対象が変わってしまう。順序を保つためのこの二段構えを崩さないこと。

### メモリ解放で押さえる点

- **`ShowDialog` はフォームを破棄しない**。モーダルダイアログは `Using` で開くこと。開くたびにコントロール一式とウィンドウハンドルが残る
- **`CellValueNeeded` は再描画のたびに呼ばれる**。ここで `Image` を生成すると GDI+ のビットマップが積み上がる。`TablePreview` は生成済みサムネイルをキャッシュし、ページ切替とフォーム終了で破棄する
- **表の全行データは閉じた時点で参照を切る**。`TablePreview` / `Workspace` の `FormClosed` で保持データを解放する。数万行以上を閉じた場合は LOH 圧縮を伴う回収を要求しないと、解放してもプロセスのメモリ使用量が下がって見えない

## Legal Documents

| ドキュメント | 内容 |
|---|---|
| [EULA.md](EULA.md) | エンドユーザー使用許諾契約書 |
| [CLA.md](CLA.md) | コントリビューターライセンス同意書 |
| [SECURITY.md](SECURITY.md) | セキュリティポリシー |
| [利用規約](https://www.odv.dev/legal#terms) | Web版 |
| [プライバシーポリシー](https://www.odv.dev/legal#privacy) | Web版 |

## Contact & Support

- ライセンス: [https://www.odv.dev/](https://www.odv.dev/)
- セキュリティ報告: inquiry@ta-yan.ai
- バグレポート・機能リクエスト: [GitHub Issues](https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/issues)
