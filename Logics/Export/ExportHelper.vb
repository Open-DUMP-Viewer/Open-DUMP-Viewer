Imports System.IO
Imports System.Text

''' <summary>
''' エクスポート共通ヘルパー
''' 型マッピング、エスケープ関数、ダイアログ表示などの共通機能を提供
''' </summary>
Public Module ExportHelper

#Region "DBMS 定数"
    Public Const DBMS_ORACLE As Integer = 0
    Public Const DBMS_POSTGRES As Integer = 4
    Public Const DBMS_MYSQL As Integer = 5
    Public Const DBMS_SQLSERVER As Integer = 6
#End Region

#Region "SaveFileDialog"
    ''' <summary>
    ''' SaveFileDialog を表示し、選択されたファイルパスを返す
    ''' </summary>
    ''' <param name="filter">ファイルフィルタ (例: "CSV ファイル|*.csv")</param>
    ''' <param name="defaultFileName">デフォルトのファイル名</param>
    ''' <returns>選択されたパス。キャンセル時は Nothing</returns>
    Public Function ShowSaveFileDialog(filter As String, defaultFileName As String) As String
        Using dlg As New SaveFileDialog()
            dlg.Filter = filter
            dlg.FileName = defaultFileName
            dlg.OverwritePrompt = True
            dlg.RestoreDirectory = True
            If dlg.ShowDialog() = DialogResult.OK Then
                Return dlg.FileName
            End If
        End Using
        Return Nothing
    End Function
#End Region

#Region "CSV エスケープ (RFC 4180)"
    ''' <summary>
    ''' RFC 4180 準拠の CSV 値エスケープ
    ''' </summary>
    Public Function EscapeCsvValue(value As String) As String
        If value Is Nothing Then Return ""
        If value.Contains(","c) OrElse value.Contains(""""c) OrElse
           value.Contains(vbCr) OrElse value.Contains(vbLf) Then
            Return """" & value.Replace("""", """""") & """"
        End If
        Return value
    End Function
#End Region

#Region "SQL エスケープ"
    ''' <summary>
    ''' SQL 識別子をエスケープ (DBMS 別)
    ''' </summary>
    Public Function EscapeSqlIdentifier(name As String, dbmsType As Integer) As String
        Select Case dbmsType
            Case DBMS_MYSQL
                Return "`" & name.Replace("`", "``") & "`"
            Case DBMS_SQLSERVER
                Return "[" & name.Replace("]", "]]") & "]"
            Case Else ' Oracle, PostgreSQL
                Return """" & name.Replace("""", """""") & """"
        End Select
    End Function

    ''' <summary>
    ''' SQL 文字列リテラルをエスケープ
    ''' </summary>
    Public Function EscapeSqlString(value As String) As String
        If value Is Nothing Then Return "NULL"
        Return "'" & value.Replace("'", "''") & "'"
    End Function
#End Region

#Region "NUMBER 精度・スケール解析"
    ''' <summary>
    ''' Oracle NUMBER 型から精度とスケールを解析する
    ''' NUMBER(10,2) → prec=10, scale=2  /  NUMBER(10) → prec=10, scale=0
    ''' </summary>
    Private Sub ParseNumberPrecScale(original As String, ByRef prec As Integer, ByRef scale As Integer, ByRef hasParen As Boolean)
        prec = 0
        scale = 0
        hasParen = False
        Dim idx = original.IndexOf("("c)
        If idx < 0 Then Return
        hasParen = True
        Dim inner = original.Substring(idx + 1).TrimEnd(")"c, " "c)
        Dim parts = inner.Split(","c)
        Integer.TryParse(parts(0).Trim(), prec)
        If parts.Length > 1 Then Integer.TryParse(parts(1).Trim(), scale)
    End Sub

    ''' <summary>
    ''' スケール0の NUMBER 型を整数型にマッピングする
    ''' </summary>
    Private Function MapIntegerType(prec As Integer, smallInt As String, int_ As String, bigInt As String, fallback As String) As String
        If prec <= 4 Then Return smallInt
        If prec <= 9 Then Return int_
        If prec <= 18 Then Return bigInt
        Return fallback
    End Function
