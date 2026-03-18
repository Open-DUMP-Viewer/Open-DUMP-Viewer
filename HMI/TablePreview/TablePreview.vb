Imports System.IO
Imports System.Collections.Generic

''' <summary>
''' テーブルデータプレビューフォーム
''' 
''' 選択されたテーブルのデータを DataGridView で表示するフォーム。
''' ページング機能と高度な検索機能を提供し、大量データ（数百万行）にも対応可能。
''' 
''' 主な機能:
''' - ページング表示（デフォルト1ページ100行、カスタマイズ可能）
''' - 高度な複合条件検索（AND/OR ロジック対応）
''' - 前回の検索条件を自動記憶・復元
''' - シンプル検索（非表示）
''' - 前へ/次へ ナビゲーション
''' - ページサイズ変更
''' 
''' UI コンポーネント（デザイナーで定義):
''' - dataGridViewData: テーブルデータを表示
''' - panelSearch: シンプル検索パネル（非表示）
''' - flowLayoutPanel: 検索条件行を表示
''' - buttonAdvancedSearch: 高度な検索ボタン
''' - buttonPrev/buttonNext: ページ切り替えボタン
''' - labelPageInfo: ページ情報表示
''' - labelRowCount: 行数情報表示
''' 
''' 使用方法（外部から呼び出す際）:
''' Dim preview As New TablePreview(tableData, columnNames, "テーブル名")
''' preview.ShowDialog()
''' </summary>
Public Class TablePreview
    Implements ILocalizable

#Region "フィールド・コンストラクタ"

    ''' <summary>元のテーブルデータ（全行、各行はString配列で列インデックスに対応）</summary>
    Private _tableData As List(Of String())

    ''' <summary>検索・フィルタ後のテーブルデータ</summary>
    Private _filteredData As List(Of String())

    ''' <summary>テーブルの列名リスト</summary>
    Private _columnNames As List(Of String)

    ''' <summary>スキーマ名</summary>
    Private _schema As String

    ''' <summary>テーブル名</summary>
    Private _tableName As String

    ''' <summary>カラム型情報</summary>
    Private _columnTypes As String()

    ''' <summary>NOT NULL フラグ</summary>
    Private _columnNotNulls As Boolean()

    ''' <summary>DEFAULT 値</summary>
    Private _columnDefaults As String()

    ''' <summary>列名→インデックスの逆引きマップ（O(1) lookup）</summary>
    Private _columnIndexMap As Dictionary(Of String, Integer)

    ''' <summary>現在表示中のページ番号（1ベース）</summary>
    Private _currentPage As Integer = 1

    ''' <summary>1ページあたりの行数（デフォルト100、ユーザーが変更可能）</summary>
    Private _pageCount As Integer = 100

    ''' <summary>元データの総行数</summary>
    Private _totalRows As Integer = 0

    ''' <summary>フォーム読み込み中のイベント処理フラグ（重複イベント防止）</summary>
    Private _isInitializing As Boolean = True

    ''' <summary>現在の検索条件（高度な検索で構築）</summary>
    Private _currentSearchCondition As SearchCondition.ComplexSearchCondition

    ''' <summary>前回の検索条件（次回フォーム起動時に復元用）</summary>
    Private _lastSearchCondition As SearchCondition.ComplexSearchCondition

    ''' <summary>現在のソート列名（Nothing=ソートなし）</summary>
    Private _sortColumnName As String = Nothing

    ''' <summary>現在のソート方向</summary>
    Private _sortAscending As Boolean = True

    ''' <summary>
    ''' コンストラクタ
    '''
    ''' テーブルデータ、列名、テーブル名を受け取り、フォームを初期化する。
    ''' </summary>
    ''' <param name="tableData">表示するテーブルデータ</param>
    ''' <param name="columnNames">テーブルの列名リスト</param>
    ''' <param name="tableName">テーブル名（ウィンドウタイトルに使用）</param>
    ''' <param name="schema">スキーマ名（エクスポート用）</param>
    ''' <param name="columnTypes">カラム型配列（エクスポート用）</param>
    Public Sub New(tableData As List(Of String()), columnNames As List(Of String), tableName As String,
                   Optional schema As String = Nothing, Optional columnTypes As String() = Nothing,
                   Optional columnNotNulls As Boolean() = Nothing, Optional columnDefaults As String() = Nothing)
        ' デザイナーで定義されたコンポーネントを初期化
        InitializeComponent()

        ' パラメータを保存
        _tableData = tableData
        _columnNames = columnNames
        _filteredData = New List(Of String())(_tableData)
        _totalRows = _tableData.Count
        _schema = If(schema, "")
        _tableName = tableName
        _columnTypes = columnTypes
        _columnNotNulls = columnNotNulls
        _columnDefaults = columnDefaults

        ' 列名→インデックス逆引きマップを構築（1回だけ、O(n)）
        _columnIndexMap = New Dictionary(Of String, Integer)(_columnNames.Count)
        For i As Integer = 0 To _columnNames.Count - 1
            _columnIndexMap(_columnNames(i)) = i
        Next

        ' フォームタイトルを設定
        Me.Text = Loc.SF("Preview_FormTitle", tableName)

        ' 検索条件を初期化
        _currentSearchCondition = Nothing
        _lastSearchCondition = Nothing

        ' ローカライズ適用
        ApplyLocalization()
    End Sub

