Imports System.IO
Imports System.ComponentModel

''' <summary>
''' 一括エクスポートロジック
'''
''' 複数テーブルを順次処理し、各形式の既存 ExportLogic を再利用する。
''' テーブルごとに ParseDump → Export → メモリ解放 のパターン。
''' </summary>
Public Class BulkExportLogic

    ''' <summary>
    ''' CSV 一括エクスポート: フォルダに テーブルごとの CSV ファイルを生成
    ''' </summary>
    Public Shared Function ExportCsv(contexts As List(Of ExportHelper.TableExportContext),
                                      outputFolder As String,
                                      worker As BackgroundWorker) As Boolean
        For i As Integer = 0 To contexts.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

            Dim ctx = contexts(i)
            Dim safeName = SafeFileBase(ctx)
            Dim outputPath = Path.Combine(outputFolder, $"{safeName}.csv")

            ' C DLL ストリーミング (進捗表示付き)
            Dim ok = CsvExportLogic.ExportFromDump(ctx, outputPath, worker)
            If Not ok Then Return False

            ReportTableProgress(worker, ctx.TableName, i + 1, contexts.Count)
        Next
        Return True
    End Function

    ''' <summary>
    ''' SQL 一括エクスポート: フォルダに テーブルごとの SQL ファイルを生成
    ''' </summary>
    Public Shared Function ExportSql(contexts As List(Of ExportHelper.TableExportContext),
                                      outputFolder As String,
                                      dbmsType As Integer,
                                      worker As BackgroundWorker,
                                      Optional databaseName As String = Nothing) As Boolean
        For i As Integer = 0 To contexts.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

            Dim ctx = contexts(i)
            Dim safeName = SafeFileBase(ctx)
            Dim outputPath = Path.Combine(outputFolder, $"{safeName}.sql")

            Dim ok As Boolean
            If ExportOptions.SqlInferInteger Then
                ' InferInteger ON: データを読み込んで VB.NET パスで出力 (値は SQL 出力と同じ形、日付は SQL 用の固定の書式)
                ok = SqlExportLogic.ExportTableViaWriter(ctx, outputPath, dbmsType, worker, databaseName)
            Else
                ' C DLL ストリーミング (進捗表示付き)
                ok = SqlExportLogic.ExportFromDump(ctx, outputPath, dbmsType, worker, databaseName)
            End If
            If Not ok Then Return False

            ReportTableProgress(worker, ctx.TableName, i + 1, contexts.Count)
        Next
        Return True
    End Function

    ''' <summary>
    ''' Excel 一括エクスポート: 1つの .xlsx ファイルにテーブルごとのシートを作成
    ''' </summary>
    Public Shared Function ExportExcel(contexts As List(Of ExportHelper.TableExportContext),
                                        outputPath As String,
                                        worker As BackgroundWorker) As Boolean
        Try
            Using wb As New ClosedXML.Excel.XLWorkbook()
                For i As Integer = 0 To contexts.Count - 1
                    If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

                    Dim ctx = contexts(i)

                    ' テーブルデータを取得 (UIスレッド非依存の直接呼び出し。日付は固定の書式で日時のセルに)
                    Dim marks As New Open_NativeParser.ValueMarks()
                    Dim tableData = LoadTableData(ctx, True, marks)

                    Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
                    Dim columnList = New List(Of String)(colNames)

                    ' シート名 (31文字制限、禁止文字置換)
                    Dim sheetName = SanitizeSheetName(ctx.TableName)
                    Dim ws = wb.Worksheets.Add(sheetName)

                    ' ヘッダ行・データ行 (セルの型は列の型で決める。単表の Excel 出力と同じ)
                    If Not ExcelExportLogic.WriteSheet(ws, tableData, columnList, ctx.ColumnTypes, Nothing, ctx.TableName, marks) Then Return False

                    ' メモリ解放
                    tableData = Nothing

                    ReportTableProgress(worker, ctx.TableName, i + 1, contexts.Count)
                Next

                wb.SaveAs(outputPath)
            End Using
            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("BulkExcelExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>
    ''' Access 一括エクスポート: 1つの .accdb ファイルに複数テーブルを作成
    ''' </summary>
    Public Shared Function ExportAccess(contexts As List(Of ExportHelper.TableExportContext),
                                         outputPath As String,
                                         worker As BackgroundWorker) As Boolean
        ' 既存ファイルを削除 (新規作成のため)
        If IO.File.Exists(outputPath) Then
            IO.File.Delete(outputPath)
        End If

        ' 最初のテーブルで .accdb を新規作成、以降はテーブル追加
        For i As Integer = 0 To contexts.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

            Dim ctx = contexts(i)

            Dim marks As New Open_NativeParser.ValueMarks()
            Dim tableData = LoadTableData(ctx, False, marks)

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)

            Dim ok = AccessExportLogic.Export(tableData, columnList, ctx.ColumnTypes,
                                              ctx.TableName, outputPath, worker,
                                              i + 1, contexts.Count, marks)
            If Not ok Then Return False

            tableData = Nothing

            ReportTableProgress(worker, ctx.TableName, i + 1, contexts.Count)
        Next
        Return True
    End Function

    ''' <summary>
    ''' SQL Server 一括エクスポート: 接続先 DB に複数テーブルを作成
    ''' </summary>
    Public Shared Function ExportSqlServer(contexts As List(Of ExportHelper.TableExportContext),
                                            connectionString As String,
                                            worker As BackgroundWorker) As Boolean
        For i As Integer = 0 To contexts.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

            Dim ctx = contexts(i)

            Dim marks As New Open_NativeParser.ValueMarks()
            Dim tableData = LoadTableData(ctx, False, marks)

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)

            Dim ok = SqlServerExportLogic.Export(tableData, columnList, ctx.ColumnTypes,
                                                 ctx.Schema, ctx.TableName, connectionString, worker, marks)
            If Not ok Then Return False

            tableData = Nothing

            ReportTableProgress(worker, ctx.TableName, i + 1, contexts.Count)
        Next
        Return True
    End Function

    ''' <summary>
    ''' ODBC 一括エクスポート: 接続先 DB に複数テーブルを作成
    ''' </summary>
    Public Shared Function ExportOdbc(contexts As List(Of ExportHelper.TableExportContext),
                                       connectionString As String,
                                       worker As BackgroundWorker) As Boolean
        For i As Integer = 0 To contexts.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

            Dim ctx = contexts(i)

            Dim marks As New Open_NativeParser.ValueMarks()
            Dim tableData = LoadTableData(ctx, True, marks)

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)

            Dim ok = OdbcExportLogic.Export(tableData, columnList, ctx.ColumnTypes,
                                             ctx.TableName, connectionString, worker, marks)
            If Not ok Then Return False

            tableData = Nothing

            ReportTableProgress(worker, ctx.TableName, i + 1, contexts.Count)
        Next
        Return True
    End Function

#Region "ヘルパー"
    ''' <summary>
    ''' テーブルデータを DLL 経由で取得 (スレッドセーフ: UIアクセスなし)
    ''' AnalyzeLogic.AnalyzeTable() はCOMMONのUI操作を含むため、
    ''' BackgroundWorkerスレッドからは直接 Open_NativeParser.ParseDump() を使用する。
    ''' 値はエクスポートの書き手と同じ形 (ODV_VALUES_EXPORT: LOB・LONG は全体。値でないものは marks に記録)
    ''' </summary>
    ''' <param name="sqlDateFormat">日付・時刻を SQL 用の固定の書式 (SYYYY/MM/DD HH24:MI:SS[.F]) で (Excel・ODBC の書き手用)</param>
    Private Shared Function LoadTableData(ctx As ExportHelper.TableExportContext, sqlDateFormat As Boolean,
                                          marks As Open_NativeParser.ValueMarks) As List(Of String())
        Return ExportHelper.LoadExportRows(ctx, sqlDateFormat, marks)
    End Function

    ''' <summary>テーブル単位の進捗報告</summary>
    Private Shared Sub ReportTableProgress(worker As BackgroundWorker, tableName As String,
                                            currentIndex As Integer, totalCount As Integer)
        If worker Is Nothing Then Return
        Dim pct As Integer = CInt(currentIndex * 100 \ totalCount)
        worker.ReportProgress(pct,
            New ExportProgressDialog.ProgressInfo(tableName, 0, 0, currentIndex, totalCount))
    End Sub

    ''' <summary>
    ''' 出力ファイル名の基底を作る。区画を 1 つだけ出す子行コンテキスト (Partition 非空) は
    ''' 表名に区画名を付けて、同じ表の区画どうしでファイル名が衝突しないようにする (H-14)。
    ''' 親行・通常表 (Partition 空) は表名のみ。
    ''' </summary>
    Private Shared Function SafeFileBase(ctx As ExportHelper.TableExportContext) As String
        Dim name = ctx.TableName
        If ctx.Partition IsNot Nothing AndAlso ctx.Partition.Length > 0 Then
            name = name & "_" & ctx.Partition
        End If
        Return String.Join("_", name.Split(Path.GetInvalidFileNameChars()))
    End Function

    ''' <summary>Excel シート名の禁止文字を除去 (31文字制限)</summary>
    Private Shared Function SanitizeSheetName(name As String) As String
        Dim forbidden = {"\"c, "/"c, "*"c, "?"c, ":"c, "["c, "]"c}
        Dim result = name
        For Each ch In forbidden
            result = result.Replace(ch, "_"c)
        Next
        If result.Length > 31 Then result = result.Substring(0, 31)
        If String.IsNullOrWhiteSpace(result) Then result = "Sheet"
        Return result
    End Function
#End Region

End Class
