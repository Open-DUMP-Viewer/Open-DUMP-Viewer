Imports System.IO
Imports System.Text

''' <summary>
''' SQL INSERT 文エクスポートロジック
'''
''' 2つのエクスポート経路を提供:
''' 1. C DLL 経由 (Workspace): DUMP ファイルを再パースしながらストリーミング出力
''' 2. VB.NET インメモリ (TablePreview の絞り込み後のデータ、裸 NUMBER の整数推定): 行データから書き出し
'''
''' 対応 DBMS: Oracle, PostgreSQL, MySQL, SQL Server
'''
''' 2 の書き手 (SqlTableWriter) は DLL の SQL 出力 (odv_sql.c) と同じ規則で書く (型の対応・値の書き方・制約・
''' NOTE の文言・ファイルの並び。規則の正本は SQL エクスポートの型の対応の仕様 TYPE_MAPPING_SPEC)。
''' 同じ表・同じ値は、どちらの経路でも同じ SQL になる。行データの日付・時刻は SQL 用の固定の書式
''' (Open_NativeParser.DATE_FMT_SLASH) で解析したものを渡すこと。
''' </summary>
Public Class SqlExportLogic

    ''' <summary>
    ''' C DLL 経由で SQL エクスポート (全行・ストリーミング)
    ''' </summary>
    ''' <param name="databaseName">SQL Server: 先頭に USE [DB] を書く (空なら書かない。VB の書き手と同じ)</param>
    Public Shared Function ExportFromDump(ctx As ExportHelper.TableExportContext, outputPath As String, dbmsType As Integer,
                                          Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                          Optional databaseName As String = Nothing) As Boolean
        Dim progressAction As Action(Of Long, String, Integer) = Nothing
        If worker IsNot Nothing Then
            progressAction = Sub(rows As Long, tbl As String, pct As Integer)
                                 worker.ReportProgress(0,
                                     New ExportProgressDialog.ProgressInfo(tbl, rows, ctx.RowCount))
                             End Sub
        End If

        Dim useDb = (dbmsType = ExportHelper.DBMS_SQLSERVER AndAlso Not String.IsNullOrEmpty(databaseName))
        Dim dllPath = If(useDb, outputPath & ".part", outputPath)
        Dim rc = Open_NativeParser.ExportSql(ctx.DumpFilePath, ctx.TableName, dllPath, dbmsType,
                                               ctx.Schema, ctx.DataOffset, progressAction, ctx.Partition, ctx.DataOffsets,
                                               ctx.DdlHint)
        If rc <> Open_NativeParser.ODV_OK Then
            If useDb Then TryDelete(dllPath)
            Throw New Exception(Loc.SF("SqlExport_ErrorRc", rc))
        End If
        If useDb Then
            ' USE [DB] + GO を先頭に置き、DLL の出力をその後ろへ写す
            Try
                Using outFs As New FileStream(outputPath, FileMode.Create, FileAccess.Write, FileShare.Read)
                    Dim head = New UTF8Encoding(False).GetBytes($"USE [{databaseName.Replace("]", "]]")}]" & vbLf & "GO" & vbLf & vbLf)
                    outFs.Write(head, 0, head.Length)
                    Using inFs As New FileStream(dllPath, FileMode.Open, FileAccess.Read, FileShare.Read)
                        inFs.CopyTo(outFs)
                    End Using
                End Using
            Finally
                TryDelete(dllPath)
            End Try
        End If
        Return True
    End Function

    Private Shared Sub TryDelete(path As String)
        Try
            If File.Exists(path) Then File.Delete(path)
        Catch
            ' 一時ファイルの削除の失敗は無視
        End Try
    End Sub

    ''' <summary>
    ''' 表の全行を VB の書き手で書く (裸 NUMBER の整数推定のとき。Workspace・一括)。値はエクスポートの書き手と同じ形
    ''' (ODV_VALUES_EXPORT: LOB・LONG は全体)、日付は SQL 用の固定の書式。Data Pump の表の DDL は DLL から読む
    ''' </summary>
    Public Shared Function ExportTableViaWriter(ctx As ExportHelper.TableExportContext, outputPath As String, dbmsType As Integer,
                                                Optional worker As System.ComponentModel.BackgroundWorker = Nothing,
                                                Optional databaseName As String = Nothing) As Boolean
        Dim marks As New Open_NativeParser.ValueMarks()
        Dim rows = ExportHelper.LoadExportRows(ctx, True, marks)
        Dim ddl = ExportHelper.TableDdlForSql(ctx)
        Dim colNames = New List(Of String)(If(ctx.ColumnNames, Array.Empty(Of String)()))
        Return ExportFromData(rows, colNames, ctx.ColumnTypes, ctx.Schema, ctx.TableName, outputPath, dbmsType, worker,
                              ddl, databaseName, marks)
    End Function

    ''' <summary>
    ''' VB.NET インメモリデータから SQL を書き出す (DLL の経路と同じ形)。
    ''' 行データはエクスポートの書き手と同じ値 (ODV_VALUES_EXPORT。ExportHelper.LoadExportRows / RowsForExport) で、
    ''' 日付・時刻は SQL 用の固定の書式 (DATE_FMT_SLASH) で解析した値であること。
    ''' BackgroundWorker から呼び出して進捗報告可能
    ''' </summary>
    ''' <param name="data">行データ (値 Nothing = NULL、String.Empty = NULL でない空)</param>
    ''' <param name="columnTypes">列の Oracle の型名</param>
    ''' <param name="ddl">表の DDL (NOT NULL・DEFAULT・制約・索引・コメント・注記。ExportHelper.TableDdlForSql)。Nothing なら書かない</param>
    ''' <param name="databaseName">SQL Server: 先頭に USE [DB] を書く (空なら書かない)</param>
    ''' <param name="marks">値の形を持たない値・上限を超えた値の位置 (NULL + 行の NOTE にする)</param>
    Public Shared Function ExportFromData(data As List(Of String()),
                                          columnNames As List(Of String),
                                          columnTypes As String(),
                                          schema As String,
                                          tableName As String,
                                          outputPath As String,
                                          dbmsType As Integer,
                                          worker As System.ComponentModel.BackgroundWorker,
                                          ddl As Open_NativeParser.TableDdl,
                                          Optional databaseName As String = Nothing,
                                          Optional marks As Open_NativeParser.ValueMarks = Nothing) As Boolean
        Try
            Dim w As New SqlTableWriter(dbmsType, If(schema, ""), tableName, columnNames, columnTypes,
                                        ddl?.ColumnNotNulls, ddl?.ColumnDefaults, ddl?.ConstraintsJson)
            ' 裸の NUMBER の整数推定 (オプション。Oracle の型はそのまま)
            If ExportOptions.SqlInferInteger AndAlso columnTypes IsNot Nothing Then
                w.TypeOverrides = ExportHelper.InferIntegerTypes(columnTypes, data, ddl?.ColumnDefaults, dbmsType)
            End If
            w.CreateTable = ExportOptions.SqlCreateTable
            w.CreateIndex = ExportOptions.SqlCreateIndex
            w.WriteComments = ExportOptions.SqlWriteComments
            w.WriteInserts = ExportOptions.SqlWriteInserts
            w.Marks = marks

            Using fs As New FileStream(outputPath, FileMode.Create, FileAccess.Write, FileShare.Read)
                Using sw As New StreamWriter(fs, New UTF8Encoding(False))
                    If dbmsType = ExportHelper.DBMS_SQLSERVER AndAlso Not String.IsNullOrEmpty(databaseName) Then
                        sw.Write($"USE [{databaseName.Replace("]", "]]")}]" & vbLf & "GO" & vbLf & vbLf)
                    End If
                    Return w.Write(sw, data, worker)
                End Using
            End Using

        Catch ex As Exception
            Throw New Exception(Loc.SF("SqlExport_Error", ex.Message), ex)
        End Try
    End Function

    ''' <summary>制約情報のデシリアライズ用クラス (constraints_json の 1 要素)</summary>
    Friend Class ConstraintInfo
        ''' <summary>0=PRIMARY KEY, 1=UNIQUE, 2=FOREIGN KEY, 3=CHECK, 4=INDEX, 5=コメント, 6=注記</summary>
        Public Property type As Integer
        Public Property name As String
        Public Property columns As String()
        Public Property ref_schema As String
        Public Property ref_table As String
        Public Property ref_columns As String()
        Public Property condition As String
        Public Property index_expr As String
        Public Property table_comment As String
        Public Property col_comments As Dictionary(Of String, String)
        Public Property notes As String()
        ''' <summary>外部キー: "CASCADE" / "SET NULL"</summary>
        Public Property delete_rule As String
        ''' <summary>"DEFERRABLE" のとき延期可能</summary>
        Public Property deferrable As String
        ''' <summary>"DEFERRED" のとき INITIALLY DEFERRED</summary>
        Public Property deferred As String
        ''' <summary>"DISABLED" のとき無効 (DISABLE で作られた)</summary>
        Public Property status As String
        ''' <summary>
        ''' 一意 (type=1): 制約ではなく CREATE UNIQUE INDEX で作られた一意の索引 (1 / true)。DLL の SQL 出力は
        ''' これを CREATE UNIQUE INDEX で制約より前に書く (同じ列の主キーがその索引を使う。ORA-02261 を避ける)
        ''' </summary>
        Public Property unique_index As Text.Json.JsonElement?

        ''' <summary>制約でない一意の索引か</summary>
        Public ReadOnly Property IsUniqueIndex As Boolean
            Get
                If type <> 1 OrElse Not unique_index.HasValue Then Return False
                Dim e = unique_index.Value
                Select Case e.ValueKind
                    Case Text.Json.JsonValueKind.True : Return True
                    Case Text.Json.JsonValueKind.Number : Return e.GetDouble() <> 0
                    Case Text.Json.JsonValueKind.String : Return e.GetString() = "1" OrElse String.Equals(e.GetString(), "true", StringComparison.OrdinalIgnoreCase)
                End Select
                Return False
            End Get
        End Property
    End Class

