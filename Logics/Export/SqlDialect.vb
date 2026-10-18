Imports System.Text

''' <summary>
''' SQL エクスポートの方言ごとの書き方 (型の対応・リテラル・式)。
'''
''' DLL の SQL 出力 (Logics/DumpParser/odv_sql.c) と同じ規則で書く。同じ Oracle の型・同じ値は、
''' Workspace から (DLL の経路) でも TablePreview から (VB の経路) でも同じ SQL になる。
''' 規則の正本は SQL エクスポートの型の対応の仕様 (TYPE_MAPPING_SPEC)。ここを変えるときは odv_sql.c も同じに変える。
''' 長さ・行の長さはすべて UTF-8 のバイト数で数える (DLL と同じ。Oracle のリテラルの上限 4000 バイトと
''' SQL*Plus の 1 行の上限はバイト数)。
''' </summary>
Friend Module SqlDialect

#Region "定数"
    ''' <summary>SQL*Plus の 1 行の上限 (11.2 は 2,498 バイト) に収める行の長さ</summary>
    Public Const SQL_LINE_MAX As Integer = 2400
    ''' <summary>長いリテラルを分けるときの 1 片の長さ (書いた形のバイト数)</summary>
    Public Const SQL_PIECE As Integer = 1000
    ''' <summary>Oracle の文字列リテラル・VARCHAR2 の上限 (MAX_STRING_SIZE=STANDARD)</summary>
    Public Const ORA_STR_MAX As Integer = 4000
    ''' <summary>Oracle の SQL の RAW の上限</summary>
    Public Const ORA_RAW_MAX As Integer = 2000
    ''' <summary>PL/SQL の LONG / LONG RAW 変数の上限</summary>
    Public Const ORA_PLSQL_LONG As Integer = 32760
    ''' <summary>MySQL の行の大きさの上限</summary>
    Public Const MYSQL_ROW_MAX As Integer = 65535
    ''' <summary>解析エンジンがエクスポートで 1 値を全体で持つ上限 (ODV_FULL_VALUE_MAX = 512 MiB)。超えた値は NULL + NOTE</summary>
    Public Const FULL_VALUE_MAX_MIB As Integer = 512
#End Region

#Region "文字の種類 (C の ctype と同じく ASCII だけを見る)"
    Public Function IsCSpace(c As Char) As Boolean
        Return c = " "c OrElse c = ControlChars.Tab OrElse c = ControlChars.Lf OrElse c = ControlChars.Cr OrElse
               c = ChrW(11) OrElse c = ChrW(12)
    End Function

    Public Function IsAsciiDigit(c As Char) As Boolean
        Return c >= "0"c AndAlso c <= "9"c
    End Function

    Public Function IsAsciiAlpha(c As Char) As Boolean
        Return (c >= "A"c AndAlso c <= "Z"c) OrElse (c >= "a"c AndAlso c <= "z"c)
    End Function

    Public Function IsAsciiAlnum(c As Char) As Boolean
        Return IsAsciiAlpha(c) OrElse IsAsciiDigit(c)
    End Function

    Public Function IsHexDigit(c As Char) As Boolean
        Return IsAsciiDigit(c) OrElse (c >= "A"c AndAlso c <= "F"c) OrElse (c >= "a"c AndAlso c <= "f"c)
    End Function

    Public Function AsciiUpper(c As Char) As Char
        If c >= "a"c AndAlso c <= "z"c Then Return ChrW(AscW(c) - 32)
        Return c
    End Function

    Public Function AsciiUpper(s As String) As String
        Dim sb As New StringBuilder(s.Length)
        For Each c In s
            sb.Append(AsciiUpper(c))
        Next
        Return sb.ToString()
    End Function

    ''' <summary>C の atoi: 先頭の空白・符号・数字だけを読む (無ければ 0)</summary>
    Public Function Atoi(s As String) As Integer
        Dim i = 0
        While i < s.Length AndAlso IsCSpace(s(i))
            i += 1
        End While
        Dim neg = False
        If i < s.Length AndAlso (s(i) = "+"c OrElse s(i) = "-"c) Then
            neg = (s(i) = "-"c)
            i += 1
        End If
        Dim v As Long = 0
        While i < s.Length AndAlso IsAsciiDigit(s(i))
            v = v * 10 + (AscW(s(i)) - 48)
            If v > Integer.MaxValue Then v = Integer.MaxValue
            i += 1
        End While
        Return CInt(If(neg, -v, v))
    End Function
#End Region

#Region "UTF-8 のバイト数"
    ''' <summary>1 つの Char の UTF-8 のバイト数 (サロゲートの組は上位で 4、下位で 0)</summary>
    Public Function Utf8Bytes(c As Char) As Integer
        Dim v = Convert.ToInt32(c)
        If v < &H80 Then Return 1
        If v < &H800 Then Return 2
        If Char.IsHighSurrogate(c) Then Return 4
        If Char.IsLowSurrogate(c) Then Return 0
        Return 3
    End Function

    Public Function Utf8Count(s As String, start As Integer, count As Integer) As Integer
        Dim n = 0
        For i = start To start + count - 1
            n += Utf8Bytes(s(i))
        Next
        Return n
    End Function

    Public Function Utf8Count(s As String) As Integer
        If s Is Nothing Then Return 0
        Return Utf8Count(s, 0, s.Length)
    End Function

    ''' <summary>先頭から UTF-8 で maxBytes バイトまで (文字の途中では切らない。DLL の "%.40s" に当たる)</summary>
    Public Function Utf8Prefix(s As String, maxBytes As Integer) As String
        Dim i = 0, bytes = 0
        While i < s.Length
            Dim chars As Integer
            Dim n = RuneAt(s, i, chars)
            If bytes + n > maxBytes Then Exit While
            bytes += n
            i += chars
        End While
        Return s.Substring(0, i)
    End Function

    ''' <summary>位置 i の 1 文字の UTF-8 のバイト数。chars にその文字の Char の数 (1 か 2) を返す</summary>
    Public Function RuneAt(s As String, i As Integer, ByRef chars As Integer) As Integer
        Dim c = s(i)
        If Char.IsHighSurrogate(c) AndAlso i + 1 < s.Length AndAlso Char.IsLowSurrogate(s(i + 1)) Then
            chars = 2
            Return 4
        End If
        chars = 1
        Return Utf8Bytes(c)
    End Function
#End Region

