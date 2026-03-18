Imports System.Runtime.InteropServices
Imports System.Text

''' <summary>
''' OraDB_DumpParser.dll P/Invoke ラッパー
''' C言語ネイティブDLLとの連携を担当
''' </summary>
Public Class OraDB_NativeParser

#Region "定数"
    ' DLL名
    Private Const DLL_NAME As String = "OraDB_DumpParser.dll"

    ' Return codes
    Public Const ODV_OK As Integer = 0
    Public Const ODV_ERROR As Integer = -1
    Public Const ODV_ERROR_CANCELLED As Integer = -200

    ' Dump types
    Public Const DUMP_UNKNOWN As Integer = -1
    Public Const DUMP_EXPDP As Integer = 0
    Public Const DUMP_EXPDP_COMPRESS As Integer = 1
    Public Const DUMP_EXP As Integer = 10
    Public Const DUMP_EXP_DIRECT As Integer = 11

    ' Date format constants
    Public Const DATE_FMT_SLASH As Integer = 0      ' YYYY/MM/DD HH:MI:SS
    Public Const DATE_FMT_COMPACT As Integer = 1    ' YYYYMMDD
    Public Const DATE_FMT_FULL As Integer = 2       ' YYYYMMDDHHMMSS
    Public Const DATE_FMT_CUSTOM As Integer = 3     ' Custom format string
#End Region

#Region "コールバックデリゲート"
    ''' <summary>
    ''' 行データ配送コールバック (1行ごとに呼ばれる)
    ''' </summary>
    <UnmanagedFunctionPointer(CallingConvention.StdCall)>
    Public Delegate Sub RowCallback(
        schema As IntPtr,
        table As IntPtr,
        colCount As Integer,
        colNames As IntPtr,
        colValues As IntPtr,
        userData As IntPtr
    )

    ''' <summary>
    ''' 進捗通知コールバック (ファイル位置のパーセンテージが変わるたびに呼ばれる、最大101回)
    ''' </summary>
    <UnmanagedFunctionPointer(CallingConvention.StdCall)>
    Public Delegate Sub ProgressCallback(
        rowsProcessed As Long,
        currentTable As IntPtr,
        userData As IntPtr
    )

    ''' <summary>
    ''' テーブル発見コールバック (テーブルごとに呼ばれる)
    ''' dataOffset: DDLのファイル位置（odv_set_data_offsetで高速シークに使用）
    ''' </summary>
    <UnmanagedFunctionPointer(CallingConvention.StdCall)>
    Public Delegate Sub TableCallback(
        schema As IntPtr,
        table As IntPtr,
        colCount As Integer,
        colNames As IntPtr,
        colTypes As IntPtr,
        colNotNulls As IntPtr,
        colDefaults As IntPtr,
        rowCount As Long,
        dataOffset As Long,
        userData As IntPtr
    )
#End Region

