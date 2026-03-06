Imports Microsoft.Data.SqlClient
Imports System.Data

''' <summary>
''' SQL Server 直接出力ロジック
'''
''' SqlBulkCopy で高速一括挿入。
''' DataTable を 1000 行単位でバッチ処理してメモリ効率を確保。
''' テーブル存在時は DROP→再作成。
''' </summary>
Public Class SqlServerExportLogic

    Private Const BATCH_SIZE As Integer = 1000

    ''' <summary>
    ''' テーブルデータを SQL Server に直接出力
    ''' </summary>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  schema As String,
                                  tableName As String,
                                  connectionString As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count
            Dim fullTableName = $"[{schema.Replace("]", "]]")}].[{tableName.Replace("]", "]]")}]"

            Using conn As New SqlConnection(connectionString)
                conn.Open()

                ' スキーマが存在しなければ作成
                EnsureSchema(conn, schema)

                ' テーブルが存在する場合は DROP
                DropTableIfExists(conn, fullTableName)

                ' CREATE TABLE
                Dim createSql = BuildCreateTableSql(fullTableName, columnNames, columnTypes)
                Using cmd As New SqlCommand(createSql, conn)
                    cmd.ExecuteNonQuery()
                End Using

                ' SqlBulkCopy でバッチ挿入
                Using bulk As New SqlBulkCopy(conn)
                    bulk.DestinationTableName = fullTableName
                    bulk.BatchSize = BATCH_SIZE
                    bulk.BulkCopyTimeout = 600

                    ' カラムマッピング
                    For i As Integer = 0 To columnNames.Count - 1
                        bulk.ColumnMappings.Add(columnNames(i), columnNames(i))
                    Next

                    ' バッチ単位で DataTable を作成して書き込み
                    Dim rowIdx As Long = 0
                    While rowIdx < totalRows
                        If worker IsNot Nothing AndAlso worker.CancellationPending Then
                            Return False
                        End If

                        Dim batchEnd = CInt(Math.Min(rowIdx + BATCH_SIZE, totalRows))
                        Dim dt = BuildDataTable(data, columnNames, columnTypes, CInt(rowIdx), batchEnd)
                        bulk.WriteToServer(dt)
                        dt.Dispose()

                        rowIdx = batchEnd

                        ' 進捗報告
                        If worker IsNot Nothing Then
                            Dim pct As Integer = CInt(If(totalRows > 0, rowIdx * 100 \ totalRows, 100))
                            worker.ReportProgress(pct,
                                New ExportProgressDialog.ProgressInfo($"{schema}.{tableName}", rowIdx, totalRows))
                        End If
                    End While
                End Using
            End Using

            Return True

        Catch ex As Exception
            Throw New Exception(Loc.SF("SqlServerExport_Error", ex.Message), ex)
        End Try
    End Function

    Private Shared Sub EnsureSchema(conn As SqlConnection, schema As String)
        Try
            Dim sql = $"IF NOT EXISTS (SELECT 1 FROM sys.schemas WHERE name = @s) EXEC('CREATE SCHEMA [{schema.Replace("]", "]]")}]')"
            Using cmd As New SqlCommand(sql, conn)
                cmd.Parameters.AddWithValue("@s", schema)
                cmd.ExecuteNonQuery()
            End Using
        Catch
            ' スキーマ作成失敗は無視 (dbo で作成される場合がある)
        End Try
    End Sub

    Private Shared Sub DropTableIfExists(conn As SqlConnection, fullTableName As String)
        Using cmd As New SqlCommand($"IF OBJECT_ID('{fullTableName.Replace("'", "''")}', 'U') IS NOT NULL DROP TABLE {fullTableName}", conn)
            cmd.ExecuteNonQuery()
        End Using
    End Sub

    Private Shared Function BuildCreateTableSql(fullTableName As String, columnNames As List(Of String), columnTypes As String()) As String
        Dim sb As New System.Text.StringBuilder()
        sb.Append($"CREATE TABLE {fullTableName} (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            sb.Append($"[{columnNames(i).Replace("]", "]]")}] ")
            sb.Append(ExportHelper.MapOracleType(If(columnTypes IsNot Nothing AndAlso i < columnTypes.Length, columnTypes(i), "VARCHAR2(4000)"), ExportHelper.DBMS_SQLSERVER))
            sb.Append(" NULL")
        Next

        sb.Append(")")
        Return sb.ToString()
    End Function

    Private Shared Function BuildDataTable(data As List(Of String()), columnNames As List(Of String),
                                            columnTypes As String(), startIdx As Integer, endIdx As Integer) As DataTable
        Dim dt As New DataTable()

        ' カラム定義
        For i As Integer = 0 To columnNames.Count - 1
            dt.Columns.Add(columnNames(i), GetType(String))
        Next

        ' データ行
        For rowIdx As Integer = startIdx To endIdx - 1
            Dim row = data(rowIdx)
            Dim dr = dt.NewRow()
            For colIdx As Integer = 0 To columnNames.Count - 1
                If colIdx < row.Length AndAlso row(colIdx) IsNot Nothing Then
                    dr(colIdx) = row(colIdx)
                Else
                    dr(colIdx) = DBNull.Value
                End If
            Next
            dt.Rows.Add(dr)
        Next

        Return dt
    End Function

End Class
