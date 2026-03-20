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

    Private Const BATCH_SIZE As Integer = 1000
    Private Const ACE_PROVIDER As String = "Microsoft.ACE.OLEDB.12.0"

    ''' <summary>
    ''' テーブルデータを Access (.accdb) ファイルにエクスポート
    ''' </summary>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  tableName As String,
                                  outputPath As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing) As Boolean
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

            ' 既存ファイルを削除 (新規作成のため)
            If File.Exists(outputPath) Then
                File.Delete(outputPath)
            End If

            Dim connStr = $"Provider={ACE_PROVIDER};Data Source={outputPath};Jet OLEDB:Engine Type=6;"

            ' 空の .accdb ファイルを作成
            CreateEmptyDatabase(connStr)

            Dim totalRows As Long = data.Count

            Using conn As New OleDbConnection(connStr)
                conn.Open()

                ' CREATE TABLE
                Dim createSql = BuildCreateTableSql(tableName, columnNames, columnTypes)
                Using cmd As New OleDbCommand(createSql, conn)
                    cmd.ExecuteNonQuery()
                End Using

                ' INSERT (バッチトランザクション)
                Dim insertSql = BuildInsertSql(tableName, columnNames)
                Dim rowIdx As Long = 0

                While rowIdx < totalRows
                    If worker IsNot Nothing AndAlso worker.CancellationPending Then
                        Return False
                    End If

                    Using txn = conn.BeginTransaction()
                        Dim batchEnd = Math.Min(rowIdx + BATCH_SIZE, totalRows)

                        For i As Long = rowIdx To batchEnd - 1
                            Using cmd As New OleDbCommand(insertSql, conn, txn)
                                For colIdx As Integer = 0 To columnNames.Count - 1
                                    Dim value As Object = DBNull.Value
                                    If colIdx < data(CInt(i)).Length AndAlso data(CInt(i))(colIdx) IsNot Nothing Then
                                        value = data(CInt(i))(colIdx)
                                    End If
                                    cmd.Parameters.AddWithValue($"@p{colIdx}", value)
                                Next
                                cmd.ExecuteNonQuery()
                            End Using
                        Next

                        txn.Commit()
                    End Using

                    rowIdx += BATCH_SIZE

                    ' 進捗報告
                    If worker IsNot Nothing Then
                        Dim processed = Math.Min(rowIdx, totalRows)
                        Dim pct As Integer = CInt(If(totalRows > 0, processed * 100 \ totalRows, 100))
                        worker.ReportProgress(pct,
                            New ExportProgressDialog.ProgressInfo(tableName, processed, totalRows))
                    End If
                End While
            End Using

            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("AccessExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>
    ''' ACE ドライバが利用可能か確認
    ''' </summary>
    Private Shared Function IsAceDriverAvailable() As Boolean
        Try
            Dim factory = System.Data.Common.DbProviderFactories.GetFactory("System.Data.OleDb")
            Using conn = factory.CreateConnection()
                conn.ConnectionString = $"Provider={ACE_PROVIDER};Data Source=:memory:;"
                ' ACE ドライバの存在をレジストリから確認
                Dim key = Microsoft.Win32.Registry.ClassesRoot.OpenSubKey($"{ACE_PROVIDER}\\CLSID")
                Return key IsNot Nothing
            End Using
        Catch
            Return False
        End Try
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
        If columnTypes Is Nothing OrElse colIdx >= columnTypes.Length Then Return "MEMO"

        Dim oracleType = columnTypes(colIdx).Trim().ToUpperInvariant()
        Dim baseName = oracleType.Split("("c)(0).Trim()

        Select Case baseName
            Case "NUMBER"
                If oracleType.Contains(",") Then Return "DOUBLE"
                Return "LONG"
            Case "DATE", "TIMESTAMP"
                Return "DATETIME"
            Case "VARCHAR2", "NVARCHAR2", "CHAR", "NCHAR"
                Return "MEMO"
            Case "CLOB", "NCLOB", "LONG"
                Return "MEMO"
            Case "BINARY_FLOAT", "BINARY_DOUBLE"
                Return "DOUBLE"
            Case Else
                Return "MEMO"
        End Select
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
