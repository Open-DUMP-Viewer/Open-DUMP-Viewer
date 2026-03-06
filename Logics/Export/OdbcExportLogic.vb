Imports System.Data.Odbc

''' <summary>
''' ODBC 汎用エクスポートロジック
'''
''' ODBC 接続経由で任意のデータベースにテーブルデータを出力。
''' CREATE TABLE + パラメータ化 INSERT (1000行単位トランザクション)。
''' 汎用 SQL 型 (VARCHAR, DOUBLE, DATETIME) を使用。
''' </summary>
Public Class OdbcExportLogic

    Private Const BATCH_SIZE As Integer = 1000

    ''' <summary>
    ''' テーブルデータを ODBC 接続先に出力
    ''' </summary>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  tableName As String,
                                  connectionString As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count

            Using conn As New OdbcConnection(connectionString)
                conn.Open()

                ' CREATE TABLE
                Dim createSql = BuildCreateTableSql(tableName, columnNames, columnTypes)
                Using cmd As New OdbcCommand(createSql, conn)
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
                            Using cmd As New OdbcCommand(insertSql, conn, txn)
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
            Throw New Exception(Loc.SF("OdbcExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>
    ''' CREATE TABLE SQL を構築 (汎用 SQL 型)
    ''' </summary>
    Private Shared Function BuildCreateTableSql(tableName As String, columnNames As List(Of String), columnTypes As String()) As String
        Dim sb As New System.Text.StringBuilder()
        Dim safeName = tableName.Replace("""", """""")
        sb.Append($"CREATE TABLE ""{safeName}"" (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            Dim safeCol = columnNames(i).Replace("""", """""")
            sb.Append($"""{safeCol}"" ")
            sb.Append(MapToGenericType(columnTypes, i))
        Next

        sb.Append(")")
        Return sb.ToString()
    End Function

    ''' <summary>
    ''' Oracle 型を汎用 SQL 型にマッピング
    ''' </summary>
    Private Shared Function MapToGenericType(columnTypes As String(), colIdx As Integer) As String
        If columnTypes Is Nothing OrElse colIdx >= columnTypes.Length Then Return "VARCHAR(4000)"

        Dim oracleType = columnTypes(colIdx).Trim().ToUpperInvariant()
        Dim baseName = oracleType.Split("("c)(0).Trim()

        Select Case baseName
            Case "NUMBER", "BINARY_FLOAT", "BINARY_DOUBLE"
                Return "DOUBLE"
            Case "DATE", "TIMESTAMP"
                Return "DATETIME"
            Case Else
                Return "VARCHAR(4000)"
        End Select
    End Function

    ''' <summary>
    ''' INSERT INTO SQL を構築 (ODBC パラメータプレースホルダ: ?)
    ''' </summary>
    Private Shared Function BuildInsertSql(tableName As String, columnNames As List(Of String)) As String
        Dim sb As New System.Text.StringBuilder()
        Dim safeName = tableName.Replace("""", """""")
        sb.Append($"INSERT INTO ""{safeName}"" (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            Dim safeCol = columnNames(i).Replace("""", """""")
            sb.Append($"""{safeCol}""")
        Next

        sb.Append(") VALUES (")
        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            sb.Append("?")
        Next
        sb.Append(")")

        Return sb.ToString()
    End Function

End Class