#End Region

#Region "裸 NUMBER → INTEGER 推定"
    ' 各整数型の範囲定数
    Private Const SMALLINT_MIN As Long = -32768L
    Private Const SMALLINT_MAX As Long = 32767L
    Private Const INT_MIN As Long = -2147483648L
    Private Const INT_MAX As Long = 2147483647L

    ''' <summary>
    ''' 裸の NUMBER カラム (精度・スケールなし) の実データを走査し、
    ''' 全行が整数値なら値の範囲に応じた NUMBER(n) に書き換えた配列を返す。
    ''' NUMBER(n) は後続の MapOracleType で INTEGER 系にマッピングされる。
    ''' </summary>
    Public Function InferIntegerTypes(columnTypes As String(), data As List(Of String()),
                                      Optional columnDefaults As String() = Nothing) As String()
        If columnTypes Is Nothing Then Return columnTypes

        Dim result = CType(columnTypes.Clone(), String())

        For colIdx As Integer = 0 To result.Length - 1
            Dim upper = If(result(colIdx), "").Trim().ToUpperInvariant()
            ' 裸の NUMBER のみが対象 (括弧なし)
            If upper <> "NUMBER" Then Continue For

            Dim allInteger = True
            Dim hasNonNull = False
            ' 値の範囲を追跡: 0=SMALLINT, 1=INT, 2=BIGINT, 3=超過
            Dim rangeLevel As Integer = 0

            ' DEFAULT 値も範囲判定に含める
            If columnDefaults IsNot Nothing AndAlso colIdx < columnDefaults.Length AndAlso
               Not String.IsNullOrEmpty(columnDefaults(colIdx)) Then
                Dim defVal As Long
                If Long.TryParse(columnDefaults(colIdx).Trim(), defVal) Then
                    If defVal < SMALLINT_MIN OrElse defVal > SMALLINT_MAX Then rangeLevel = 1
                    If defVal < INT_MIN OrElse defVal > INT_MAX Then rangeLevel = 2
                End If
            End If

            For Each row In data
                If colIdx >= row.Length Then Continue For
                Dim value = row(colIdx)
                If String.IsNullOrEmpty(value) Then Continue For

                hasNonNull = True

                ' 小数点を含むかチェック
                Dim intStr = value
                If value.Contains("."c) Then
                    Dim dotPos = value.IndexOf("."c)
                    Dim fraction = value.Substring(dotPos + 1)
                    If fraction.Trim("0"c).Length > 0 Then
                        allInteger = False
                        Exit For
                    End If
                    intStr = value.Substring(0, dotPos)
                End If

                ' 値の範囲を判定
                Dim numVal As Long
                If Long.TryParse(intStr, numVal) Then
                    If rangeLevel < 1 AndAlso (numVal < SMALLINT_MIN OrElse numVal > SMALLINT_MAX) Then
                        rangeLevel = 1 ' INT 以上
                    End If
                    If rangeLevel < 2 AndAlso (numVal < INT_MIN OrElse numVal > INT_MAX) Then
                        rangeLevel = 2 ' BIGINT 以上
                    End If
                Else
                    ' Long に収まらない → BIGINT 超過
                    rangeLevel = 3
                End If
            Next

            If hasNonNull AndAlso allInteger Then
                ' 範囲に応じた精度で NUMBER(n) を生成
                Select Case rangeLevel
                    Case 0 : result(colIdx) = "NUMBER(4)"  ' → SMALLINT
                    Case 1 : result(colIdx) = "NUMBER(9)"  ' → INT
                    Case 2 : result(colIdx) = "NUMBER(18)" ' → BIGINT
                    Case Else : result(colIdx) = "NUMBER(38)" ' → DECIMAL(38,0)
                End Select
            End If
        Next

        Return result
    End Function
#End Region

#Region "Oracle → 各 DBMS 型マッピング"
    ''' <summary>
    ''' Oracle 型文字列を対象 DBMS の型に変換
    ''' </summary>
    Public Function MapOracleType(oracleType As String, dbmsType As Integer) As String
        If String.IsNullOrEmpty(oracleType) Then Return "VARCHAR(255)"

        Dim upper = oracleType.Trim().ToUpperInvariant()
        Dim baseName = upper.Split("("c)(0).Trim()

        Select Case dbmsType
            Case DBMS_POSTGRES
                Return MapToPostgres(baseName, upper)
            Case DBMS_MYSQL
                Return MapToMySQL(baseName, upper)
            Case DBMS_SQLSERVER
                Return MapToSQLServer(baseName, upper)
            Case Else ' Oracle そのまま
                Return oracleType
        End Select
    End Function

    Private Function MapToPostgres(baseName As String, original As String) As String
        Select Case baseName
            Case "VARCHAR2", "NVARCHAR2"
                Return original.Replace("VARCHAR2", "VARCHAR").Replace("NVARCHAR2", "VARCHAR")
            Case "NUMBER"
                Dim prec, scale As Integer
                Dim hasParen As Boolean
                ParseNumberPrecScale(original, prec, scale, hasParen)
                If hasParen Then
                    If scale = 0 Then Return MapIntegerType(prec, "SMALLINT", "INTEGER", "BIGINT", $"NUMERIC({prec},0)")
                    If scale < 0 Then Return $"NUMERIC({prec + (-scale)},0)"
                    Return $"NUMERIC({prec},{scale})"
                End If
                Return "NUMERIC"
            Case "DATE"
                Return "TIMESTAMP"
            Case "TIMESTAMP"
                Return "TIMESTAMP"
            Case "CLOB", "NCLOB", "LONG"
                Return "TEXT"
            Case "BLOB", "RAW", "LONG RAW"
                Return "BYTEA"
            Case "BINARY_FLOAT"
                Return "REAL"
            Case "BINARY_DOUBLE"
                Return "DOUBLE PRECISION"
            Case "CHAR", "NCHAR"
                Return original.Replace("NCHAR", "CHAR")
            Case Else
                Return "TEXT"
        End Select
    End Function

    Private Function MapToMySQL(baseName As String, original As String) As String
        Select Case baseName
            Case "VARCHAR2", "NVARCHAR2"
                Return original.Replace("VARCHAR2", "VARCHAR").Replace("NVARCHAR2", "VARCHAR")
            Case "NUMBER"
                Dim prec, scale As Integer
                Dim hasParen As Boolean
                ParseNumberPrecScale(original, prec, scale, hasParen)
                If hasParen Then
                    If scale = 0 Then Return MapIntegerType(prec, "SMALLINT", "INT", "BIGINT", $"DECIMAL({prec},0)")
                    If scale < 0 Then Return $"DECIMAL({prec + (-scale)},0)"
                    Return $"DECIMAL({prec},{scale})"
                End If
                Return "DECIMAL(38,10)"
            Case "DATE", "TIMESTAMP"
                Return "DATETIME"
            Case "CLOB", "NCLOB", "LONG"
                Return "LONGTEXT"
            Case "BLOB", "RAW", "LONG RAW"
                Return "LONGBLOB"
            Case "BINARY_FLOAT"
                Return "FLOAT"
            Case "BINARY_DOUBLE"
                Return "DOUBLE"
            Case "CHAR", "NCHAR"
                Return original.Replace("NCHAR", "CHAR")
            Case Else
                Return "TEXT"
        End Select
    End Function

    Private Function MapToSQLServer(baseName As String, original As String) As String
        Select Case baseName
            Case "VARCHAR2"
                Return original.Replace("VARCHAR2", "NVARCHAR")
            Case "NVARCHAR2"
                Return original.Replace("NVARCHAR2", "NVARCHAR")
            Case "NUMBER"
                Dim prec, scale As Integer
                Dim hasParen As Boolean
                ParseNumberPrecScale(original, prec, scale, hasParen)
                If hasParen Then
                    If scale = 0 Then Return MapIntegerType(prec, "SMALLINT", "INT", "BIGINT", $"DECIMAL({prec},0)")
                    If scale < 0 Then Return $"DECIMAL({prec + (-scale)},0)"
                    Return $"DECIMAL({prec},{scale})"
                End If
                Return "DECIMAL(38,10)"
            Case "DATE", "TIMESTAMP"
                Return "DATETIME2"
            Case "CLOB", "NCLOB", "LONG"
                Return "NVARCHAR(MAX)"
            Case "BLOB", "RAW", "LONG RAW"
                Return "VARBINARY(MAX)"
            Case "BINARY_FLOAT"
                Return "REAL"
            Case "BINARY_DOUBLE"
                Return "FLOAT"
            Case "CHAR"
                Return original.Replace("CHAR", "NCHAR")
            Case "NCHAR"
                Return original
            Case Else
                Return "NVARCHAR(MAX)"
        End Select
    End Function
