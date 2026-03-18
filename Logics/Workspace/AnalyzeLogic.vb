Imports System.IO
Imports System.Text
Imports System.Collections.Generic

''' <summary>
''' Oracle DUMPファイル解析ロジック
''' OraDB_DumpParser.dll (C言語ネイティブDLL) を呼び出して解析する
''' </summary>
Public Class AnalyzeLogic

    ''' <summary>
    ''' ダンプファイルを解析する
    ''' </summary>
    ''' <param name="filePath">ダンプファイルのパス</param>
    ''' <returns>解析結果のデータ構造（スキーマ→テーブル→データ）</returns>
    Public Shared Function AnalyzeDumpFile(filePath As String) As Dictionary(Of String, Dictionary(Of String, List(Of String())))
        Try
            ValidateFilePath(filePath)

            ' プログレスバーを0-100%モードで初期化
            COMMON.InitProgressBar()

            Dim startTime As DateTime = DateTime.Now

            ' 進捗コールバック: ファイル位置ベースのパーセンテージで更新
            ' (DLL側でパーセンテージが変わった時のみ呼ばれる、最大101回)
            Dim progressAction As Action(Of Long, String, Integer) =
                Sub(rowsProcessed As Long, currentTable As String, pct As Integer)
                    COMMON.UpdateProgress(rowsProcessed, currentTable, pct, startTime)
                End Sub

            ' DLLを使ってダンプファイルを解析
            Dim result = OraDB_NativeParser.ParseDump(filePath, progressAction)

            Dim elapsed As TimeSpan = DateTime.Now - startTime

            ' プログレスバーをリセット
            COMMON.ResetProgressBar()

            ' 完了メッセージ
            Dim totalRows As Long = 0
            Dim totalTables As Integer = 0
            For Each schema In result
                For Each table In schema.Value
                    totalTables += 1
                    totalRows += table.Value.Count
                Next
            Next
            COMMON.Set_StatusLavel_AutoReset(Loc.SF("Status_AnalysisComplete", totalTables, $"{totalRows:#,0}", $"{elapsed.TotalSeconds:F1}"))

            Return result

        Catch ex As DllNotFoundException
            MessageBox.Show(Loc.SF("Analyze_DllNotFound", ex.Message),
                           Loc.S("Title_DllError"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New Dictionary(Of String, Dictionary(Of String, List(Of String())))()

        Catch ex As Exception
            MessageBox.Show(Loc.SF("Analyze_ParseError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New Dictionary(Of String, Dictionary(Of String, List(Of String())))()
        End Try
    End Function

    ''' <summary>
    ''' テーブル一覧のみ取得する（高速・メモリ軽量）
    ''' 行データは読み込まない
    ''' </summary>
    ''' <param name="filePath">ダンプファイルのパス</param>
    ''' <param name="columnNamesMap">テーブルごとのカラム名辞書 (キー: "schema.table")</param>
    ''' <param name="columnTypesMap">テーブルごとのカラム型辞書 (キー: "schema.table")</param>
    ''' <returns>テーブル情報のリスト (スキーマ名, テーブル名, カラム数, 行数, データオフセット)</returns>
    Public Shared Function ListTables(filePath As String,
                                      Optional ByRef columnNamesMap As Dictionary(Of String, String()) = Nothing,
                                      Optional ByRef columnTypesMap As Dictionary(Of String, String()) = Nothing,
                                      Optional ByRef columnNotNullsMap As Dictionary(Of String, Boolean()) = Nothing,
                                      Optional ByRef columnDefaultsMap As Dictionary(Of String, String()) = Nothing) As List(Of Tuple(Of String, String, Integer, Long, Long))
        Try
            ValidateFilePath(filePath)

            COMMON.SetProgressBarMarquee()
            COMMON.Set_StatusLavel(Loc.S("Status_GettingTableList"))

            Dim startTime As DateTime = DateTime.Now
            Dim tables = OraDB_NativeParser.ListTables(filePath, columnNamesMap, columnTypesMap, columnNotNullsMap, columnDefaultsMap)
            Dim elapsed As TimeSpan = DateTime.Now - startTime

            COMMON.ResetProgressBar()
            COMMON.Set_StatusLavel_AutoReset(Loc.SF("Status_TableListComplete", tables.Count, $"{elapsed.TotalSeconds:F1}"))

            Return tables

        Catch ex As DllNotFoundException
            MessageBox.Show(Loc.SF("Analyze_DllNotFound", ex.Message),
                           Loc.S("Title_DllError"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New List(Of Tuple(Of String, String, Integer, Long, Long))()

        Catch ex As Exception
            MessageBox.Show(Loc.SF("Analyze_TableListError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New List(Of Tuple(Of String, String, Integer, Long, Long))()
        End Try
    End Function

    ''' <summary>
    ''' 指定されたテーブルのみ解析する（メモリ効率的）
    ''' DUMPファイル全体をスキャンするが、行データは指定テーブルのみ蓄積する
    ''' </summary>
    ''' <param name="filePath">ダンプファイルのパス</param>
    ''' <param name="schemaName">スキーマ名</param>
    ''' <param name="tableName">テーブル名</param>
    ''' <returns>行データのリスト</returns>
    Public Shared Function AnalyzeTable(filePath As String, schemaName As String, tableName As String, Optional dataOffset As Long = 0) As List(Of String())
        Try
            ValidateFilePath(filePath)

            COMMON.InitProgressBar()
            Dim startTime As DateTime = DateTime.Now

            Dim progressAction As Action(Of Long, String, Integer) =
                Sub(rowsProcessed As Long, currentTable As String, pct As Integer)
                    COMMON.UpdateProgress(rowsProcessed, currentTable, pct, startTime)
                End Sub

            ' テーブルフィルタ付きで解析（dataOffset>0ならDDL位置に高速シーク）
            Dim result = OraDB_NativeParser.ParseDump(filePath, progressAction, schemaName, tableName, dataOffset)

            Dim elapsed As TimeSpan = DateTime.Now - startTime
            COMMON.ResetProgressBar()

            ' 結果からテーブルデータを抽出
            If result.ContainsKey(schemaName) AndAlso result(schemaName).ContainsKey(tableName) Then
                Dim rows = result(schemaName)(tableName)
                COMMON.Set_StatusLavel_AutoReset(Loc.SF("Status_TableAnalysisComplete", schemaName, tableName, $"{rows.Count:#,0}", $"{elapsed.TotalSeconds:F1}"))
                Return rows
            End If

            COMMON.Set_StatusLavel_AutoReset(Loc.SF("Status_TableAnalysisComplete", schemaName, tableName, "0", $"{elapsed.TotalSeconds:F1}"))
            Return New List(Of String())()

        Catch ex As DllNotFoundException
            MessageBox.Show(Loc.SF("Analyze_DllNotFound", ex.Message),
                           Loc.S("Title_DllError"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New List(Of String())()

        Catch ex As Exception
            MessageBox.Show(Loc.SF("Analyze_TableError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New List(Of String())()
        End Try
    End Function

    ''' <summary>
    ''' 指定されたテーブルのみ非同期で解析する（UIスレッドをブロックしない）
    ''' </summary>
    Public Shared Async Function AnalyzeTableAsync(filePath As String, schemaName As String, tableName As String,
                                                    Optional dataOffset As Long = 0,
                                                    Optional expectedRowCount As Long = 0) As Task(Of List(Of String()))
        Try
            ValidateFilePath(filePath)

            COMMON.InitProgressBar()
            Dim startTime As DateTime = DateTime.Now

            ' 進捗コールバック: BeginInvoke でUIスレッドにマーシャル
            Dim mainForm = Application.OpenForms.OfType(Of OraDB_DUMP_Viewer)().FirstOrDefault()
            Dim progressAction As Action(Of Long, String, Integer) = Nothing
            If mainForm IsNot Nothing Then
                progressAction = Sub(rowsProcessed As Long, currentTable As String, pct As Integer)
                                     mainForm.BeginInvoke(Sub()
                                                              COMMON.UpdateProgress(rowsProcessed, currentTable, pct, startTime)
                                                          End Sub)
                                 End Sub
            End If

            ' バックグラウンドスレッドで解析実行
            Dim result = Await Task.Run(Function()
                                            Return OraDB_NativeParser.ParseDump(filePath, progressAction, schemaName, tableName, dataOffset, expectedRowCount)
                                        End Function)

            Dim elapsed As TimeSpan = DateTime.Now - startTime
            COMMON.ResetProgressBar()

            ' 結果からテーブルデータを抽出
            If result.ContainsKey(schemaName) AndAlso result(schemaName).ContainsKey(tableName) Then
                Dim rows = result(schemaName)(tableName)
                COMMON.Set_StatusLavel_AutoReset(Loc.SF("Status_TableAnalysisComplete", schemaName, tableName, $"{rows.Count:#,0}", $"{elapsed.TotalSeconds:F1}"))
                Return rows
            End If

            COMMON.Set_StatusLavel_AutoReset(Loc.SF("Status_TableAnalysisComplete", schemaName, tableName, "0", $"{elapsed.TotalSeconds:F1}"))
            Return New List(Of String())()

        Catch ex As DllNotFoundException
            MessageBox.Show(Loc.SF("Analyze_DllNotFound", ex.Message),
                           Loc.S("Title_DllError"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New List(Of String())()

        Catch ex As Exception
            MessageBox.Show(Loc.SF("Analyze_TableError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            COMMON.ResetProgressBar()
            COMMON.ReSet_StatusLavel()
            Return New List(Of String())()
        End Try
    End Function

    ''' <summary>
    ''' ファイルパスの妥当性をチェック
    ''' </summary>
    Private Shared Sub ValidateFilePath(filePath As String)
        If String.IsNullOrEmpty(filePath) Then
            Throw New ArgumentException(Loc.S("Analyze_FilePathEmpty"))
        End If

        If Not File.Exists(filePath) Then
            Throw New FileNotFoundException(Loc.SF("Analyze_FileNotFound", filePath))
        End If

        If Not filePath.EndsWith(".dmp", StringComparison.OrdinalIgnoreCase) Then
            Throw New ArgumentException(Loc.S("Analyze_InvalidFormat"))
        End If
    End Sub

End Class