End Class

''' <summary>
''' 1 つの表の SQL を書く (DLL の odv_sql.c の write_sql_file と同じ並び):
''' ファイルの先頭 → "-- Table:" → 型の NOTE → DROP / CREATE TABLE → INSERT (行の NOTE・長い値の PL/SQL) →
''' 制約・索引・コメント・注記 → 外部キー
''' </summary>
Friend NotInheritable Class SqlTableWriter

    Private ReadOnly _dbms As Integer
    Private ReadOnly _schema As String
    Private ReadOnly _table As String
    Private ReadOnly _names As List(Of String)
    Private ReadOnly _types As String()
    ''' <summary>0 = NULL 可、1 = NOT NULL、2 = 主キーの列なので NOT NULL (CREATE TABLE には書かない)</summary>
    Private ReadOnly _notNulls As Integer()
    Private ReadOnly _defaults As String()
    Private ReadOnly _constraints As List(Of SqlExportLogic.ConstraintInfo)
    Private _ci As SqlDialect.SqlColumnInfo()
    Private _hasDbTimeZone As Boolean

    ''' <summary>列ごとの書き出し先の型 (裸 NUMBER の整数推定)。Nothing・空の要素は通常の写像</summary>
    Public Property TypeOverrides As String()
    Public Property CreateTable As Boolean = True
    Public Property CreateIndex As Boolean = True
    Public Property WriteComments As Boolean = True
    Public Property WriteInserts As Boolean = True
    ''' <summary>
    ''' 行データのうち値でないもの (値の形を持たない印・上限を超えた値) の位置 (ParseDump の exportValues)。
    ''' それらは値として書かず NULL + 行の NOTE (DLL の put_value と同じ)
    ''' </summary>
    Public Property Marks As Open_NativeParser.ValueMarks

    ' 1 行分
    Private ReadOnly _decl As New StringBuilder()
    Private ReadOnly _pre As New StringBuilder()
    Private ReadOnly _post As New StringBuilder()
    Private ReadOnly _rnotes As New StringBuilder()
    Private _plsql As Boolean

    Public Sub New(dbms As Integer, schema As String, table As String, names As List(Of String), types As String(),
                   notNulls As Integer(), defaults As String(), constraintsJson As String)
        _dbms = dbms
        _schema = schema
        _table = table
        _names = If(names, New List(Of String)())
        _types = If(types, Array.Empty(Of String)())
        _notNulls = notNulls
        _defaults = defaults
        _constraints = New List(Of SqlExportLogic.ConstraintInfo)()
        If Not String.IsNullOrEmpty(constraintsJson) AndAlso constraintsJson <> "[]" Then
            Try
                Dim items = Text.Json.JsonSerializer.Deserialize(Of List(Of SqlExportLogic.ConstraintInfo))(constraintsJson)
                If items IsNot Nothing Then _constraints.AddRange(items.Where(Function(c) c IsNot Nothing))
            Catch
                ' 読めない JSON は使わない (制約・コメントを書かない)
            End Try
        End If
    End Sub

#Region "表全体"
    ''' <summary>表の SQL を書く。キャンセルされたら False</summary>
    Public Function Write(sw As TextWriter, data As List(Of String()), worker As System.ComponentModel.BackgroundWorker) As Boolean
        Dim nl = vbLf

        ' ファイルの先頭 (仕様 7 章)
        Select Case _dbms
            Case ExportHelper.DBMS_ORACLE
                ' SQL*Plus: & の置換をしない、文中の空行を許す (H-20)
                sw.Write("SET DEFINE OFF" & nl & "SET SQLBLANKLINES ON" & nl & nl)
            Case ExportHelper.DBMS_POSTGRES
                sw.Write("SET standard_conforming_strings = on;" & nl & nl)
            Case ExportHelper.DBMS_MYSQL
                sw.Write("-- Backslashes in string literals are escaped (\\): run with the default sql_mode, not NO_BACKSLASH_ESCAPES." & nl & nl)
        End Select

        ' DBTIMEZONE がダンプにあるか (TIMESTAMP WITH LOCAL TIME ZONE の値の表示に付いている)
        _hasDbTimeZone = DetectDbTimeZone(data)

        ' 列の型の情報 (値の書き方に使う)
        _ci = New SqlDialect.SqlColumnInfo(_names.Count - 1) {}
        For i = 0 To _names.Count - 1
            If i < _types.Length Then
                MapType(i, Nothing, False)
            Else
                _ci(i) = New SqlDialect.SqlColumnInfo() With {.Kind = SqlDialect.SqlKind.Text}
            End If
        Next

        ' 見出し・型の NOTE・DROP / CREATE TABLE
        Dim head As New StringBuilder()
        head.Append("-- Table: ")
        If _schema.Length > 0 Then head.Append(_schema).Append("."c)
        head.Append(_table).Append(nl)
        Dim ver = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version?.ToString()
        If Not String.IsNullOrEmpty(ver) Then
            head.Append("-- Generated by Open DUMP Viewer for Oracle database v").Append(ver).Append(nl)
        Else
            head.Append("-- Generated by Open DUMP Viewer for Oracle database").Append(nl)
        End If
        Dim tn As New StringBuilder()
        If CreateTable Then
            Dim ddl = BuildCreateTable(tn)
            head.Append(tn).Append(nl).Append(ddl)
        Else
            ' CREATE TABLE を書かなくても、型の NOTE は値に当てはまる
            For i = 0 To Math.Min(_names.Count, _types.Length) - 1
                MapType(i, tn, False)
            Next
            head.Append(tn).Append(nl)
        End If
        sw.Write(head.ToString())

        ' INSERT
        If WriteInserts AndAlso data IsNot Nothing Then
            Dim prefix = BuildPrefix()
            Dim total As Long = data.Count
            For rowIdx As Integer = 0 To data.Count - 1
                If worker IsNot Nothing AndAlso worker.CancellationPending Then Return False
                sw.Write(BuildRow(prefix, data(rowIdx)))
                If worker IsNot Nothing AndAlso (rowIdx Mod 1000 = 0 OrElse rowIdx = total - 1) Then
                    Dim pct As Integer = CInt(If(total > 0, (rowIdx + 1) * 100L \ total, 100))
                    worker.ReportProgress(pct,
                        New ExportProgressDialog.ProgressInfo($"{_schema}.{_table}", rowIdx + 1, total))
                End If
            Next
        End If

        ' 制約・索引・コメント・注記、外部キー (ファイルの最後)
        Dim ddlTail As New StringBuilder()
        Dim fks As New List(Of KeyValuePair(Of String, String))()
        RenderTableDdl(ddlTail, fks)
        Dim tail As New StringBuilder()
        If ddlTail.Length > 0 Then
            tail.Append(nl & "-- Constraints, indexes and comments" & nl)
            tail.Append(nl).Append(ddlTail)
        End If
        If fks.Count > 0 Then
            tail.Append(nl & "-- Foreign keys" & nl & nl)
            ' 名前の付いたものを名前の順、システム名のものはその後に並びのまま (DLL と同じ)
            Dim ordered = fks.Select(Function(kv, idx) New With {kv.Key, kv.Value, idx}).
                OrderBy(Function(x) x.Key, StringComparer.Ordinal).ThenBy(Function(x) x.idx)
            For Each x In ordered
                tail.Append(x.Value)
            Next
        End If
        sw.Write(tail.ToString())
        Return True
    End Function

    ''' <summary>TIMESTAMP WITH LOCAL TIME ZONE の値に DBTIMEZONE の表示が付いているか (無ければダンプに DBTIMEZONE が無い)</summary>
    Private Function DetectDbTimeZone(data As List(Of String())) As Boolean
        Dim cols As New List(Of Integer)()
        For i = 0 To Math.Min(_names.Count, _types.Length) - 1
            Dim t = SqlDialect.ParseOraType(_types(i))
            If SqlDialect.KindOf(_types(i), t) = SqlDialect.SqlKind.TimestampLtz Then cols.Add(i)
        Next
        If cols.Count = 0 OrElse data Is Nothing Then Return True
        For Each row In data
            For Each i In cols
                If i < row.Length AndAlso Not String.IsNullOrEmpty(row(i)) Then
                    Dim rest As String = Nothing
                    Dim t = SqlDialect.ParseSqlDateTime(row(i), rest)
                    If t IsNot Nothing Then Return rest.Trim().Length > 0
                End If
            Next
        Next
        Return True
    End Function

    Private Function MapType(i As Integer, tn As StringBuilder, mysqlText As Boolean) As String
        Dim info As SqlDialect.SqlColumnInfo = Nothing
        Dim overridden = TypeOverrides IsNot Nothing AndAlso i < TypeOverrides.Length AndAlso
                         Not String.IsNullOrEmpty(TypeOverrides(i)) AndAlso _dbms <> ExportHelper.DBMS_ORACLE
        ' 推定した整数型を使う列には精度なし NUMBER の型の NOTE を書かない
        Dim ty = SqlDialect.MapColumnType(_names(i), _types(i), _dbms, info, If(overridden, Nothing, tn), mysqlText, _hasDbTimeZone)
        If overridden Then
            ' 裸 NUMBER の整数推定: 全行が整数で範囲に収まる (値の NOTE は要らない)
            ty = TypeOverrides(i)
            info.DecP = 0
            info.DecS = 0
        End If
        _ci(i) = info
        Return ty
    End Function

    Private Function QName() As String
        Return SqlDialect.QuoteQName(_schema, _table, _dbms)
    End Function
#End Region

#Region "CREATE TABLE と INSERT の前半"
    Private Function BuildCreateTable(tn As StringBuilder) As String
        Dim b As New SqlText()
        Dim nl = vbLf
        Select Case _dbms
            Case ExportHelper.DBMS_ORACLE
                ' PL/SQL ブロック (23ai より前は IF EXISTS が無い)。名前は文字列リテラルの中なので ' を二重に
                b.Append("BEGIN EXECUTE IMMEDIATE 'DROP TABLE ")
                If _schema.Length > 0 Then b.Append(SqlDialect.PlsqlIdent(_schema) & ".")
                b.Append(SqlDialect.PlsqlIdent(_table))
                b.Append(" CASCADE CONSTRAINTS PURGE'; EXCEPTION WHEN OTHERS THEN NULL; END;" & nl & "/" & nl & nl)
            Case ExportHelper.DBMS_MYSQL
                b.Append("DROP TABLE IF EXISTS " & QName() & ";" & nl & nl)
            Case ExportHelper.DBMS_SQLSERVER
                b.Append("IF OBJECT_ID(" & SqlDialect.OtherText(QName(), _dbms) & ", N'U') IS NOT NULL DROP TABLE " & QName() & ";" & nl & nl)
            Case Else
                b.Append("DROP TABLE IF EXISTS " & QName() & " CASCADE;" & nl & nl)
        End Select

        Dim mysqlText = False
        If _dbms = ExportHelper.DBMS_MYSQL Then
            ' MySQL の行の大きさの上限 (全列で 65,535 バイト): 超えるなら長い VARCHAR を TEXT に
            Dim total As Long = 0
            For i = 0 To Math.Min(_names.Count, _types.Length) - 1
                total += SqlDialect.MySqlColBytes(MapType(i, Nothing, False))
            Next
            If total > SqlDialect.MYSQL_ROW_MAX Then
                mysqlText = True
                tn.Append($"-- NOTE: the VARCHAR columns would exceed MySQL's row size limit ({SqlDialect.MYSQL_ROW_MAX} bytes); those longer than 255 characters are TEXT" & nl)
            End If
        End If

        b.Append("CREATE TABLE " & QName() & " (" & nl)
        For i = 0 To _names.Count - 1
            If i > 0 Then b.Append("," & nl)
            b.Append("    ")
            If i < _types.Length Then
                ColDefinition(b, i, tn, mysqlText)
            Else
                b.Append(SqlDialect.QuoteIdent(_names(i), _dbms))
                b.Append(If(_dbms = ExportHelper.DBMS_ORACLE, " VARCHAR2(4000)", " VARCHAR(255)"))
            End If
        Next
        b.Append(nl & ");" & nl & nl)
        Return b.ToString()
    End Function

    ''' <summary>列の定義: 名前 型 [DEFAULT 式] [NOT NULL] (DEFAULT が先。逆は Oracle で ORA-00907: M-32)</summary>
    Private Sub ColDefinition(b As SqlText, i As Integer, tn As StringBuilder, mysqlText As Boolean)
        Dim colName = _names(i)
        b.Append(SqlDialect.QuoteIdent(colName, _dbms))
        b.Append(" "c)
        b.Append(MapType(i, tn, mysqlText))
        Dim dflt = If(_defaults IsNot Nothing AndAlso i < _defaults.Length, _defaults(i), Nothing)
        If Not String.IsNullOrEmpty(dflt) Then
            If _dbms = ExportHelper.DBMS_ORACLE Then
                Dim d = TrimCSpace(dflt)
                If d.Length > 0 Then
                    b.Append(" DEFAULT ")
                    SqlDialect.PutWrapped(b, d)
                End If
            Else
                Dim lit As String = Nothing
                If SqlDialect.DefaultPortable(dflt, lit, _dbms, _ci(i).Kind) Then
                    b.Append(" DEFAULT " & lit)
                ElseIf tn IsNot Nothing Then
                    SqlDialect.NoteCol(tn, colName, $"DEFAULT {dflt} is an Oracle expression and was not written")
                End If
            End If
        End If
        ' 主キーだけの NOT NULL (2) は書かない (主キーが NOT NULL にする。書くと往復で NOT NULL 制約が増える)
        If _notNulls IsNot Nothing AndAlso i < _notNulls.Length AndAlso _notNulls(i) = 1 Then b.Append(" NOT NULL")
    End Sub

    Private Shared Function TrimCSpace(s As String) As String
        Dim a = 0, e = s.Length
        While a < e AndAlso SqlDialect.IsCSpace(s(a))
            a += 1
        End While
        While e > a AndAlso SqlDialect.IsCSpace(s(e - 1))
            e -= 1
        End While
        Return s.Substring(a, e - a)
    End Function

    ''' <summary>INSERT INTO s.t (c1, ...) VALUES ( (長さの上限なし。Oracle は SQL*Plus の 1 行に収まるよう改行)</summary>
    Private Function BuildPrefix() As String
        Dim b As New SqlText()
        b.Append("INSERT INTO " & QName() & " (")
        For i = 0 To _names.Count - 1
            If i > 0 Then
                b.Append(", ")
                If _dbms = ExportHelper.DBMS_ORACLE AndAlso b.Col + SqlDialect.Utf8Count(_names(i)) + 8 > SqlDialect.SQL_LINE_MAX Then
                    b.Append(ControlChars.Lf)
                End If
            End If
            b.Append(SqlDialect.QuoteIdent(_names(i), _dbms))
        Next
        b.Append(") VALUES (")
        Return b.ToString()
    End Function
#End Region

#Region "1 行 (仕様 4 章)"
    Private Function BuildRow(prefix As String, row As String()) As String
        _decl.Clear()
        _pre.Clear()
        _post.Clear()
        _rnotes.Clear()
        _plsql = False
        Dim n = _names.Count
        Dim vals(n - 1) As String
        Dim valBytes = 0
        Dim anyNl = False
        For i = 0 To n - 1
            Dim b As New SqlText()
            Dim state = If(Marks Is Nothing, Open_NativeParser.ValueState.Value, Marks.StateOf(row, i))
            PutValue(b, i, If(i < row.Length, row(i), Nothing), state)
            vals(i) = b.ToString()
            valBytes += SqlDialect.Utf8Count(vals(i))
            If vals(i).IndexOf(ControlChars.Lf) >= 0 Then anyNl = True
        Next

        Dim out As New SqlText()
        Dim oneLine = SqlDialect.Utf8Count(prefix) + valBytes + 2 * n + 2
        If _dbms <> ExportHelper.DBMS_ORACLE OrElse
           (oneLine <= SqlDialect.SQL_LINE_MAX AndAlso prefix.IndexOf(ControlChars.Lf) < 0 AndAlso Not anyNl) Then
            out.Append(prefix)
            For i = 0 To n - 1
                If i > 0 Then out.Append(", ")
                out.Append(vals(i))
            Next
            out.Append(");" & vbLf)
        Else
            ' Oracle で 1 行に収まらない: 行がいっぱいなら値を新しい行から。片に分けた値はもともと 1 片 1 行
            out.Append(prefix)
            For i = 0 To n - 1
                Dim v = vals(i)
                Dim nlPos = v.IndexOf(ControlChars.Lf)
                Dim first = If(nlPos >= 0, SqlDialect.Utf8Count(v, 0, nlPos), SqlDialect.Utf8Count(v))
                If i > 0 Then out.Append(", ")
                If out.Col + first + 3 > SqlDialect.SQL_LINE_MAX OrElse (nlPos >= 0 AndAlso out.Col > 0) Then
                    out.Append(ControlChars.Lf)
                End If
                out.Append(v)
            Next
            out.Append(");" & vbLf)
        End If

        Dim sb As New StringBuilder()
        sb.Append(_rnotes)
        If _plsql Then
            sb.Append("DECLARE" & vbLf).Append(_decl).Append("BEGIN" & vbLf).Append(_pre)
            sb.Append(out.ToString()).Append(_post).Append("END;" & vbLf & "/" & vbLf)
        Else
            sb.Append(out.ToString())
        End If
        Return sb.ToString()
    End Function

    ''' <summary>行の NOTE: "-- NOTE: column "C" of the next row: ..."</summary>
    Private Sub RowNote(i As Integer, msg As String)
        SqlDialect.PutNote(_rnotes, "-- NOTE: column """, _names(i), """ of the next row: ", msg)
    End Sub

    Private Sub PutValue(b As SqlText, i As Integer, v As String, state As Open_NativeParser.ValueState)
        Dim ci = _ci(i)
        Dim kind = ci.Kind
        ' 値の形を持たない値 (印。仕様 5 章・12.1) と、上限を超えて全体を持てなかった値は書かない (DLL の put_value と同じ)
        If state = Open_NativeParser.ValueState.Marker Then
            b.Append("NULL")
            If kind = SqlDialect.SqlKind.Udt Then
                RowNote(i, $"the value of type {ci.OracleType} cannot be decoded (its type is not in the dump); written as NULL")
            Else
                RowNote(i, $"the value could not be decoded ({SqlDialect.Utf8Prefix(If(v, ""), 40)}); written as NULL")
            End If
            Return
        End If
        If state = Open_NativeParser.ValueState.TooLong Then
            b.Append("NULL")
            RowNote(i, $"the value is longer than {SqlDialect.FULL_VALUE_MAX_MIB} MiB and was not kept whole; written as NULL")
            Return
        End If
        If v Is Nothing Then
            b.Append("NULL")
            Return
        End If
        If kind = SqlDialect.SqlKind.Udt Then
            ' オブジェクト型・VARRAY の値はリテラルで書けない (文字列は Oracle で ORA-00932)
            b.Append("NULL")
            RowNote(i, $"the value of type {ci.OracleType} has no SQL literal form; written as NULL")
            Return
        End If
        If v.Length = 0 Then
            ' NULL でない空 (Oracle では LOB だけがなりうる)
            If _dbms = ExportHelper.DBMS_ORACLE Then
                If kind = SqlDialect.SqlKind.Clob OrElse kind = SqlDialect.SqlKind.NClob Then
                    b.Append("EMPTY_CLOB()")
                ElseIf kind = SqlDialect.SqlKind.Blob Then
                    b.Append("EMPTY_BLOB()")
                Else
                    b.Append("NULL")
                End If
            ElseIf kind = SqlDialect.SqlKind.Blob OrElse kind = SqlDialect.SqlKind.Raw OrElse kind = SqlDialect.SqlKind.LongRaw Then
                b.Append(If(_dbms = ExportHelper.DBMS_POSTGRES, "'\x'::BYTEA", If(_dbms = ExportHelper.DBMS_MYSQL, "X''", "0x")))
            Else
                b.Append(SqlDialect.OtherText("", _dbms))
            End If
            Return
        End If

        Select Case kind
            Case SqlDialect.SqlKind.Num, SqlDialect.SqlKind.FloatNum, SqlDialect.SqlKind.BinFloat, SqlDialect.SqlKind.BinDouble
                If PutNumber(b, i, v) Then Return
            Case SqlDialect.SqlKind.DateKind, SqlDialect.SqlKind.Timestamp
                PutDateTime(b, i, v)
                Return
            Case SqlDialect.SqlKind.TimestampTz, SqlDialect.SqlKind.TimestampLtz
                PutTimeZone(b, i, v)
                Return
            Case SqlDialect.SqlKind.IntervalYm, SqlDialect.SqlKind.IntervalDs
                If _dbms = ExportHelper.DBMS_POSTGRES Then
                    PutIntervalPg(b, i, v)
                    Return
                End If
            Case SqlDialect.SqlKind.Bool
                If v = "TRUE" OrElse v = "FALSE" Then
                    b.Append(If(_dbms = ExportHelper.DBMS_SQLSERVER, If(v(0) = "T"c, "1", "0"), v))
                    Return
                End If
            Case SqlDialect.SqlKind.BFile
                If _dbms = ExportHelper.DBMS_ORACLE AndAlso IsBfilename(v) Then
                    b.Append(v)
                    Return
                End If
            Case SqlDialect.SqlKind.Raw, SqlDialect.SqlKind.LongRaw, SqlDialect.SqlKind.Blob
                PutBinary(b, i, v)
                Return
        End Select

        PutText(b, i, v)
    End Sub

    ''' <summary>数値の列。書けたら True (数値でない値は文字列として書く)</summary>
    Private Function PutNumber(b As SqlText, i As Integer, v As String) As Boolean
        Dim ci = _ci(i)
        Dim kind = ci.Kind
        If (kind = SqlDialect.SqlKind.BinFloat OrElse kind = SqlDialect.SqlKind.BinDouble) AndAlso
           (v = "NaN" OrElse v = "Inf" OrElse v = "-Inf") Then
            Dim isF = (kind = SqlDialect.SqlKind.BinFloat)
            If _dbms = ExportHelper.DBMS_ORACLE Then
                Dim inf = If(isF, "BINARY_FLOAT_INFINITY", "BINARY_DOUBLE_INFINITY")
                If v = "NaN" Then
                    b.Append(If(isF, "BINARY_FLOAT_NAN", "BINARY_DOUBLE_NAN"))
                ElseIf v = "Inf" Then
                    b.Append(inf)
                Else
                    b.Append("-" & inf)
                End If
            ElseIf _dbms = ExportHelper.DBMS_POSTGRES Then
                b.Append($"'{If(v = "Inf", "Infinity", If(v = "-Inf", "-Infinity", v))}'::{If(isF, "REAL", "DOUBLE PRECISION")}")
            Else
                b.Append("NULL")
                RowNote(i, $"{v} has no {If(_dbms = ExportHelper.DBMS_MYSQL, "MySQL value", "SQL Server value")}; written as NULL")
            End If
            Return True
        End If
        If Not SqlDialect.IsNumericLiteral(v) Then Return False
        b.Append(v)
        ' Oracle: BINARY_FLOAT / BINARY_DOUBLE のリテラル (1.5f / 1E+300d)。接尾辞が無いと NUMBER として読まれ、
        ' 1E126 を超える値は ORA-01426
        If _dbms = ExportHelper.DBMS_ORACLE AndAlso (kind = SqlDialect.SqlKind.BinFloat OrElse kind = SqlDialect.SqlKind.BinDouble) Then
            b.Append(If(kind = SqlDialect.SqlKind.BinFloat, "f"c, "d"c))
        End If
        If _dbms <> ExportHelper.DBMS_ORACLE AndAlso (ci.DecP > 0 OrElse ci.TgtDouble) Then
            Dim intd, fracd, sig As Integer
            SqlDialect.NumberDigits(v, intd, fracd, sig)
            If ci.TgtDouble AndAlso sig > 15 Then
                RowNote(i, $"{v} has {sig} significant digits; the double keeps 15-17")
            ElseIf ci.DecP > 0 AndAlso intd > ci.DecP - ci.DecS Then
                RowNote(i, $"{v} does not fit DECIMAL({ci.DecP},{ci.DecS}); the INSERT fails")
            ElseIf ci.DecP > 0 AndAlso fracd > ci.DecS Then
                RowNote(i, $"{v} is rounded to {ci.DecS} decimal places by DECIMAL({ci.DecP},{ci.DecS})")
            End If
        End If
        Return True
    End Function

    Private Function DateRangeName() As String
        Return If(_dbms = ExportHelper.DBMS_MYSQL, "MySQL", "SQL Server")
    End Function

    ''' <summary>小数秒の数字を keep 桁まで (元の末尾の 0 は残す)</summary>
    Private Shared Function FracPart(frac As String, keep As Integer) As String
        Dim n = Math.Min(frac.Length, keep)
        Return If(n > 0, "." & frac.Substring(0, n), "")
    End Function

    ''' <summary>DATE / TIMESTAMP の値 (解析エンジンの固定の形 SYYYY/MM/DD HH24:MI:SS[.F] から。M-13 / M-14)</summary>
    Private Sub PutDateTime(b As SqlText, i As Integer, v As String)
        Dim ci = _ci(i)
        Dim rest As String = Nothing
        Dim t = SqlDialect.ParseSqlDateTime(v, rest)
        If t Is Nothing OrElse rest.Length > 0 Then
            ' 固定の形でない (起きないはず): 文字列として
            If _dbms = ExportHelper.DBMS_ORACLE Then
                SqlDialect.OraText(b, v)
            Else
                b.Append(SqlDialect.OtherText(v, _dbms))
            End If
            Return
        End If
        Dim isDate = (ci.Kind = SqlDialect.SqlKind.DateKind)
        Dim hms = $"{t.Hour:00}:{t.Minute:00}:{t.Second:00}"
        If _dbms = ExportHelper.DBMS_ORACLE Then
            b.Append(If(isDate, "TO_DATE('", "TO_TIMESTAMP('"))
            b.Append($"{SqlDialect.OraYear(t.Year)}/{t.Month:00}/{t.Day:00} {hms}")
            If Not isDate Then b.Append(FracPart(t.Frac, 9))
            b.Append(If(isDate, "', 'SYYYY/MM/DD HH24:MI:SS')", "', 'SYYYY/MM/DD HH24:MI:SS.FF')"))
            Return
        End If
        If t.Year < 0 AndAlso _dbms <> ExportHelper.DBMS_POSTGRES Then
            b.Append("NULL")
            RowNote(i, $"{-t.Year:0000} BC is outside the {DateRangeName()} date range; written as NULL")
            Return
        End If
        If _dbms = ExportHelper.DBMS_MYSQL AndAlso t.Year < 1000 Then
            RowNote(i, $"year {t.Year} is before MySQL's supported DATETIME range (1000-9999)")
        End If
        If t.Frac.Length > ci.TgtFrac AndAlso Not isDate Then
            If t.Frac.Substring(ci.TgtFrac).TrimStart("0"c).Length > 0 Then
                RowNote(i, $"fractional seconds .{t.Frac} cut to {ci.TgtFrac} digits")
            End If
        End If
        b.Append(If(_dbms = ExportHelper.DBMS_SQLSERVER, "N'", "'"))
        b.Append($"{Math.Abs(t.Year):0000}-{t.Month:00}-{t.Day:00}{If(_dbms = ExportHelper.DBMS_SQLSERVER, "T"c, " "c)}{hms}")
        If Not isDate Then b.Append(FracPart(t.Frac, ci.TgtFrac))
        If t.Year < 0 Then b.Append(" BC")
        b.Append("'"c)
    End Sub

    ''' <summary>
    ''' TIMESTAMP WITH TIME ZONE / WITH LOCAL TIME ZONE の値。VB の経路は解析エンジンの表示の文字列
    ''' (「現地時刻 時差」「現地時刻 地域名 その時点の時差」、TSLTZ は「DB の時刻 DBTIMEZONE」) から、
    ''' DLL が保存されたバイト列から書くのと同じ SQL を作る (仕様 4.4)
    ''' </summary>
    Private Sub PutTimeZone(b As SqlText, i As Integer, v As String)
        Dim ci = _ci(i)
        Dim isTz = (ci.Kind = SqlDialect.SqlKind.TimestampTz)
        Dim rest As String = Nothing
        Dim local = SqlDialect.ParseSqlDateTime(v, rest)
        If local Is Nothing Then
            ' 時刻として読めない: Oracle は文字列 (DLL も読めない値は表示の文字列で書く)、他は NULL
            If _dbms = ExportHelper.DBMS_ORACLE Then
                SqlDialect.OraText(b, v)
            Else
                b.Append("NULL")
                RowNote(i, "the time zone value could not be decoded; written as NULL")
            End If
            Return
        End If
        rest = rest.Trim(" "c)

        ' 表示の後ろの部分: 時差 / 地域名 + 時差 / UTC (unknown ...) / DBTIMEZONE の名前 / 無し
        Dim offset As Integer = 0
        Dim hasOffset = False
        Dim region As String = Nothing
        Dim unknownZone As String = Nothing
        Dim zoneName As String = Nothing
        If rest.Length > 0 Then
            If rest.StartsWith("UTC (", StringComparison.Ordinal) AndAlso rest.EndsWith(")", StringComparison.Ordinal) Then
                unknownZone = rest.Substring(5, rest.Length - 6)
            ElseIf SqlDialect.ParseOffset(rest, offset) Then
                hasOffset = True
                zoneName = rest
            Else
                Dim sp = rest.LastIndexOf(" "c)
                If sp > 0 AndAlso SqlDialect.ParseOffset(rest.Substring(sp + 1), offset) Then
                    hasOffset = True
                    region = rest.Substring(0, sp)
                    zoneName = region
                ElseIf rest.IndexOf(" "c) < 0 Then
                    zoneName = rest            ' TSLTZ: 時差・地域名として読めない DBTIMEZONE
                Else
                    local = Nothing
                End If
            End If
        End If
        If local Is Nothing OrElse (isTz AndAlso unknownZone Is Nothing AndAlso Not hasOffset) OrElse
           (Not isTz AndAlso unknownZone IsNot Nothing) Then
            If _dbms = ExportHelper.DBMS_ORACLE Then
                SqlDialect.OraText(b, v)
            Else
                b.Append("NULL")
                RowNote(i, "the time zone value could not be decoded; written as NULL")
            End If
            Return
        End If

        If _dbms = ExportHelper.DBMS_ORACLE Then
            ' 時差: FROM_TZ(現地時刻, 時差)。地域: FROM_TZ(UTC, 'UTC') AT TIME ZONE 地域 (時差ファイルの版に左右されない)。
            ' TSLTZ: FROM_TZ(DB の時刻, DBTIMEZONE)
            If isTz AndAlso unknownZone IsNot Nothing Then
                b.Append($"FROM_TZ({OraTsText(local)}, 'UTC') /* {unknownZone} */")
            ElseIf isTz AndAlso region IsNot Nothing Then
                b.Append($"FROM_TZ({OraTsText(SqlDialect.ShiftSeconds(local, -offset))}, 'UTC') AT TIME ZONE '{region}'")
            ElseIf zoneName IsNot Nothing Then
                b.Append($"FROM_TZ({OraTsText(local)}, '{zoneName}')")
            Else
                b.Append(OraTsText(local))     ' TSLTZ で DBTIMEZONE が無い: 読み込むセッションのタイムゾーンで解釈される
            End If
            Return
        End If

        If isTz AndAlso unknownZone IsNot Nothing Then
            b.Append("NULL")
            RowNote(i, "the time zone value could not be decoded; written as NULL")
            Return
        End If
        Dim utc = If(hasOffset, SqlDialect.ShiftSeconds(local, -offset), local)
        Dim w As SqlDialect.SqlDateTime
        Dim useOff As Boolean
        If _dbms = ExportHelper.DBMS_MYSQL Then
            w = utc : useOff = False
        Else
            w = local : useOff = hasOffset
        End If
        If _dbms = ExportHelper.DBMS_SQLSERVER AndAlso useOff AndAlso
           (offset Mod 60 <> 0 OrElse offset > 14 * 3600 OrElse offset < -14 * 3600) Then
            w = utc : offset = 0          ' datetimeoffset は ±14:00 以内の分単位の時差だけ
        End If
        If w.Year < 0 AndAlso _dbms <> ExportHelper.DBMS_POSTGRES Then
            b.Append("NULL")
            RowNote(i, $"{-w.Year:0000} BC is outside the {DateRangeName()} date range; written as NULL")
            Return
        End If
        Dim nano = w.Frac.PadRight(9, "0"c)
        If nano.Length > 9 Then nano = nano.Substring(0, 9)
        Dim keep = Math.Min(ci.TsFrac, ci.TgtFrac)
        If nano.Substring(keep).TrimStart("0"c).Length > 0 Then
            RowNote(i, $"fractional seconds .{nano} cut to {keep} digits")
        End If
        Dim frac = nano.Substring(0, keep)
        b.Append(If(_dbms = ExportHelper.DBMS_SQLSERVER, "N'", "'"))
        b.Append($"{Math.Abs(w.Year):0000}-{w.Month:00}-{w.Day:00}{If(_dbms = ExportHelper.DBMS_SQLSERVER, "T"c, " "c)}{w.Hour:00}:{w.Minute:00}:{w.Second:00}")
        If frac.Length > 0 Then b.Append("." & frac)
        If useOff Then b.Append(SqlDialect.FormatOffset(offset))
        If w.Year < 0 Then b.Append(" BC")
        b.Append("'"c)
    End Sub

    ''' <summary>TO_TIMESTAMP('SYYYY-MM-DD HH24:MI:SS[.F]', ...) (小数秒は表示の桁。0 桁ならマスクに .FF を付けない)</summary>
    Private Shared Function OraTsText(t As SqlDialect.SqlDateTime) As String
        Dim lit = $"{SqlDialect.OraYear(t.Year)}-{t.Month:00}-{t.Day:00} {t.Hour:00}:{t.Minute:00}:{t.Second:00}"
        Dim frac = t.Frac
        If frac.TrimEnd("0"c).Length = 0 AndAlso frac.Length = 0 Then
            Return $"TO_TIMESTAMP('{lit}', 'SYYYY-MM-DD HH24:MI:SS')"
        End If
        Return $"TO_TIMESTAMP('{lit}.{frac}', 'SYYYY-MM-DD HH24:MI:SS.FF')"
    End Function

    ''' <summary>PostgreSQL の INTERVAL: Oracle の +Y-M / +D HH:MI:SS.F を単位の語で</summary>
    Private Sub PutIntervalPg(b As SqlText, i As Integer, v As String)
        Dim ci = _ci(i)
        Dim sg = If(v.StartsWith("-"c), "-", "")
        Dim p = If(v.StartsWith("-"c) OrElse v.StartsWith("+"c), v.Substring(1), v)
        Dim pos = 0
        If ci.Kind = SqlDialect.SqlKind.IntervalYm Then
            Dim y, m As Integer
            If Not (ScanLong(p, pos, y) AndAlso pos < p.Length AndAlso p(pos) = "-"c AndAlso ScanLongAfter(p, pos, m)) Then
                b.Append(SqlDialect.OtherText(v, ExportHelper.DBMS_POSTGRES))
                Return
            End If
            ' PostgreSQL の interval は月数を 32 ビットで持つ (178956970 年 7 か月まで。16 で実測)
            If CLng(y) * 12 + m > 2147483647L + If(sg.Length > 0, 1, 0) Then
                b.Append("NULL")
                RowNote(i, $"{v} is outside PostgreSQL's interval range (about 178 million years); written as NULL")
                Return
            End If
            b.Append($"'{sg}{y} years {sg}{m} months'")
        Else
            Dim d, h, mi, se As Integer
            If Not (ScanLong(p, pos, d) AndAlso SkipSpace(p, pos) AndAlso ScanLong(p, pos, h) AndAlso Expect(p, pos, ":"c) AndAlso
                    ScanLong(p, pos, mi) AndAlso Expect(p, pos, ":"c) AndAlso ScanLong(p, pos, se)) Then
                b.Append(SqlDialect.OtherText(v, ExportHelper.DBMS_POSTGRES))
                Return
            End If
            Dim frac = ""
            If pos < p.Length AndAlso p(pos) = "."c Then
                Dim st = pos + 1, e = pos + 1
                While e < p.Length AndAlso SqlDialect.IsAsciiDigit(p(e)) AndAlso e - st < 15
                    e += 1
                End While
                frac = p.Substring(st, e - st)
            End If
            If frac.Length > 6 Then
                If frac.Substring(6).TrimStart("0"c).Length > 0 Then RowNote(i, $"fractional seconds .{frac} cut to 6 digits")
                frac = frac.Substring(0, 6)
            End If
            b.Append($"'{sg}{d} days {sg}{h} hours {sg}{mi} minutes {sg}{se}{If(frac.Length > 0, "." & frac, "")} seconds'")
        End If
    End Sub

    Private Shared Function ScanLong(s As String, ByRef p As Integer, ByRef v As Integer) As Boolean
        While p < s.Length AndAlso SqlDialect.IsCSpace(s(p))
            p += 1
        End While
        Dim st = p
        If p < s.Length AndAlso (s(p) = "+"c OrElse s(p) = "-"c) Then p += 1
        Dim ds = p
        While p < s.Length AndAlso SqlDialect.IsAsciiDigit(s(p))
            p += 1
        End While
        If p = ds Then Return False
        v = SqlDialect.Atoi(s.Substring(st, p - st))
        Return True
    End Function

    ''' <summary>"-" の後ろの数 (sscanf の "%ld-%ld" の 2 つ目)</summary>
    Private Shared Function ScanLongAfter(s As String, ByRef p As Integer, ByRef v As Integer) As Boolean
        p += 1
        Return ScanLong(s, p, v)
    End Function

    Private Shared Function SkipSpace(s As String, ByRef p As Integer) As Boolean
        While p < s.Length AndAlso SqlDialect.IsCSpace(s(p))
            p += 1
        End While
        Return True
    End Function

    Private Shared Function Expect(s As String, ByRef p As Integer, c As Char) As Boolean
        If p < s.Length AndAlso s(p) = c Then
            p += 1
            Return True
        End If
        Return False
    End Function

    ''' <summary>解析エンジンの BFILENAME('dir', 'file') の形か (引用符は二重化済み)</summary>
    Private Shared Function IsBfilename(v As String) As Boolean
        If Not v.StartsWith("BFILENAME('", StringComparison.Ordinal) OrElse Not v.EndsWith(")", StringComparison.Ordinal) Then Return False
        Dim p = 11, part = 0
        While p < v.Length
            If v(p) = "'"c Then
                If p + 1 < v.Length AndAlso v(p + 1) = "'"c Then
                    p += 2
                    Continue While
                End If
                If part = 0 AndAlso String.CompareOrdinal(v, p, "', '", 0, 4) = 0 Then
                    part = 1
                    p += 4
                    Continue While
                End If
                If part = 1 AndAlso p = v.Length - 2 AndAlso v(p + 1) = ")"c Then Return True
                Return False
            End If
            p += 1
        End While
        Return False
    End Function

    Private Function PlsqlVar(i As Integer, type As String) As String
        Dim nm = "v" & (i + 1).ToString(Globalization.CultureInfo.InvariantCulture)
        _decl.Append("  ").Append(nm).Append(" "c).Append(type).Append(";" & vbLf)
        _plsql = True
        Return nm
    End Function

    ''' <summary>RAW / LONG RAW / BLOB (16 進。exp の RAW の表示の 0x は外す)</summary>
    Private Sub PutBinary(b As SqlText, i As Integer, v As String)
        Dim kind = _ci(i).Kind
        Dim h = v
        If h.Length >= 2 AndAlso h(0) = "0"c AndAlso (h(1) = "x"c OrElse h(1) = "X"c) Then h = h.Substring(2)
        If h.Length Mod 2 <> 0 OrElse Not h.All(Function(c) SqlDialect.IsHexDigit(c)) Then
            b.Append("NULL")
            RowNote(i, "the value is not hexadecimal; written as NULL")
            Return
        End If
        Dim hn = h.Length
        Dim bytes = hn \ 2
        Select Case _dbms
            Case ExportHelper.DBMS_POSTGRES
                b.Append("'\x" & h & "'::BYTEA")
                Return
            Case ExportHelper.DBMS_MYSQL
                b.Append("X'" & h & "'")
                Return
            Case ExportHelper.DBMS_SQLSERVER
                b.Append("0x" & h)
                Return
        End Select
        Dim pc = SqlDialect.SQL_PIECE
        If bytes <= SqlDialect.ORA_RAW_MAX Then
            ' HEXTORAW('..') (|| でつないでも RAW の 2000 バイト以内)
            If hn + 12 <= pc Then
                b.Append("HEXTORAW('" & h & "')")
                Return
            End If
            Dim pos = 0
            While pos < hn
                Dim k = Math.Min(pc, hn - pos)
                If pos > 0 Then b.Append("||" & vbLf)
                b.Append("HEXTORAW('" & h.Substring(pos, k) & "')")
                pos += k
            End While
            Return
        End If
        If kind = SqlDialect.SqlKind.Blob Then
            Dim nm = PlsqlVar(i, "BLOB")
            _pre.Append($"  DBMS_LOB.CREATETEMPORARY({nm}, TRUE);" & vbLf)
            Dim pos = 0
            While pos < hn
                Dim k = Math.Min(pc, hn - pos)
                _pre.Append($"  DBMS_LOB.WRITEAPPEND({nm}, {k \ 2}, HEXTORAW('").Append(h, pos, k).Append("'));" & vbLf)
                pos += k
            End While
            _post.Append($"  DBMS_LOB.FREETEMPORARY({nm});" & vbLf)
            b.Append(nm)
            Return
        End If
        If bytes > SqlDialect.ORA_PLSQL_LONG Then
            b.Append("NULL")
            RowNote(i, $"{bytes} bytes is more than a SQL script can insert into {If(kind = SqlDialect.SqlKind.Raw, "RAW", "LONG RAW")} (PL/SQL limit 32760); written as NULL")
            Return
        End If
        Dim nv = PlsqlVar(i, If(kind = SqlDialect.SqlKind.Raw, "RAW(32767)", "LONG RAW"))
        Dim q = 0
        While q < hn
            Dim k = Math.Min(pc, hn - q)
            _pre.Append($"  {nv} := UTL_RAW.CONCAT({nv}, HEXTORAW('").Append(h, q, k).Append("'));" & vbLf)
            q += k
        End While
        b.Append(nv)
    End Sub

    ''' <summary>Oracle: CLOB / NCLOB を一時 LOB に片ずつ足す PL/SQL</summary>
    Private Function OraLobVar(i As Integer, v As String, type As String, fn As String) As String
        Dim nm = PlsqlVar(i, type)
        _pre.Append($"  DBMS_LOB.CREATETEMPORARY({nm}, TRUE);" & vbLf)
        Dim pos = 0
        While pos < v.Length
            Dim k = SqlDialect.NextPiece(v, pos, SqlDialect.SQL_PIECE, SqlDialect.ORA_STR_MAX)
            Dim piece As New SqlText()
            SqlDialect.OraPiece(piece, v, pos, k)
            _pre.Append($"  DBMS_LOB.APPEND({nm}, {fn}(").Append(piece.ToString()).Append("));" & vbLf)
            pos += k
        End While
        _post.Append($"  DBMS_LOB.FREETEMPORARY({nm});" & vbLf)
        Return nm
    End Function

    ''' <summary>文字列の値 (仕様 4.2 / 4.3)</summary>
    Private Sub PutText(b As SqlText, i As Integer, v As String)
        Dim kind = _ci(i).Kind
        If _dbms <> ExportHelper.DBMS_ORACLE Then
            If _dbms = ExportHelper.DBMS_POSTGRES AndAlso v.IndexOf(ChrW(0)) >= 0 Then
                ' PostgreSQL の text は U+0000 を持てない (リテラルでも関数でも作れない)。利用者の決定 (10/18):
                ' 0x00 を除いた値を書き、除いたことを行・列ごとに NOTE で残す
                Dim w = v.Replace(ChrW(0).ToString(), "")
                b.Append(If(w.Length = 0, "''", SqlDialect.OtherText(w, _dbms)))
                RowNote(i, $"{v.Length - w.Length} byte(s) 0x00 removed from the value (PostgreSQL text cannot hold U+0000)")
                Return
            End If
            b.Append(SqlDialect.OtherText(v, _dbms))
            Return
        End If
        Dim n = SqlDialect.Utf8Count(v)
        If n <= SqlDialect.ORA_STR_MAX Then
            SqlDialect.OraText(b, v)
            Return
        End If
        Select Case kind
            Case SqlDialect.SqlKind.Clob, SqlDialect.SqlKind.Json, SqlDialect.SqlKind.Vector
                b.Append(OraLobVar(i, v, "CLOB", "TO_CLOB"))
            Case SqlDialect.SqlKind.NClob
                b.Append(OraLobVar(i, v, "NCLOB", "TO_NCLOB"))
            Case SqlDialect.SqlKind.Xml
                b.Append($"XMLTYPE({OraLobVar(i, v, "CLOB", "TO_CLOB")})")
            Case SqlDialect.SqlKind.LongText
                If n > SqlDialect.ORA_PLSQL_LONG Then
                    b.Append("NULL")
                    RowNote(i, $"{n} bytes is more than a SQL script can insert into LONG (PL/SQL limit 32760); written as NULL")
                    Return
                End If
                Dim nm = PlsqlVar(i, "LONG")
                Dim pos = 0
                While pos < v.Length
                    Dim k = SqlDialect.NextPiece(v, pos, SqlDialect.SQL_PIECE, SqlDialect.ORA_STR_MAX)
                    Dim piece As New SqlText()
                    SqlDialect.OraPiece(piece, v, pos, k)
                    _pre.Append($"  {nm} := {nm} || ").Append(piece.ToString()).Append(";" & vbLf)
                    pos += k
                End While
                b.Append(nm)
            Case SqlDialect.SqlKind.NText
                SqlDialect.OraClobConcat(b, v, "TO_NCLOB")
            Case Else
                ' VARCHAR2 / CHAR の 4000 バイト超 (1 バイト系の文字セットから、MAX_STRING_SIZE=EXTENDED): CLOB として連結
                SqlDialect.OraClobConcat(b, v, "TO_CLOB")
        End Select
    End Sub
#End Region

#Region "制約・索引・コメント (仕様 3 章)"
    Private Const CON_PK As Integer = 0
    Private Const CON_UNIQUE As Integer = 1
    Private Const CON_FK As Integer = 2
    Private Const CON_CHECK As Integer = 3
    Private Const CON_INDEX As Integer = 4
    Private Const CON_COMMENT As Integer = 5
    Private Const CON_NOTES As Integer = 6

    ''' <summary>システムが付けた名前 (SYS_C...)。名前を付けずに書く</summary>
    Private Shared Function GeneratedName(n As String) As Boolean
        Return String.IsNullOrEmpty(n) OrElse (n.StartsWith("SYS_C", StringComparison.Ordinal) AndAlso n.Length > 5 AndAlso SqlDialect.IsAsciiDigit(n(5)))
    End Function

    Private Shared Function ConRank(c As SqlExportLogic.ConstraintInfo) As Integer
        Select Case c.type
            Case CON_PK : Return 1
            Case CON_UNIQUE : Return If(c.IsUniqueIndex, 0, 2)
            Case CON_CHECK : Return 3
            Case CON_FK : Return 4
            Case Else : Return 5
        End Select
    End Function

    Private Function ColIsLob(name As String) As Boolean
        For i = 0 To Math.Min(_names.Count, _types.Length) - 1
            If _names(i) = name Then
                Select Case _ci(i).Kind
                    Case SqlDialect.SqlKind.Clob, SqlDialect.SqlKind.NClob, SqlDialect.SqlKind.LongText, SqlDialect.SqlKind.Blob,
                         SqlDialect.SqlKind.LongRaw, SqlDialect.SqlKind.Json, SqlDialect.SqlKind.Xml, SqlDialect.SqlKind.Vector,
                         SqlDialect.SqlKind.Udt
                        Return True
                End Select
                Return False
            End If
        Next
        Return False
    End Function

    Private Function ColList(cols As String()) As String
        Return "(" & String.Join(", ", If(cols, Array.Empty(Of String)()).Select(Function(c) SqlDialect.QuoteIdent(c, _dbms))) & ")"
    End Function

    ''' <summary>
    ''' 表の行の後ろに置く DDL: 種類の順 (主キー → 一意 → CHECK → 外部キー → 索引)、同じ種類は名前の順 (システム名は後ろに並びのまま)、
    ''' コメント、注記。外部キーは fks に (ファイルの最後に書く)
    ''' </summary>
    Private Sub RenderTableDdl(ddl As StringBuilder, fks As List(Of KeyValuePair(Of String, String)))
        Dim notes As New StringBuilder()
        Dim cons = _constraints.Where(Function(c) c.type >= CON_PK AndAlso c.type <= CON_INDEX).ToList()
        If (CreateIndex OrElse CreateTable) AndAlso cons.Count > 0 Then
            Dim ordered = cons.Select(Function(c, idx) New With {c, idx}).
                OrderBy(Function(x) ConRank(x.c)).
                ThenBy(Function(x) If(GeneratedName(x.c.name), 1, 0)).
                ThenBy(Function(x) If(GeneratedName(x.c.name), "", x.c.name), StringComparer.Ordinal).
                ThenBy(Function(x) x.idx).Select(Function(x) x.c).ToList()
            For Each c In ordered
                Dim isIndex = (c.type = CON_INDEX OrElse c.IsUniqueIndex)
                If If(isIndex, CreateIndex, CreateTable) Then RenderConstraint(c, ddl, fks, notes)
            Next
        End If
        If WriteComments Then RenderComments(ddl, notes)
        For Each c In _constraints.Where(Function(x) x.type = CON_NOTES AndAlso x.notes IsNot Nothing)
            For Each n In c.notes
                For Each line In If(n, "").Split(ControlChars.Lf)
                    notes.Append("-- NOTE: ").Append(line.TrimEnd(ControlChars.Cr)).Append(vbLf)
                Next
            Next
        Next
        ddl.Append(notes)
    End Sub

    Private Sub RenderConstraint(c As SqlExportLogic.ConstraintInfo, b As StringBuilder,
                                 fks As List(Of KeyValuePair(Of String, String)), nb As StringBuilder)
        Dim nl = vbLf
        Dim cname = If(c.name, "")
        Dim named = Not GeneratedName(cname)
        Dim disabled = String.Equals(c.status, "DISABLED", StringComparison.OrdinalIgnoreCase)
        Dim deferrable = String.Equals(c.deferrable, "DEFERRABLE", StringComparison.OrdinalIgnoreCase)
        Dim deferred = String.Equals(c.deferred, "DEFERRED", StringComparison.OrdinalIgnoreCase)
        Dim isOra = (_dbms = ExportHelper.DBMS_ORACLE)
        Dim isMs = (_dbms = ExportHelper.DBMS_SQLSERVER)
        Dim isMy = (_dbms = ExportHelper.DBMS_MYSQL)
        Dim cols = If(c.columns, Array.Empty(Of String)())

        If c.type = CON_INDEX Then
            Dim expr = SqlDialect.TrimParenWs(If(c.index_expr, ""))
            If isOra Then
                Dim o As New SqlText()
                o.Append("CREATE INDEX ")
                If cname.Length > 0 Then o.Append(SqlDialect.QuoteIdent(cname, _dbms) & " ")
                o.Append("ON " & QName() & " ")
                If expr.Length > 0 Then
                    SqlDialect.PutWrapped(o, expr)
                Else
                    o.Append(ColList(cols))
                End If
                o.Append(";" & nl)
                b.Append(o.ToString())
                Return
            End If
            Dim e As New StringBuilder()
            If expr.Length > 0 Then
                If Not SqlDialect.TranslateExpr(expr, e, _dbms) Then
                    nb.Append($"-- NOTE: index ""{cname}"" uses an Oracle expression and was not written: {expr}" & nl)
                    Return
                End If
            Else
                e.Append(ColList(cols))
            End If
            If isMy AndAlso cols.Any(Function(x) ColIsLob(x)) Then
                nb.Append($"-- NOTE: index ""{cname}"" is on a TEXT / BLOB column (MySQL needs a prefix length) and was not written" & nl)
                Return
            End If
            b.Append("CREATE INDEX " & SqlDialect.QuoteIdent(If(cname.Length > 0, cname, "IX"), _dbms) & " ON " & QName() & " " & e.ToString() & ";" & nl)
            Return
        End If

        If c.type = CON_CHECK Then
            ' exp は外側の括弧付き、Data Pump は括弧無しで持つ: 全体を囲む 1 組を外してから CHECK (...) に
            Dim cond = SqlDialect.StripOuterParens(If(c.condition, ""))
            If cond.Length = 0 Then Return
            Dim e As New StringBuilder()
            If Not isOra Then
                If Not SqlDialect.TranslateExpr(cond, e, _dbms) Then
                    nb.Append($"-- NOTE: CHECK constraint ""{cname}"" uses an Oracle expression and was not written: {cond}" & nl)
                    Return
                End If
                If disabled AndAlso Not isMs AndAlso Not isMy Then
                    nb.Append($"-- NOTE: CHECK constraint ""{cname}"" is disabled in the source; PostgreSQL cannot create a disabled constraint and it was not written" & nl)
                    Return
                End If
            End If
            Dim o As New SqlText()
            o.Append("ALTER TABLE " & QName())
            If isMs AndAlso disabled Then o.Append(" WITH NOCHECK")
            o.Append(" ADD ")
            If named Then o.Append("CONSTRAINT " & SqlDialect.QuoteIdent(cname, _dbms) & " ")
            o.Append("CHECK (")
            If isOra Then
                SqlDialect.PutWrapped(o, cond)
            Else
                o.Append(e.ToString())
            End If
            o.Append(")"c)
            If isOra AndAlso disabled Then o.Append(" DISABLE")
            If isMy AndAlso disabled Then o.Append(" NOT ENFORCED")
            o.Append(";" & nl)
            If isMs AndAlso disabled AndAlso named Then
                o.Append("ALTER TABLE " & QName() & " NOCHECK CONSTRAINT " & SqlDialect.QuoteIdent(cname, _dbms) & ";" & nl)
            End If
            b.Append(o.ToString())
            Return
        End If

        ' 主キー / 一意 / 外部キー
        If cols.Length = 0 Then Return
        If isMy AndAlso c.type <> CON_FK AndAlso cols.Any(Function(x) ColIsLob(x)) Then
            nb.Append($"-- NOTE: {If(c.type = CON_PK, "PRIMARY KEY", "UNIQUE")} ""{cname}"" is on a TEXT / BLOB column (MySQL needs a prefix length) and was not written" & nl)
            Return
        End If
        If c.IsUniqueIndex Then
            ' 制約でない一意の索引: CREATE UNIQUE INDEX (同じ列の主キーはこの索引を使う)
            b.Append("CREATE UNIQUE INDEX " & SqlDialect.QuoteIdent(If(cname.Length > 0, cname, "UX"), _dbms) & " ON " & QName() & " " & ColList(cols) & ";" & nl)
            Return
        End If
        If disabled AndAlso Not isOra AndAlso Not (isMs AndAlso c.type = CON_FK) Then
            nb.Append($"-- NOTE: constraint ""{cname}"" is disabled in the source and cannot be created disabled here; it was not written" & nl)
            Return
        End If
        Dim s As New StringBuilder()
        s.Append("ALTER TABLE " & QName())
        If isMs AndAlso disabled Then s.Append(" WITH NOCHECK")
        s.Append(" ADD ")
        If named Then s.Append("CONSTRAINT " & SqlDialect.QuoteIdent(cname, _dbms) & " ")
        Select Case c.type
            Case CON_PK : s.Append("PRIMARY KEY ")
            Case CON_UNIQUE : s.Append("UNIQUE ")
            Case Else : s.Append("FOREIGN KEY ")
        End Select
        s.Append(ColList(cols))
        If c.type = CON_FK Then
            Dim rs = If(Not String.IsNullOrEmpty(c.ref_schema), c.ref_schema, _schema)
            s.Append(" REFERENCES " & SqlDialect.QuoteQName(rs, If(c.ref_table, ""), _dbms) & " ")
            If c.ref_columns IsNot Nothing AndAlso c.ref_columns.Length > 0 Then s.Append(ColList(c.ref_columns))
            If Not String.IsNullOrEmpty(c.delete_rule) Then s.Append(" ON DELETE " & c.delete_rule)
        End If
        If deferrable AndAlso (isOra OrElse _dbms = ExportHelper.DBMS_POSTGRES) Then
            s.Append(" DEFERRABLE")
            If deferred Then s.Append(" INITIALLY DEFERRED")
        ElseIf deferrable Then
            nb.Append($"-- NOTE: constraint ""{cname}"" is DEFERRABLE in the source; {If(isMy, "MySQL", "SQL Server")} has no deferred constraints" & nl)
        End If
        If isOra AndAlso disabled Then s.Append(" DISABLE")
        s.Append(";" & nl)
        If isMs AndAlso disabled AndAlso named Then
            s.Append("ALTER TABLE " & QName() & " NOCHECK CONSTRAINT " & SqlDialect.QuoteIdent(cname, _dbms) & ";" & nl)
        End If
        If c.type = CON_FK Then
            ' 並べ替えの鍵: 名前の付いたものは名前、システム名のものは並びのまま後ろに
            fks.Add(New KeyValuePair(Of String, String)(If(named, "0" & cname, "1"), s.ToString()))
        Else
            b.Append(s.ToString())
        End If
    End Sub

    ''' <summary>Oracle の COMMENT ON (改行を含む・1 行に収まらないものは EXECUTE IMMEDIATE で組み立てる)</summary>
    Private Sub OraComment(b As StringBuilder, column As String, text As String)
        Dim nl = vbLf
        Dim head = 40 + SqlDialect.Utf8Count(_schema) + SqlDialect.Utf8Count(_table) + SqlDialect.Utf8Count(If(column, ""))
        If text.IndexOfAny({ControlChars.Cr, ControlChars.Lf}) < 0 AndAlso head + SqlDialect.OraRenderedLen(text) <= SqlDialect.SQL_LINE_MAX Then
            Dim t As New SqlText()
            t.Append(If(column IsNot Nothing, "COMMENT ON COLUMN ", "COMMENT ON TABLE ") & QName())
            If column IsNot Nothing Then t.Append("." & SqlDialect.QuoteIdent(column, _dbms))
            t.Append(" IS ")
            SqlDialect.OraPiece(t, text, 0, text.Length)
            t.Append(";" & nl)
            b.Append(t.ToString())
            Return
        End If
        b.Append("BEGIN" & nl & "    EXECUTE IMMEDIATE 'COMMENT ON ")
        b.Append(If(column IsNot Nothing, "COLUMN ", "TABLE "))
        If _schema.Length > 0 Then b.Append(SqlDialect.PlsqlIdent(_schema) & ".")
        b.Append(SqlDialect.PlsqlIdent(_table))
        If column IsNot Nothing Then b.Append("." & SqlDialect.PlsqlIdent(column))
        b.Append(" IS '''")
        Dim pos = 0
        While pos < text.Length
            Dim k = SqlDialect.NextPiece(text, pos, SqlDialect.SQL_PIECE \ 2, SqlDialect.ORA_STR_MAX)
            b.Append(nl & "    || '")
            For j = pos To pos + k - 1
                Dim ch = text(j)
                If ch = "'"c Then
                    b.Append("''''")
                ElseIf ch = ControlChars.Lf OrElse ch = ControlChars.Cr Then
                    b.Append($"' || CHR({Convert.ToInt32(ch)}) || '")
                Else
                    b.Append(ch)
                End If
            Next
            b.Append("'"c)
            pos += k
        End While
        b.Append(nl & "    || '''';" & nl & "END;" & nl & "/" & nl)
    End Sub

    Private Sub RenderComments(b As StringBuilder, nb As StringBuilder)
        Dim nl = vbLf
        Dim tblComment As String = Nothing
        Dim colComments As New Dictionary(Of String, String)(StringComparer.Ordinal)
        For Each c In _constraints.Where(Function(x) x.type = CON_COMMENT)
            If Not String.IsNullOrEmpty(c.table_comment) Then tblComment = c.table_comment
            If c.col_comments IsNot Nothing Then
                For Each kv In c.col_comments
                    colComments(kv.Key) = kv.Value
                Next
            End If
        Next
        Dim schemaOrDbo = If(_schema.Length > 0, _schema, "dbo")
        If Not String.IsNullOrEmpty(tblComment) Then
            Select Case _dbms
                Case ExportHelper.DBMS_ORACLE
                    OraComment(b, Nothing, tblComment)
                Case ExportHelper.DBMS_POSTGRES
                    b.Append("COMMENT ON TABLE " & QName() & " IS " & SqlDialect.OtherText(tblComment, _dbms) & ";" & nl)
                Case ExportHelper.DBMS_MYSQL
                    If SqlDialect.Utf8Count(tblComment) > 2048 * 4 Then
                        nb.Append("-- NOTE: the table comment is longer than MySQL allows (2048 characters) and was not written" & nl)
                    Else
                        b.Append("ALTER TABLE " & QName() & " COMMENT = " & SqlDialect.OtherText(tblComment, _dbms) & ";" & nl)
                    End If
                Case Else
                    b.Append("EXEC sp_addextendedproperty N'MS_Description', " & SqlDialect.OtherText(tblComment, _dbms) &
                             ", N'SCHEMA', " & SqlDialect.OtherText(schemaOrDbo, _dbms) & ", N'TABLE', " & SqlDialect.OtherText(_table, _dbms) & ";" & nl)
            End Select
        End If
        For i = 0 To Math.Min(_names.Count, _types.Length) - 1
            Dim cc As String = Nothing
            If Not colComments.TryGetValue(_names(i), cc) OrElse String.IsNullOrEmpty(cc) Then Continue For
            Dim name = _names(i)
            Select Case _dbms
                Case ExportHelper.DBMS_ORACLE
                    OraComment(b, name, cc)
                Case ExportHelper.DBMS_POSTGRES
                    b.Append("COMMENT ON COLUMN " & QName() & "." & SqlDialect.QuoteIdent(name, _dbms) & " IS " & SqlDialect.OtherText(cc, _dbms) & ";" & nl)
                Case ExportHelper.DBMS_MYSQL
                    If SqlDialect.Utf8Count(cc) > 1024 * 4 Then
                        nb.Append($"-- NOTE: the comment of column ""{name}"" is longer than MySQL allows (1024 characters) and was not written" & nl)
                    Else
                        Dim d As New SqlText()
                        ColDefinition(d, i, Nothing, False)
                        b.Append("ALTER TABLE " & QName() & " MODIFY COLUMN " & d.ToString() & " COMMENT " & SqlDialect.OtherText(cc, _dbms) & ";" & nl)
                    End If
                Case Else
                    b.Append("EXEC sp_addextendedproperty N'MS_Description', " & SqlDialect.OtherText(cc, _dbms) &
                             ", N'SCHEMA', " & SqlDialect.OtherText(schemaOrDbo, _dbms) & ", N'TABLE', " & SqlDialect.OtherText(_table, _dbms) &
                             ", N'COLUMN', " & SqlDialect.OtherText(name, _dbms) & ";" & nl)
            End Select
        Next
    End Sub
#End Region

End Class
