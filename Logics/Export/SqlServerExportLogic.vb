Imports Microsoft.Data.SqlClient
Imports System.Data
Imports System.Data.SqlTypes
Imports System.Globalization

''' <summary>
''' SQL Server 直接出力ロジック
'''
''' SqlBulkCopy で高速一括挿入。
''' DataTable を 1000 行単位でバッチ処理してメモリ効率を確保。
''' テーブル存在時は DROP→再作成。
''' 数値の列は値を型付きで渡す (DECIMAL は SqlDecimal、FLOAT は Double、REAL は Single。文字列のまま渡すと
''' SqlBulkCopy が利用者のカルチャで読み、ドイツ語の書式では 1234.56 が 123456 になる。System.Decimal を経ると 29 桁目から落ちる)。
''' 精度なしの NUMBER は全行の値から丸めずに入る DECIMAL(38,s) を選ぶ (PlanBareNumber)
''' </summary>
Public Class SqlServerExportLogic

    Private Const BATCH_SIZE As Integer = 1000
    ''' <summary>SQL Server の DECIMAL の最大精度</summary>
    Private Const MAX_DEC As Integer = 38

    ''' <summary>DataTable に入れる値の型</summary>
    Friend Enum ValueKind
        ''' <summary>文字列のまま (日付・文字列・バイナリなど。これまでどおり)</summary>
        Text
        ''' <summary>DECIMAL(p,s): SqlDecimal (38 桁まで落とさない)</summary>
        Dec
        ''' <summary>FLOAT(53): Double</summary>
        Dbl
        ''' <summary>REAL: Single</summary>
        Sgl
    End Enum

    ''' <summary>列の書き方 (CREATE TABLE の型と値の型) と、値を変えて書いた数</summary>
    Friend NotInheritable Class ColumnPlan
        Public Name As String
        Public OracleType As String
        Public SqlType As String
        Public Kind As ValueKind
        ''' <summary>Dec: 位取り</summary>
        Public Scale As Integer
        ''' <summary>Dec: 整数部に入る桁 (p - s)</summary>
        Public IntDigits As Integer
        ''' <summary>FLOAT(p) / 精度なし NUMBER を FLOAT(53) に: 有効数字 15 桁を超える値を数える</summary>
        Public CountApprox As Boolean
        Public Rounded As Long
        Public RoundedExample As String
        Public Approx As Long
        Public ApproxExample As String
        Public Nulled As Long
        Public NulledExample As String
    End Class

    ''' <summary>
    ''' テーブルデータを SQL Server に直接出力
    ''' </summary>
    ''' <param name="data">行データ (エクスポートの書き手と同じ値: LOB・LONG は全体)</param>
    ''' <param name="marks">値の形を持たない値・上限を超えた値の位置 (値として書かず NULL にする)</param>
    ''' <param name="notes">元の値のとおりには書けなかった列の説明を足す先 (丸め・FLOAT(53)・NULL。Nothing なら数えるだけ)</param>
    Public Shared Function Export(data As List(Of String()),
                                  columnNames As List(Of String),
                                  columnTypes As String(),
                                  schema As String,
                                  tableName As String,
                                  connectionString As String,
                                  Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                  Optional marks As Open_NativeParser.ValueMarks = Nothing,
                                  Optional notes As List(Of String) = Nothing) As Boolean
        Try
            Dim totalRows As Long = data.Count
            Dim fullTableName = $"[{schema.Replace("]", "]]")}].[{tableName.Replace("]", "]]")}]"
            Dim plans = PlanColumns(data, columnNames, columnTypes, marks)

            Using conn As New SqlConnection(connectionString)
                conn.Open()

                ' スキーマが存在しなければ作成
                EnsureSchema(conn, schema)

                ' テーブルが存在する場合は DROP
                DropTableIfExists(conn, fullTableName)

                ' CREATE TABLE
                Dim createSql = BuildCreateTableSql(fullTableName, columnNames, plans)
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
                        Dim dt = BuildDataTable(data, columnNames, plans, CInt(rowIdx), batchEnd, marks)
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

            If notes IsNot Nothing Then DescribeChanges(schema, tableName, plans, notes)
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