#End Region

#Region "数値判定"
    ''' <summary>
    ''' 値が数値として扱えるかどうかを判定
    ''' </summary>
    Public Function IsNumericValue(value As String) As Boolean
        If String.IsNullOrEmpty(value) Then Return False
        Dim dummy As Double
        Return Double.TryParse(value, dummy)
    End Function
#End Region

#Region "テーブルコンテキスト取得"
    ''' <summary>
    ''' アクティブなワークスペースから選択テーブルの情報を取得
    ''' </summary>
    Public Class TableExportContext
        Public Property DumpFilePath As String
        Public Property Schema As String
        Public Property TableName As String
        Public Property ColumnNames As String()
        Public Property ColumnTypes As String()
        Public Property ColumnNotNulls As Boolean()
        Public Property ColumnDefaults As String()
        Public Property RowCount As Long
        Public Property DataOffset As Long
    End Class

    ''' <summary>
    ''' メインフォームのアクティブなワークスペースまたはTablePreviewから選択中のテーブル情報を取得
    ''' </summary>
    ''' <returns>テーブル情報。テーブル未選択時は Nothing</returns>
    Public Function GetActiveTableContext() As TableExportContext
        Dim mainForm = TryCast(Application.OpenForms(0), OraDB_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing

        ' まず Workspace をチェック
        Dim workspace = TryCast(mainForm.ActiveMdiChild, Workspace)
        If workspace IsNot Nothing Then
            Return workspace.GetSelectedTableExportContext()
        End If

        ' 次に TablePreview をチェック
        Dim preview = TryCast(mainForm.ActiveMdiChild, TablePreview)
        If preview IsNot Nothing Then
            Return preview.GetExportContext()
        End If

        Return Nothing
    End Function

    ''' <summary>
    ''' メインフォームのアクティブなワークスペースから可視テーブル一覧を取得 (一括エクスポート用)
    ''' </summary>
    Public Function GetActiveVisibleTableContexts() As List(Of TableExportContext)
        Dim mainForm = TryCast(Application.OpenForms(0), OraDB_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing

        Dim workspace = TryCast(mainForm.ActiveMdiChild, Workspace)
        If workspace Is Nothing Then Return Nothing

        Return workspace.GetVisibleTableContexts()
    End Function

    ''' <summary>
    ''' メインフォームのアクティブなワークスペースを取得
    ''' </summary>
    Public Function GetActiveWorkspace() As Workspace
        Dim mainForm = TryCast(Application.OpenForms(0), OraDB_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing
        Return TryCast(mainForm.ActiveMdiChild, Workspace)
    End Function
#End Region

End Module
