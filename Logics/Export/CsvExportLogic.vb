Imports System.IO
Imports System.Text

''' <summary>
''' CSV エクスポートロジック
'''
''' 2つのエクスポート経路を提供:
''' 1. C DLL 経由 (Workspace): DUMP ファイルを再パースしながらストリーミング出力
''' 2. VB.NET インメモリ (TablePreview): フィルタ後データを StreamWriter で逐次書き出し
''' </summary>
Public Class CsvExportLogic

    ''' <summary>
    ''' C DLL 経由で CSV エクスポート (全行・ストリーミング)
    ''' DUMP ファイルを直接再パースし、1行ずつ CSV に書き出す (メモリ最小)
    ''' </summary>
    ''' <param name="ctx">テーブルエクスポートコンテキスト</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <returns>成功なら True</returns>
    Public Shared Function ExportFromDump(ctx As ExportHelper.TableExportContext, outputPath As String,
                                          Optional worker As System.ComponentModel.BackgroundWorker = Nothing) As Boolean
        Dim progressAction As Action(Of Long, String, Integer) = Nothing
        If worker IsNot Nothing Then
            progressAction = Sub(rows As Long, tbl As String, pct As Integer)
                                 worker.ReportProgress(0,
                                     New ExportProgressDialog.ProgressInfo(tbl, rows, ctx.RowCount))
                             End Sub
        End If

        Dim rc = Open_NativeParser.ExportCsv(ctx.DumpFilePath, ctx.TableName, outputPath,
                                               ctx.Schema, ctx.DataOffset, progressAction, ctx.Partition, ctx.DataOffsets)
        If rc <> Open_NativeParser.ODV_OK Then
            Throw New Exception(Loc.SF("CsvExport_ErrorRc", rc))
        End If
        Return True
    End Function

    ''' <summary>notes.txt に書く行の上限 (DLL の CSV 出力と同じ。超えた分は件数だけ)</summary>
    Private Const MAX_NOTE_LINES As Long = 100000

    ''' <summary>
    ''' VB.NET インメモリデータから CSV エクスポート (フィルタ後データ対応)。
    ''' 行データはエクスポートの書き手と同じ値 (ODV_VALUES_EXPORT。ExportHelper.RowsForExport) であること。
    ''' 書き方は DLL の CSV 出力と同じ: RFC 4180 (行の終わりは CRLF、区切り・引用符・改行を含む値は引用符で囲み、
    ''' 値の中の改行はそのまま)、UTF-8 (BOM なし)。値でないもの (印・上限超え) は空欄にして、
    ''' その欄を &lt;出力&gt;.notes.txt に 1 行ずつ書く (行の終わりは CRLF)。
    ''' BackgroundWorker から呼び出して進捗報告可能
    ''' </summary>
    ''' <param name="data">エクスポートするデータ行</param>
    ''' <param name="columnNames">列名リスト</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <param name="worker">進捗報告用の BackgroundWorker (Nothing可)</param>
    ''' <param name="tableName">テーブル名 (進捗表示・notes 用)</param>
    ''' <param name="columnTypes">列の型 (型の行用)</param>
    ''' <param name="marks">値の形を持たない値・上限を超えた値の位置</param>
    ''' <returns>成功なら True</returns>
    Public Shared Function ExportFromData(data As List(Of String()),
                                          columnNames As List(Of String),
                                          outputPath As String,
                                          Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                          Optional tableName As String = "",
                                          Optional columnTypes As String() = Nothing,
                                          Optional marks As Open_NativeParser.ValueMarks = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count
            ' 行の区切りは CRLF (RFC 4180。DLL の CSV 出力と同じ)
            Dim nl = vbCrLf

            ' 値として書かず空欄にした欄
            Dim notes As New StringBuilder()
            Dim noteCount As Long = 0

            Using sw As New StreamWriter(outputPath, False, New UTF8Encoding(False))
                ' ヘッダ行
                If ExportOptions.CsvWriteHeader Then
                    For i As Integer = 0 To columnNames.Count - 1
                        If i > 0 Then sw.Write(ExportOptions.CsvDelimiter)
                        sw.Write(ExportHelper.EscapeCsvValue(columnNames(i)))
                    Next
                    sw.Write(nl)
                End If

                ' 型行 (ヘッダの後に出力)
                If ExportOptions.CsvWriteTypes AndAlso columnTypes IsNot Nothing Then
                    For i As Integer = 0 To columnNames.Count - 1
                        If i > 0 Then sw.Write(ExportOptions.CsvDelimiter)
                        If i < columnTypes.Length Then
                            sw.Write(ExportHelper.EscapeCsvValue(columnTypes(i)))
                        End If
                    Next
                    sw.Write(nl)
                End If

                ' データ行 (1行ずつストリーミング書き出し)
                For rowIdx As Long = 0 To totalRows - 1
                    ' キャンセルチェック
                    If worker IsNot Nothing AndAlso worker.CancellationPending Then
                        Return False
                    End If

                    Dim row = data(CInt(rowIdx))
                    For colIdx As Integer = 0 To columnNames.Count - 1
                        If colIdx > 0 Then sw.Write(ExportOptions.CsvDelimiter)
                        If colIdx < row.Length Then
                            Dim v = row(colIdx)
                            Dim why As String = Nothing
                            Select Case If(marks Is Nothing, Open_NativeParser.ValueState.Value, marks.StateOf(row, colIdx))
                                Case Open_NativeParser.ValueState.Marker
                                    why = $"the value could not be decoded ({SqlDialect.Utf8Prefix(If(v, ""), 40)})"
                                Case Open_NativeParser.ValueState.TooLong
                                    why = $"the value is longer than the export keeps whole ({SqlDialect.FULL_VALUE_MAX_MIB} MiB)"
                            End Select
                            If why IsNot Nothing Then
                                noteCount += 1
                                If noteCount <= MAX_NOTE_LINES Then
                                    notes.Append($"row {rowIdx + 1} of {tableName}, column {columnNames(colIdx)}: {why}; written as an empty field").Append(nl)
                                End If
                                Continue For
                            End If
                            sw.Write(ExportHelper.EscapeCsvValue(v))
                        End If
                    Next
                    sw.Write(nl)

                    ' 進捗報告 (1000行ごと または 最終行)
                    If worker IsNot Nothing AndAlso (rowIdx Mod 1000 = 0 OrElse rowIdx = totalRows - 1) Then
                        Dim pct As Integer = CInt(If(totalRows > 0, (rowIdx + 1) * 100 \ totalRows, 100))
                        worker.ReportProgress(pct,
                            New ExportProgressDialog.ProgressInfo(tableName, rowIdx + 1, totalRows))
                    End If
                Next
            End Using

            ' 空欄にした欄の一覧 (欄だけでは NULL と見分けられないので、黙って空にしない)
            If noteCount > 0 Then
                If noteCount > MAX_NOTE_LINES Then notes.Append($"... {noteCount} fields in all").Append(nl)
                File.WriteAllText(outputPath & ".notes.txt", notes.ToString(), New UTF8Encoding(False))
            End If

            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("CsvExport_Error", ex.Message), ex)
        End Try
    End Function

End Class
