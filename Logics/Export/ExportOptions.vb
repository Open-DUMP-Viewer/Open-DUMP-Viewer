''' <summary>
''' エクスポートオプション一元管理モジュール
''' 全エクスポート経路 (DLL/VB.NET、単一/一括) で共有される設定
''' My.Settings に永続化
''' </summary>
Public Module ExportOptions

    ''' <summary>日付フォーマット (0=SLASH, 1=COMPACT, 2=FULL, 3=CUSTOM)</summary>
    Public Property DateFormat As Integer = OraDB_NativeParser.DATE_FMT_SLASH

    ''' <summary>カスタム日付フォーマット文字列 (トークン: YYYY, MM, DD, HH24, MI, SS)</summary>
    Public Property CustomDateFormat As String = "YYYY-MM-DD HH24:MI:SS"

    ''' <summary>CSV: カラム名ヘッダ行を出力するか</summary>
    Public Property CsvWriteHeader As Boolean = True

    ''' <summary>CSV: カラム型行を出力するか</summary>
    Public Property CsvWriteTypes As Boolean = False

    ''' <summary>SQL: DROP TABLE + CREATE TABLE DDL を出力するか</summary>
    Public Property SqlCreateTable As Boolean = True

    ''' <summary>SQL: 裸の NUMBER を実データから整数型に推定するか</summary>
    Public Property SqlInferInteger As Boolean = False

    ''' <summary>
    ''' My.Settings から設定を読み込み
    ''' </summary>
    Public Sub Load()
        Try
            DateFormat = My.Settings.ExportDateFormat
            CustomDateFormat = My.Settings.ExportCustomDateFormat
            CsvWriteHeader = My.Settings.ExportCsvWriteHeader
            CsvWriteTypes = My.Settings.ExportCsvWriteTypes
            SqlCreateTable = My.Settings.ExportSqlCreateTable
            SqlInferInteger = My.Settings.ExportSqlInferInteger
        Catch
            ' 初回起動時や設定未登録時はデフォルト値を使用
        End Try
    End Sub

    ''' <summary>
    ''' My.Settings に設定を保存
    ''' </summary>
    Public Sub Save()
        Try
            My.Settings.ExportDateFormat = DateFormat
            My.Settings.ExportCustomDateFormat = CustomDateFormat
            My.Settings.ExportCsvWriteHeader = CsvWriteHeader
            My.Settings.ExportCsvWriteTypes = CsvWriteTypes
            My.Settings.ExportSqlCreateTable = SqlCreateTable
            My.Settings.ExportSqlInferInteger = SqlInferInteger
            My.Settings.Save()
        Catch
            ' 設定保存失敗は無視
        End Try
    End Sub

    ''' <summary>
    ''' 現在の日付フォーマット設定に基づいて日付文字列を整形
    ''' (VB.NET インメモリエクスポート用)
    ''' DLL経由の場合は odv_set_date_format で設定済みなので不要
    ''' </summary>
    Public Function FormatDateString(dateStr As String) As String
        ' DLL経由の場合は既にフォーマット適用済みなので、VB.NET側では変換不要
        ' ここではパススルー（DLL出力そのまま）
        Return dateStr
    End Function

End Module
