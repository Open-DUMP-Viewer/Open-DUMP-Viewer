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
    Public Shared Function ExportFromDump(ctx As ExportHelper.TableExportContext, outputPath As String) As Boolean
        Dim rc = OraDB_NativeParser.ExportCsv(ctx.DumpFilePath, ctx.TableName, outputPath,
                                               ctx.Schema, ctx.DataOffset)
        If rc <> OraDB_NativeParser.ODV_OK Then
            Throw New Exception($"CSV エクスポートエラー (rc={rc})")
        End If
        Return True
    End Function

    ''' <summary>
    ''' VB.NET インメモリデータから CSV エクスポート (フィルタ後データ対応)
    ''' BackgroundWorker から呼び出して進捗報告可能
    ''' </summary>
    ''' <param name="data">エクスポートするデータ行</param>
    ''' <param name="columnNames">列名リスト</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <param name="worker">進捗報告用の BackgroundWorker (Nothing可)</param>
    ''' <param name="tableName">テーブル名 (進捗表示用)</param>
    ''' <returns>成功なら True</returns>
    Public Shared Function ExportFromData(data As List(Of String()),
                                          columnNames As List(Of String),
                                          outputPath As String,
                                          Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                          Optional tableName As String = "",
                                          Optional columnTypes As String() = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count

            Using sw As New StreamWriter(outputPath, False, New UTF8Encoding(False))
                ' ヘッダ行
                If ExportOptions.CsvWriteHeader Then
                    For i As Integer = 0 To columnNames.Count - 1
                        If i > 0 Then sw.Write(",")
                        sw.Write(ExportHelper.EscapeCsvValue(columnNames(i)))
                    Next
                    sw.WriteLine()

                    ' 型行 (ヘッダの後に出力)
                    If ExportOptions.CsvWriteTypes AndAlso columnTypes IsNot Nothing Then
                        For i As Integer = 0 To columnNames.Count - 1
                            If i > 0 Then sw.Write(",")
                            If i < columnTypes.Length Then
                                sw.Write(ExportHelper.EscapeCsvValue(columnTypes(i)))
                            End If
                        Next
                        sw.WriteLine()
                    End If
                End If

                ' データ行 (1行ずつストリーミング書き出し)
                For rowIdx As Long = 0 To totalRows - 1
                    ' キャンセルチェック
                    If worker IsNot Nothing AndAlso worker.CancellationPending Then
                        Return False
                    End If

                    Dim row = data(CInt(rowIdx))
                    For colIdx As Integer = 0 To columnNames.Count - 1
                        If colIdx > 0 Then sw.Write(",")
                        If colIdx < row.Length Then
                            sw.Write(ExportHelper.EscapeCsvValue(row(colIdx)))
                        End If
                    Next
                    sw.WriteLine()

                    ' 進捗報告 (1000行ごと または 最終行)
                    If worker IsNot Nothing AndAlso (rowIdx Mod 1000 = 0 OrElse rowIdx = totalRows - 1) Then
                        Dim pct As Integer = CInt(If(totalRows > 0, (rowIdx + 1) * 100 \ totalRows, 100))
                        worker.ReportProgress(pct,
                            New ExportProgressDialog.ProgressInfo(tableName, rowIdx + 1, totalRows))
                    End If
                Next
            End Using

            Return True

        Catch ex As Exception
            Throw New Exception($"CSV エクスポートエラー: {ex.Message}", ex)
        End Try
    End Function

End Class
