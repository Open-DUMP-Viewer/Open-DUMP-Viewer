Imports System.Collections.Generic
Imports System.ComponentModel

Public Class Workspace
    Inherits Form
    Implements ILocalizable

#Region "フィールド・コンストラクタ"
    <DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    <Browsable(False)>
    Public Property DumpFilePath As String
    <DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    <Browsable(False)>
    Public Property WorkspacePath As String
    ''' <summary>テーブル一覧メタデータ (スキーマ名→(テーブル名, 行数, データオフセット)リスト)</summary>
    Private _tableList As New Dictionary(Of String, List(Of Tuple(Of String, Long, Long)))
    ''' <summary>テーブルごとのカラム名 (キー: "schema.table")</summary>
    Private _columnNamesMap As New Dictionary(Of String, String())
    ''' <summary>テーブルごとのカラム型 (キー: "schema.table")</summary>
    Private _columnTypesMap As New Dictionary(Of String, String())
    Private _columnNotNullsMap As New Dictionary(Of String, Boolean())
    Private _columnDefaultsMap As New Dictionary(Of String, String())
    Private _currentSchema As String = String.Empty
    ''' <summary>除外テーブル (キー: "schema.table")</summary>
    Private _excludedTables As New HashSet(Of String)
    ''' <summary>除外操作の元に戻すスタック</summary>
    Private _undoStack As New Stack(Of HashSet(Of String))
    ''' <summary>除外操作のやり直しスタック</summary>
    Private _redoStack As New Stack(Of HashSet(Of String))

    ' 引数ありコンストラクタ
    Public Sub New(value1 As String, value2 As String)
        InitializeComponent()
        ApplyLocalization()
        DumpFilePath = value1
        WorkspacePath = value2
    End Sub
#End Region

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        ' ListView 列ヘッダー
        lstTableList.Columns(0).Text = Loc.S("Workspace_Column_Name")
        lstTableList.Columns(1).Text = Loc.S("Workspace_Column_Owner")
        lstTableList.Columns(2).Text = Loc.S("Workspace_Column_Type")
        lstTableList.Columns(3).Text = Loc.S("Workspace_Column_RowCount")

        ' コンテキストメニュー
        mnuExclude.Text = Loc.S("Workspace_ContextMenu_Exclude")
        mnuInvertExclusion.Text = Loc.S("Workspace_ContextMenu_InvertSelection")
        mnuRestoreAll.Text = Loc.S("Workspace_ContextMenu_RestoreAll")

        ' 検索ボタン
        btnTableSearch.Text = Loc.S("Button_Search")
    End Sub
#End Region

