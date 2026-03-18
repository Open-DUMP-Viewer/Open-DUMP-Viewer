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
            Dim outputPath = Path.Combine(outputFolder, $"{ctx.TableName}.csv")

            ' C DLL ストリーミング (進捗はテーブル単位)
            Dim ok = CsvExportLogic.ExportFromDump(ctx, outputPath)
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
            Dim outputPath = Path.Combine(outputFolder, $"{ctx.TableName}.sql")

            Dim ok As Boolean
            If ExportOptions.SqlInferInteger Then
                ' InferInteger ON: データを読み込んで VB.NET パスで出力
                Dim tableData = LoadTableData(ctx)
                Dim colNames = New List(Of String)(If(ctx.ColumnNames, Array.Empty(Of String)()))
                ok = SqlExportLogic.ExportFromData(tableData, colNames, ctx.ColumnTypes,
                        ctx.Schema, ctx.TableName, outputPath, dbmsType, worker,
                        ctx.ColumnNotNulls, ctx.ColumnDefaults, databaseName)
                tableData = Nothing
            Else
                ' C DLL ストリーミング
                ok = SqlExportLogic.ExportFromDump(ctx, outputPath, dbmsType)
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

                    ' テーブルデータを取得 (UIスレッド非依存の直接呼び出し)
                    Dim tableData = LoadTableData(ctx)

                    Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
                    Dim columnList = New List(Of String)(colNames)

                    ' シート名 (31文字制限、禁止文字置換)
                    Dim sheetName = SanitizeSheetName(ctx.TableName)
                    Dim ws = wb.Worksheets.Add(sheetName)

                    ' ヘッダ行
                    For col As Integer = 0 To columnList.Count - 1
                        ws.Cell(1, col + 1).Value = columnList(col)
                        ws.Cell(1, col + 1).Style.Font.Bold = True
                        ws.Cell(1, col + 1).Style.Fill.BackgroundColor = ClosedXML.Excel.XLColor.LightSteelBlue
                    Next

                    ' データ行
                    For row As Integer = 0 To tableData.Count - 1
                        For col As Integer = 0 To columnList.Count - 1
                            If col < tableData(row).Length AndAlso tableData(row)(col) IsNot Nothing Then
                                Dim cellValue = tableData(row)(col)
                                Dim dblVal As Double
                                If Double.TryParse(cellValue, dblVal) Then
                                    ws.Cell(row + 2, col + 1).Value = dblVal
                                Else
                                    ws.Cell(row + 2, col + 1).Value = cellValue
                                End If
                            End If
                        Next
                    Next

                    ' 列幅自動調整
                    ws.Columns().AdjustToContents(1, Math.Min(tableData.Count + 1, 100))

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
        ' 最初のテーブルで .accdb を新規作成、以降はテーブル追加
        For i As Integer = 0 To contexts.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False

            Dim ctx = contexts(i)

            Dim tableData = LoadTableData(ctx)

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)

            Dim ok = AccessExportLogic.Export(tableData, columnList, ctx.ColumnTypes,
                                              ctx.TableName, outputPath, worker)
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

            Dim tableData = LoadTableData(ctx)

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)

            Dim ok = SqlServerExportLogic.Export(tableData, columnList, ctx.ColumnTypes,
                                                 ctx.Schema, ctx.TableName, connectionString, worker)
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

            Dim tableData = LoadTableData(ctx)

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)

            Dim ok = OdbcExportLogic.Export(tableData, columnList, ctx.ColumnTypes,
                                             ctx.TableName, connectionString, worker)
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
    ''' BackgroundWorkerスレッドからは直接 OraDB_NativeParser.ParseDump() を使用する
    ''' </summary>
    Private Shared Function LoadTableData(ctx As ExportHelper.TableExportContext) As List(Of String())
        Dim result = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
            Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)

        If result IsNot Nothing AndAlso
           result.ContainsKey(ctx.Schema) AndAlso
           result(ctx.Schema).ContainsKey(ctx.TableName) Then
            Return result(ctx.Schema)(ctx.TableName)
        End If

        Return New List(Of String())
    End Function

    ''' <summary>テーブル単位の進捗報告</summary>
    Private Shared Sub ReportTableProgress(worker As BackgroundWorker, tableName As String,
                                            currentIndex As Integer, totalCount As Integer)
        If worker Is Nothing Then Return
        Dim pct As Integer = CInt(currentIndex * 100 \ totalCount)
        worker.ReportProgress(pct,
            New ExportProgressDialog.ProgressInfo(tableName, 0, 0, currentIndex, totalCount))
    End Sub

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
