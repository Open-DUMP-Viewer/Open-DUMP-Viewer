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
                If original.Contains("(") Then
                    Return "NUMERIC" & original.Substring(original.IndexOf("("))
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
                If original.Contains("(") Then
                    Return "DECIMAL" & original.Substring(original.IndexOf("("))
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
                If original.Contains("(") Then
                    Return "DECIMAL" & original.Substring(original.IndexOf("("))
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
        Public Property RowCount As Long
        Public Property DataOffset As Long
    End Class

    ''' <summary>
    ''' メインフォームのアクティブなワークスペースから選択中のテーブル情報を取得
    ''' </summary>
    ''' <returns>テーブル情報。テーブル未選択時は Nothing</returns>
    Public Function GetActiveTableContext() As TableExportContext
        Dim mainForm = TryCast(Application.OpenForms(0), OraDB_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing

        Dim workspace = TryCast(mainForm.ActiveMdiChild, Workspace)
        If workspace Is Nothing Then Return Nothing

        Return workspace.GetSelectedTableExportContext()
    End Function
#End Region

End Module