#Region "DLLインポート"
    ' セッション管理
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_create_session(ByRef session As IntPtr) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_destroy_session(session As IntPtr) As Integer
    End Function

    ' 設定
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall, CharSet:=CharSet.Ansi)>
    Private Shared Function odv_set_dump_file(session As IntPtr, <MarshalAs(UnmanagedType.LPStr)> path As String) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_row_callback(session As IntPtr, cb As RowCallback, userData As IntPtr) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_progress_callback(session As IntPtr, cb As ProgressCallback, userData As IntPtr) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_table_callback(session As IntPtr, cb As TableCallback, userData As IntPtr) As Integer
    End Function

    ' データオフセット設定（高速シーク用）
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_data_offset(session As IntPtr, offset As Long) As Integer
    End Function

    ' フィルタ名はUTF-8でDLLに渡す（DLL側でUTF-8→dump charsetに逆変換して比較する）
    ' LPStrだとANSI(=SJIS)に変換されてしまい、DLL側のUTF-8→SJIS変換で文字化けする
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_table_filter(session As IntPtr,
        <MarshalAs(UnmanagedType.LPUTF8Str)> schema As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> table As String) As Integer
    End Function

    ' エクスポートオプション
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_date_format(session As IntPtr, fmt As Integer,
        <MarshalAs(UnmanagedType.LPUTF8Str)> customFmt As String) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_csv_options(session As IntPtr, writeHeader As Integer, writeTypes As Integer) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_sql_options(session As IntPtr, createTable As Integer) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_set_app_version(session As IntPtr,
        <MarshalAs(UnmanagedType.LPUTF8Str)> ver As String) As Integer
    End Function

    ' 操作
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_check_dump_kind(session As IntPtr, ByRef dumpType As Integer) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_list_tables(session As IntPtr) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_parse_dump(session As IntPtr) As Integer
    End Function

    ' テーブル名はUTF-8で渡す（DLL側でUTF-8→dump charsetに変換して比較するため）
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall, CharSet:=CharSet.Ansi)>
    Private Shared Function odv_export_csv(session As IntPtr,
        <MarshalAs(UnmanagedType.LPUTF8Str)> tableName As String,
        <MarshalAs(UnmanagedType.LPStr)> outputPath As String) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall, CharSet:=CharSet.Ansi)>
    Private Shared Function odv_export_sql(session As IntPtr,
        <MarshalAs(UnmanagedType.LPUTF8Str)> tableName As String,
        <MarshalAs(UnmanagedType.LPStr)> outputPath As String,
        dbmsType As Integer) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_cancel(session As IntPtr) As Integer
    End Function

    ' LOB抽出
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_extract_lob(session As IntPtr,
        <MarshalAs(UnmanagedType.LPUTF8Str)> schema As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> table As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> lobColumn As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> outputDir As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> filenameCol As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> extension As String,
        dataOffset As Long) As Integer
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_get_lob_files_written(session As IntPtr) As Long
    End Function

    ' ユーティリティ
    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_get_version() As IntPtr
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_get_last_error(session As IntPtr) As IntPtr
    End Function

    <DllImport(DLL_NAME, CallingConvention:=CallingConvention.StdCall)>
    Private Shared Function odv_get_progress_pct(session As IntPtr) As Integer
    End Function
#End Region