#Region "文字列リテラル (仕様 4.2)"
    ''' <summary>Oracle のリテラルの中での 1 バイトの文字の長さ ('' は 2、CR/LF/NUL は '||CHR(n)||' の 12)</summary>
    Private Function OraCharCost(c As Char) As Integer
        If c = "'"c Then Return 2
        If c = ControlChars.Lf OrElse c = ControlChars.Cr OrElse c = ChrW(0) Then Return 12
        Return 1
    End Function

    Private Function OraCost(s As String, i As Integer, ByRef chars As Integer) As Integer
        Dim k = RuneAt(s, i, chars)
        Return If(k = 1, OraCharCost(s(i)), k)
    End Function

    ''' <summary>Oracle のリテラル 1 片: '...' (CR / LF / NUL は '||CHR(n)||' でつなぐ。H-20、M-22)</summary>
    Public Sub OraPiece(b As SqlText, s As String, start As Integer, count As Integer)
        Dim sb As New StringBuilder(count + 16)
        sb.Append("'"c)
        For i = start To start + count - 1
            Dim c = s(i)
            If c = "'"c Then
                sb.Append("''")
            ElseIf c = ControlChars.Lf OrElse c = ControlChars.Cr OrElse c = ChrW(0) Then
                sb.Append("'||CHR(").Append(Convert.ToInt32(c)).Append(")||'")
            Else
                sb.Append(c)
            End If
        Next
        sb.Append("'"c)
        b.Append(sb.ToString())
    End Sub

    ''' <summary>
    ''' pos から始まる片の長さ (Char の数): 書いた形で cap バイト以内、元の値で srcMax バイト以内、文字の境目で切る
    ''' </summary>
    Public Function NextPiece(s As String, pos As Integer, cap As Integer, srcMax As Integer) As Integer
        Dim i = pos, cost = 0, src = 0
        While i < s.Length
            Dim chars As Integer
            Dim k = RuneAt(s, i, chars)
            Dim c = If(k = 1, OraCharCost(s(i)), k)
            If i > pos AndAlso (cost + c > cap OrElse src + k > srcMax) Then Exit While
            cost += c
            src += k
            i += chars
        End While
        Return i - pos
    End Function

    Public Function OraRenderedLen(s As String) As Integer
        Dim r = 2, i = 0
        While i < s.Length
            Dim chars As Integer
            r += OraCost(s, i, chars)
            i += chars
        End While
        Return r
    End Function

    ''' <summary>Oracle の 4000 バイト以内の文字列: 1 つのリテラル、1 行に収まらなければ || でつないだ片を 1 片 1 行に</summary>
    Public Sub OraText(b As SqlText, s As String)
        If OraRenderedLen(s) <= SQL_PIECE Then
            OraPiece(b, s, 0, s.Length)
            Return
        End If
        Dim pos = 0
        While pos < s.Length
            Dim k = NextPiece(s, pos, SQL_PIECE, Integer.MaxValue)
            If pos > 0 Then b.Append("||" & vbLf)
            OraPiece(b, s, pos, k)
            pos += k
        End While
    End Sub

    ''' <summary>Oracle の長い文字列: TO_CLOB('..')||TO_CLOB('..') (fn = TO_CLOB / TO_NCLOB)。CLOB として連結するので 4000 バイトの上限に掛からない</summary>
    Public Sub OraClobConcat(b As SqlText, s As String, fn As String)
        Dim pos = 0
        While pos < s.Length
            Dim k = NextPiece(s, pos, SQL_PIECE, ORA_STR_MAX)
            If pos > 0 Then b.Append("||" & vbLf)
            b.Append(fn & "(")
            OraPiece(b, s, pos, k)
            b.Append(")"c)
            pos += k
        End While
    End Sub

    ''' <summary>
    ''' Oracle 以外の文字列リテラル。PostgreSQL '...' ('' のみ。\ はそのまま)、MySQL '...' ('' と \\ と \0)、
    ''' SQL Server N'...' (NUL は CONCAT(CAST(N'..' AS NVARCHAR(MAX)), NCHAR(0), N'..'))
    ''' </summary>
    Public Function OtherText(s As String, dbms As Integer) As String
        Dim sb As New StringBuilder(s.Length + 8)
        If dbms = ExportHelper.DBMS_SQLSERVER AndAlso s.IndexOf(ChrW(0)) >= 0 Then
            Dim st = 0
            sb.Append("CONCAT(CAST(N'")
            For i = 0 To s.Length
                If i = s.Length OrElse s(i) = ChrW(0) Then
                    sb.Append(s, st, i - st)
                    sb.Replace("'", "''", sb.Length - (i - st), i - st)
                    If st = 0 Then
                        sb.Append("' AS NVARCHAR(MAX))")
                    Else
                        sb.Append("'"c)
                    End If
                    If i < s.Length Then sb.Append(", NCHAR(0), N'")
                    st = i + 1
                End If
            Next
            sb.Append(")"c)
            Return sb.ToString()
        End If
        If dbms = ExportHelper.DBMS_SQLSERVER Then sb.Append("N"c)
        sb.Append("'"c)
        For Each c In s
            If c = "'"c Then
                sb.Append("''")
            ElseIf dbms = ExportHelper.DBMS_MYSQL AndAlso c = "\"c Then
                sb.Append("\\")
            ElseIf dbms = ExportHelper.DBMS_MYSQL AndAlso c = ChrW(0) Then
                sb.Append("\0")
            Else
                sb.Append(c)
            End If
        Next
        sb.Append("'"c)
        Return sb.ToString()
    End Function

    ''' <summary>識別子 (方言の引用符。名前の中の閉じ引用符は二重にする)</summary>
    Public Function QuoteIdent(name As String, dbms As Integer) As String
        Dim open = """"c, close = """"c
        If dbms = ExportHelper.DBMS_MYSQL Then
            open = "`"c : close = "`"c
        ElseIf dbms = ExportHelper.DBMS_SQLSERVER Then
            open = "["c : close = "]"c
        End If
        Return open & If(name, "").Replace(close.ToString(), close & close) & close
    End Function

    ''' <summary>"schema"."table" (スキーマが空なら表名だけ)</summary>
    Public Function QuoteQName(schema As String, table As String, dbms As Integer) As String
        If Not String.IsNullOrEmpty(schema) Then Return QuoteIdent(schema, dbms) & "." & QuoteIdent(table, dbms)
        Return QuoteIdent(table, dbms)
    End Function

    ''' <summary>PL/SQL の文字列リテラルの中に書く "name" (' を二重に)</summary>
    Public Function PlsqlIdent(name As String) As String
        Return """" & name.Replace("'", "''").Replace("""", """""") & """"
    End Function
#End Region

#Region "Oracle の型名の読み取り (仕様 0 章)"
    ''' <summary>型名を読んだ結果 (大文字、空白を詰めたもの)</summary>
    Public Class OraType
        ''' <summary>最初の括弧までの名前</summary>
        Public Base As String = ""
        Public NArgs As Integer
        Public A1 As String = ""
        Public A2 As String = ""
        ''' <summary>A1 が " CHAR" で終わっていた</summary>
        Public CharSem As Boolean
        ''' <summary>A1 が " BYTE" で終わっていた</summary>
        Public ByteSem As Boolean
        ''' <summary>最初の括弧の後ろ (WITH TIME ZONE、TO SECOND(9) など)</summary>
        Public Rest As String = ""
        ''' <summary>Rest の中の括弧の数 (TO SECOND(9) の 9)。無ければ -1</summary>
        Public RestArg As Integer = -1
    End Class

    ''' <summary>前後の空白を除き、ASCII を大文字にし、中の空白の並びを 1 つの空白にする</summary>
    Private Function TrimUpper(s As String) As String
        Dim a = 0, e = s.Length
        While a < e AndAlso IsCSpace(s(a))
            a += 1
        End While
        While e > a AndAlso IsCSpace(s(e - 1))
            e -= 1
        End While
        Dim sb As New StringBuilder(e - a)
        For i = a To e - 1
            Dim c = AsciiUpper(s(i))
            If IsCSpace(c) Then
                If sb.Length > 0 AndAlso sb(sb.Length - 1) = " "c Then Continue For
                c = " "c
            End If
            sb.Append(c)
        Next
        Return sb.ToString()
    End Function

    Public Function ParseOraType(ts As String) As OraType
        Dim t As New OraType()
        If ts Is Nothing Then ts = ""
        Dim lp = ts.IndexOf("("c)
        If lp < 0 Then
            t.Base = TrimUpper(ts)
            Return t
        End If
        t.Base = TrimUpper(ts.Substring(0, lp))
        Dim rp = ts.IndexOf(")"c, lp)
        Dim closed = (rp >= 0)
        If Not closed Then rp = ts.Length
        Dim inner = ts.Substring(lp + 1, rp - lp - 1)
        Dim comma = inner.IndexOf(","c)
        If comma >= 0 Then
            t.A1 = TrimUpper(inner.Substring(0, comma))
            t.A2 = TrimUpper(inner.Substring(comma + 1))
            t.NArgs = 2
        Else
            t.A1 = TrimUpper(inner)
            t.NArgs = If(t.A1.Length > 0, 1, 0)
        End If
        If t.A1.Length > 5 AndAlso t.A1.EndsWith(" CHAR", StringComparison.Ordinal) Then
            t.CharSem = True
            t.A1 = t.A1.Substring(0, t.A1.Length - 5)
        ElseIf t.A1.Length > 5 AndAlso t.A1.EndsWith(" BYTE", StringComparison.Ordinal) Then
            t.ByteSem = True
            t.A1 = t.A1.Substring(0, t.A1.Length - 5)
        End If
        If closed Then
            t.Rest = TrimUpper(ts.Substring(rp + 1))
            Dim q = t.Rest.IndexOf("("c)
            If q >= 0 Then t.RestArg = Atoi(t.Rest.Substring(q + 1))
        End If
        Return t
    End Function

    ''' <summary>括弧の数: '*' は 38 (Oracle の既定の精度)、無ければ -999</summary>
    Public Function TypeArg(a As String) As Integer
        If String.IsNullOrEmpty(a) Then Return -999
        If a(0) = "*"c Then Return 38
        Return Atoi(a)
    End Function

    ''' <summary>
    ''' Oracle の型名を exp と expdp で同じ 1 つの書き方にする (NUMBER(10, 0) → NUMBER(10)、TIMESTAMP (6) → TIMESTAMP(6)、
    ''' INTERVAL DAY (9) TO SECOND (9) → INTERVAL DAY(9) TO SECOND(9))。引用符付きの型名 (オブジェクト型) はそのまま
    ''' </summary>
    Public Function OraTypeCanon(ts As String) As String
        If String.IsNullOrEmpty(ts) Then Return "VARCHAR2(4000)"
        If ts.IndexOf(""""c) >= 0 Then Return ts
        Dim t = ParseOraType(ts)
        Dim b = t.Base
        If Not (b = "VARCHAR2" OrElse b = "NVARCHAR2" OrElse b = "CHAR" OrElse b = "NCHAR" OrElse b = "VARCHAR" OrElse
                b = "RAW" OrElse b = "NUMBER" OrElse b = "FLOAT" OrElse b = "UROWID" OrElse
                b.StartsWith("TIMESTAMP", StringComparison.Ordinal) OrElse b.StartsWith("INTERVAL", StringComparison.Ordinal)) Then
            Return ts
        End If
        Dim sb As New StringBuilder(b)
        If t.NArgs >= 1 Then
            sb.Append("("c).Append(t.A1)
            If t.CharSem Then
                sb.Append(" CHAR")
            ElseIf t.ByteSem Then
                sb.Append(" BYTE")
            End If
            ' NUMBER(p,0) は NUMBER(p)。ただし NUMBER(*,0) (INTEGER) は NUMBER(*) ではない
            If t.NArgs = 2 AndAlso Not (b = "NUMBER" AndAlso t.A2 = "0" AndAlso t.A1 <> "*") Then
                sb.Append(","c).Append(t.A2)
            End If
            sb.Append(")"c)
        End If
        If t.Rest.Length > 0 Then
            sb.Append(" "c)
            For i = 0 To t.Rest.Length - 1
                If t.Rest(i) = " "c AndAlso i + 1 < t.Rest.Length AndAlso t.Rest(i + 1) = "("c Then Continue For
                sb.Append(t.Rest(i))
            Next
        End If
        Return sb.ToString()
    End Function
#End Region

#Region "列の種類と型の写像 (仕様 2 章)"
    ''' <summary>値の書き方を決める列の種類</summary>
    Public Enum SqlKind
        Text
        NText
        Num
        FloatNum
        BinFloat
        BinDouble
        DateKind
        Timestamp
        TimestampTz
        TimestampLtz
        IntervalYm
        IntervalDs
        Raw
        LongRaw
        Blob
        Clob
        NClob
        LongText
        RowId
        BFile
        Xml
        Json
        Vector
        Bool
        Udt
    End Enum

    ''' <summary>列の種類と、値を書くときに要る型の情報</summary>
    Public Class SqlColumnInfo
        Public Kind As SqlKind = SqlKind.Text
        ''' <summary>TIMESTAMP / INTERVAL DAY TO SECOND の列の小数秒の桁</summary>
        Public TsFrac As Integer = 6
        ''' <summary>書き出し先の型が持てる小数秒の桁</summary>
        Public TgtFrac As Integer = 9
        ''' <summary>Oracle 以外の DECIMAL(p,s) の p (0 = 上限なし) と s</summary>
        Public DecP As Integer
        Public DecS As Integer
        ''' <summary>Oracle 以外で IEEE の倍精度に写した</summary>
        Public TgtDouble As Boolean
        ''' <summary>元の Oracle の型名 (NOTE に書く)</summary>
        Public OracleType As String = ""
    End Class

    ''' <summary>
    ''' 型名から列の種類を決める。DLL は解析器の型コードで決めるが、VB の経路は型名しか持たない。
    ''' 型が VARRAY・オブジェクト型 (引用符付きの名前) なら Udt (仕様 5 章)
    ''' </summary>
    Public Function KindOf(ts As String, t As OraType) As SqlKind
        Dim up = AsciiUpper(If(ts, ""))
        If t.Base.StartsWith("XMLTYPE", StringComparison.Ordinal) OrElse up.Contains("XMLTYPE") Then Return SqlKind.Xml
        ' "OWNER"."TYPE": ユーザー定義の型 (DLL の col_kind と同じく先頭の引用符で見る。REF "T" は違う)
        If up.Length > 0 AndAlso up(0) = """"c Then Return SqlKind.Udt
        Dim b = t.Base
        If b.StartsWith("TIMESTAMP", StringComparison.Ordinal) Then
            Dim all = TrimUpper(up)
            If all.Contains("LOCAL TIME ZONE") Then Return SqlKind.TimestampLtz
            If all.Contains("TIME ZONE") Then Return SqlKind.TimestampTz
            Return SqlKind.Timestamp
        End If
        If b.StartsWith("INTERVAL YEAR", StringComparison.Ordinal) Then Return SqlKind.IntervalYm
        If b.StartsWith("INTERVAL DAY", StringComparison.Ordinal) Then Return SqlKind.IntervalDs
        Select Case b
            Case "VARCHAR2", "VARCHAR", "CHAR", "CHARACTER"
                Return SqlKind.Text
            Case "NVARCHAR2", "NCHAR"
                Return SqlKind.NText
            Case "FLOAT"
                Return SqlKind.FloatNum
            Case "NUMBER", "INTEGER", "INT", "SMALLINT", "DECIMAL", "DEC", "NUMERIC", "REAL", "DOUBLE PRECISION"
                Return SqlKind.Num
            Case "BINARY_FLOAT"
                Return SqlKind.BinFloat
            Case "BINARY_DOUBLE"
                Return SqlKind.BinDouble
            Case "DATE"
                Return SqlKind.DateKind
            Case "RAW"
                Return SqlKind.Raw
            Case "LONG RAW"
                Return SqlKind.LongRaw
            Case "BLOB"
                Return SqlKind.Blob
            Case "CLOB"
                Return SqlKind.Clob
            Case "NCLOB"
                Return SqlKind.NClob
            Case "LONG"
                Return SqlKind.LongText
            Case "ROWID", "UROWID"
                Return SqlKind.RowId
            Case "BFILE"
                Return SqlKind.BFile
            Case "JSON"
                Return SqlKind.Json
            Case "VECTOR"
                Return SqlKind.Vector
            Case "BOOLEAN"
                Return SqlKind.Bool
            Case Else
                Return SqlKind.Text
        End Select
    End Function

    ''' <summary>数値の列 (値を引用符なしで書く) か</summary>
    Public Function IsNumericKind(k As SqlKind) As Boolean
        Return k = SqlKind.Num OrElse k = SqlKind.FloatNum OrElse k = SqlKind.BinFloat OrElse k = SqlKind.BinDouble
    End Function

    ''' <summary>値の種類と型名から (列の型名だけを持つ書き手用)</summary>
    Public Function KindOfType(oracleType As String) As SqlKind
        If oracleType Is Nothing Then Return SqlKind.Text
        Return KindOf(oracleType, ParseOraType(oracleType))
    End Function

    ''' <summary>表の NOTE: "-- NOTE: column "C": ..."</summary>
    Friend Sub NoteCol(tn As StringBuilder, col As String, msg As String)
        If tn Is Nothing Then Return
        PutNote(tn, "-- NOTE: column """, col, """: ", msg)
    End Sub

    ''' <summary>
    ''' "-- NOTE:" の 1 行 (DLL の put_note と同じ): head・col・mid の後に本文を切らずに全文。本文の中の改行
    ''' (CR / LF / CRLF) の後は "--   " で始めて注釈のまま続ける (改行の後が SQL として読まれないように)
    ''' </summary>
    Friend Sub PutNote(sb As StringBuilder, head As String, col As String, mid As String, msg As String)
        sb.Append(head).Append(col).Append(mid)
        Dim p = 0
        While p < msg.Length
            Dim nl = msg.IndexOfAny({ControlChars.Cr, ControlChars.Lf}, p)
            If nl < 0 Then
                sb.Append(msg, p, msg.Length - p)
                Exit While
            End If
            sb.Append(msg, p, nl - p)
            If msg(nl) = ControlChars.Cr AndAlso nl + 1 < msg.Length AndAlso msg(nl + 1) = ControlChars.Lf Then nl += 1
            sb.Append(vbLf & "--   ")
            p = nl + 1
        End While
        sb.Append(vbLf)
    End Sub

    Private Sub FitDecimal(ByRef p As Integer, ByRef s As Integer, maxp As Integer, maxs As Integer, ByRef clamped As Boolean)
        clamped = False
        If s > maxs Then s = maxs : clamped = True
        If p > maxp Then p = maxp : clamped = True
        If s > p Then s = p : clamped = True
    End Sub

    Private Function DbmsName(dbms As Integer) As String
        Select Case dbms
            Case ExportHelper.DBMS_POSTGRES : Return "PostgreSQL"
            Case ExportHelper.DBMS_MYSQL : Return "MySQL"
            Case Else : Return "SQL Server"
        End Select
    End Function

    ''' <summary>
    ''' 列の型を書き出し先の DBMS の型にする (CREATE TABLE)。型で表せないことは tn に表の NOTE として足す。
    ''' Oracle は型名を exp と expdp で同じ書き方にしたもの。他の DBMS は仕様 2 章の表のとおり。
    ''' </summary>
    ''' <param name="colName">列名 (NOTE に書く)</param>
    ''' <param name="ts">ダンプの型名</param>
    ''' <param name="info">値を書くときに要る型の情報を返す</param>
    ''' <param name="tn">表の NOTE (Nothing なら書かない)</param>
    ''' <param name="mysqlText">MySQL の行の大きさの上限を超える表: 長い VARCHAR を TEXT に</param>
    ''' <param name="hasDbTimeZone">ダンプに DBTIMEZONE がある (TIMESTAMP WITH LOCAL TIME ZONE の NOTE)</param>
    Public Function MapColumnType(colName As String, ts As String, dbms As Integer, ByRef info As SqlColumnInfo,
                                  tn As StringBuilder, mysqlText As Boolean, hasDbTimeZone As Boolean) As String
        If ts Is Nothing Then ts = ""
        Dim t = ParseOraType(ts)
        info = New SqlColumnInfo()
        info.OracleType = ts
        info.Kind = KindOf(ts, t)
        info.TsFrac = 6
        info.TgtFrac = 9
        If info.Kind = SqlKind.Timestamp OrElse info.Kind = SqlKind.TimestampTz OrElse info.Kind = SqlKind.TimestampLtz Then
            info.TsFrac = If(t.NArgs >= 1, TypeArg(t.A1), 6)
        ElseIf info.Kind = SqlKind.IntervalDs Then
            info.TsFrac = If(t.RestArg >= 0, t.RestArg, 6)
        End If
        If info.TsFrac < 0 OrElse info.TsFrac > 9 Then info.TsFrac = 6

        If dbms = ExportHelper.DBMS_ORACLE Then
            If info.Kind = SqlKind.Udt Then
                NoteCol(tn, colName, $"type {ts} must exist (CREATE TYPE) before this table is created")
            End If
            Return OraTypeCanon(ts)
        End If

        Dim isPg = (dbms = ExportHelper.DBMS_POSTGRES)
        Dim isMy = (dbms = ExportHelper.DBMS_MYSQL)
        Dim isMs = Not isPg AndAlso Not isMy
        Dim n = If(t.NArgs >= 1, TypeArg(t.A1), -999)

        Select Case info.Kind
            Case SqlKind.Text, SqlKind.NText
                Dim isChar = t.Base.StartsWith("CHAR", StringComparison.Ordinal) OrElse t.Base.StartsWith("NCHAR", StringComparison.Ordinal)
                If n < 1 Then n = If(isChar, 1, 4000)
                If isChar Then
                    If isMs Then
                        If t.CharSem Then
                            NoteCol(tn, colName, $"{ts} -> NCHAR({n}): a value with characters outside the BMP needs more than {n} UTF-16 units and does not fit")
                        End If
                        Return $"NCHAR({If(n > 4000, 4000, n)})"
                    ElseIf isMy AndAlso n > 255 Then
                        Return $"VARCHAR({n})"
                    End If
                    Return $"CHAR({n})"
                End If
                If isMs Then
                    Dim units = If(t.CharSem, 2 * n, n)
                    Return If(units > 4000, "NVARCHAR(MAX)", $"NVARCHAR({units})")
                End If
                If isMy AndAlso mysqlText AndAlso n > 255 Then Return "TEXT"
                Return $"VARCHAR({n})"

            Case SqlKind.Clob, SqlKind.NClob, SqlKind.LongText
                Return If(isPg, "TEXT", If(isMy, "LONGTEXT", "NVARCHAR(MAX)"))

            Case SqlKind.Num
                If t.NArgs = 0 Then
                    ' 精度なしの NUMBER: 38 桁で位取りは自由
                    If isPg Then Return "NUMERIC"
                    If isMy Then
                        info.DecP = 65 : info.DecS = 30
                    Else
                        info.DecP = 38 : info.DecS = 10
                    End If
                    NoteCol(tn, colName, $"NUMBER without precision -> DECIMAL({info.DecP},{info.DecS}): values with more than {info.DecP - info.DecS} integer digits do not fit, decimals beyond {info.DecS} places are rounded")
                    Return $"DECIMAL({info.DecP},{info.DecS})"
                End If
                Dim p = n
                Dim sc = If(t.NArgs >= 2, TypeArg(t.A2), 0)
                If p < 1 Then p = 38
                If sc = -999 Then sc = 0
                Dim pp, ss As Integer
                If sc < 0 Then
                    pp = p - sc : ss = 0
                ElseIf sc > p Then
                    pp = sc : ss = sc
                Else
                    pp = p : ss = sc
                End If
                Dim cl = False
                If isMy Then
                    FitDecimal(pp, ss, 65, 30, cl)
                ElseIf isMs Then
                    FitDecimal(pp, ss, 38, 38, cl)
                End If
                info.DecP = pp
                info.DecS = ss
                If cl Then
                    NoteCol(tn, colName, $"{ts} cannot be represented exactly; DECIMAL({pp},{ss}) is used (values that do not fit are noted per row)")
                End If
                Return $"{If(isPg, "NUMERIC", "DECIMAL")}({pp},{ss})"

            Case SqlKind.FloatNum
                Dim bp = If(n >= 1, n, 126)
                If isPg Then Return "NUMERIC"
                info.TgtDouble = True
                Dim dbl = If(isMy, "DOUBLE", "FLOAT(53)")
                If bp > 49 Then
                    NoteCol(tn, colName, $"{ts} holds up to {CInt(Math.Truncate(bp * 0.30103)) + 1} significant digits; {dbl} keeps 15 (values with more are noted per row)")
                End If
                Return dbl

            Case SqlKind.BinFloat
                Return If(isMy, "FLOAT", "REAL")

            Case SqlKind.BinDouble
                Return If(isPg, "DOUBLE PRECISION", If(isMy, "DOUBLE", "FLOAT(53)"))

            Case SqlKind.DateKind
                info.TgtFrac = 0
                Return If(isPg, "TIMESTAMP(0)", If(isMy, "DATETIME", "DATETIME2(0)"))

            Case SqlKind.Timestamp, SqlKind.TimestampTz, SqlKind.TimestampLtz
                Dim maxf = If(isMs, 7, 6)
                Dim f = Math.Min(info.TsFrac, maxf)
                info.TgtFrac = f
                Dim ty As String
                If info.Kind = SqlKind.Timestamp Then
                    ty = $"{If(isPg, "TIMESTAMP", If(isMy, "DATETIME", "DATETIME2"))}({f})"
                ElseIf isPg Then
                    ty = $"TIMESTAMPTZ({f})"
                ElseIf isMy Then
                    ty = $"DATETIME({f})"
                Else
                    ty = $"DATETIMEOFFSET({f})"
                End If
                If info.TsFrac > maxf Then
                    NoteCol(tn, colName, $"{ts}: {DbmsName(dbms)} keeps {maxf} fractional digits of a second; digits {maxf + 1}-{info.TsFrac} are dropped")
                End If
                If info.Kind = SqlKind.TimestampTz Then
                    If isPg Then
                        NoteCol(tn, colName, "TIMESTAMP WITH TIME ZONE -> TIMESTAMPTZ keeps the instant; the original offset / region is not kept")
                    ElseIf isMy Then
                        NoteCol(tn, colName, "TIMESTAMP WITH TIME ZONE -> DATETIME holding UTC; the offset / region is not kept")
                    Else
                        NoteCol(tn, colName, "TIMESTAMP WITH TIME ZONE -> DATETIMEOFFSET keeps the offset; a region name is written as its offset at that instant")
                    End If
                ElseIf info.Kind = SqlKind.TimestampLtz Then
                    If isMy Then NoteCol(tn, colName, "TIMESTAMP WITH LOCAL TIME ZONE -> DATETIME holding UTC")
                    If Not hasDbTimeZone Then
                        NoteCol(tn, colName, "the dump does not give DBTIMEZONE; values are the database's wall clock without an offset")
                    End If
                End If
                Return ty

            Case SqlKind.IntervalYm
                If isPg Then Return "INTERVAL YEAR TO MONTH"
                NoteCol(tn, colName, $"{ts} has no {DbmsName(dbms)} type; the value is kept as Oracle's text (+Y-M)")
                Return If(isMy, "VARCHAR(64)", "NVARCHAR(64)")

            Case SqlKind.IntervalDs
                Dim f = Math.Min(info.TsFrac, 6)
                If isPg Then
                    info.TgtFrac = f
                    If info.TsFrac > 6 Then
                        NoteCol(tn, colName, $"{ts}: PostgreSQL keeps 6 fractional digits of a second; digits 7-{info.TsFrac} are dropped")
                    End If
                    Return $"INTERVAL DAY TO SECOND({f})"
                End If
                NoteCol(tn, colName, $"{ts} has no {DbmsName(dbms)} type; the value is kept as Oracle's text (+D HH:MI:SS.F)")
                Return If(isMy, "VARCHAR(64)", "NVARCHAR(64)")

            Case SqlKind.Raw
                If n < 1 Then n = 2000
                If isPg Then Return "BYTEA"
                If isMy Then Return $"VARBINARY({n})"
                Return If(n > 8000, "VARBINARY(MAX)", $"VARBINARY({n})")

            Case SqlKind.LongRaw, SqlKind.Blob
                Return If(isPg, "BYTEA", If(isMy, "LONGBLOB", "VARBINARY(MAX)"))

            Case SqlKind.RowId
                If t.Base.StartsWith("UROWID", StringComparison.Ordinal) Then Return If(isMs, "NVARCHAR(MAX)", "TEXT")
                Return If(isMs, "NVARCHAR(18)", "VARCHAR(18)")

            Case SqlKind.BFile
                NoteCol(tn, colName, "BFILE has no counterpart; the locator is kept as text BFILENAME('dir', 'file')")
                Return If(isMs, "NVARCHAR(530)", "VARCHAR(530)")

            Case SqlKind.Xml
                Return If(isMy, "LONGTEXT", "XML")

            Case SqlKind.Json
                If isMy Then NoteCol(tn, colName, "MySQL's JSON type normalizes the document (key order, duplicate keys)")
                Return If(isMs, "NVARCHAR(MAX)", "JSON")

            Case SqlKind.Vector
                NoteCol(tn, colName, $"{ts} has no standard counterpart; kept as its text form [x,y,...]")
                Return If(isPg, "TEXT", If(isMy, "LONGTEXT", "NVARCHAR(MAX)"))

            Case SqlKind.Bool
                Return If(isMs, "BIT", "BOOLEAN")

            Case Else ' Udt
                NoteCol(tn, colName, $"Oracle type {ts} has no counterpart; values are written as NULL")
                Return If(isPg, "TEXT", If(isMy, "LONGTEXT", "NVARCHAR(MAX)"))
        End Select
    End Function

    ''' <summary>MySQL の行の大きさの見積り (1 列の型からバイト数)</summary>
    Public Function MySqlColBytes(ty As String) As Long
        If ty.StartsWith("VARCHAR(", StringComparison.Ordinal) Then Return 4L * Atoi(ty.Substring(8)) + 2
        If ty.StartsWith("CHAR(", StringComparison.Ordinal) Then Return 4L * Atoi(ty.Substring(5))
        If ty.StartsWith("VARBINARY(", StringComparison.Ordinal) Then Return CLng(Atoi(ty.Substring(10))) + 2
        If ty.StartsWith("DECIMAL(", StringComparison.Ordinal) Then Return Atoi(ty.Substring(8)) \ 2 + 2
        If ty.StartsWith("DATETIME", StringComparison.Ordinal) Then Return 8
        If ty.Contains("TEXT") OrElse ty.Contains("BLOB") OrElse ty = "JSON" Then Return 12
        Return 8
    End Function
#End Region

#Region "数値の値"
    ''' <summary>引用符なしで書ける数値の文字列か (符号、数字、小数点 1 つまで、指数)</summary>
    Public Function IsNumericLiteral(v As String) As Boolean
        If String.IsNullOrEmpty(v) Then Return False
        Dim i = 0
        If v(0) = "-"c OrElse v(0) = "+"c Then i = 1
        If i >= v.Length Then Return False
        Dim digits = 0, dots = 0
        While i < v.Length
            Dim c = v(i)
            If c = "."c Then
                dots += 1
                If dots > 1 Then Return False
            ElseIf IsAsciiDigit(c) Then
                digits += 1
            ElseIf (c = "E"c OrElse c = "e"c) AndAlso digits > 0 Then
                Dim q = i + 1
                If q < v.Length AndAlso (v(q) = "+"c OrElse v(q) = "-"c) Then q += 1
                If q >= v.Length Then Return False
                While q < v.Length
                    If Not IsAsciiDigit(v(q)) Then Return False
                    q += 1
                End While
                Return True
            Else
                Return False
            End If
            i += 1
        End While
        Return digits > 0
    End Function

    ''' <summary>10 進の文字列 (指数可) の整数部の桁・小数部の桁・有効数字の桁</summary>
    Public Sub NumberDigits(v As String, ByRef intDigits As Integer, ByRef fracDigits As Integer, ByRef sig As Integer)
        Dim p = 0
        If v.Length > 0 AndAlso (v(0) = "-"c OrElse v(0) = "+"c) Then p = 1
        Dim digs As New StringBuilder()
        Dim dot = -1
        While p < v.Length AndAlso v(p) <> "E"c AndAlso v(p) <> "e"c
            If v(p) = "."c Then
                dot = digs.Length
            ElseIf digs.Length < 160 Then
                digs.Append(v(p))
            End If
            p += 1
        End While
        Dim exp10 = 0
        If p < v.Length Then exp10 = Atoi(v.Substring(p + 1))
        Dim nd = digs.Length
        If dot < 0 Then dot = nd
        Dim first = -1, last = -1
        For i = 0 To nd - 1
            If digs(i) <> "0"c Then
                If first < 0 Then first = i
                last = i
            End If
        Next
        sig = If(first < 0, 0, last - first + 1)
        If first < 0 Then
            intDigits = 0 : fracDigits = 0
            Return
        End If
        Dim pos = dot + exp10
        intDigits = Math.Max(0, pos - first)
        fracDigits = Math.Max(0, last + 1 - pos)
    End Sub
#End Region

#Region "日付・時刻の値"
    ''' <summary>解析エンジンが SQL 用に渡す固定の形 SYYYY/MM/DD HH24:MI:SS[.F...] を読んだもの</summary>
    Public Class SqlDateTime
        Public Year As Integer
        Public Month As Integer
        Public Day As Integer
        Public Hour As Integer
        Public Minute As Integer
        Public Second As Integer
        ''' <summary>小数秒の数字 (書かれていた桁のまま)</summary>
        Public Frac As String = ""
    End Class

    ''' <summary>C の sscanf の %d と同じく、空白・符号・数字を読む</summary>
    Private Function ScanInt(s As String, ByRef p As Integer, ByRef v As Integer) As Boolean
        While p < s.Length AndAlso IsCSpace(s(p))
            p += 1
        End While
        Dim st = p
        If p < s.Length AndAlso (s(p) = "+"c OrElse s(p) = "-"c) Then p += 1
        Dim ds = p
        While p < s.Length AndAlso IsAsciiDigit(s(p))
            p += 1
        End While
        If p = ds Then
            p = st
            Return False
        End If
        v = Atoi(s.Substring(st, p - st))
        Return True
    End Function

    Private Function ScanLit(s As String, ByRef p As Integer, c As Char) As Boolean
        If c = " "c Then
            While p < s.Length AndAlso IsCSpace(s(p))
                p += 1
            End While
            Return True
        End If
        If p < s.Length AndAlso s(p) = c Then
            p += 1
            Return True
        End If
        Return False
    End Function

    ''' <summary>
    ''' "Y/M/D H:M:S[.F]" を読む (年は負もある)。rest に続きの文字列 (時差・地域の表示) を返す。
    ''' 読めなければ Nothing
    ''' </summary>
    Public Function ParseSqlDateTime(v As String, ByRef rest As String) As SqlDateTime
        rest = ""
        If v Is Nothing Then Return Nothing
        Dim t As New SqlDateTime()
        Dim p = 0
        If Not (ScanInt(v, p, t.Year) AndAlso ScanLit(v, p, "/"c) AndAlso ScanInt(v, p, t.Month) AndAlso ScanLit(v, p, "/"c) AndAlso
                ScanInt(v, p, t.Day) AndAlso ScanLit(v, p, " "c) AndAlso ScanInt(v, p, t.Hour) AndAlso ScanLit(v, p, ":"c) AndAlso
                ScanInt(v, p, t.Minute) AndAlso ScanLit(v, p, ":"c) AndAlso ScanInt(v, p, t.Second)) Then
            Return Nothing
        End If
        If p < v.Length AndAlso v(p) = "."c Then
            p += 1
            Dim st = p
            While p < v.Length AndAlso IsAsciiDigit(v(p)) AndAlso p - st < 15
                p += 1
            End While
            t.Frac = v.Substring(st, p - st)
        End If
        rest = v.Substring(p)
        If t.Year = 0 Then Return Nothing
        Return t
    End Function

    ''' <summary>SYYYY の年 (負は -0099)</summary>
    Public Function OraYear(y As Integer) As String
        If y < 0 Then Return "-" & (-y).ToString("0000")
        Return y.ToString("0000")
    End Function

    ' --- Oracle の暦 (1582-10-15 より前はユリウス暦。紀元前 1 年 = -1、0 年は無い) ---
    Private Function FloorDiv(a As Long, b As Long) As Long
        Dim q = a \ b
        If a Mod b <> 0 AndAlso ((a < 0) <> (b < 0)) Then q -= 1
        Return q
    End Function

    Private Function DaysFromCivil(y As Long, m As Integer, d As Integer) As Long
        y -= If(m <= 2, 1, 0)
        Dim era = FloorDiv(y, 400)
        Dim yoe = y - era * 400
        Dim doy As Long = (153 * (m + If(m > 2, -3, 9)) + 2) \ 5 + d - 1
        Dim doe = yoe * 365 + yoe \ 4 - yoe \ 100 + doy
        Return era * 146097 + doe - 719468
    End Function

    Private Sub CivilFromDays(z As Long, ByRef y As Long, ByRef m As Integer, ByRef d As Integer)
        z += 719468
        Dim era = FloorDiv(z, 146097)
        Dim doe = z - era * 146097
        Dim yoe = (doe - doe \ 1460 + doe \ 36524 - doe \ 146096) \ 365
        Dim doy = doe - (365 * yoe + yoe \ 4 - yoe \ 100)
        Dim mp = (5 * doy + 2) \ 153
        d = CInt(doy - (153 * mp + 2) \ 5 + 1)
        m = CInt(If(mp < 10, mp + 3, mp - 9))
        y = yoe + era * 400 + If(m <= 2, 1, 0)
    End Sub

    Private Const JDN_UNIX_EPOCH As Long = 2440588
    Private Const JDN_GREGORIAN_START As Long = 2299161

    Private Function DaysFromJulian(y As Long, m As Integer, d As Integer) As Long
        Dim a As Long = (14 - m) \ 12
        Dim yy = y + 4800 - a
        Dim mm = m + 12 * a - 3
        Return d + (153 * mm + 2) \ 5 + 365 * yy + FloorDiv(yy, 4) - 32083 - JDN_UNIX_EPOCH
    End Function

    Private Sub JulianFromDays(days As Long, ByRef y As Long, ByRef m As Integer, ByRef d As Integer)
        Dim j = days + JDN_UNIX_EPOCH
        Dim c = j + 32082
        Dim dd = FloorDiv(4 * c + 3, 1461)
        Dim e = c - FloorDiv(1461 * dd, 4)
        Dim mm = (5 * e + 2) \ 153
        d = CInt(e - (153 * mm + 2) \ 5 + 1)
        m = CInt(mm + 3 - 12 * (mm \ 10))
        y = dd - 4800 + mm \ 10
    End Sub

    Private Function DaysFromOracle(y As Integer, m As Integer, d As Integer) As Long
        Dim a As Long = If(y < 0, CLng(y) + 1, y)
        If y > 1582 OrElse (y = 1582 AndAlso (m > 10 OrElse (m = 10 AndAlso d >= 15))) Then Return DaysFromCivil(a, m, d)
        Return DaysFromJulian(a, m, d)
    End Function

    Private Sub OracleFromDays(days As Long, ByRef y As Integer, ByRef m As Integer, ByRef d As Integer)
        Dim a As Long
        If days + JDN_UNIX_EPOCH >= JDN_GREGORIAN_START Then
            CivilFromDays(days, a, m, d)
        Else
            JulianFromDays(days, a, m, d)
        End If
        y = CInt(If(a <= 0, a - 1, a))
    End Sub

    ''' <summary>時刻から秒を引いたもの (Oracle の暦で。小数秒はそのまま)</summary>
    Public Function ShiftSeconds(t As SqlDateTime, secs As Long) As SqlDateTime
        Dim total = DaysFromOracle(t.Year, t.Month, t.Day) * 86400L + t.Hour * 3600L + t.Minute * 60L + t.Second + secs
        Dim days = FloorDiv(total, 86400)
        Dim secOfDay = total - days * 86400
        Dim r As New SqlDateTime()
        OracleFromDays(days, r.Year, r.Month, r.Day)
        r.Hour = CInt(secOfDay \ 3600)
        r.Minute = CInt(secOfDay \ 60 Mod 60)
        r.Second = CInt(secOfDay Mod 60)
        r.Frac = t.Frac
        Return r
    End Function

    ''' <summary>"+09:00" / "-04:00" / "+00:19:32" (時差) を秒に。時差の形でなければ False</summary>
    Public Function ParseOffset(s As String, ByRef secs As Integer) As Boolean
        If String.IsNullOrEmpty(s) OrElse (s(0) <> "+"c AndAlso s(0) <> "-"c) Then Return False
        Dim parts = s.Substring(1).Split(":"c)
        If parts.Length < 2 OrElse parts.Length > 3 Then Return False
        Dim v(2) As Integer
        For i = 0 To parts.Length - 1
            If parts(i).Length = 0 OrElse Not parts(i).All(Function(c) IsAsciiDigit(c)) Then Return False
            v(i) = Integer.Parse(parts(i), Globalization.CultureInfo.InvariantCulture)
        Next
        secs = v(0) * 3600 + v(1) * 60 + v(2)
        If s(0) = "-"c Then secs = -secs
        Return True
    End Function

    ''' <summary>秒の時差を +HH:MM (秒があれば +HH:MM:SS) に</summary>
    Public Function FormatOffset(secs As Integer) As String
        Dim a = Math.Abs(secs)
        Dim s = $"{If(secs < 0, "-"c, "+"c)}{a \ 3600:00}:{a \ 60 Mod 60:00}"
        If a Mod 60 <> 0 Then s &= $":{a Mod 60:00}"
        Return s
    End Function
#End Region

#Region "制約の条件・索引の式・DEFAULT (仕様 3 章)"
    Private Function IsCondKeyword(w As String) As Boolean
        Select Case w
            Case "AND", "OR", "NOT", "IN", "BETWEEN", "LIKE", "IS", "NULL", "ESCAPE", "ASC", "DESC"
                Return True
        End Select
        Return False
    End Function

    ''' <summary>
    ''' Oracle の CHECK の条件・索引の式を他の DBMS 向けに書き直す。識別子・数・文字列・比較と論理と算術の演算子・括弧だけで
    ''' できている式に限る (関数呼び出し・||・DATE '...' などを含めば False。呼び出し側が NOTE を書く)
    ''' </summary>
    Public Function TranslateExpr(e As String, out As StringBuilder, dbms As Integer) As Boolean
        Dim p = 0
        Dim afterIs = 0  ' 1 = IS の後、2 = IS NOT の後: NULL だけを認める (IS JSON などは Oracle の述語)
        While p < e.Length
            Dim c = e(p)
            If IsCSpace(c) Then
                out.Append(" "c)
                While p < e.Length AndAlso IsCSpace(e(p))
                    p += 1
                End While
                Continue While
            End If
            If afterIs <> 0 AndAlso Not (IsAsciiAlpha(c) OrElse c = "_"c) Then Return False
            If c = """"c Then
                Dim q = e.IndexOf(""""c, p + 1)
                If q < 0 Then Return False
                out.Append(QuoteIdent(e.Substring(p + 1, q - p - 1), dbms))
                p = q + 1
                Continue While
            End If
            If c = "'"c Then
                Dim raw As New StringBuilder()
                p += 1
                Do
                    If p >= e.Length Then Return False
                    If e(p) = "'"c Then
                        If p + 1 < e.Length AndAlso e(p + 1) = "'"c Then
                            raw.Append("'"c)
                            p += 2
                            Continue Do
                        End If
                        p += 1
                        Exit Do
                    End If
                    raw.Append(e(p))
                    p += 1
                Loop
                out.Append(OtherText(raw.ToString(), If(dbms = ExportHelper.DBMS_SQLSERVER, ExportHelper.DBMS_POSTGRES, dbms)))
                Continue While
            End If
            If IsAsciiDigit(c) OrElse (c = "."c AndAlso p + 1 < e.Length AndAlso IsAsciiDigit(e(p + 1))) Then
                While p < e.Length AndAlso (IsAsciiAlnum(e(p)) OrElse e(p) = "."c OrElse
                      ((e(p) = "+"c OrElse e(p) = "-"c) AndAlso (e(p - 1) = "E"c OrElse e(p - 1) = "e"c)))
                    out.Append(e(p))
                    p += 1
                End While
                Continue While
            End If
            If IsAsciiAlpha(c) OrElse c = "_"c OrElse Convert.ToInt32(c) >= &H80 Then
                Dim w As New StringBuilder()
                While p < e.Length AndAlso (IsAsciiAlnum(e(p)) OrElse e(p) = "_"c OrElse e(p) = "$"c OrElse e(p) = "#"c OrElse
                                            Convert.ToInt32(e(p)) >= &H80)
                    w.Append(AsciiUpper(e(p)))
                    p += 1
                End While
                Dim word = w.ToString()
                Dim q = p
                While q < e.Length AndAlso IsCSpace(e(q))
                    q += 1
                End While
                If q < e.Length AndAlso e(q) = "("c AndAlso Not IsCondKeyword(word) Then Return False   ' 関数呼び出し
                If q < e.Length AndAlso e(q) = "'"c Then Return False                                    ' DATE '...' など
                If afterIs <> 0 Then
                    If afterIs = 1 AndAlso word = "NOT" Then
                        afterIs = 2
                    ElseIf word = "NULL" Then
                        afterIs = 0
                    Else
                        Return False
                    End If
                ElseIf word = "IS" Then
                    afterIs = 1
                End If
                If IsCondKeyword(word) Then
                    out.Append(word)
                Else
                    out.Append(QuoteIdent(word, dbms))
                End If
                Continue While
            End If
            If c = "|"c AndAlso p + 1 < e.Length AndAlso e(p + 1) = "|"c Then Return False
            If "()=<>!+-*/,".IndexOf(c) >= 0 Then
                If c = "!"c AndAlso p + 1 < e.Length AndAlso e(p + 1) = "="c Then
                    out.Append("<>")
                    p += 2
                    Continue While
                End If
                out.Append(c)
                p += 1
                Continue While
            End If
            Return False
        End While
        Return afterIs = 0
    End Function

    ''' <summary>
    ''' Oracle の DDL の文字列 (CHECK の条件・DEFAULT・索引の式) を SQL_LINE_MAX バイト以内の行に分けて書く。
    ''' リテラル・引用符付きの名前の外の空白で改行し、1,000 バイトを超えるリテラルは '...'||'...' の片に分ける (同じ値)
    ''' </summary>
    Public Sub PutWrapped(b As SqlText, text As String)
        Dim p = 0
        While p < text.Length
            Dim ch = text(p)
            If ch = "'"c Then
                Dim e = p + 1
                Dim nlit = (p > 0 AndAlso (text(p - 1) = "N"c OrElse text(p - 1) = "n"c) AndAlso
                            (p - 1 = 0 OrElse Not (IsAsciiAlnum(text(p - 2)) OrElse text(p - 2) = "_"c OrElse text(p - 2) = "$"c OrElse
                                                   text(p - 2) = "#"c OrElse text(p - 2) = """"c)))
                While e < text.Length
                    If text(e) = "'"c Then
                        If e + 1 < text.Length AndAlso text(e + 1) = "'"c Then
                            e += 2
                            Continue While
                        End If
                        Exit While
                    End If
                    e += 1
                End While
                Dim litBytes = Utf8Count(text, p, e - p)
                If b.Col > 0 AndAlso b.Col + litBytes + 2 > SQL_LINE_MAX Then
                    ' リテラルがこの行に入らない: 新しい行から (N'...' の N も連れて行く)
                    If nlit AndAlso b.Length > 0 Then
                        b.RemoveLast()
                        b.Append(ControlChars.Lf)
                        b.Append(text(p - 1))
                    Else
                        b.Append(ControlChars.Lf)
                    End If
                End If
                If litBytes <= SQL_PIECE Then
                    Dim endIdx = If(e < text.Length, e + 1, e)
                    b.Append(text.Substring(p, endIdx - p))
                    p = endIdx
                    Continue While
                End If
                b.Append("'"c)
                Dim cnt = 0
                Dim s = p + 1
                While s < e
                    Dim k As Integer, chars As Integer
                    If text(s) = "'"c Then
                        k = 2 : chars = 2
                    Else
                        k = RuneAt(text, s, chars)
                    End If
                    If cnt > 0 AndAlso cnt + k > SQL_PIECE Then
                        b.Append("'||" & vbLf)
                        If nlit Then b.Append("N"c)
                        b.Append("'"c)
                        cnt = 0
                    End If
                    b.Append(text.Substring(s, Math.Min(chars, e - s)))
                    cnt = If(text(s) = ControlChars.Lf, 0, cnt + k)
                    s += chars
                End While
                If e < text.Length Then
                    b.Append("'"c)
                    p = e + 1
                Else
                    p = e
                End If
                Continue While
            End If
            If ch = """"c Then
                Dim e = text.IndexOf(""""c, p + 1)
                Dim n = If(e >= 0, e - p + 1, text.Length - p)
                b.Append(text.Substring(p, n))
                p += n
                Continue While
            End If
            If ch = " "c AndAlso b.Col > SQL_LINE_MAX - 200 Then
                b.Append(ControlChars.Lf)
                p += 1
                Continue While
            End If
            If ch = ControlChars.Lf OrElse ch = ControlChars.Cr Then
                ' リテラルの外の改行は空白と同じ
                b.Append(ControlChars.Lf)
                p += 1
                Continue While
            End If
            b.Append(ch)
            p += 1
        End While
    End Sub

    ''' <summary>CHECK の条件の全体を囲む括弧を 1 組外し、前後の空白を除く ((A) OR (B) は外さない)</summary>
    Public Function StripOuterParens(text As String) As String
        Dim b = 0, e = text.Length
        While b < e AndAlso (text(b) = " "c OrElse text(b) = ControlChars.Tab OrElse text(b) = ControlChars.Lf OrElse text(b) = ControlChars.Cr)
            b += 1
        End While
        While e > b AndAlso (text(e - 1) = " "c OrElse text(e - 1) = ControlChars.Tab OrElse text(e - 1) = ControlChars.Lf OrElse text(e - 1) = ControlChars.Cr)
            e -= 1
        End While
        If e - b >= 2 AndAlso text(b) = "("c AndAlso text(e - 1) = ")"c Then
            Dim depth = 0
            Dim q As Char = ChrW(0)
            Dim p = b
            While p < e
                Dim c = text(p)
                If q <> ChrW(0) Then
                    If c = q Then q = ChrW(0)
                ElseIf c = "'"c OrElse c = """"c Then
                    q = c
                ElseIf c = "("c Then
                    depth += 1
                ElseIf c = ")"c Then
                    depth -= 1
                    If depth = 0 Then Exit While
                End If
                p += 1
            End While
            If p = e - 1 Then
                b += 1
                e -= 1
            End If
        End If
        Return text.Substring(b, e - b)
    End Function

    ''' <summary>索引の列の並びの、括弧の内側とカンマの前の空白を詰める (exp の ("A" , "B" ) を expdp と同じ形に)</summary>
    Public Function TrimParenWs(text As String) As String
        Dim sb As New StringBuilder(text.Length)
        Dim q As Char = ChrW(0), last As Char = ChrW(0)
        Dim p = 0
        While p < text.Length
            Dim c = text(p)
            If q <> ChrW(0) Then
                If c = q Then q = ChrW(0)
            ElseIf c = "'"c OrElse c = """"c Then
                q = c
            ElseIf c = " "c OrElse c = ControlChars.Tab Then
                Dim r = p
                While r < text.Length AndAlso (text(r) = " "c OrElse text(r) = ControlChars.Tab)
                    r += 1
                End While
                If last = "("c OrElse (r < text.Length AndAlso (text(r) = ")"c OrElse text(r) = ","c)) Then
                    p = r
                    Continue While
                End If
            End If
            last = c
            sb.Append(c)
            p += 1
        End While
        Return sb.ToString()
    End Function

    ''' <summary>
    ''' Oracle 以外へ移せる DEFAULT (数値・文字列リテラル・NULL) なら lit に方言の形を返す。他の式は False (呼び出し側が NOTE)
    ''' </summary>
    Public Function DefaultPortable(d As String, ByRef lit As String, dbms As Integer, kind As SqlKind) As Boolean
        lit = Nothing
        Dim a = 0, n = d.Length
        While a < n AndAlso IsCSpace(d(a))
            a += 1
        End While
        While n > a AndAlso (IsCSpace(d(n - 1)) OrElse d(n - 1) = ";"c)
            n -= 1
        End While
        If n <= a Then Return False
        ' exp は "(0)" のように括弧で包むことがある
        While n - a >= 2 AndAlso d(a) = "("c AndAlso d(n - 1) = ")"c
            a += 1
            n -= 1
            While a < n AndAlso IsCSpace(d(a))
                a += 1
            End While
            While n > a AndAlso IsCSpace(d(n - 1))
                n -= 1
            End While
        End While
        Dim p = d.Substring(a, n - a)
        If p.Length = 4 AndAlso String.Equals(p, "NULL", StringComparison.OrdinalIgnoreCase) Then
            lit = "NULL"
            Return True
        End If
        If p.Length >= 2 AndAlso p(0) = "'"c AndAlso p(p.Length - 1) = "'"c Then
            Dim raw As New StringBuilder()
            Dim i = 1
            While i + 1 < p.Length
                If p(i) = "'"c Then
                    If i + 2 < p.Length AndAlso p(i + 1) = "'"c Then
                        raw.Append("'"c)
                        i += 2
                        Continue While
                    End If
                    Return False   ' 'a' || 'b' など
                End If
                raw.Append(p(i))
                i += 1
            End While
            Dim t = OtherText(raw.ToString(), dbms)
            If dbms = ExportHelper.DBMS_MYSQL AndAlso (kind = SqlKind.Clob OrElse kind = SqlKind.NClob OrElse kind = SqlKind.LongText OrElse
                                                     kind = SqlKind.Json OrElse kind = SqlKind.Blob OrElse kind = SqlKind.Vector) Then
                t = "(" & t & ")"   ' MySQL の TEXT / BLOB / JSON の列は DEFAULT を式の形で
            End If
            lit = t
            Return True
        End If
        Dim j = 0, digits = 0
        If p(0) = "-"c OrElse p(0) = "+"c Then j = 1
        While j < p.Length
            Dim c = p(j)
            If IsAsciiDigit(c) Then
                digits += 1
            ElseIf c <> "."c AndAlso c <> "E"c AndAlso c <> "e"c AndAlso c <> "+"c AndAlso c <> "-"c Then
                Return False
            End If
            j += 1
        End While
        If digits = 0 Then Return False
        lit = p
        Return True
    End Function
#End Region

End Module

''' <summary>
''' SQL の文を組み立てる文字列。いまの行のバイト数 (UTF-8) を数える (SQL*Plus の 1 行の上限に合わせて改行するため)
''' </summary>
Friend NotInheritable Class SqlText
    Private ReadOnly _sb As New StringBuilder()
    Private _col As Integer

    Public Sub Append(s As String)
        If String.IsNullOrEmpty(s) Then Return
        _sb.Append(s)
        Dim nl = s.LastIndexOf(ControlChars.Lf)
        If nl >= 0 Then
            _col = SqlDialect.Utf8Count(s, nl + 1, s.Length - nl - 1)
        Else
            _col += SqlDialect.Utf8Count(s, 0, s.Length)
        End If
    End Sub

    Public Sub Append(c As Char)
        _sb.Append(c)
        If c = ControlChars.Lf Then
            _col = 0
        Else
            _col += SqlDialect.Utf8Bytes(c)
        End If
    End Sub

    Public Sub Append(t As SqlText)
        Append(t.ToString())
    End Sub

    ''' <summary>いまの行のバイト数</summary>
    Public ReadOnly Property Col As Integer
        Get
            Return _col
        End Get
    End Property

    ''' <summary>Char の数</summary>
    Public ReadOnly Property Length As Integer
        Get
            Return _sb.Length
        End Get
    End Property

    ''' <summary>最後の 1 文字を取り除く (改行は取り除かない前提)</summary>
    Public Sub RemoveLast()
        If _sb.Length = 0 Then Return
        _col -= SqlDialect.Utf8Bytes(_sb(_sb.Length - 1))
        _sb.Length -= 1
    End Sub

    Public Sub Clear()
        _sb.Clear()
        _col = 0
    End Sub

    Public Overrides Function ToString() As String
        Return _sb.ToString()
    End Function
End Class
