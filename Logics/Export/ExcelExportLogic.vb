Imports ClosedXML.Excel

''' <summary>
''' Excel (.xlsx) エクスポートロジック
'''
''' ClosedXML を使用して .xlsx ファイルを生成。
''' ヘッダ行 (太字・背景色) + データ行。
''' Excel 行上限 1,048,576 行チェック付き。
'''
''' セルの型は列の Oracle の型で決める (値の見た目では決めない。'00123' の VARCHAR2 は文字列のまま):
''' 数値の列は数値 (Excel が正確に持てない 15 桁を超える値は文字列)、DATE / TIMESTAMP は日時、
''' タイムゾーン付きの時刻・INTERVAL・RAW などは文字列。行データの日付・時刻は SQL 用の固定の書式
''' (Open_NativeParser.DATE_FMT_SLASH、ParseDump の sqlDateFormat) で解析した値であること。
''' 行データはエクスポートの書き手と同じ値 (ODV_VALUES_EXPORT: LOB・LONG は全体)。値の形を持たない値 (印)・上限を
''' 超えた値は値として書かず、空のセルにメモを付ける。
''' </summary>
Public Class ExcelExportLogic

    Private Const EXCEL_MAX_ROWS As Integer = 1048576
    ''' <summary>Excel の 1 セルの文字数の上限</summary>
    Private Const EXCEL_MAX_CELL_CHARS As Integer = 32767

    ''' <summary>
    ''' テーブルデータを Excel (.xlsx) ファイルにエクスポート
    ''' </summary>
    ''' <param name="data">エクスポートするデータ行 (日付は SQL 用の固定の書式)</param>
    ''' <param name="columnNames">列名リスト</param>
    ''' <param name="columnTypes">列型リスト (書式設定用、Nothing可)</param>
    ''' <param name="sheetName">シート名</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <param name="worker">進捗報告用の BackgroundWorker</param>
    ''' <returns>成功なら True</returns>
    ''' <param name="marks">値の形を持たない値・上限を超えた値の位置</param>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  sheetName As String,
                                  outputPath As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                  Optional marks As Open_NativeParser.ValueMarks = Nothing) As Boolean
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

                If Not WriteSheet(ws, data, columnNames, columnTypes, worker, sheetName, marks) Then Return False

                wb.SaveAs(outputPath)
            End Using

            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("ExcelExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>
    ''' シートにヘッダ行とデータ行を書く (単表・一括の共通)。キャンセルされたら False
    ''' </summary>
    Friend Shared Function WriteSheet(ws As IXLWorksheet, data As List(Of String()), columnNames As List(Of String),
                                      columnTypes As String(), worker As System.ComponentModel.BackgroundWorker,
                                      progressName As String,
                                      Optional marks As Open_NativeParser.ValueMarks = Nothing) As Boolean
        Dim totalRows As Long = data.Count

        ' ヘッダ行
        For colIdx As Integer = 0 To columnNames.Count - 1
            Dim cell = ws.Cell(1, colIdx + 1)
            cell.Value = columnNames(colIdx)
            cell.Style.Font.Bold = True
            cell.Style.Fill.BackgroundColor = XLColor.LightSteelBlue
        Next

        ' 列の種類 (値の型)
        Dim kinds(columnNames.Count - 1) As SqlDialect.SqlKind
        For colIdx As Integer = 0 To columnNames.Count - 1
            Dim t = If(columnTypes IsNot Nothing AndAlso colIdx < columnTypes.Length, columnTypes(colIdx), Nothing)
            kinds(colIdx) = If(t Is Nothing, SqlDialect.SqlKind.Text, SqlDialect.KindOf(t, SqlDialect.ParseOraType(t)))
        Next

        ' データ行
        For rowIdx As Integer = 0 To data.Count - 1
            If worker IsNot Nothing AndAlso worker.CancellationPending Then
                Return False
            End If

            Dim row = data(rowIdx)
            For colIdx As Integer = 0 To columnNames.Count - 1
                If colIdx >= row.Length Then Continue For
                Dim state = If(marks Is Nothing, Open_NativeParser.ValueState.Value, marks.StateOf(row, colIdx))
                If row(colIdx) IsNot Nothing OrElse state <> Open_NativeParser.ValueState.Value Then
                    SetCellValue(ws.Cell(rowIdx + 2, colIdx + 1), row(colIdx), kinds(colIdx), state)
                End If
            Next

            ' 進捗報告 (1000行ごと)
            If worker IsNot Nothing AndAlso (rowIdx Mod 1000 = 0 OrElse rowIdx = totalRows - 1) Then
                Dim pct As Integer = CInt(If(totalRows > 0, (rowIdx + 1) * 100L \ totalRows, 100))
                worker.ReportProgress(pct,
                    New ExportProgressDialog.ProgressInfo(progressName, rowIdx + 1, totalRows))
            End If
        Next

        ' 列幅を自動調整
        ws.Columns().AdjustToContents(1, Math.Min(CInt(totalRows + 1), 100))
        Return True
    End Function

    ''' <summary>
    ''' セルに値を設定 (列の型で決める)
    ''' </summary>
    Friend Shared Sub SetCellValue(cell As IXLCell, value As String, kind As SqlDialect.SqlKind, state As Open_NativeParser.ValueState)
        ' 値の形を持たない値 (印)・上限を超えて全体を持てなかった値は、値として書かない (空のセルにメモ)
        If state = Open_NativeParser.ValueState.Marker Then
            AddNote(cell, $"The value could not be decoded ({SqlDialect.Utf8Prefix(If(value, ""), 40)}).")
            Return
        End If
        If state = Open_NativeParser.ValueState.TooLong Then
            AddNote(cell, $"The value is longer than the export keeps whole ({SqlDialect.FULL_VALUE_MAX_MIB} MiB); it was not written.")
            Return
        End If

        Select Case kind
            Case SqlDialect.SqlKind.Num, SqlDialect.SqlKind.FloatNum, SqlDialect.SqlKind.BinFloat, SqlDialect.SqlKind.BinDouble
                Dim d As Double
                If ToExcelNumber(value, d) Then
                    cell.Value = d
                    Return
                End If
            Case SqlDialect.SqlKind.DateKind, SqlDialect.SqlKind.Timestamp
                Dim dt As DateTime
                Dim fmt As String = Nothing
                If ToExcelDate(value, dt, fmt) Then
                    cell.Value = dt
                    cell.Style.DateFormat.Format = fmt
                    Return
                End If
        End Select

        ' 文字列 (Excel の 1 セルの上限を超える値は書かない: 切った値を入れない)
        If value.Length > EXCEL_MAX_CELL_CHARS Then
            AddNote(cell, $"The value is {value.Length:#,0} characters long, more than an Excel cell holds ({EXCEL_MAX_CELL_CHARS:#,0}); it was not written.")
            Return
        End If
        cell.Value = value
    End Sub

    ''' <summary>値を書かなかったセルにメモを付ける (作成者は製品名。OS のユーザー名をファイルに残さない)</summary>
    Private Shared Sub AddNote(cell As IXLCell, text As String)
        Dim c = cell.CreateComment()
        c.Author = "Open DUMP Viewer"
        c.AddText(text)
    End Sub

    ''' <summary>
    ''' 数値の文字列を Excel の数値にする。Excel は倍精度 (有効 15 桁) なので、それを超える桁の値は False (文字列で書く)
    ''' </summary>
    Private Shared Function ToExcelNumber(value As String, ByRef d As Double) As Boolean
        If Not SqlDialect.IsNumericLiteral(value) Then Return False
        Dim intd, fracd, sig As Integer
        SqlDialect.NumberDigits(value, intd, fracd, sig)
        If sig > 15 Then Return False
        If Not Double.TryParse(value, Globalization.NumberStyles.Float, Globalization.CultureInfo.InvariantCulture, d) Then Return False
        Return Not Double.IsInfinity(d)
    End Function

    ''' <summary>
    ''' SQL 用の固定の書式 (SYYYY/MM/DD HH24:MI:SS[.F]) の日時を Excel の日時にする。Excel が表せない日付
    ''' (1900-03-01 より前。1900 年の閏日の誤りを避ける) や、ミリ秒より細かい小数秒を持つ値は False (文字列で書く)
    ''' </summary>
    Private Shared Function ToExcelDate(value As String, ByRef dt As DateTime, ByRef fmt As String) As Boolean
        Dim rest As String = Nothing
        Dim t = SqlDialect.ParseSqlDateTime(value, rest)
        If t Is Nothing OrElse rest.Length > 0 Then Return False
        If t.Year < 1900 OrElse t.Year > 9999 OrElse (t.Year = 1900 AndAlso t.Month < 3) Then Return False
        Dim frac = t.Frac
        If frac.Length > 3 AndAlso frac.Substring(3).TrimEnd("0"c).Length > 0 Then Return False
        Dim ms = If(frac.Length = 0, 0, Integer.Parse(frac.PadRight(3, "0"c).Substring(0, 3), Globalization.CultureInfo.InvariantCulture))
        Try
            dt = New DateTime(t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second, ms)
        Catch
            Return False
        End Try
        fmt = If(frac.Length > 0, "yyyy-mm-dd hh:mm:ss.000", "yyyy-mm-dd hh:mm:ss")
        Return True
    End Function

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