#Region "ヘルパーメソッド"
    ''' <summary>
    ''' IntPtr (char*) からマネージド文字列に変換 (UTF-8)
    ''' </summary>
    Private Shared Function PtrToStringUTF8(ptr As IntPtr) As String
        If ptr = IntPtr.Zero Then Return String.Empty
        Return Marshal.PtrToStringUTF8(ptr)
    End Function

    ''' <summary>
    ''' セッションにエクスポートオプションを適用
    ''' </summary>
    Private Shared Sub ApplyExportOptions(session As IntPtr)
        odv_set_date_format(session, ExportOptions.DateFormat, ExportOptions.CustomDateFormat)
        odv_set_csv_options(session, If(ExportOptions.CsvWriteHeader, 1, 0), If(ExportOptions.CsvWriteTypes, 1, 0))
        odv_set_sql_options(session, If(ExportOptions.SqlCreateTable, 1, 0))

        ' アプリバージョンをDLLに渡す（エクスポートコメントに使用）
        Dim ver As String = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version?.ToString()
        If Not String.IsNullOrEmpty(ver) Then
            odv_set_app_version(session, ver)
        End If
    End Sub

    ''' <summary>
    ''' char** 配列から String配列に変換
    ''' </summary>
    Private Shared Function PtrArrayToStrings(ptrArray As IntPtr, count As Integer) As String()
        Dim result(count - 1) As String
        For i As Integer = 0 To count - 1
            Dim strPtr As IntPtr = Marshal.ReadIntPtr(ptrArray, i * IntPtr.Size)
            result(i) = PtrToStringUTF8(strPtr)
        Next
        Return result
    End Function
#End Region

#Region "パースコンテキスト"
    ''' <summary>
    ''' DLL解析中の状態を保持するコンテキスト
    ''' GCHandle経由でコールバックに渡される
    ''' </summary>
    Public Class ParseContext
        Public AllData As New Dictionary(Of String, Dictionary(Of String, List(Of String())))
        Public RowsProcessed As Long = 0
        Public CurrentTable As String = ""
        Public ProgressAction As Action(Of Long, String, Integer) = Nothing
        Public ColumnNamesCache As New Dictionary(Of String, String())
        Public SessionHandle As IntPtr = IntPtr.Zero

        ' テーブルフィルタ（Nothing=全テーブル、指定時=そのテーブルのみ蓄積）
        Public FilterSchema As String = Nothing
        Public FilterTable As String = Nothing

        ' List<T>の初期容量に使う期待行数 (ListTablesで取得済みの行数)
        Public ExpectedRowCount As Long = 0

        ' テーブル切替検出用キャッシュ（毎行のマーシャリングを回避）
        Public LastSchema As String = ""
        Public LastTable As String = ""
        Private LastSchemaPtr As IntPtr = IntPtr.Zero
        Private LastTablePtr As IntPtr = IntPtr.Zero
        Private LastTableKey As String = ""

        ''' <summary>
        ''' テーブルのフルキー (schema.table) — String版
        ''' </summary>
        Public Shared Function TableKey(schema As String, table As String) As String
            Return $"{schema}.{table}"
        End Function

        ''' <summary>
        ''' テーブルのフルキー (IntPtr版) — ポインタ値でテーブル切替を高速検出
        ''' ポインタが変わらなければ同じテーブル（DLL側はバッファ再利用）
        ''' </summary>
        Public Function TableKey(schemaPtr As IntPtr, tablePtr As IntPtr) As String
            ' ポインタが前回と同じなら高速パス
            If schemaPtr = LastSchemaPtr AndAlso tablePtr = LastTablePtr Then
                Return LastTableKey
            End If

            ' 新しいテーブル: 文字列をマーシャリングしてキーを構築
            LastSchemaPtr = schemaPtr
            LastTablePtr = tablePtr
            LastSchema = String.Intern(PtrToStringUTF8(schemaPtr))
            LastTable = String.Intern(PtrToStringUTF8(tablePtr))
            LastTableKey = $"{LastSchema}.{LastTable}"
            Return LastTableKey
        End Function
    End Class
#End Region

