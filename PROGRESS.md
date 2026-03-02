# エクスポート機能実装 進捗状況

## 全体進捗: Phase 8/8 完了 (v1.0.0)

| Phase | 内容 | 状態 |
|-------|------|------|
| 1 | 共通基盤 (ExportHelper, ExportProgressDialog, ツールバー変更) | **完了** |
| 2 | CSV エクスポート (C DLL + VB.NET) | **完了** |
| 3 | SQL スクリプト生成 (C DLL API拡張 + VB.NET) | **完了** |
| 4 | Excel エクスポート (ClosedXML) | **完了** |
| 5 | Access エクスポート (OleDb) | **完了** |
| 6 | SQL Server 出力 (SqlBulkCopy) | **完了** |
| 7 | ODBC 出力 | **完了** |
| 8 | 統合・リリース準備 (v1.0.0) | **完了** |

## 新規ファイル一覧

### Logics/Export/
| ファイル | 内容 |
|----------|------|
| ExportHelper.vb | 共通ヘルパー (型マッピング, エスケープ, ダイアログ, TableExportContext) |
| CsvExportLogic.vb | CSV ストリーミング出力 (C DLL + VB.NET) |
| SqlExportLogic.vb | SQL INSERT 文生成 (Oracle/PG/MySQL/MSSQL) |
| ExcelExportLogic.vb | Excel .xlsx 出力 (ClosedXML) |
| AccessExportLogic.vb | Access .accdb 出力 (ADOX + OleDb) |
| SqlServerExportLogic.vb | SQL Server 直接出力 (SqlBulkCopy) |
| OdbcExportLogic.vb | ODBC 汎用出力 |

### HMI/Export/
| ファイル | 内容 |
|----------|------|
| ExportProgressDialog.vb + Designer + resx | 進捗ダイアログ (BackgroundWorker) |
| SqlExportDialog.vb + Designer + resx | DB 選択ダイアログ |
| DatabaseConnectionDialog.vb + Designer + resx | DB 接続ダイアログ (SQL Server / ODBC) |

### C DLL 変更
| ファイル | 内容 |
|----------|------|
| odv_api.h | `odv_export_sql()` API 追加 |
| odv_api.c | `odv_export_sql()` 実装 (write_sql_file へディスパッチ) |

### 変更ファイル
- `OraDB DUMP Viewer.vbproj` — バージョン 1.0.0, NuGet 追加
- `HMI/OraDB DUMP Viewer/OraDB DUMP Viewer.vb` — 全エクスポートボタンハンドラ
- `HMI/OraDB DUMP Viewer/OraDB DUMP Viewer.Designer.vb` — ToolStripButton10 削除, ボタン名変更
- `HMI/Workspace/Workspace.vb` — GetSelectedTableExportContext() 追加
- `HMI/TablePreview/TablePreview.vb` — CSV 出力ボタン追加
- `Logics/Workspace/OraDB_NativeParser.vb` — ExportSql() P/Invoke 追加
- `CHANGELOG.md` — v1.0.0 リリースノート

## NuGet パッケージ
- ClosedXML 0.105.0 (Excel)
- Microsoft.Data.SqlClient 6.1.4 (SQL Server)
- System.Data.OleDb 10.0.3 (Access)
- System.Data.Odbc 10.0.3 (ODBC)
