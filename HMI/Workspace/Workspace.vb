Imports System.Collections.Generic

Public Class Workspace
    Inherits Form

#Region "フィールド・コンストラクタ"
    Private DumpFilePath As String
    Private WorkspacePath As String
    ''' <summary>テーブル一覧メタデータ (スキーマ名→(テーブル名, 行数, データオフセット)リスト)</summary>
    Private _tableList As New Dictionary(Of String, List(Of Tuple(Of String, Long, Long)))
    ''' <summary>テーブルごとのカラム名 (キー: "schema.table")</summary>
    Private _columnNamesMap As New Dictionary(Of String, String())
    ''' <summary>テーブルごとのカラム型 (キー: "schema.table")</summary>
    Private _columnTypesMap As New Dictionary(Of String, String())
    Private _currentSchema As String = String.Empty
    ''' <summary>除外テーブル (キー: "schema.table")</summary>
    Private _excludedTables As New HashSet(Of String)

    ' 引数ありコンストラクタ
    Public Sub New(value1 As String, value2 As String)
        InitializeComponent()
        DumpFilePath = value1
        WorkspacePath = value2
    End Sub
#End Region

#Region "イベント処理"
    Private Sub Workspace_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' フェーズ1: テーブル一覧のみ取得（高速・メモリ軽量）
        Dim colMap As Dictionary(Of String, String()) = Nothing
        Dim typeMap As Dictionary(Of String, String()) = Nothing
        Dim tables = AnalyzeLogic.ListTables(DumpFilePath, colMap, typeMap)
        If colMap IsNot Nothing Then _columnNamesMap = colMap
        If typeMap IsNot Nothing Then _columnTypesMap = typeMap

        ' テーブル一覧をスキーマ別に整理
        _tableList.Clear()
        For Each t In tables
            Dim schema = t.Item1
            Dim tableName = t.Item2
            Dim rowCount = t.Item4
            Dim dataOffset = t.Item5
            If Not _tableList.ContainsKey(schema) Then
                _tableList(schema) = New List(Of Tuple(Of String, Long, Long))
            End If
            _tableList(schema).Add(Tuple.Create(tableName, rowCount, dataOffset))
        Next

        'TreeViewにスキーマ一覧を追加
        PopulateSchemaTree()

        ' TreeView のスキーマノードをクリック時のイベント設定
        AddHandler treeDBList.NodeMouseClick, AddressOf TreeDBList_NodeMouseClick

        ' ListViewのダブルクリック時のイベント設定
        AddHandler lstTableList.DoubleClick, AddressOf LstTableList_DoubleClick

        ' テーブル検索テキストボックスのイベント設定
        AddHandler txtTableSearch.TextChanged, AddressOf TxtTableSearch_TextChanged
    End Sub

    ''' <summary>
    ''' テーブル検索テキストボックスの入力変更時のイベント
    ''' 入力文字列で現在のスキーマのテーブル一覧を絞り込む
    ''' </summary>
    Private Sub TxtTableSearch_TextChanged(sender As Object, e As EventArgs)
        If Not String.IsNullOrEmpty(_currentSchema) Then
            DisplayTablesForSchema(_currentSchema)
        End If
    End Sub

    ''' <summary>
    ''' TreeView のスキーマノードがクリックされた時のイベント
    ''' 選択されたスキーマのテーブルをListViewに表示する
    ''' </summary>
    Private Sub TreeDBList_NodeMouseClick(sender As Object, e As TreeNodeMouseClickEventArgs)
        Dim node = e.Node

        ' ルートノード（Database）の場合はスキップ
        If node.Parent Is Nothing Then
            lstTableList.Items.Clear()
            _currentSchema = String.Empty
            Return
        End If

        ' スキーマノードの場合 - 括弧を削除してスキーマ名を抽出
        Dim schemaName As String = ExtractSchemaName(node.Text)

        If _tableList.ContainsKey(schemaName) Then
            _currentSchema = schemaName
            DisplayTablesForSchema(schemaName)
        End If
    End Sub

    ''' <summary>
    ''' ノードテキストからスキーマ名を抽出する
    ''' 「スキーマ名 (テーブル数)」形式から「スキーマ名」を取得
    ''' </summary>
    Private Function ExtractSchemaName(nodeText As String) As String
        Dim parts = nodeText.Split("("c)
        If parts.Length > 0 Then
            Return parts(0).Trim()
        End If
        Return nodeText
    End Function

    ''' <summary>
    ''' ListViewのアイテムがダブルクリックされた時のイベント
    ''' 選択されたテーブルをTablePreviewで表示
    ''' </summary>
    ''' <summary>
    ''' テーブルダブルクリック時: オンデマンドで選択テーブルのみ解析
    ''' DUMPファイル全体をスキャンするが、行データは選択テーブルのみメモリに蓄積
    ''' </summary>
    Private Sub LstTableList_DoubleClick(sender As Object, e As EventArgs)
        If lstTableList.SelectedItems.Count = 0 Then
            MessageBox.Show("テーブルが選択されていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Try
            Dim selectedItem = lstTableList.SelectedItems(0)
            Dim tableName As String = selectedItem.Text

            If String.IsNullOrEmpty(_currentSchema) Then
                MessageBox.Show("スキーマが選択されていません。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
                Return
            End If

            ' テーブルのデータオフセットを取得（高速シーク用）
            Dim dataOffset As Long = 0
            If _tableList.ContainsKey(_currentSchema) Then
                Dim entry = _tableList(_currentSchema).Find(Function(x) x.Item1 = tableName)
                If entry IsNot Nothing Then
                    dataOffset = entry.Item3
                End If
            End If

            ' フェーズ2: 選択テーブルのみ解析（dataOffset>0ならDDL位置に高速シーク）
            Dim tableData = AnalyzeLogic.AnalyzeTable(DumpFilePath, _currentSchema, tableName, dataOffset)

            ' 列名を取得（Phase1のListTablesで取得済みのキャッシュから）
            Dim columnNames As New List(Of String)
            Dim tableKey = $"{_currentSchema}.{tableName}"
            If _columnNamesMap.ContainsKey(tableKey) Then
                columnNames = New List(Of String)(_columnNamesMap(tableKey))
            End If

            If columnNames.Count = 0 Then
                MessageBox.Show("テーブルに列情報がありません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
                Return
            End If

            ' TablePreview を表示（0行の場合も列ヘッダーは表示される）
            TablePreviewLogic.DisplayTableData(Me.MdiParent,
                                               If(tableData, New List(Of String())),
                                               columnNames,
                                               $"{_currentSchema}.{tableName}")

        Catch ex As Exception
            MessageBox.Show($"テーブル表示エラー: {ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
#End Region

#Region "テーブルプロパティ"
    ''' <summary>
    ''' 選択中のテーブルのプロパティダイアログを表示する
    ''' メインフォームのメニュー/ツールバーから呼び出される
    ''' </summary>
    Public Sub ShowTableProperty()
        If lstTableList.SelectedItems.Count = 0 Then
            MessageBox.Show("テーブルが選択されていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Dim selectedItem = lstTableList.SelectedItems(0)
        Dim tableName As String = selectedItem.Text

        If String.IsNullOrEmpty(_currentSchema) Then
            MessageBox.Show("スキーマが選択されていません。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If

        ' 行数を取得
        Dim rowCount As Long = 0
        If _tableList.ContainsKey(_currentSchema) Then
            Dim entry = _tableList(_currentSchema).Find(Function(x) x.Item1 = tableName)
            If entry IsNot Nothing Then
                rowCount = entry.Item2
            End If
        End If

        ' カラム名・カラム型を取得
        Dim tableKey = $"{_currentSchema}.{tableName}"
        Dim columnNames As String() = Nothing
        Dim columnTypes As String() = Nothing
        If _columnNamesMap.ContainsKey(tableKey) Then
            columnNames = _columnNamesMap(tableKey)
        End If
        If _columnTypesMap.ContainsKey(tableKey) Then
            columnTypes = _columnTypesMap(tableKey)
        End If

        ' プロパティダイアログを表示
        Dim dlg As New TablePropertyDialog(_currentSchema, tableName, columnNames, columnTypes, rowCount)
        dlg.ShowDialog(Me)
    End Sub
#End Region

#Region "テーブル除外機能"
    ''' <summary>
    ''' 選択中のテーブル（複数可）を除外リストに追加し、一覧から非表示にする
    ''' </summary>
    Public Sub ExcludeSelectedTable()
        If lstTableList.SelectedItems.Count = 0 Then
            MessageBox.Show("テーブルが選択されていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        If String.IsNullOrEmpty(_currentSchema) Then Return

        For Each item As ListViewItem In lstTableList.SelectedItems
            Dim tableKey = $"{_currentSchema}.{item.Text}"
            _excludedTables.Add(tableKey)
        Next
        DisplayTablesForSchema(_currentSchema)
    End Sub

    ''' <summary>
    ''' ListView の選択状態を反転する（選択中→未選択、未選択→選択中）
    ''' </summary>
    Public Sub InvertSelection()
        For Each item As ListViewItem In lstTableList.Items
            item.Selected = Not item.Selected
        Next
        lstTableList.Focus()
    End Sub

    ''' <summary>
    ''' すべての除外を解除し、全テーブルを再表示する
    ''' </summary>
    Public Sub RestoreAllExcludedTables()
        _excludedTables.Clear()
        If Not String.IsNullOrEmpty(_currentSchema) Then
            DisplayTablesForSchema(_currentSchema)
        End If
    End Sub

    ' コンテキストメニュー: 除外
    Private Sub mnuExclude_Click(sender As Object, e As EventArgs) Handles mnuExclude.Click
        ExcludeSelectedTable()
    End Sub

    ' コンテキストメニュー: 選択を反転
    Private Sub mnuInvertExclusion_Click(sender As Object, e As EventArgs) Handles mnuInvertExclusion.Click
        InvertSelection()
    End Sub

    ' コンテキストメニュー: すべての除外を解除
    Private Sub mnuRestoreAll_Click(sender As Object, e As EventArgs) Handles mnuRestoreAll.Click
        RestoreAllExcludedTables()
    End Sub
#End Region

#Region "エクスポート用コンテキスト取得"
    ''' <summary>
    ''' 選択中のテーブルのエクスポート用コンテキスト情報を返す
    ''' メインフォームのエクスポートボタンから呼び出される
    ''' </summary>
    Public Function GetSelectedTableExportContext() As ExportHelper.TableExportContext
        If lstTableList.SelectedItems.Count = 0 Then Return Nothing
        If String.IsNullOrEmpty(_currentSchema) Then Return Nothing

        Dim tableName = lstTableList.SelectedItems(0).Text
        Dim tableKey = $"{_currentSchema}.{tableName}"

        Dim ctx As New ExportHelper.TableExportContext()
        ctx.DumpFilePath = DumpFilePath
        ctx.Schema = _currentSchema
        ctx.TableName = tableName

        If _columnNamesMap.ContainsKey(tableKey) Then
            ctx.ColumnNames = _columnNamesMap(tableKey)
        End If
        If _columnTypesMap.ContainsKey(tableKey) Then
            ctx.ColumnTypes = _columnTypesMap(tableKey)
        End If

        If _tableList.ContainsKey(_currentSchema) Then
            Dim entry = _tableList(_currentSchema).Find(Function(x) x.Item1 = tableName)
            If entry IsNot Nothing Then
                ctx.RowCount = entry.Item2
                ctx.DataOffset = entry.Item3
            End If
        End If

        Return ctx
    End Function

    ''' <summary>
    ''' 現在表示中（除外されていない）の全テーブルのエクスポートコンテキストを返す
    ''' 一括エクスポート用
    ''' </summary>
    Public Function GetVisibleTableContexts() As List(Of ExportHelper.TableExportContext)
        Dim result As New List(Of ExportHelper.TableExportContext)
        If String.IsNullOrEmpty(_currentSchema) Then Return result
        If Not _tableList.ContainsKey(_currentSchema) Then Return result

        For Each tableInfo In _tableList(_currentSchema)
            Dim tableName = tableInfo.Item1
            Dim tableKey = $"{_currentSchema}.{tableName}"

            ' 除外テーブルをスキップ
            If _excludedTables.Contains(tableKey) Then Continue For

            Dim ctx As New ExportHelper.TableExportContext()
            ctx.DumpFilePath = DumpFilePath
            ctx.Schema = _currentSchema
            ctx.TableName = tableName
            ctx.RowCount = tableInfo.Item2
            ctx.DataOffset = tableInfo.Item3

            If _columnNamesMap.ContainsKey(tableKey) Then
                ctx.ColumnNames = _columnNamesMap(tableKey)
            End If
            If _columnTypesMap.ContainsKey(tableKey) Then
                ctx.ColumnTypes = _columnTypesMap(tableKey)
            End If

            result.Add(ctx)
        Next

        Return result
    End Function
#End Region

#Region "データ表示処理"
    ''' <summary>
    ''' TreeViewにスキーマ一覧のみを表示
    ''' </summary>
    Private Sub PopulateSchemaTree()
        treeDBList.Nodes.Clear()

        If _tableList Is Nothing OrElse _tableList.Count = 0 Then
            MessageBox.Show("データベースのスキーマが見つかりません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Try
            ' ルートノードを作成
            Dim rootNode As TreeNode = treeDBList.Nodes.Add(System.IO.Path.GetFileNameWithoutExtension(DumpFilePath))

            ' スキーマノードを作成
            For Each schemaKvp In _tableList
                Dim schemaName As String = schemaKvp.Key
                Dim tableCount As Integer = schemaKvp.Value.Count

                ' スキーマノードを作成（テーブル数を表示）
                Dim schemaNode As TreeNode = rootNode.Nodes.Add($"{schemaName} ({tableCount})")
            Next

            ' ルートノードを展開
            rootNode.Expand()

            ' 最初のスキーマを自動選択
            If rootNode.Nodes.Count > 0 Then
                treeDBList.SelectedNode = rootNode.Nodes(0)
                Dim firstSchemaName As String = ExtractSchemaName(rootNode.Nodes(0).Text)
                If _tableList.ContainsKey(firstSchemaName) Then
                    _currentSchema = firstSchemaName
                    DisplayTablesForSchema(firstSchemaName)
                End If
            End If

        Catch ex As Exception
            MessageBox.Show($"スキーマツリーの作成に失敗しました: {ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>
    ''' 指定されたスキーマのテーブル一覧をListViewに表示
    ''' </summary>
    Private Sub DisplayTablesForSchema(schemaName As String)
        lstTableList.Items.Clear()

        If String.IsNullOrEmpty(schemaName) Then
            Return
        End If

        If Not _tableList.ContainsKey(schemaName) Then
            MessageBox.Show($"スキーマ '{schemaName}' が見つかりません。", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If

        Try
            Dim tables = _tableList(schemaName)

            If tables Is Nothing OrElse tables.Count = 0 Then
                Return
            End If

            ' テーブル検索フィルタ（大文字小文字区別しない部分一致）
            Dim searchText = txtTableSearch.Text.Trim()

            For Each tableInfo In tables
                Dim tableName = tableInfo.Item1
                Dim rowCount = tableInfo.Item2

                ' 除外テーブルをスキップ
                Dim tableKey = $"{schemaName}.{tableName}"
                If _excludedTables.Contains(tableKey) Then
                    Continue For
                End If

                ' フィルタ: 検索文字列が空でなければ部分一致でスキップ判定
                If searchText.Length > 0 AndAlso
                   Not tableName.Contains(searchText, StringComparison.OrdinalIgnoreCase) Then
                    Continue For
                End If

                ' ListViewItemを作成
                Dim item As New ListViewItem(tableName)
                item.SubItems.Add(schemaName)       ' 所有者
                item.SubItems.Add("TABLE")          ' 種類
                item.SubItems.Add(rowCount.ToString("#,0"))  ' 行数

                lstTableList.Items.Add(item)
            Next

        Catch ex As Exception
            MessageBox.Show($"テーブル一覧の表示に失敗しました: {ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
#End Region

End Class