#Region "イベント処理"
    Private Sub Workspace_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' フェーズ1: テーブル一覧のみ取得（高速・メモリ軽量）
        Dim colMap As Dictionary(Of String, String()) = Nothing
        Dim typeMap As Dictionary(Of String, String()) = Nothing
        Dim nnMap As Dictionary(Of String, Boolean()) = Nothing
        Dim defMap As Dictionary(Of String, String()) = Nothing
        Dim tables = AnalyzeLogic.ListTables(DumpFilePath, colMap, typeMap, nnMap, defMap)
        If colMap IsNot Nothing Then _columnNamesMap = colMap
        If typeMap IsNot Nothing Then _columnTypesMap = typeMap
        If nnMap IsNot Nothing Then _columnNotNullsMap = nnMap
        If defMap IsNot Nothing Then _columnDefaultsMap = defMap

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
        OpenSelectedTable()
    End Sub

    ''' <summary>
    ''' 選択中のテーブルをTablePreviewで開く (非同期版: UIスレッドをブロックしない)
    ''' メインフォームのメニューから呼び出し可能
    ''' </summary>
    Public Async Sub OpenSelectedTable()
        If lstTableList.SelectedItems.Count = 0 Then
            MessageBox.Show(Loc.S("Workspace_TableNotSelected"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Try
            Dim selectedItem = lstTableList.SelectedItems(0)
            Dim tableName As String = selectedItem.Text

            If String.IsNullOrEmpty(_currentSchema) Then
                MessageBox.Show(Loc.S("Workspace_SchemaNotSelected"), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
                Return
            End If

            ' テーブルのデータオフセットと期待行数を取得
            Dim dataOffset As Long = 0
            Dim expectedRowCount As Long = 0
            If _tableList.ContainsKey(_currentSchema) Then
                Dim entry = _tableList(_currentSchema).Find(Function(x) x.Item1 = tableName)
                If entry IsNot Nothing Then
                    dataOffset = entry.Item3
                    expectedRowCount = entry.Item2
                End If
            End If

            ' フェーズ2: 選択テーブルのみ非同期解析（UIスレッドをブロックしない）
            Dim tableData = Await AnalyzeLogic.AnalyzeTableAsync(DumpFilePath, _currentSchema, tableName, dataOffset, expectedRowCount)

            ' 列名を取得（Phase1のListTablesで取得済みのキャッシュから）
            Dim columnNames As New List(Of String)
            Dim tableKey = $"{_currentSchema}.{tableName}"
            If _columnNamesMap.ContainsKey(tableKey) Then
                columnNames = New List(Of String)(_columnNamesMap(tableKey))
            End If

            If columnNames.Count = 0 Then
                MessageBox.Show(Loc.S("Workspace_NoColumnInfo"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                Return
            End If

            ' カラム型を取得
            Dim columnTypes As String() = Nothing
            If _columnTypesMap.ContainsKey(tableKey) Then
                columnTypes = _columnTypesMap(tableKey)
            End If
            Dim columnNotNulls As Boolean() = Nothing
            If _columnNotNullsMap.ContainsKey(tableKey) Then
                columnNotNulls = _columnNotNullsMap(tableKey)
            End If
            Dim columnDefaults As String() = Nothing
            If _columnDefaultsMap.ContainsKey(tableKey) Then
                columnDefaults = _columnDefaultsMap(tableKey)
            End If

            ' TablePreview を表示（0行の場合も列ヘッダーは表示される）
            TablePreviewLogic.DisplayTableData(Me.MdiParent,
                                               If(tableData, New List(Of String())),
                                               columnNames,
                                               tableName, _currentSchema, columnTypes,
                                               columnNotNulls, columnDefaults)

        Catch ex As Exception
            MessageBox.Show(Loc.SF("Workspace_TableDisplayError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
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
            MessageBox.Show(Loc.S("Workspace_TableNotSelected"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Dim selectedItem = lstTableList.SelectedItems(0)
        Dim tableName As String = selectedItem.Text

        If String.IsNullOrEmpty(_currentSchema) Then
            MessageBox.Show(Loc.S("Workspace_SchemaNotSelected"), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
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
            MessageBox.Show(Loc.S("Workspace_TableNotSelected"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        If String.IsNullOrEmpty(_currentSchema) Then Return

        ' Undo 用に現在の状態を保存
        _undoStack.Push(New HashSet(Of String)(_excludedTables))
        _redoStack.Clear()

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

    ''' <summary>除外操作を元に戻す</summary>
    Public Sub UndoExclusion()
        If _undoStack.Count = 0 Then Return
        _redoStack.Push(New HashSet(Of String)(_excludedTables))
        _excludedTables = _undoStack.Pop()
        If Not String.IsNullOrEmpty(_currentSchema) Then
            DisplayTablesForSchema(_currentSchema)
        End If
    End Sub

    ''' <summary>除外操作をやり直す</summary>
    Public Sub RedoExclusion()
        If _redoStack.Count = 0 Then Return
        _undoStack.Push(New HashSet(Of String)(_excludedTables))
        _excludedTables = _redoStack.Pop()
        If Not String.IsNullOrEmpty(_currentSchema) Then
            DisplayTablesForSchema(_currentSchema)
        End If
    End Sub

    Public ReadOnly Property CanUndo As Boolean
        Get
            Return _undoStack.Count > 0
        End Get
    End Property

    Public ReadOnly Property CanRedo As Boolean
        Get
            Return _redoStack.Count > 0
        End Get
    End Property

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

#Region "ワークスペース保存/復元"
    ''' <summary>現在の状態を WorkspaceData に詰めて返す</summary>
    Public Function GetWorkspaceData() As WorkspaceData
        Dim data As New WorkspaceData()
        data.DumpFilePath = DumpFilePath
        data.ExcludedTables = New List(Of String)(_excludedTables)
        data.SearchFilter = txtTableSearch.Text
        data.CurrentSchema = _currentSchema

        ' TreeView の展開ノード名を収集
        data.ExpandedNodes = New List(Of String)
        If treeDBList.Nodes.Count > 0 Then
            CollectExpandedNodes(treeDBList.Nodes(0), data.ExpandedNodes)
        End If
        Return data
    End Function

    Private Sub CollectExpandedNodes(node As TreeNode, result As List(Of String))
        If node.IsExpanded Then result.Add(node.Text)
        For Each child As TreeNode In node.Nodes
            CollectExpandedNodes(child, result)
        Next
    End Sub

    ''' <summary>WorkspaceData から除外テーブル・検索フィルタ・TreeView展開を復元する</summary>
    Public Sub LoadWorkspaceState(data As WorkspaceData)
        ' 除外テーブル復元
        _excludedTables.Clear()
        If data.ExcludedTables IsNot Nothing Then
            For Each t In data.ExcludedTables
                _excludedTables.Add(t)
            Next
        End If
        _undoStack.Clear()
        _redoStack.Clear()

        ' 検索フィルタ復元
        If data.SearchFilter IsNot Nothing Then
            txtTableSearch.Text = data.SearchFilter
        End If

        ' スキーマ選択復元
        If Not String.IsNullOrEmpty(data.CurrentSchema) AndAlso _tableList.ContainsKey(data.CurrentSchema) Then
            _currentSchema = data.CurrentSchema
            ' TreeView で該当ノードを選択
            If treeDBList.Nodes.Count > 0 Then
                For Each child As TreeNode In treeDBList.Nodes(0).Nodes
                    If ExtractSchemaName(child.Text) = data.CurrentSchema Then
                        treeDBList.SelectedNode = child
                        Exit For
                    End If
                Next
            End If
            DisplayTablesForSchema(data.CurrentSchema)
        End If

        ' TreeView 展開ノード復元
        If data.ExpandedNodes IsNot Nothing AndAlso treeDBList.Nodes.Count > 0 Then
            RestoreExpandedNodes(treeDBList.Nodes(0), data.ExpandedNodes)
        End If
    End Sub

    Private Sub RestoreExpandedNodes(node As TreeNode, expandedNames As List(Of String))
        If expandedNames.Contains(node.Text) Then node.Expand()
        For Each child As TreeNode In node.Nodes
            RestoreExpandedNodes(child, expandedNames)
        Next
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
        If _columnNotNullsMap.ContainsKey(tableKey) Then
            ctx.ColumnNotNulls = _columnNotNullsMap(tableKey)
        End If
        If _columnDefaultsMap.ContainsKey(tableKey) Then
            ctx.ColumnDefaults = _columnDefaultsMap(tableKey)
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
            If _columnNotNullsMap.ContainsKey(tableKey) Then
                ctx.ColumnNotNulls = _columnNotNullsMap(tableKey)
            End If
            If _columnDefaultsMap.ContainsKey(tableKey) Then
                ctx.ColumnDefaults = _columnDefaultsMap(tableKey)
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
            MessageBox.Show(Loc.S("Workspace_NoSchema"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
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
            MessageBox.Show(Loc.SF("Workspace_SchemaTreeError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>
    ''' テーブル一覧レポートをテキストファイルに出力
    ''' </summary>
    Public Sub ExportTableListReport(outputPath As String)
        Using sw As New System.IO.StreamWriter(outputPath, False, System.Text.Encoding.UTF8)
            sw.WriteLine(Loc.SF("Report_ObjectList_Title", System.IO.Path.GetFileName(DumpFilePath)))
            sw.WriteLine(Loc.SF("Report_OutputDate", DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss")))
            sw.WriteLine(New String("-"c, 80))
            sw.WriteLine($"{Loc.S("Report_Column_Schema"),-20} {Loc.S("Report_Column_TableName"),-30} {Loc.S("Report_Column_RowCount"),10}")
            sw.WriteLine(New String("-"c, 80))
            For Each schemaKvp In _tableList
                For Each t In schemaKvp.Value
                    sw.WriteLine($"{schemaKvp.Key,-20} {t.Item1,-30} {t.Item2,10:#,0}")
                Next
            Next
        End Using
    End Sub

    ''' <summary>
    ''' テーブル定義レポートをテキストファイルに出力
    ''' </summary>
    Public Sub ExportTableDefinitionReport(outputPath As String)
        If lstTableList.SelectedItems.Count = 0 OrElse String.IsNullOrEmpty(_currentSchema) Then Return
        Using sw As New System.IO.StreamWriter(outputPath, False, System.Text.Encoding.UTF8)
            sw.WriteLine(Loc.SF("Report_TableDef_Title", System.IO.Path.GetFileName(DumpFilePath)))
            sw.WriteLine(Loc.SF("Report_OutputDate", DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss")))
            For Each selectedItem As ListViewItem In lstTableList.SelectedItems
                Dim tableName = selectedItem.Text
                Dim tableKey = $"{_currentSchema}.{tableName}"
                sw.WriteLine()
                sw.WriteLine(New String("="c, 60))
                sw.WriteLine(Loc.SF("Report_Table", $"{_currentSchema}.{tableName}"))
                sw.WriteLine(New String("="c, 60))
                Dim colNames As String() = Nothing
                Dim colTypes As String() = Nothing
                If _columnNamesMap.ContainsKey(tableKey) Then colNames = _columnNamesMap(tableKey)
                If _columnTypesMap.ContainsKey(tableKey) Then colTypes = _columnTypesMap(tableKey)
                If colNames IsNot Nothing Then
                    sw.WriteLine($"{Loc.S("Report_Column_Number"),4} {Loc.S("Report_Column_ColumnName"),-30} {Loc.S("Report_Column_ColumnType"),-30}")
                    sw.WriteLine(New String("-"c, 60))
                    For i = 0 To colNames.Length - 1
                        Dim typeName = If(colTypes IsNot Nothing AndAlso i < colTypes.Length, colTypes(i), "")
                        sw.WriteLine($"{i + 1,4} {colNames(i),-30} {typeName,-30}")
                    Next
                End If
            Next
        End Using
    End Sub

    ''' <summary>
    ''' 指定されたスキーマのテーブル一覧をListViewに表示
    ''' </summary>
    Private Sub DisplayTablesForSchema(schemaName As String)
        If String.IsNullOrEmpty(schemaName) Then
            lstTableList.Items.Clear()
            Return
        End If

        If Not _tableList.ContainsKey(schemaName) Then
            MessageBox.Show(Loc.SF("Workspace_SchemaNotFound", schemaName), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If

        Try
            Dim tables = _tableList(schemaName)

            ' BeginUpdate/EndUpdate で描画を抑制（大量テーブル時のパフォーマンス改善）
            lstTableList.BeginUpdate()
            Try
                lstTableList.Items.Clear()

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
            Finally
                lstTableList.EndUpdate()
            End Try

        Catch ex As Exception
            MessageBox.Show(Loc.SF("Workspace_TableListError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
#End Region

End Class