Imports ClosedXML.Excel

''' <summary>
''' Excel (.xlsx) エクスポートロジック
'''
''' ClosedXML を使用して .xlsx ファイルを生成。
''' ヘッダ行 (太字・背景色) + データ行。
''' Excel 行上限 1,048,576 行チェック付き。
''' </summary>
Public Class ExcelExportLogic

    Private Const EXCEL_MAX_ROWS As Integer = 1048576

    ''' <summary>
    ''' テーブルデータを Excel (.xlsx) ファイルにエクスポート
    ''' </summary>
    ''' <param name="data">エクスポートするデータ行</param>
    ''' <param name="columnNames">列名リスト</param>
    ''' <param name="columnTypes">列型リスト (書式設定用、Nothing可)</param>
    ''' <param name="sheetName">シート名</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <param name="worker">進捗報告用の BackgroundWorker</param>
    ''' <returns>成功なら True</returns>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  sheetName As String,
                                  outputPath As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count

            ' Excel 行上限チェック (ヘッダ1行 + データ行)
            If totalRows + 1 > EXCEL_MAX_ROWS Then
                Throw New Exception(Loc.SF("ExcelExport_RowLimitExceeded", $"{totalRows:#,0}", $"{EXCEL_MAX_ROWS:#,0}"))
            End If

            Using wb As New XLWorkbook()
                ' シート名のサニタイズ (31文字制限、禁止文字除去)
                Dim safeName = SanitizeSheetName(sheetName)
                Dim ws = wb.Worksheets.Add(safeName)

                ' ヘッダ行
                For colIdx As Integer = 0 To columnNames.Count - 1
                    Dim cell = ws.Cell(1, colIdx + 1)
                    cell.Value = columnNames(colIdx)
                    cell.Style.Font.Bold = True
                    cell.Style.Fill.BackgroundColor = XLColor.LightSteelBlue
                Next

                ' データ行
                For rowIdx As Long = 0 To totalRows - 1
                    If worker IsNot Nothing AndAlso worker.CancellationPending Then
                        Return False
                    End If

                    Dim row = data(CInt(rowIdx))
                    For colIdx As Integer = 0 To columnNames.Count - 1
                        Dim cell = ws.Cell(CInt(rowIdx + 2), colIdx + 1)
                        If colIdx < row.Length AndAlso row(colIdx) IsNot Nothing Then
                            SetCellValue(cell, row(colIdx), columnTypes, colIdx)
                        End If
                    Next

                    ' 進捗報告 (1000行ごと)
                    If worker IsNot Nothing AndAlso (rowIdx Mod 1000 = 0 OrElse rowIdx = totalRows - 1) Then
                        Dim pct As Integer = CInt(If(totalRows > 0, (rowIdx + 1) * 100 \ totalRows, 100))
                        worker.ReportProgress(pct,
                            New ExportProgressDialog.ProgressInfo(sheetName, rowIdx + 1, totalRows))
                    End If
                Next

                ' 列幅を自動調整
                ws.Columns().AdjustToContents(1, Math.Min(CInt(totalRows + 1), 100))

                wb.SaveAs(outputPath)
            End Using

            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("ExcelExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>
    ''' セルに値を設定 (型に応じた書式設定)
    ''' </summary>
    Private Shared Sub SetCellValue(cell As IXLCell, value As String, columnTypes As String(), colIdx As Integer)
        ' 数値判定
        Dim numValue As Double
        If Double.TryParse(value, numValue) Then
            cell.Value = numValue
            Return
        End If

        ' 日付判定 (Oracle DATE/TIMESTAMP 形式)
        Dim dateValue As DateTime
        If DateTime.TryParse(value, dateValue) Then
            cell.Value = dateValue
            cell.Style.DateFormat.Format = "yyyy-MM-dd HH:mm:ss"
            Return
        End If

        ' 文字列
        cell.Value = value
    End Sub

    ''' <summary>
    ''' シート名をサニタイズ (Excel 制限: 31文字、禁止文字 :\/?*[])
    ''' </summary>
    Private Shared Function SanitizeSheetName(name As String) As String
        If String.IsNullOrEmpty(name) Then Return "Sheet1"

        Dim invalid = {"\"c, "/"c, "?"c, "*"c, "["c, "]"c, ":"c}
        For Each c In invalid
            name = name.Replace(c, "_"c)
        Next

        If name.Length > 31 Then
            name = name.Substring(0, 31)
        End If

        Return name
    End Function

End Class
