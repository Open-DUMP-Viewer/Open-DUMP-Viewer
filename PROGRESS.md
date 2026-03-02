# エクスポート機能実装 進捗状況

## 全体進捗: Phase 1/8 完了

| Phase | 内容 | 状態 |
|-------|------|------|
| 1 | 共通基盤 (ExportHelper, ExportProgressDialog, ツールバー変更) | **完了** |
| 2 | CSV エクスポート (C DLL + VB.NET) | 未着手 |
| 3 | SQL スクリプト生成 (C DLL API拡張 + VB.NET) | 未着手 |
| 4 | Excel エクスポート (ClosedXML) | 未着手 |
| 5 | Access エクスポート (OleDb) | 未着手 |
| 6 | SQL Server 出力 (SqlBulkCopy) | 未着手 |
| 7 | ODBC 出力 | 未着手 |
| 8 | 統合・リリース準備 (v1.0.0) | 未着手 |

## Phase 1: 共通基盤 (完了)

### 新規ファイル
- `Logics/Export/ExportHelper.vb` — 共通ヘルパー (型マッピング, エスケープ, ダイアログ, TableExportContext)
- `HMI/Export/ExportProgressDialog.vb` — 進捗ダイアログ (BackgroundWorker)
- `HMI/Export/ExportProgressDialog.Designer.vb` — Designer
- `HMI/Export/ExportProgressDialog.resx` — リソース

### 変更ファイル
- `HMI/OraDB DUMP Viewer/OraDB DUMP Viewer.Designer.vb` — ToolStripButton10削除, ボタン名変更
- `HMI/OraDB DUMP Viewer/OraDB DUMP Viewer.vb` — エクスポートボタンハンドラ追加
- `HMI/Workspace/Workspace.vb` — GetSelectedTableExportContext() 追加

## Phase 2: CSV エクスポート (次の作業)

### 予定
- Workspace → C DLL (`odv_export_csv()`) 経由のストリーミング CSV 出力
- TablePreview → VB.NET インメモリフィルタ後 CSV 出力
- `Logics/Export/CsvExportLogic.vb` 新規作成
- ToolStripButton6 (CSV 出力) のハンドラを実装に接続