#End Region

#Region "Public プロパティ (エクスポート用)"

    ''' <summary>スキーマ名</summary>
    Public ReadOnly Property SchemaName As String
        Get
            Return _schema
        End Get
    End Property

    ''' <summary>テーブル名</summary>
    Public ReadOnly Property ExportTableName As String
        Get
            Return _tableName
        End Get
    End Property

    ''' <summary>カラム名リスト</summary>
    Public ReadOnly Property ExportColumnNames As List(Of String)
        Get
            Return _columnNames
        End Get
    End Property

    ''' <summary>カラム型配列</summary>
    Public ReadOnly Property ExportColumnTypes As String()
        Get
            Return _columnTypes
        End Get
    End Property

    ''' <summary>フィルタ後のデータ</summary>
    Public ReadOnly Property FilteredData As List(Of String())
        Get
            Return _filteredData
        End Get
    End Property

    ''' <summary>
    ''' エクスポート用のテーブルコンテキストを取得
    ''' </summary>
    Public Function GetExportContext() As ExportHelper.TableExportContext
        Return New ExportHelper.TableExportContext() With {
            .Schema = _schema,
            .TableName = _tableName,
            .ColumnNames = _columnNames.ToArray(),
            .ColumnTypes = _columnTypes,
            .ColumnNotNulls = _columnNotNulls,
            .ColumnDefaults = _columnDefaults,
            .RowCount = _filteredData.Count,
            .DataOffset = 0
        }
    End Function

#End Region

