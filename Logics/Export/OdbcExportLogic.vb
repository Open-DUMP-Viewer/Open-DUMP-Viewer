Imports System.Data.Odbc

''' <summary>
''' ODBC 汎用エクスポートロジック
'''
''' ODBC 接続経由で任意のデータベースにテーブルデータを出力。
''' CREATE TABLE + パラメータ化 INSERT (1000行単位トランザクション)。
''' 汎用 SQL 型 (DECIMAL, DOUBLE, DATETIME, VARCHAR) を使用。行データの日付・時刻は SQL 用の固定の書式
''' (Open_NativeParser.DATE_FMT_SLASH、ParseDump の sqlDateFormat) で解析した値であること。
''' </summary>
Public Class OdbcExportLogic

    Private Const BATCH_SIZE As Integer = 1000

    ''' <summary>
    ''' テーブルデータを ODBC 接続先に出力
    ''' </summary>
    ''' <param name="data">行データ (エクスポートの書き手と同じ値。日付は SQL 用の固定の書式)</param>
    ''' <param name="marks">値の形を持たない値・上限を超えた値の位置 (NULL にする)</param>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  tableName As String,
                                  connectionString As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                  Optional marks As Open_NativeParser.ValueMarks = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count

            ' 列の種類 (値の渡し方)
            Dim kinds(columnNames.Count - 1) As SqlDialect.SqlKind
            For i As Integer = 0 To columnNames.Count - 1
                Dim t = If(columnTypes IsNot Nothing AndAlso i < columnTypes.Length, columnTypes(i), Nothing)
                kinds(i) = If(t Is Nothing, SqlDialect.SqlKind.Text, SqlDialect.KindOf(t, SqlDialect.ParseOraType(t)))
            Next

            Using conn As New OdbcConnection(connectionString)
                conn.Open()

                ' DROP TABLE IF EXISTS
                Dim safeName = tableName.Replace("""", """""")
                Dim dropSql = $"DROP TABLE IF EXISTS ""{safeName}"""
                Try
                    Using cmd As New OdbcCommand(dropSql, conn)
                        cmd.ExecuteNonQuery()
                    End Using
                Catch
                    ' DROP TABLE IF EXISTS 未対応の ODBC ドライバでは無視
                End Try

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
                                Dim row = data(CInt(i))
                                For colIdx As Integer = 0 To columnNames.Count - 1
                                    Dim raw As String = Nothing
                                    If colIdx < row.Length Then raw = row(colIdx)
                                    Dim state = If(marks Is Nothing, Open_NativeParser.ValueState.Value, marks.StateOf(row, colIdx))
                                    cmd.Parameters.AddWithValue($"@p{colIdx}", ToParameterValue(raw, kinds(colIdx), state))
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
    Friend Shared Function BuildCreateTableSql(tableName As String, columnNames As List(Of String), columnTypes As String()) As String
        Dim sb As New System.Text.StringBuilder()
        Dim safeName = tableName.Replace("""", """""")
        sb.Append($"CREATE TABLE ""{safeName}"" (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            Dim safeCol = columnNames(i).Replace("""", """""")
            sb.Append($"""{safeCol}"" ")
            sb.Append(MapToGenericType(If(columnTypes IsNot Nothing AndAlso i < columnTypes.Length, columnTypes(i), Nothing)))
        Next

        sb.Append(")")
        Return sb.ToString()
    End Function

    ''' <summary>
    ''' Oracle 型を汎用 SQL 型にマッピング。
    ''' 精度付きの NUMBER は DECIMAL(p,s) (値を丸めない。位取りの写し方は SQL エクスポートの SQL Server と同じ)、
    ''' 精度なしの NUMBER・FLOAT・BINARY_FLOAT/DOUBLE は DOUBLE、DATE / TIMESTAMP は DATETIME、
    ''' タイムゾーン付きの時刻・INTERVAL・その他は文字列
    ''' </summary>
    Friend Shared Function MapToGenericType(oracleType As String) As String
        If String.IsNullOrEmpty(oracleType) Then Return "VARCHAR(4000)"
        Dim t = SqlDialect.ParseOraType(oracleType)
        Select Case SqlDialect.KindOf(oracleType, t)
            Case SqlDialect.SqlKind.Num
                If t.NArgs = 0 Then Return "DOUBLE"
                ' SQL Server 方言と同じ DECIMAL(p,s) (精度 38 まで、負の位取りは整数の桁に)
                Return SqlDialect.MapColumnType("", oracleType, ExportHelper.DBMS_SQLSERVER, Nothing, Nothing, False, True)
            Case SqlDialect.SqlKind.FloatNum, SqlDialect.SqlKind.BinFloat, SqlDialect.SqlKind.BinDouble
                Return "DOUBLE"
            Case SqlDialect.SqlKind.DateKind, SqlDialect.SqlKind.Timestamp
                Return "DATETIME"
            Case Else
                Return "VARCHAR(4000)"
        End Select
    End Function

    ''' <summary>
    ''' 1 つの値を ODBC のパラメータにする。DATE / TIMESTAMP は日時 (固定の書式から。表せない日付は文字列のまま渡して
    ''' 接続先に判断させる)。値の形を持たない値 (印)・上限を超えて全体を持てなかった値は値として書かず NULL
    ''' </summary>
    Friend Shared Function ToParameterValue(raw As String, kind As SqlDialect.SqlKind, state As Open_NativeParser.ValueState) As Object
        If raw Is Nothing OrElse state <> Open_NativeParser.ValueState.Value Then Return DBNull.Value
        If kind = SqlDialect.SqlKind.DateKind OrElse kind = SqlDialect.SqlKind.Timestamp Then
            Dim rest As String = Nothing
            Dim t = SqlDialect.ParseSqlDateTime(raw, rest)
            If t IsNot Nothing AndAlso rest.Length = 0 AndAlso t.Year >= 1 AndAlso t.Year <= 9999 Then
                Try
                    Dim ticks As Long = 0
                    If t.Frac.Length > 0 Then
                        ticks = Long.Parse(t.Frac.PadRight(7, "0"c).Substring(0, 7), Globalization.CultureInfo.InvariantCulture)
                    End If
                    Return New DateTime(t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second).AddTicks(ticks)
                Catch
                    ' 表せない日付: 文字列のまま
                End Try
            End If
        End If
        Return raw
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
