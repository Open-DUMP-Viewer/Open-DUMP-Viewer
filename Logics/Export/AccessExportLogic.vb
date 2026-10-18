Imports System.Data.OleDb
Imports System.IO

''' <summary>
''' Access (.accdb) エクスポートロジック
'''
''' Microsoft Access Database Engine (ACE) を使用して .accdb ファイルを作成。
''' CREATE TABLE + バッチ INSERT (1000行単位トランザクション)。
''' ドライバ未検出時はダウンロードリンク付きメッセージを表示。
''' </summary>
Public Class AccessExportLogic

    Private Const BATCH_SIZE As Integer = 50000
    Private Shared ReadOnly ACE_PROVIDERS As String() = {"Microsoft.ACE.OLEDB.16.0", "Microsoft.ACE.OLEDB.12.0"}
    Private Shared _aceProvider As String

    ''' <summary>
    ''' テーブルデータを Access (.accdb) ファイルにエクスポート
    ''' </summary>
    ''' <param name="data">行データ (エクスポートの書き手と同じ値: LOB・LONG は全体)</param>
    ''' <param name="marks">値の形を持たない値・上限を超えた値の位置 (値として書かず NULL にする)</param>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  tableName As String,
                                  outputPath As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                  Optional currentTableIndex As Integer = 0,
                                  Optional totalTableCount As Integer = 0,
                                  Optional marks As Open_NativeParser.ValueMarks = Nothing) As Boolean
        Try
            ' ACE ドライバチェック
            If Not IsAceDriverAvailable() Then
                Dim result = MessageBox.Show(
                    Loc.S("AccessExport_AceNotInstalled") & vbCrLf & vbCrLf &
                    Loc.S("AccessExport_OpenDownloadPage"),
                    Loc.S("Title_Error"), MessageBoxButtons.YesNo, MessageBoxIcon.Error)
                If result = DialogResult.Yes Then
                    Process.Start(New ProcessStartInfo("https://www.microsoft.com/download/details.aspx?id=54920") With {.UseShellExecute = True})
                End If
                Return False
            End If

            Dim connStr = $"Provider={_aceProvider};Data Source={outputPath};Jet OLEDB:Engine Type=6;"

            ' ファイルが無い場合のみ新規作成
            If Not File.Exists(outputPath) Then
                CreateEmptyDatabase(connStr)
            End If

            Dim totalRows As Long = data.Count

            Using conn As New OleDbConnection(connStr)
                conn.Open()

                ' Access のカラム数上限 (255) を超える場合はスキップ
                If columnNames.Count > 255 Then
                    If worker IsNot Nothing Then
                        worker.ReportProgress(100,
                            New ExportProgressDialog.ProgressInfo(
                                tableName & $" (スキップ: カラム数 {columnNames.Count} > 255)", 0, 0,
                                currentTableIndex, totalTableCount))
                    End If
                    Return True
                End If

                ' CREATE TABLE
                Dim createSql = BuildCreateTableSql(tableName, columnNames, columnTypes)
                Using cmd As New OleDbCommand(createSql, conn)
                    cmd.ExecuteNonQuery()
                End Using

                ' INSERT (コマンド再利用 + バッチトランザクション)
                Dim insertSql = BuildInsertSql(tableName, columnNames)
                Dim colCount = columnNames.Count

                Using cmd As New OleDbCommand(insertSql, conn)
                    ' パラメータを事前定義 (ループ中は Value のみ更新)
                    For colIdx As Integer = 0 To colCount - 1
                        cmd.Parameters.Add(New OleDbParameter($"@p{colIdx}", OleDbType.LongVarWChar))
                    Next

                    Dim rowIdx As Long = 0
                    While rowIdx < totalRows
                        If worker IsNot Nothing AndAlso worker.CancellationPending Then
                            Return False
                        End If

                        Using txn = conn.BeginTransaction()
                            cmd.Transaction = txn
                            Dim batchEnd = Math.Min(rowIdx + BATCH_SIZE, totalRows)

                            For i As Long = rowIdx To batchEnd - 1
                                Dim row = data(CInt(i))
                                For colIdx As Integer = 0 To colCount - 1
                                    Dim raw As String = Nothing
                                    If colIdx < row.Length Then raw = row(colIdx)
                                    ' 値の形を持たない値の印 (%BLOB% など) は値ではない
                                    If marks IsNot Nothing AndAlso marks.StateOf(row, colIdx) <> Open_NativeParser.ValueState.Value Then raw = Nothing
                                    cmd.Parameters(colIdx).Value = If(String.IsNullOrEmpty(raw), DirectCast(DBNull.Value, Object), raw)
                                Next
                                cmd.ExecuteNonQuery()
                            Next

                            txn.Commit()
                        End Using

                        rowIdx += BATCH_SIZE

                        ' 進捗報告
                        If worker IsNot Nothing Then
                            Dim processed = Math.Min(rowIdx, totalRows)
                            Dim pct As Integer = CInt(If(totalRows > 0, processed * 100 \ totalRows, 100))
                            worker.ReportProgress(pct,
                                New ExportProgressDialog.ProgressInfo(tableName, processed, totalRows,
                                    currentTableIndex, totalTableCount))
                        End If
                    End While
                End Using
            End Using

            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("AccessExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>
    ''' ACE ドライバが利用可能か確認 (16.0 → 12.0 の優先順でチェック)
    ''' </summary>
    Private Shared Function IsAceDriverAvailable() As Boolean
        For Each provider In ACE_PROVIDERS
            Dim key = Microsoft.Win32.Registry.ClassesRoot.OpenSubKey($"{provider}\CLSID")
            If key IsNot Nothing Then
                key.Dispose()
                _aceProvider = provider
                Return True
            End If
        Next
        Return False
    End Function

    ''' <summary>
    ''' 空の .accdb データベースを作成 (ADOX 経由)
    ''' </summary>
    Private Shared Sub CreateEmptyDatabase(connStr As String)
        Dim catType = Type.GetTypeFromProgID("ADOX.Catalog")
        If catType Is Nothing Then
            Throw New Exception(Loc.S("AccessExport_AdoxUnavailable"))
        End If

        Dim cat = Activator.CreateInstance(catType)
        Try
            catType.InvokeMember("Create", Reflection.BindingFlags.InvokeMethod, Nothing, cat, {connStr})
            ' ADOX が開いた接続を閉じる
            Dim adoConn = catType.InvokeMember("ActiveConnection",
                Reflection.BindingFlags.GetProperty, Nothing, cat, Nothing)
            If adoConn IsNot Nothing Then
                adoConn.GetType().InvokeMember("Close",
                    Reflection.BindingFlags.InvokeMethod, Nothing, adoConn, Nothing)
                System.Runtime.InteropServices.Marshal.ReleaseComObject(adoConn)
            End If
        Finally
            System.Runtime.InteropServices.Marshal.ReleaseComObject(cat)
        End Try
    End Sub

    ''' <summary>
    ''' CREATE TABLE SQL を構築
    ''' </summary>
    Private Shared Function BuildCreateTableSql(tableName As String, columnNames As List(Of String), columnTypes As String()) As String
        Dim sb As New System.Text.StringBuilder()
        sb.Append($"CREATE TABLE [{tableName.Replace("]", "]]")}] (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            sb.Append($"[{columnNames(i).Replace("]", "]]")}] ")
            sb.Append(MapToAccessType(columnTypes, i))
        Next

        sb.Append(")")
        Return sb.ToString()
    End Function

    ''' <summary>
    ''' Oracle 型を Access 型にマッピング
    ''' </summary>
    Private Shared Function MapToAccessType(columnTypes As String(), colIdx As Integer) As String
        ' ダンプデータは全て文字列のため、全カラム MEMO で統一
        Return "MEMO"
    End Function

    ''' <summary>
    ''' INSERT INTO SQL を構築 (パラメータ化)
    ''' </summary>
    Private Shared Function BuildInsertSql(tableName As String, columnNames As List(Of String)) As String
        Dim sb As New System.Text.StringBuilder()
        sb.Append($"INSERT INTO [{tableName.Replace("]", "]]")}] (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            sb.Append($"[{columnNames(i).Replace("]", "]]")}]")
        Next

        sb.Append(") VALUES (")
        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            sb.Append($"@p{i}")
        Next
        sb.Append(")")

        Return sb.ToString()
    End Function


End Class
