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
        Dim delim As String = ExportOptions.CsvDelimiter
        If value.Contains(delim) OrElse value.Contains(""""c) OrElse
           value.Contains(vbCr) OrElse value.Contains(vbLf) Then
            Return """" & value.Replace("""", """""") & """"
        End If
        Return value
    End Function
#End Region

#Region "SQL エスケープ"
    ''' <summary>
    ''' SQL 識別子をエスケープ (DBMS 別。名前の中の閉じ引用符は二重にする)
    ''' </summary>
    Public Function EscapeSqlIdentifier(name As String, dbmsType As Integer) As String
        Return SqlDialect.QuoteIdent(name, dbmsType)
    End Function

    ''' <summary>
    ''' SQL 文字列リテラル (DLL の SQL 出力と同じ書き方。SQL エクスポートの型の対応の仕様 4.2)。
    ''' Oracle は '...' で、改行 (LF)・復帰 (CR)・U+0000 を '…'||CHR(n)||'…' の連結で書く (H-20、M-22。SQL*Plus で
    ''' 文が壊れず、生の CR が落ちない)。PostgreSQL は '...' ('' のみ。\ はそのまま)、MySQL は '...' ('' と \\ と \0。M-16)、
    ''' SQL Server は N'...' (U+0000 は CONCAT(CAST(N'..' AS NVARCHAR(MAX)), NCHAR(0), N'..'))。
    ''' 4000 バイトを超える Oracle の値の分割はしない (行の値は SqlExportLogic が書く)。
    ''' </summary>
    Public Function EscapeSqlString(value As String, Optional dbmsType As Integer = -1) As String
        If value Is Nothing Then Return "NULL"
        If dbmsType = DBMS_ORACLE Then
            Dim t As New SqlText()
            SqlDialect.OraPiece(t, value, 0, value.Length)
            Return t.ToString()
        End If
        Return SqlDialect.OtherText(value, dbmsType)
    End Function
#End Region

#Region "裸 NUMBER → 整数型の推定 (SqlInferInteger)"
    ' 各整数型の範囲定数
    Private Const SMALLINT_MIN As Long = -32768L
    Private Const SMALLINT_MAX As Long = 32767L
    Private Const INT_MIN As Long = -2147483648L
    Private Const INT_MAX As Long = 2147483647L

    ''' <summary>
    ''' 裸の NUMBER カラム (精度・位取りなし) の実データを走査し、全行が整数値なら、値の範囲に収まる書き出し先の
    ''' 整数型 (SMALLINT / INTEGER / BIGINT、それを超えれば DECIMAL(38,0)) を返す (列ごと。推定しない列は Nothing)。
    ''' Oracle 方言は推定しない (元の NUMBER のまま。NUMBER(4) などに狭めると 10000 以上の値が入らない)。
    ''' </summary>
    Public Function InferIntegerTypes(columnTypes As String(), data As List(Of String()),
                                      Optional columnDefaults As String() = Nothing,
                                      Optional dbmsType As Integer = DBMS_POSTGRES) As String()
        If columnTypes Is Nothing Then Return Nothing
        If dbmsType = DBMS_ORACLE Then Return Nothing

        Dim result(columnTypes.Length - 1) As String

        For colIdx As Integer = 0 To columnTypes.Length - 1
            Dim upper = If(columnTypes(colIdx), "").Trim().ToUpperInvariant()
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
                If Long.TryParse(columnDefaults(colIdx).Trim(), Globalization.NumberStyles.AllowLeadingSign,
                                 Globalization.CultureInfo.InvariantCulture, defVal) Then
                    If defVal < SMALLINT_MIN OrElse defVal > SMALLINT_MAX Then rangeLevel = 1
                    If defVal < INT_MIN OrElse defVal > INT_MAX Then rangeLevel = 2
                End If
            End If

            For Each row In data
                If colIdx >= row.Length Then Continue For
                Dim value = row(colIdx)
                If String.IsNullOrEmpty(value) Then Continue For

                hasNonNull = True

                ' 整数の形 (符号・数字、小数部が 0 だけ) でなければ推定しない (指数表記 1E+30 なども)
                If Not SqlDialect.IsNumericLiteral(value) OrElse value.IndexOfAny({"E"c, "e"c}) >= 0 Then
                    allInteger = False
                    Exit For
                End If
                Dim intStr = value
                Dim dotPos = value.IndexOf("."c)
                If dotPos >= 0 Then
                    If value.Substring(dotPos + 1).Trim("0"c).Length > 0 Then
                        allInteger = False
                        Exit For
                    End If
                    intStr = value.Substring(0, dotPos)
                End If

                ' 値の範囲を判定
                Dim numVal As Long
                If Long.TryParse(intStr, Globalization.NumberStyles.AllowLeadingSign,
                                 Globalization.CultureInfo.InvariantCulture, numVal) Then
                    If rangeLevel < 1 AndAlso (numVal < SMALLINT_MIN OrElse numVal > SMALLINT_MAX) Then rangeLevel = 1
                    If rangeLevel < 2 AndAlso (numVal < INT_MIN OrElse numVal > INT_MAX) Then rangeLevel = 2
                Else
                    ' Long に収まらない → BIGINT 超過 (Oracle の NUMBER は 38 桁まで)
                    rangeLevel = 3
                End If
            Next

            If hasNonNull AndAlso allInteger Then
                Select Case rangeLevel
                    Case 0 : result(colIdx) = "SMALLINT"
                    Case 1 : result(colIdx) = If(dbmsType = DBMS_POSTGRES, "INTEGER", "INT")
                    Case 2 : result(colIdx) = "BIGINT"
                    Case Else : result(colIdx) = If(dbmsType = DBMS_POSTGRES, "NUMERIC(38,0)", "DECIMAL(38,0)")
                End Select
            End If
        Next

        Return result
    End Function
#End Region

#Region "Oracle → 各 DBMS 型マッピング"
    ''' <summary>
    ''' Oracle 型文字列を対象 DBMS の型に変換 (SQL エクスポートと同じ写像。SQL エクスポートの型の対応の仕様 2 章)。
    ''' Oracle は型名を exp と expdp で同じ書き方にしたもの
    ''' </summary>
    Public Function MapOracleType(oracleType As String, dbmsType As Integer) As String
        Dim info As SqlDialect.SqlColumnInfo = Nothing
        Return SqlDialect.MapColumnType("", oracleType, dbmsType, info, Nothing, False, True)
    End Function
#End Region

#Region "数値判定"
    ''' <summary>
    ''' 値が「引用符なしで出せる単純な数値リテラル」かを判定する (H-08)。
    ''' 先頭の + / - は任意、以降は数字と小数点 (1 個まで)、数字の後ろに指数 (E+n) を許す。
    ''' カルチャ非依存にするため Double.TryParse は使わない ('1,000'・' 12'・'NaN'・'∞' を数値と見なさない)。
    ''' </summary>
    Public Function IsNumericValue(value As String) As Boolean
        Return SqlDialect.IsNumericLiteral(value)
    End Function

    ''' <summary>
    ''' 列の Oracle 型が数値型 (引用符なしで値を出してよい型) かを判定する (H-08)。
    ''' 数値型だけを引用符なしにし、文字列型は値が数字に見えても常に引用符付きにする。
    ''' </summary>
    Public Function IsNumericColumnType(oracleType As String) As Boolean
        If String.IsNullOrEmpty(oracleType) Then Return False
        Return SqlDialect.IsNumericKind(SqlDialect.KindOf(oracleType, SqlDialect.ParseOraType(oracleType)))
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
        Public Property ConstraintsJson As String
        Public Property RowCount As Long
        Public Property DataOffset As Long
        ''' <summary>区画名フィルタ。空なら表全体（全区画）。区画を 1 つだけ出す子行用 (H-14)</summary>
        Public Property Partition As String = ""
        ''' <summary>区画表の表全体のとき、その表の全エントリの data_offset (群9)。Nothing なら DataOffset を使う</summary>
        Public Property DataOffsets As Long() = Nothing
        ''' <summary>表から離れた DDL の位置のヒント (一覧の TableEntry.DdlHint。"" = なし。表ごとの SQL を速くする)</summary>
        Public Property DdlHint As String = ""
    End Class

    ''' <summary>
    ''' 表の全行を、エクスポートの書き手と同じ値で読む (ODV_VALUES_EXPORT: LOB・LONG は全体、値の形を持たない値は
    ''' marks に記録)。BackgroundWorker のスレッドから呼んでよい (画面に触れない)
    ''' </summary>
    ''' <param name="ctx">表の情報 (ダンプのパス・位置・区画)</param>
    ''' <param name="sqlDateFormat">日付・時刻を SQL 用の固定の書式で (SQL・Excel・ODBC)。False なら利用者の日付書式 (CSV など)</param>
    ''' <param name="marks">値でないもの (印・上限超え) の位置を記録する先</param>
    ''' <param name="wantedRows">取り出す行の番号 (Nothing = 全行。含まれない行は Nothing)</param>
    Public Function LoadExportRows(ctx As TableExportContext, sqlDateFormat As Boolean, marks As Open_NativeParser.ValueMarks,
                                   Optional wantedRows As ISet(Of Integer) = Nothing) As List(Of String())
        Dim parsed = Open_NativeParser.ParseDump(ctx.DumpFilePath, Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset, ctx.RowCount,
                                                 ctx.Partition, ctx.DataOffsets, sqlDateFormat:=sqlDateFormat,
                                                 exportValues:=True, marks:=marks, wantedRows:=wantedRows)
        If parsed IsNot Nothing AndAlso parsed.ContainsKey(ctx.Schema) AndAlso parsed(ctx.Schema).ContainsKey(ctx.TableName) Then
            Return parsed(ctx.Schema)(ctx.TableName)
        End If
        Return New List(Of String())
    End Function

    ''' <summary>
    ''' 表を開いたときの行 (表示用の値) のうち shownRows (絞り込み・並べ替えの後の行) を、エクスポートの書き手と同じ値の行
    ''' にして返す (TablePreview から VB の書き手で出すとき)。同じ表を ODV_VALUES_EXPORT で解析し直し、shownRows の行だけを
    ''' 同じ位置から取り出す (shownRows の要素は allRows の要素そのもの)。LOB・LONG は全体になり、プレビューの長さで
    ''' 切れた値を書くことはない。日付・時刻は sqlDateFormat なら SQL 用の固定の書式 (M-14)、でなければ利用者の日付書式。
    ''' BackgroundWorker のスレッドから呼んでよい (画面に触れない)
    ''' </summary>
    ''' <param name="src">表の情報 (ダンプのパス・位置・区画)</param>
    ''' <param name="allRows">表を開いたときの全行</param>
    ''' <param name="shownRows">出力する行 (allRows の要素)</param>
    ''' <param name="marks">値でないもの (印・上限超え) の位置を記録する先</param>
    Public Function RowsForExport(src As TableExportContext, sqlDateFormat As Boolean,
                                  allRows As List(Of String()), shownRows As List(Of String()),
                                  marks As Open_NativeParser.ValueMarks) As List(Of String())
        If src Is Nothing OrElse String.IsNullOrEmpty(src.DumpFilePath) Then
            Throw New InvalidOperationException("The table cannot be read again for the export: the dump file is not known.")
        End If
        If allRows Is Nothing OrElse shownRows Is Nothing Then Return New List(Of String())
        ' 表示中の行は元の行の配列そのもの: 元の位置を引く
        Dim index As New Dictionary(Of String(), Integer)(allRows.Count, ReferenceEqualityComparer.Instance)
        For i = 0 To allRows.Count - 1
            index(allRows(i)) = i
        Next
        Dim wanted As New HashSet(Of Integer)()
        For Each row In shownRows
            wanted.Add(index(row))
        Next
        Dim all = LoadExportRows(src, sqlDateFormat, marks, wanted)
        If all.Count <> allRows.Count Then
            Throw New InvalidOperationException("The table read again for the export does not have the same rows as on display.")
        End If
        ' 解析し直した同じ位置の行を取る (並べ替えの順も保つ)
        Dim result As New List(Of String())(shownRows.Count)
        For Each row In shownRows
            result.Add(all(index(row)))
        Next
        Return result
    End Function

    ''' <summary>
    ''' SQL の書き手に渡す表の DDL: Data Pump の表は DLL から読み直す (一覧は制約・DEFAULT・コメントを読まない。
    ''' 主キーだけの NOT NULL を見分ける)。exp の表は一覧の DDL のまま。BackgroundWorker のスレッドから呼んでよい
    ''' </summary>
    ''' <returns>NOT NULL (0 / 1 / 2 = 主キーだけ)・DEFAULT・制約の JSON を ctx から写した (Data Pump なら読み直した) もの</returns>
    Public Function TableDdlForSql(ctx As TableExportContext) As Open_NativeParser.TableDdl
        Dim ddl As Open_NativeParser.TableDdl = Nothing
        If Not String.IsNullOrEmpty(ctx.DumpFilePath) Then
            ddl = Open_NativeParser.ReadTableDdl(ctx.DumpFilePath, ctx.Schema, ctx.TableName, ctx.DataOffset,
                                                 ctx.Partition, ctx.DataOffsets, ctx.DdlHint)
        End If
        If ddl IsNot Nothing Then Return ddl
        ' exp (一覧が DDL を持つ)
        Dim nn As Integer() = Nothing
        If ctx.ColumnNotNulls IsNot Nothing Then nn = ctx.ColumnNotNulls.Select(Function(b) If(b, 1, 0)).ToArray()
        Return New Open_NativeParser.TableDdl() With {
            .ColumnNames = ctx.ColumnNames, .ColumnTypes = ctx.ColumnTypes, .ColumnNotNulls = nn,
            .ColumnDefaults = ctx.ColumnDefaults, .ConstraintsJson = ctx.ConstraintsJson}
    End Function

    ''' <summary>
    ''' メインフォームのアクティブなワークスペースまたはTablePreviewから選択中のテーブル情報を取得
    ''' </summary>
    ''' <returns>テーブル情報。テーブル未選択時は Nothing</returns>
    Public Function GetActiveTableContext() As TableExportContext
        Dim mainForm = TryCast(Application.OpenForms(0), Open_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing

        ' まず Workspace をチェック
        Dim workspace = TryCast(mainForm.ActiveMdiChild, Workspace)
        If workspace IsNot Nothing Then
            ' 複数選択時は Nothing を返して一括エクスポートパスに入る
            Dim selected = workspace.GetSelectedTableContexts()
            If selected IsNot Nothing AndAlso selected.Count > 1 Then
                Return Nothing  ' → GetBulkExportContexts() が呼ばれる
            End If
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
        Dim mainForm = TryCast(Application.OpenForms(0), Open_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing

        Dim workspace = TryCast(mainForm.ActiveMdiChild, Workspace)
        If workspace Is Nothing Then Return Nothing

        ' 複数選択がある場合は選択テーブルのみ返す
        Dim selected = workspace.GetSelectedTableContexts()
        If selected IsNot Nothing AndAlso selected.Count > 1 Then
            Return selected
        End If

        ' 選択なし or 1件のみ → 全可視テーブルを返す
        Return workspace.GetVisibleTableContexts()
    End Function

    ''' <summary>
    ''' メインフォームのアクティブなワークスペースを取得
    ''' </summary>
    Public Function GetActiveWorkspace() As Workspace
        Dim mainForm = TryCast(Application.OpenForms(0), Open_DUMP_Viewer)
        If mainForm Is Nothing Then Return Nothing
        Return TryCast(mainForm.ActiveMdiChild, Workspace)
    End Function
#End Region

End Module