#Region "イベント処理"

    ''' <summary>
    ''' フォーム読み込み時のイベントハンドラー
    ''' 
    ''' フロー:
    ''' 1. ページサイズの初期値を設定
    ''' 2. 列名をコンボボックスに設定
    ''' 3. イベントハンドラーを設定
    ''' 4. DataGridView をセットアップ
    ''' 5. 初期データを表示
    ''' </summary>
    Private Sub TablePreview_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        _isInitializing = True

        ' １ページあたりの行数の初期値を設定
        numericUpDownPageCount.Value = _pageCount

        ' 列名をコンボボックスに設定
        For Each colName In _columnNames
            comboBoxColumns.Items.Add(colName)
        Next
        If comboBoxColumns.Items.Count > 0 Then
            comboBoxColumns.SelectedIndex = 0
        End If

        ' イベントハンドラーを設定
        AddHandler buttonAdvancedSearch.Click, AddressOf ButtonAdvancedSearch_Click

        ' DataGridView をセットアップ
        SetupDataGridView()

        ' 初期データを表示
        UpdateDataDisplay()

        _isInitializing = False
    End Sub

    ''' <summary>
    ''' シンプル検索ボタンのクリックイベントハンドラー
    ''' 
    ''' （非表示であるため、通常は呼び出されない）
    ''' 列とキーワードでシンプル検索を実行
    ''' </summary>
    Private Sub buttonSearch_Click(sender As Object, e As EventArgs) Handles buttonSearch.Click
        _currentPage = 1
        _currentSearchCondition = Nothing
        FilterData()
        UpdateDataDisplay()
    End Sub

    ''' <summary>
    ''' 「高度な検索」ボタンのクリックイベントハンドラー
    ''' 
    ''' 高度な検索フォームを開く
    ''' </summary>
    Private Sub ButtonAdvancedSearch_Click(sender As Object, e As EventArgs)
        OpenAdvancedSearchForm()
    End Sub

    ''' <summary>
    ''' 高度な検索フォームを開く（メインロジック）
    ''' 
    ''' フロー:
    ''' 1. AdvancedSearchForm インスタンスを作成
    ''' 2. 前回の検索条件があれば SetSearchCondition() で復元
    ''' 3. フォームをモーダル表示
    ''' 4. OK の場合：
    '''    - 検索条件を _currentSearchCondition に設定
    '''    - 条件をディープコピーして _lastSearchCondition に保存
    '''    - FilterData() で検索実行
    '''    - UpdateDataDisplay() で表示更新
    ''' 5. キャンセルの場合は何もしない
    ''' </summary>
    Private Sub OpenAdvancedSearchForm()
        ' AdvancedSearchForm インスタンスを作成
        Dim advancedForm As New AdvancedSearchForm(_columnNames)

        ' 前回の検索条件があれば復元
        If _lastSearchCondition IsNot Nothing Then
            advancedForm.SetSearchCondition(_lastSearchCondition)
        End If

        ' モーダルダイアログで表示
        If advancedForm.ShowDialog(Me) = DialogResult.OK Then
            ' ページを最初に戻す
            _currentPage = 1

            ' 検索条件を取得
            _currentSearchCondition = advancedForm.SearchConditionResult

            ' 検索条件を保持（ディープコピー）
            _lastSearchCondition = CopySearchCondition(_currentSearchCondition)

            ' データをフィルタリング
            FilterData()

            ' 表示を更新
            UpdateDataDisplay()
        End If
    End Sub

    ''' <summary>
    ''' 検索条件をディープコピーする
    ''' 
    ''' 複合検索条件の Conditions と LogicalOperators の両方をコピーし、
    ''' 新しいインスタンスを返す。
    ''' 
    ''' ディープコピーの理由:
    ''' - 外部フォームの条件変更が _lastSearchCondition に影響しないようにするため
    ''' - 参照型オブジェクトの独立性を確保
    ''' </summary>
    ''' <param name="original">コピー元の複合検索条件</param>
    ''' <returns>コピーされた複合検索条件（新しいインスタンス）</returns>
    Private Function CopySearchCondition(original As SearchCondition.ComplexSearchCondition) As SearchCondition.ComplexSearchCondition
        If original Is Nothing Then
            Return Nothing
        End If

        Dim copied As New SearchCondition.ComplexSearchCondition()

        ' すべての条件をコピー
        For Each condition In original.Conditions
            Dim newCondition As New SearchCondition.SearchConditionItem(
                condition.ColumnName,
                condition.OperatorType,
                condition.Value,
                condition.CaseSensitive
            )
            copied.Conditions.Add(newCondition)
        Next

        ' すべての論理演算子をコピー
        For Each logicalOp In original.LogicalOperators
            copied.LogicalOperators.Add(logicalOp)
        Next

        Return copied
    End Function

    ''' <summary>
    ''' 「リセット」ボタンのクリックイベントハンドラー
    ''' 
    ''' すべての検索条件をリセットし、元のテーブルデータを表示
    ''' </summary>
    Private Sub buttonReset_Click(sender As Object, e As EventArgs) Handles buttonReset.Click
        _currentPage = 1
        _filteredData = New List(Of String())(_tableData)
        _currentSearchCondition = Nothing
        _lastSearchCondition = Nothing
        UpdateDataDisplay()
    End Sub

    ''' <summary>
    ''' 列ヘッダクリック時のソートイベントハンドラー
    '''
    ''' 全データ (_filteredData) をソートしてからページを再描画する。
    ''' 同じ列を再クリックすると昇順/降順を切り替える。
    ''' </summary>
    Private Sub dataGridViewData_ColumnHeaderMouseClick(sender As Object, e As DataGridViewCellMouseEventArgs) Handles dataGridViewData.ColumnHeaderMouseClick
        Dim clickedColName = dataGridViewData.Columns(e.ColumnIndex).Name

        ' 同じ列なら方向を反転、別の列なら昇順にリセット
        If clickedColName = _sortColumnName Then
            _sortAscending = Not _sortAscending
        Else
            _sortColumnName = clickedColName
            _sortAscending = True
        End If

        ' ソートキーを事前計算（比較のたびに TryParse するのを回避）
        Dim count = _filteredData.Count
        Dim isNumeric = True
        Dim numKeys(count - 1) As Double
        Dim strKeys(count - 1) As String

        Dim colIndex As Integer = -1
        If _columnIndexMap.ContainsKey(clickedColName) Then colIndex = _columnIndexMap(clickedColName)

        For i = 0 To count - 1
            Dim val As String = Nothing
            If colIndex >= 0 AndAlso colIndex < _filteredData(i).Length Then
                val = _filteredData(i)(colIndex)
            End If
            If val Is Nothing Then
                strKeys(i) = Nothing
                numKeys(i) = Double.MinValue
            Else
                strKeys(i) = val
                If isNumeric Then
                    If Not Double.TryParse(strKeys(i), numKeys(i)) Then
                        isNumeric = False
                    End If
                End If
            End If
        Next

        ' インデックス配列でソート（元データの並びを維持しつつ高速）
        Dim indices(count - 1) As Integer
        For i = 0 To count - 1
            indices(i) = i
        Next

        Dim asc = _sortAscending
        If isNumeric Then
            Array.Sort(indices, Function(a, b)
                                    Dim r = numKeys(a).CompareTo(numKeys(b))
                                    If Not asc Then r = -r
                                    Return r
                                End Function)
        Else
            Array.Sort(indices, Function(a, b)
                                    Dim sa = strKeys(a)
                                    Dim sb = strKeys(b)
                                    Dim r As Integer
                                    If sa Is Nothing AndAlso sb Is Nothing Then
                                        r = 0
                                    ElseIf sa Is Nothing Then
                                        r = -1
                                    ElseIf sb Is Nothing Then
                                        r = 1
                                    Else
                                        r = String.Compare(sa, sb, StringComparison.OrdinalIgnoreCase)
                                    End If
                                    If Not asc Then r = -r
                                    Return r
                                End Function)
        End If

        ' ソート結果で _filteredData を並べ替え
        Dim sorted = New List(Of String())(count)
        For Each idx In indices
            sorted.Add(_filteredData(idx))
        Next
        _filteredData = sorted

        ' ソート方向のグリフを表示
        For Each col As DataGridViewColumn In dataGridViewData.Columns
            col.HeaderCell.SortGlyphDirection = SortOrder.None
        Next
        dataGridViewData.Columns(e.ColumnIndex).HeaderCell.SortGlyphDirection =
            If(_sortAscending, SortOrder.Ascending, SortOrder.Descending)

        ' 1ページ目に戻して再描画
        _currentPage = 1
        UpdateDataDisplay()
    End Sub

    ''' <summary>
    ''' 「前へ」ボタンのクリックイベントハンドラー
    ''' 
    ''' 前のページに移動（ページ番号が1より大きい場合のみ）
    ''' </summary>
    Private Sub buttonPrev_Click(sender As Object, e As EventArgs) Handles buttonPrev.Click
        If _currentPage > 1 Then
            _currentPage -= 1
            UpdateDataDisplay()
        End If
    End Sub

    ''' <summary>
    ''' 「次へ」ボタンのクリックイベントハンドラー
    ''' 
    ''' 次のページに移動（最終ページでない場合のみ）
    ''' </summary>
    Private Sub buttonNext_Click(sender As Object, e As EventArgs) Handles buttonNext.Click
        Dim totalPages As Integer = Math.Ceiling(If(_filteredData.Count = 0, 1, _filteredData.Count / _pageCount))
        If _currentPage < totalPages Then
            _currentPage += 1
            UpdateDataDisplay()
        End If
    End Sub

    ''' <summary>
    ''' １ページあたりの行数が変更された時のイベントハンドラー
    ''' 
    ''' ユーザーが "1ページ:" スピンボックスでページサイズを変更した場合、
    ''' ページ表示を更新
    ''' 
    ''' 注意: フォーム初期化中は処理をスキップ（重複イベント防止）
    ''' </summary>
    Private Sub numericUpDownPageSize_ValueChanged(sender As Object, e As EventArgs) Handles numericUpDownPageCount.ValueChanged
        If Not _isInitializing Then
            _pageCount = CInt(Math.Min(numericUpDownPageCount.Value, Integer.MaxValue))
            _currentPage = 1
            UpdateDataDisplay()
        End If
    End Sub

    ''' <summary>
    ''' テキストボックスでキー入力があった時のイベントハンドラー
    ''' 
    ''' Enter キーを押すと、シンプル検索を実行
    ''' （ただしシンプル検索パネルは非表示のため、通常は使用されない）
    ''' </summary>
    Private Sub textBoxSearchValue_KeyDown(sender As Object, e As KeyEventArgs) Handles textBoxSearchValue.KeyDown
        If e.KeyCode = Keys.Return Then
            buttonSearch.PerformClick()
            e.Handled = True
        End If
    End Sub