#Region "列の書き方"
    ''' <summary>列ごとの型と値の渡し方を決める (精度なしの NUMBER は全行の値を見る)</summary>
    Friend Shared Function PlanColumns(data As List(Of String()), columnNames As List(Of String), columnTypes As String(),
                                       marks As Open_NativeParser.ValueMarks) As ColumnPlan()
        Dim plans(columnNames.Count - 1) As ColumnPlan
        For i = 0 To columnNames.Count - 1
            Dim ts = If(columnTypes IsNot Nothing AndAlso i < columnTypes.Length, columnTypes(i), "VARCHAR2(4000)")
            Dim info As SqlDialect.SqlColumnInfo = Nothing
            Dim p As New ColumnPlan With {.Name = columnNames(i), .OracleType = ts}
            p.SqlType = SqlDialect.MapColumnType("", ts, ExportHelper.DBMS_SQLSERVER, info, Nothing, False, True)
            Select Case info.Kind
                Case SqlDialect.SqlKind.Num
                    p.Kind = ValueKind.Dec
                    If SqlDialect.ParseOraType(ts).NArgs = 0 Then
                        PlanBareNumber(p, data, i, marks)
                    Else
                        ' NUMBER(p,s): 型のとおり (SQL Server の 38 桁に詰めた型に入らない値は誤りになる。これまでどおり)
                        p.Scale = info.DecS
                        p.IntDigits = info.DecP - info.DecS
                    End If
                Case SqlDialect.SqlKind.FloatNum
                    p.Kind = ValueKind.Dbl
                    p.CountApprox = True
                Case SqlDialect.SqlKind.BinDouble
                    p.Kind = ValueKind.Dbl       ' %.17g: 元の倍精度の値に戻る
                Case SqlDialect.SqlKind.BinFloat
                    p.Kind = ValueKind.Sgl       ' %.10g: 元の単精度の値に戻る
                Case Else
                    p.Kind = ValueKind.Text
            End Select
            plans(i) = p
        Next
        Return plans
    End Function

    ''' <summary>
    ''' 精度なしの NUMBER: 全行の値の整数部の桁 I・小数部の桁 F の最大から型を選ぶ。
    '''   I + F &lt;= 38 → DECIMAL(38,F)。丸めない (整数部は 38-F 桁まで入る)
    '''   I &lt;= 38 &lt; I + F → DECIMAL(38,38-I)。小数部の多い値を丸めて数える (繰り上がりで整数部が増える値があれば 1 桁詰める)
    '''   I &gt; 38 → FLOAT(53)。DECIMAL に入らない。有効数字 15 桁を超える値を数える
    ''' 値が 1 つも無い列は従来どおり DECIMAL(38,10)
    ''' </summary>
    Private Shared Sub PlanBareNumber(p As ColumnPlan, data As List(Of String()), col As Integer, marks As Open_NativeParser.ValueMarks)
        Dim maxInt = 0, maxFrac = 0, any = False
        For Each row In data
            Dim v = ValueAt(row, col, marks)
            If String.IsNullOrEmpty(v) OrElse Not SqlDialect.IsNumericLiteral(v) Then Continue For
            Dim intd, fracd, sig As Integer
            SqlDialect.NumberDigits(v, intd, fracd, sig)
            any = True
            If intd > maxInt Then maxInt = intd
            If fracd > maxFrac Then maxFrac = fracd
        Next
        If Not any Then
            SetDecimal(p, 10)
        ElseIf maxInt > MAX_DEC Then
            p.Kind = ValueKind.Dbl
            p.SqlType = "FLOAT(53)"
            p.CountApprox = True
        ElseIf maxInt + maxFrac <= MAX_DEC Then
            SetDecimal(p, maxFrac)
        Else
            Dim scale = MAX_DEC - maxInt
            ' 丸めの繰り上がりで整数部が maxInt 桁を超える値 (9.99…) があれば、位取りを 1 つ減らす
            For Each row In data
                Dim v = ValueAt(row, col, marks)
                If String.IsNullOrEmpty(v) Then Continue For
                Dim changed As Boolean
                Dim r = RoundToScale(v, scale, changed)
                If changed AndAlso r IsNot Nothing AndAlso IntDigitsOf(r) > maxInt Then
                    scale -= 1
                    Exit For
                End If
            Next
            SetDecimal(p, Math.Max(scale, 0))
        End If
    End Sub

    Private Shared Sub SetDecimal(p As ColumnPlan, scale As Integer)
        p.Kind = ValueKind.Dec
        p.Scale = scale
        p.IntDigits = MAX_DEC - scale
        p.SqlType = $"DECIMAL({MAX_DEC},{scale})"
    End Sub

    Private Shared Function ValueAt(row As String(), col As Integer, marks As Open_NativeParser.ValueMarks) As String
        If row Is Nothing OrElse col >= row.Length Then Return Nothing
        If marks IsNot Nothing AndAlso marks.StateOf(row, col) <> Open_NativeParser.ValueState.Value Then Return Nothing
        Return row(col)
    End Function

    Private Shared Function IntDigitsOf(v As String) As Integer
        Dim intd, fracd, sig As Integer
        SqlDialect.NumberDigits(v, intd, fracd, sig)
        Return intd
    End Function

    ''' <summary>
    ''' 10 進の文字列 (符号・数字・小数点 1 つ。指数なし = 解析エンジンが NUMBER を渡す形) を小数第 scale 位に丸める。
    ''' 5 は 0 から遠い方へ (SQL Server が DECIMAL に変換するときと同じ)。末尾の 0 と先頭の余分な 0 は落とす。
    ''' 丸めて値が変わったら changed = True。この形でなければ Nothing
    ''' </summary>
    Friend Shared Function RoundToScale(v As String, scale As Integer, ByRef changed As Boolean) As String
        changed = False
        If String.IsNullOrEmpty(v) Then Return Nothing
        Dim pos = 0
        Dim neg = False
        If v(0) = "-"c OrElse v(0) = "+"c Then
            neg = (v(0) = "-"c)
            pos = 1
        End If
        Dim dot = v.IndexOf("."c, pos)
        Dim ip = If(dot < 0, v.Substring(pos), v.Substring(pos, dot - pos))
        Dim fp = If(dot < 0, "", v.Substring(dot + 1))
        If ip.Length + fp.Length = 0 Then Return Nothing
        For Each c In ip & fp
            If Not SqlDialect.IsAsciiDigit(c) Then Return Nothing
        Next
        fp = fp.TrimEnd("0"c)
        If fp.Length > scale Then
            changed = True
            Dim up = fp(scale) >= "5"c
            Dim digits = (ip & fp.Substring(0, scale)).ToCharArray()
            If up Then
                Dim k = digits.Length - 1
                While k >= 0
                    If digits(k) = "9"c Then
                        digits(k) = "0"c
                        k -= 1
                    Else
                        digits(k) = ChrW(AscW(digits(k)) + 1)
                        Exit While
                    End If
                End While
                Dim s = New String(digits)
                If k < 0 Then s = "1" & s
                ip = s.Substring(0, s.Length - scale)
                fp = s.Substring(s.Length - scale)
            Else
                fp = fp.Substring(0, scale)
            End If
        End If
        ip = ip.TrimStart("0"c)
        If ip.Length = 0 Then ip = "0"
        fp = fp.TrimEnd("0"c)
        Dim r = If(fp.Length > 0, ip & "." & fp, ip)
        If r = "0" Then neg = False
        Return If(neg, "-" & r, r)
    End Function
#End Region

    Private Shared Function BuildCreateTableSql(fullTableName As String, columnNames As List(Of String), plans As ColumnPlan()) As String
        Dim sb As New System.Text.StringBuilder()
        sb.Append($"CREATE TABLE {fullTableName} (")

        For i As Integer = 0 To columnNames.Count - 1
            If i > 0 Then sb.Append(", ")
            sb.Append($"[{columnNames(i).Replace("]", "]]")}] ")
            sb.Append(plans(i).SqlType)
            sb.Append(" NULL")
        Next

        sb.Append(")")
        Return sb.ToString()
    End Function

    Private Shared Function BuildDataTable(data As List(Of String()), columnNames As List(Of String),
                                            plans As ColumnPlan(), startIdx As Integer, endIdx As Integer,
                                            marks As Open_NativeParser.ValueMarks) As DataTable
        Dim dt As New DataTable()

        ' カラム定義 (数値は型付き。文字列で渡すと SqlBulkCopy が利用者のカルチャで読む)
        For i As Integer = 0 To columnNames.Count - 1
            Dim t As Type
            Select Case plans(i).Kind
                Case ValueKind.Dec : t = GetType(SqlDecimal)
                Case ValueKind.Dbl : t = GetType(Double)
                Case ValueKind.Sgl : t = GetType(Single)
                Case Else : t = GetType(String)
            End Select
            dt.Columns.Add(columnNames(i), t)
        Next

        ' データ行
        For rowIdx As Integer = startIdx To endIdx - 1
            Dim row = data(rowIdx)
            Dim dr = dt.NewRow()
            For colIdx As Integer = 0 To columnNames.Count - 1
                ' 値の形を持たない値の印 (%BLOB% など) は値ではない
                Dim v = ValueAt(row, colIdx, marks)
                dr(colIdx) = If(v Is Nothing, DBNull.Value, ToBulkValue(v, plans(colIdx)))
            Next
            dt.Rows.Add(dr)
        Next

        Return dt
    End Function

    ''' <summary>1 つの値を DataTable に入れる形にする (数値はカルチャに依らず読む)</summary>
    Private Shared Function ToBulkValue(v As String, p As ColumnPlan) As Object
        Select Case p.Kind
            Case ValueKind.Dec
                If v.Length = 0 Then Return DBNull.Value
                Dim changed As Boolean
                Dim r = RoundToScale(v, p.Scale, changed)
                If r Is Nothing Then
                    Throw New InvalidOperationException($"column {p.Name}: '{SqlDialect.Utf8Prefix(v, 40)}' is not a number")
                End If
                If IntDigitsOf(r) > p.IntDigits Then
                    Throw New InvalidOperationException($"column {p.Name}: {v} does not fit {p.SqlType}")
                End If
                If changed Then
                    p.Rounded += 1
                    If p.RoundedExample Is Nothing Then p.RoundedExample = $"{v} -> {r}"
                End If
                Return SqlDecimal.Parse(r)

            Case ValueKind.Dbl, ValueKind.Sgl
                If v.Length = 0 Then Return DBNull.Value
                If v = "NaN" OrElse v = "Inf" OrElse v = "-Inf" Then
                    ' SQL Server の FLOAT / REAL は NaN・無限大を持てない (SQL エクスポートも NULL にして NOTE を書く)
                    p.Nulled += 1
                    If p.NulledExample Is Nothing Then p.NulledExample = v
                    Return DBNull.Value
                End If
                If p.Kind = ValueKind.Sgl Then
                    ' 単精度は単精度として直接読む (倍精度を経ると、まれに最も近い単精度の値からずれる)
                    Dim f As Single
                    If Not Single.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, f) Then
                        Throw New InvalidOperationException($"column {p.Name}: '{SqlDialect.Utf8Prefix(v, 40)}' is not a number")
                    End If
                    Return f
                End If
                Dim d As Double
                If Not Double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, d) Then
                    Throw New InvalidOperationException($"column {p.Name}: '{SqlDialect.Utf8Prefix(v, 40)}' is not a number")
                End If
                If p.CountApprox Then
                    Dim intd, fracd, sig As Integer
                    SqlDialect.NumberDigits(v, intd, fracd, sig)
                    If sig > 15 Then
                        p.Approx += 1
                        If p.ApproxExample Is Nothing Then p.ApproxExample = $"{v} -> {d.ToString("R", CultureInfo.InvariantCulture)}"
                    End If
                End If
                Return d

            Case Else
                Return v
        End Select
    End Function

    ''' <summary>元の値のとおりには書けなかった列を、利用者に見せる文にして notes に足す</summary>
    Private Shared Sub DescribeChanges(schema As String, tableName As String, plans As ColumnPlan(), notes As List(Of String))
        For Each p In plans
            Dim col = $"{schema}.{tableName}.{p.Name}"
            If p.Rounded > 0 Then notes.Add(Loc.SF("SqlServerExport_NoteRounded", col, p.SqlType, p.Rounded, p.Scale, p.RoundedExample))
            If p.Approx > 0 Then notes.Add(Loc.SF("SqlServerExport_NoteApprox", col, p.OracleType, p.SqlType, p.Approx, p.ApproxExample))
            If p.Nulled > 0 Then notes.Add(Loc.SF("SqlServerExport_NoteNulled", col, p.Nulled, p.NulledExample))
        Next
    End Sub

End Class