#Region "テーブル一覧コンテキスト"
    ''' <summary>
    ''' ListTables用のコンテキスト
    ''' </summary>
    Public Class ListTablesContext
        Public Tables As List(Of Tuple(Of String, String, Integer, Long, Long))
        ''' <summary>テーブルごとのカラム名 (キー: "schema.table")</summary>
        Public ColumnNames As New Dictionary(Of String, String())
        ''' <summary>テーブルごとのカラム型 (キー: "schema.table")</summary>
        Public ColumnTypes As New Dictionary(Of String, String())
        ''' <summary>テーブルごとの NOT NULL フラグ (キー: "schema.table")</summary>
        Public ColumnNotNulls As New Dictionary(Of String, Boolean())
        ''' <summary>テーブルごとの DEFAULT 値 (キー: "schema.table")</summary>
        Public ColumnDefaults As New Dictionary(Of String, String())
    End Class
#End Region

#Region "公開API"
    ''' <summary>
    ''' DLLバージョンを取得
    ''' </summary>
    Public Shared Function GetVersion() As String
        Try
            Dim ptr = odv_get_version()
            Return PtrToStringUTF8(ptr)
        Catch ex As Exception
            Return "N/A"
        End Try
    End Function

    ''' <summary>
    ''' ダンプファイルの種類を判定
    ''' </summary>
    Public Shared Function CheckDumpKind(filePath As String) As Integer
        Dim session As IntPtr = IntPtr.Zero
        Try
            Dim rc = odv_create_session(session)
            If rc <> ODV_OK Then Return DUMP_UNKNOWN

            rc = odv_set_dump_file(session, filePath)
            If rc <> ODV_OK Then Return DUMP_UNKNOWN

            Dim dumpType As Integer = DUMP_UNKNOWN
            rc = odv_check_dump_kind(session, dumpType)
            Return dumpType
        Catch
            Return DUMP_UNKNOWN
        Finally
            If session <> IntPtr.Zero Then
                odv_destroy_session(session)
            End If
        End Try
    End Function

    ''' <summary>
    ''' ダンプファイルを解析し、テーブルデータを返す
    ''' </summary>
    ''' <param name="filePath">ダンプファイルパス</param>
    ''' <param name="progressAction">進捗コールバック (処理行数, 現在のテーブル名, パーセンテージ0-100)</param>
    ''' <param name="filterSchema">フィルタ: このスキーマのテーブルのみ蓄積 (Nothing=全スキーマ)</param>
    ''' <param name="filterTable">フィルタ: このテーブルのみ蓄積 (Nothing=全テーブル)</param>
    ''' <returns>スキーマ→テーブル→行リストの辞書</returns>
    Public Shared Function ParseDump(filePath As String,
                                      Optional progressAction As Action(Of Long, String, Integer) = Nothing,
                                      Optional filterSchema As String = Nothing,
                                      Optional filterTable As String = Nothing,
                                      Optional dataOffset As Long = 0,
                                      Optional expectedRowCount As Long = 0) As Dictionary(Of String, Dictionary(Of String, List(Of String())))
        Dim session As IntPtr = IntPtr.Zero
        Dim ctx As New ParseContext()
        ctx.ProgressAction = progressAction
        ctx.FilterSchema = filterSchema
        ctx.FilterTable = filterTable
        ctx.ExpectedRowCount = expectedRowCount
        Dim gcHandle As GCHandle = GCHandle.Alloc(ctx)

        ' コールバックデリゲートをフィールドに保持（GC回収防止）
        Dim rowCb As New RowCallback(AddressOf OnRowCallback)
        Dim progCb As New ProgressCallback(AddressOf OnProgressCallback)

        Try
            Dim rc = odv_create_session(session)
            If rc <> ODV_OK Then
                Throw New Exception(Loc.SF("Parser_SessionCreateFailed", rc))
            End If

            ' セッションハンドルをコンテキストに保持（進捗%取得用）
            ctx.SessionHandle = session

            rc = odv_set_dump_file(session, filePath)
            If rc <> ODV_OK Then
                Dim errMsg = PtrToStringUTF8(odv_get_last_error(session))
                Throw New Exception(Loc.SF("Parser_DumpFileSettingError", errMsg))
            End If

            ' エクスポートオプション適用（日付フォーマット等）
            ApplyExportOptions(session)

            ' コールバック設定
            Dim userData As IntPtr = GCHandle.ToIntPtr(gcHandle)
            odv_set_row_callback(session, rowCb, userData)
            odv_set_progress_callback(session, progCb, userData)

            ' テーブルフィルタ設定（DLL側で文字セット変換後に比較）
            If filterTable IsNot Nothing AndAlso filterTable.Length > 0 Then
                odv_set_table_filter(session, If(filterSchema, ""), filterTable)
            End If

            ' データオフセット設定（高速シーク: list_tablesで取得したDDL位置にジャンプ）
            If dataOffset > 0 Then
                odv_set_data_offset(session, dataOffset)
            End If

            ' 解析実行
            rc = odv_parse_dump(session)
            If rc <> ODV_OK AndAlso rc <> ODV_ERROR_CANCELLED Then
                Dim errMsg = PtrToStringUTF8(odv_get_last_error(session))
                Throw New Exception(Loc.SF("Parser_DumpParseError", errMsg))
            End If

            Return ctx.AllData

        Finally
            ctx.SessionHandle = IntPtr.Zero
            If session <> IntPtr.Zero Then
                odv_destroy_session(session)
            End If
            If gcHandle.IsAllocated Then
                gcHandle.Free()
            End If
        End Try
    End Function

    ''' <summary>
    ''' テーブル一覧のみ取得（データは読み込まない）
    ''' 戻り値: (スキーマ, テーブル, カラム数, 行数, データオフセット)
    ''' </summary>
    ''' <param name="filePath">ダンプファイルのパス</param>
    ''' <param name="columnNamesMap">テーブルごとのカラム名辞書 (キー: "schema.table")</param>
    Public Shared Function ListTables(filePath As String,
                                      Optional ByRef columnNamesMap As Dictionary(Of String, String()) = Nothing,
                                      Optional ByRef columnTypesMap As Dictionary(Of String, String()) = Nothing,
                                      Optional ByRef columnNotNullsMap As Dictionary(Of String, Boolean()) = Nothing,
                                      Optional ByRef columnDefaultsMap As Dictionary(Of String, String()) = Nothing) As List(Of Tuple(Of String, String, Integer, Long, Long))
        Dim session As IntPtr = IntPtr.Zero
        Dim ctx As New ListTablesContext()
        ctx.Tables = New List(Of Tuple(Of String, String, Integer, Long, Long))
        Dim gcHandle As GCHandle = GCHandle.Alloc(ctx)
        Dim tableCb As New TableCallback(AddressOf OnTableListCallback)
        Dim progCb As New ProgressCallback(AddressOf OnListTablesProgressCallback)

        Try
            Dim rc = odv_create_session(session)
            If rc <> ODV_OK Then Return ctx.Tables

            rc = odv_set_dump_file(session, filePath)
            If rc <> ODV_OK Then Return ctx.Tables

            Dim userData As IntPtr = GCHandle.ToIntPtr(gcHandle)
            odv_set_table_callback(session, tableCb, userData)
            odv_set_progress_callback(session, progCb, userData)

            odv_list_tables(session)
            columnNamesMap = ctx.ColumnNames
            columnTypesMap = ctx.ColumnTypes
            columnNotNullsMap = ctx.ColumnNotNulls
            columnDefaultsMap = ctx.ColumnDefaults
            Return ctx.Tables

        Finally
            If session <> IntPtr.Zero Then
                odv_destroy_session(session)
            End If
            If gcHandle.IsAllocated Then
                gcHandle.Free()
            End If
        End Try
    End Function

    ''' <summary>
    ''' CSVエクスポート
    ''' </summary>
    ''' <param name="filePath">DUMPファイルパス</param>
    ''' <param name="tableName">テーブル名</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <param name="schema">スキーマ名 (テーブルフィルタ用)</param>
    ''' <param name="dataOffset">データオフセット (高速シーク用、0=先頭から)</param>
    Public Shared Function ExportCsv(filePath As String, tableName As String, outputPath As String,
                                      Optional schema As String = Nothing,
                                      Optional dataOffset As Long = 0) As Integer
        Dim session As IntPtr = IntPtr.Zero
        Try
            Dim rc = odv_create_session(session)
            If rc <> ODV_OK Then Return rc

            rc = odv_set_dump_file(session, filePath)
            If rc <> ODV_OK Then Return rc

            ' エクスポートオプション適用
            ApplyExportOptions(session)

            ' 高速シーク: DDL位置にジャンプ + テーブルフィルタ設定
            If dataOffset > 0 Then
                odv_set_data_offset(session, dataOffset)
            End If
            If tableName IsNot Nothing AndAlso tableName.Length > 0 Then
                odv_set_table_filter(session, If(schema, ""), tableName)
            End If

            Return odv_export_csv(session, tableName, outputPath)

        Finally
            If session <> IntPtr.Zero Then
                odv_destroy_session(session)
            End If
        End Try
    End Function

    ''' <summary>
    ''' SQL INSERT文エクスポート
    ''' </summary>
    ''' <param name="filePath">DUMPファイルパス</param>
    ''' <param name="tableName">テーブル名</param>
    ''' <param name="outputPath">出力先ファイルパス</param>
    ''' <param name="dbmsType">DBMS種別 (0=Oracle, 4=PostgreSQL, 5=MySQL, 6=SQL Server)</param>
    ''' <param name="schema">スキーマ名 (テーブルフィルタ用)</param>
    ''' <param name="dataOffset">データオフセット (高速シーク用、0=先頭から)</param>
    Public Shared Function ExportSql(filePath As String, tableName As String, outputPath As String, dbmsType As Integer,
                                      Optional schema As String = Nothing,
                                      Optional dataOffset As Long = 0) As Integer
        Dim session As IntPtr = IntPtr.Zero
        Try
            Dim rc = odv_create_session(session)
            If rc <> ODV_OK Then Return rc

            rc = odv_set_dump_file(session, filePath)
            If rc <> ODV_OK Then Return rc

            ' エクスポートオプション適用
            ApplyExportOptions(session)

            ' 高速シーク: DDL位置にジャンプ + テーブルフィルタ設定
            If dataOffset > 0 Then
                odv_set_data_offset(session, dataOffset)
            End If
            If tableName IsNot Nothing AndAlso tableName.Length > 0 Then
                odv_set_table_filter(session, If(schema, ""), tableName)
            End If

            Return odv_export_sql(session, tableName, outputPath, dbmsType)

        Finally
            If session <> IntPtr.Zero Then
                odv_destroy_session(session)
            End If
        End Try
    End Function
    ''' <summary>
    ''' LOBカラムのデータをファイルとして抽出
    ''' </summary>
    ''' <param name="filePath">DUMPファイルパス</param>
    ''' <param name="schema">スキーマ名</param>
    ''' <param name="tableName">テーブル名</param>
    ''' <param name="lobColumn">LOBカラム名 (BLOB/CLOB/NCLOB)</param>
    ''' <param name="outputDir">出力ディレクトリ</param>
    ''' <param name="filenameCol">ファイル名に使用するカラム名 (Nothing=連番)</param>
    ''' <param name="extension">ファイル拡張子 (Nothing="lob")</param>
    ''' <param name="dataOffset">データオフセット (高速シーク用、0=先頭から)</param>
    ''' <param name="progressAction">進捗コールバック (処理行数, 現在のテーブル名, パーセンテージ0-100)</param>
    ''' <returns>書き出したファイル数 (エラー時は-1)</returns>
    Public Shared Function ExtractLob(filePath As String, schema As String, tableName As String,
                                       lobColumn As String, outputDir As String,
                                       Optional filenameCol As String = Nothing,
                                       Optional extension As String = Nothing,
                                       Optional dataOffset As Long = 0,
                                       Optional progressAction As Action(Of Long, String, Integer) = Nothing) As Long
        Dim session As IntPtr = IntPtr.Zero
        Dim ctx As New ParseContext()
        ctx.ProgressAction = progressAction
        Dim gcHandle As GCHandle = GCHandle.Alloc(ctx)
        Dim progCb As New ProgressCallback(AddressOf OnProgressCallback)

        Try
            Dim rc = odv_create_session(session)
            If rc <> ODV_OK Then Return -1

            ctx.SessionHandle = session

            rc = odv_set_dump_file(session, filePath)
            If rc <> ODV_OK Then Return -1

            ' 進捗コールバック設定
            Dim userData As IntPtr = GCHandle.ToIntPtr(gcHandle)
            odv_set_progress_callback(session, progCb, userData)

            ' LOB抽出実行
            rc = odv_extract_lob(session, If(schema, ""), tableName, lobColumn,
                                  outputDir, filenameCol, extension, dataOffset)

            If rc <> ODV_OK AndAlso rc <> ODV_ERROR_CANCELLED Then
                Return -1
            End If

            Return odv_get_lob_files_written(session)

        Catch
            Return -1
        Finally
            ctx.SessionHandle = IntPtr.Zero
            If session <> IntPtr.Zero Then
                odv_destroy_session(session)
            End If
            If gcHandle.IsAllocated Then
                gcHandle.Free()
            End If
        End Try
    End Function
#End Region

#Region "コールバック実装"
    ''' <summary>
    ''' 行データコールバック - C DLLから1行ごとに呼ばれる
    '''
    ''' メモリ最適化:
    ''' - カラム名はテーブル単位でキャッシュ（毎行マーシャリングしない）
    ''' - スキーマ名/テーブル名はインターン化して重複排除
    ''' - Dictionary初期容量をカラム数に合わせて確保（リハッシュ防止）
    ''' - 空文字列はString.Emptyを共有
    ''' </summary>
    Private Shared Sub OnRowCallback(schemaPtr As IntPtr, tablePtr As IntPtr,
                                     colCount As Integer, colNamesPtr As IntPtr,
                                     colValuesPtr As IntPtr, userData As IntPtr)
        Try
            Dim gcHandle As GCHandle = GCHandle.FromIntPtr(userData)
            Dim ctx = DirectCast(gcHandle.Target, ParseContext)

            ' テーブルキー（schema.table）を構築してカラム名キャッシュを管理
            Dim schema As String = Nothing
            Dim table As String = Nothing
            Dim cachedColNames As String() = Nothing
            Dim tableKey As String = ctx.TableKey(schemaPtr, tablePtr)

            If ctx.ColumnNamesCache.ContainsKey(tableKey) Then
                ' キャッシュヒット: カラム名のマーシャリングをスキップ
                cachedColNames = ctx.ColumnNamesCache(tableKey)
                schema = ctx.LastSchema
                table = ctx.LastTable
            Else
                ' 新しいテーブル: カラム名をマーシャリングしてキャッシュ
                schema = String.Intern(PtrToStringUTF8(schemaPtr))
                table = String.Intern(PtrToStringUTF8(tablePtr))
                cachedColNames = New String(colCount - 1) {}
                For i As Integer = 0 To colCount - 1
                    Dim strPtr As IntPtr = Marshal.ReadIntPtr(colNamesPtr, i * IntPtr.Size)
                    Dim name = PtrToStringUTF8(strPtr)
                    cachedColNames(i) = If(String.IsNullOrEmpty(name), $"COL_{i}", String.Intern(name))
                Next
                ctx.ColumnNamesCache(tableKey) = cachedColNames
                ctx.LastSchema = schema
                ctx.LastTable = table
            End If

            ' テーブルフィルタはDLL側(odv_set_table_filter)で処理済み
            ' DLL側で文字セット変換後に比較するため、VB.NET側のフィルタは不要

            ' スキーマ辞書を確保
            Dim schemaTables As Dictionary(Of String, List(Of String())) = Nothing
            If Not ctx.AllData.TryGetValue(schema, schemaTables) Then
                schemaTables = New Dictionary(Of String, List(Of String()))
                ctx.AllData(schema) = schemaTables
            End If

            ' テーブル行リストを確保（期待行数で初期容量を予約）
            Dim tableRows As List(Of String()) = Nothing
            If Not schemaTables.TryGetValue(table, tableRows) Then
                Dim capacity = If(ctx.ExpectedRowCount > 0, CInt(Math.Min(ctx.ExpectedRowCount, 10000000)), 0)
                tableRows = New List(Of String())(capacity)
                schemaTables(table) = tableRows
            End If

            ' 行データを文字列配列に変換して追加（位置インデックスで格納）
            Dim row As String() = New String(colCount - 1) {}
            For i As Integer = 0 To colCount - 1
                Dim valPtr As IntPtr = Marshal.ReadIntPtr(colValuesPtr, i * IntPtr.Size)
                If valPtr = IntPtr.Zero Then
                    row(i) = Nothing
                Else
                    Dim s = Marshal.PtrToStringUTF8(valPtr)
                    row(i) = If(String.IsNullOrEmpty(s), Nothing, s)
                End If
            Next

            tableRows.Add(row)
            ctx.RowsProcessed += 1

        Catch
            ' コールバック中の例外は握りつぶす（DLL側に伝播させない）
        End Try
    End Sub

    ''' <summary>
    ''' 進捗コールバック - ファイル位置のパーセンテージが変わるたびに呼ばれる
    ''' </summary>
    Private Shared Sub OnProgressCallback(rowsProcessed As Long, currentTablePtr As IntPtr, userData As IntPtr)
        Try
            Dim gcHandle As GCHandle = GCHandle.FromIntPtr(userData)
            Dim ctx = DirectCast(gcHandle.Target, ParseContext)
            Dim currentTable = PtrToStringUTF8(currentTablePtr)

            ctx.CurrentTable = currentTable

            ' セッションハンドルからパーセンテージを取得
            Dim pct As Integer = 0
            If ctx.SessionHandle <> IntPtr.Zero Then
                pct = odv_get_progress_pct(ctx.SessionHandle)
            End If

            ctx.ProgressAction?.Invoke(rowsProcessed, currentTable, pct)

            ' UIスレッドで実行中の場合のみ DoEvents でメッセージポンプを回す
            ' (Task.Run で実行中の場合はUIスレッドが自由なため不要)
            If Not Threading.Thread.CurrentThread.IsBackground Then
                Application.DoEvents()
            End If

        Catch
            ' コールバック中の例外は握りつぶす
        End Try
    End Sub

    ''' <summary>
    ''' テーブル一覧取得中の進捗コールバック
    ''' UIメッセージポンプを処理してマーキーアニメーションを動かす
    ''' </summary>
    Private Shared Sub OnListTablesProgressCallback(rowsProcessed As Long, currentTablePtr As IntPtr, userData As IntPtr)
        Try
            Application.DoEvents()
        Catch
            ' コールバック中の例外は握りつぶす
        End Try
    End Sub

    ''' <summary>
    ''' テーブル一覧コールバック
    ''' </summary>
    Private Shared Sub OnTableListCallback(schemaPtr As IntPtr, tablePtr As IntPtr,
                                           colCount As Integer, colNamesPtr As IntPtr,
                                           colTypesPtr As IntPtr, colNotNullsPtr As IntPtr,
                                           colDefaultsPtr As IntPtr, rowCount As Long,
                                           dataOffset As Long, userData As IntPtr)
        Try
            Dim gcHandle As GCHandle = GCHandle.FromIntPtr(userData)
            Dim ctx = DirectCast(gcHandle.Target, ListTablesContext)

            Dim schema = PtrToStringUTF8(schemaPtr)
            Dim table = PtrToStringUTF8(tablePtr)
            Dim key = $"{schema}.{table}"

            ctx.Tables.Add(Tuple.Create(schema, table, colCount, rowCount, dataOffset))

            ' カラム名を保持（0行テーブルでも列ヘッダーを表示するため）
            If colCount > 0 AndAlso colNamesPtr <> IntPtr.Zero Then
                ctx.ColumnNames(key) = PtrArrayToStrings(colNamesPtr, colCount)
            End If

            ' カラム型を保持
            If colCount > 0 AndAlso colTypesPtr <> IntPtr.Zero Then
                ctx.ColumnTypes(key) = PtrArrayToStrings(colTypesPtr, colCount)
            End If

            ' NOT NULL フラグを保持
            If colCount > 0 AndAlso colNotNullsPtr <> IntPtr.Zero Then
                Dim notNulls(colCount - 1) As Boolean
                Dim intArray(colCount - 1) As Integer
                Marshal.Copy(colNotNullsPtr, intArray, 0, colCount)
                For i As Integer = 0 To colCount - 1
                    notNulls(i) = (intArray(i) <> 0)
                Next
                ctx.ColumnNotNulls(key) = notNulls
            End If

            ' DEFAULT 値を保持
            If colCount > 0 AndAlso colDefaultsPtr <> IntPtr.Zero Then
                ctx.ColumnDefaults(key) = PtrArrayToStrings(colDefaultsPtr, colCount)
            End If

        Catch
            ' コールバック中の例外は握りつぶす
        End Try
    End Sub
#End Region

End Class