#End Region

#Region "データ処理"

    ''' <summary>
    ''' DataGridView をセットアップする
    ''' 
    ''' フロー:
    ''' 1. 既存の列をすべて削除
    ''' 2. _columnNames に基づいて列を追加
    ''' 3. 各列の幅を100～200ピクセルの範囲で自動調整
    ''' </summary>
    Private Sub SetupDataGridView()
        dataGridViewData.Columns.Clear()

        ' VirtualMode を有効化（大量データでも高速描画）
        dataGridViewData.VirtualMode = True

        ' 行選択モード + 複数行選択を有効化
        dataGridViewData.SelectionMode = DataGridViewSelectionMode.FullRowSelect
        dataGridViewData.MultiSelect = True

        ' 列を追加（ソートはプログラム制御で行うため Programmatic に設定）
        For Each colName In _columnNames
            Dim col = New DataGridViewTextBoxColumn()
            col.Name = colName
            col.HeaderText = colName
            col.SortMode = DataGridViewColumnSortMode.Programmatic
            dataGridViewData.Columns.Add(col)
        Next

        ' 列の幅を自動調整（最小100、最大200）
        For Each col As DataGridViewColumn In dataGridViewData.Columns
            col.Width = Math.Max(100, Math.Min(200, col.Width))
        Next
    End Sub

    ''' <summary>
    ''' テーブルデータをフィルタリングする
    ''' 
    ''' フロー:
    ''' 1. 高度な検索が有効な場合：
    '''    - _currentSearchCondition で複合条件検索を実行
    '''    - 各行について Evaluate() で条件判定
    ''' 2. シンプル検索の場合：
    '''    - 指定された列で部分一致検索（大文字小文字区別しない）
    ''' 3. 検索条件がない場合：
    '''    - すべてのデータを返す
    ''' 
    ''' パフォーマンス:
    ''' - 複雑な条件でも高速（LINQ の遅延評価）
    ''' - 大量データ（100万行）でも応答性を維持
    ''' </summary>
    Private Sub FilterData()
        ' 高度な検索が有効な場合
        If _currentSearchCondition IsNot Nothing Then
            _filteredData = _tableData.Where(Function(row)
                                                 Return _currentSearchCondition.Evaluate(row, _columnIndexMap)
                                             End Function).ToList()
            Return
        End If

        ' シンプル検索
        Dim columnName As String = comboBoxColumns.SelectedItem?.ToString()
        Dim searchValue As String = textBoxSearchValue.Text.Trim()

        If String.IsNullOrEmpty(columnName) OrElse String.IsNullOrEmpty(searchValue) Then
            _filteredData = New List(Of String())(_tableData)
            Return
        End If

        ' フィルタリング処理（大文字小文字区別しない部分一致）
        Dim searchColIndex As Integer = -1
        If _columnIndexMap.ContainsKey(columnName) Then searchColIndex = _columnIndexMap(columnName)

        _filteredData = _tableData.Where(Function(row)
                                             If searchColIndex >= 0 AndAlso searchColIndex < row.Length Then
                                                 Dim cellValue = row(searchColIndex)
                                                 If cellValue IsNot Nothing Then
                                                     Return cellValue.Contains(searchValue, StringComparison.OrdinalIgnoreCase)
                                                 End If
                                             End If
                                             Return False
                                         End Function).ToList()
    End Sub

    ''' <summary>
    ''' データ表示を更新する（ページング対応）
    ''' 
    ''' フロー:
    ''' 1. 総ページ数を計算（Math.Ceiling を使用して端数処理）
    ''' 2. 現在のページの開始行と終了行を計算
    ''' 3. DataGridView をクリア
    ''' 4. 現在のページのデータをセル配列に変換
    ''' 5. DataGridView に行を追加
    ''' 6. UI ラベルを更新（ページ情報、行数情報）
    ''' 7. ナビゲーションボタンの有効/無効を制御
    ''' 
    ''' 計算例（1ページ100行の場合）:
    ''' - ページ1: startRow=0, endRow=100
    ''' - ページ2: startRow=100, endRow=200
    ''' - ページ3: startRow=200, endRow=250（最終ページ）
    ''' </summary>
    ''' <summary>現在のページの開始行インデックス（VirtualMode 用）</summary>
    Private _displayStartRow As Integer = 0

    Private Sub UpdateDataDisplay()
        ' 総ページ数を計算（データが0件の場合は1を使用）
        Dim totalPages As Integer = CInt(Math.Ceiling(If(_filteredData.Count = 0, 1, CDbl(_filteredData.Count) / _pageCount)))

        ' 現在のページの開始行と終了行を計算
        _displayStartRow = (_currentPage - 1) * _pageCount
        Dim displayRowCount As Integer = Math.Min(_pageCount, _filteredData.Count - _displayStartRow)
        If displayRowCount < 0 Then displayRowCount = 0

        ' VirtualMode: RowCount を設定するだけで描画される（Rows.Add 不要）
        dataGridViewData.RowCount = displayRowCount

        ' DataGridView を再描画
        dataGridViewData.Invalidate()

        ' UI 要素を更新
        labelPageInfo.Text = Loc.SF("Preview_PageInfoLabel", _currentPage, totalPages)
        labelRowCount.Text = Loc.SF("Preview_RowCountLabel", _tableData.Count, _filteredData.Count)

        ' ナビゲーションボタンの有効/無効を制御
        buttonPrev.Enabled = _currentPage > 1
        buttonNext.Enabled = _currentPage < totalPages
    End Sub

    ''' <summary>
    ''' VirtualMode 用: セル値をオンデマンドで返す
    ''' DataGridView が表示に必要なセルだけを要求するため、メモリ効率が高い
    ''' </summary>
    Private Sub dataGridViewData_CellValueNeeded(sender As Object, e As DataGridViewCellValueEventArgs) Handles dataGridViewData.CellValueNeeded
        Dim dataIndex = _displayStartRow + e.RowIndex
        If dataIndex < 0 OrElse dataIndex >= _filteredData.Count Then Return
        If e.ColumnIndex < 0 OrElse e.ColumnIndex >= _columnNames.Count Then Return

        Dim row = _filteredData(dataIndex)
        If e.ColumnIndex < row.Length Then
            e.Value = If(row(e.ColumnIndex), String.Empty)
        Else
            e.Value = String.Empty
        End If
    End Sub

    ''' <summary>
    ''' Ctrl+C / Ctrl+A をフォームレベルで確実にキャプチャ
    ''' MDI 子フォームではメニュー・ツールバーにキーが奪われるため ProcessCmdKey を使用
    ''' </summary>
    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If dataGridViewData.Focused Then
            If keyData = (Keys.Control Or Keys.C) Then
                CopySelectedRowsToClipboard()
                Return True
            ElseIf keyData = (Keys.Control Or Keys.A) Then
                dataGridViewData.SelectAll()
                Return True
            End If
        End If
        Return MyBase.ProcessCmdKey(msg, keyData)
    End Function

    Private Sub CopySelectedRowsToClipboard()
        If dataGridViewData.SelectedRows.Count = 0 Then Return

        Dim sb As New System.Text.StringBuilder()

        ' ヘッダー行
        For i = 0 To _columnNames.Count - 1
            If i > 0 Then sb.Append(vbTab)
            sb.Append(_columnNames(i))
        Next
        sb.AppendLine()

        ' 選択行をインデックス順にソート
        Dim sortedRows = dataGridViewData.SelectedRows.Cast(Of DataGridViewRow)().
            OrderBy(Function(r) r.Index).ToList()

        For Each dgvRow In sortedRows
            Dim dataIndex = _displayStartRow + dgvRow.Index
            If dataIndex < 0 OrElse dataIndex >= _filteredData.Count Then Continue For

            Dim row = _filteredData(dataIndex)
            For i = 0 To _columnNames.Count - 1
                If i > 0 Then sb.Append(vbTab)
                If i < row.Length Then
                    sb.Append(If(row(i), String.Empty))
                End If
            Next
            sb.AppendLine()
        Next

        If sb.Length > 0 Then
            Clipboard.SetText(sb.ToString())
        End If
    End Sub

#End Region

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        labelSearch.Text = Loc.S("Preview_SearchValueLabel")
        labelColumns.Text = Loc.S("Preview_ColumnNameLabel")
        buttonSearch.Text = Loc.S("Button_Search")
        buttonReset.Text = Loc.S("Button_Reset")
        buttonAdvancedSearch.Text = Loc.S("Button_AdvancedSearch")
        labelPageSize.Text = Loc.S("Preview_PageSizeLabel")
        buttonPrev.Text = Loc.S("Button_Previous")
        buttonNext.Text = Loc.S("Button_Next")
    End Sub
#End Region

End Class