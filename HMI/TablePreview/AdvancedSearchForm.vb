Imports System.Collections.Generic

''' <summary>
''' 高度な検索フォーム
''' 
''' 複数の検索条件を AND/OR ロジックで組み合わせて検索できるダイアログフォーム。
''' TablePreview フォームから呼び出され、複合検索条件を構築して返す。
''' 
''' 主な機能:
''' - 動的に検索条件行を追加・削除可能
''' - 各条件で AND/OR を選択可能
''' - 前回の検索条件を復元可能
''' - 入力検証機能
''' 
''' UI コンポーネント（デザイナーで定義):
''' - flowLayoutPanel: 検索条件行を表示するコンテナ
''' - buttonAdd: 条件を追加するボタン
''' - buttonClear: 条件をクリアするボタン
''' - buttonSearch: 検索を実行するボタン
''' - buttonCancel: ダイアログをキャンセルするボタン
''' - templateConditionRow: SearchConditionRow のテンプレート（Hidden）
''' - templateLogicalPanel: AND/OR パネルのテンプレート（Hidden）
''' 
''' 使用例:
''' Dim form As New AdvancedSearchForm(columnNames)
''' If form.ShowDialog() = DialogResult.OK Then
'''     Dim condition = form.SearchConditionResult
'''     ' フィルタリング処理
''' End If
''' </summary>

Public Class AdvancedSearchForm
    Implements ILocalizable

#Region "フィールド・プロパティ"

    ''' <summary>検索可能な列名のリスト</summary>
    Private _columnNames As List(Of String)

    ''' <summary>構築された複合検索条件</summary>
    Private _searchCondition As SearchCondition.ComplexSearchCondition

    ''' <summary>現在追加されている検索条件行のリスト</summary>
    Private _conditionRows As New List(Of SearchConditionRow)()

#End Region

#Region "検索条件プロパティ"
    ''' <summary>
    ''' 検索結果の複合条件を取得/設定するプロパティ
    ''' 
    ''' SearchButton クリック時に構築され、親フォーム（TablePreview）から参照される。
    ''' Designer 非表示属性により、デザイナーでのシリアライズをスキップ。
    ''' </summary>
    <System.ComponentModel.Browsable(False)>
    <System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)>
    Public Property SearchConditionResult As SearchCondition.ComplexSearchCondition
        Get
            ' 現在の複合検索条件を返す（TablePreview側で参照される）
            Return _searchCondition
        End Get
        Set(value As SearchCondition.ComplexSearchCondition)
            ' 外部から複合検索条件をセット
            _searchCondition = value
        End Set
    End Property
#End Region

#Region "コンストラクタ"
    ''' <summary>
    ''' コンストラクタ
    ''' 
    ''' デザイナーで定義されたコンポーネントを初期化し、
    ''' テンプレートコンポーネント（templateConditionRow, templateLogicalPanel）
    ''' をコントロール階層から削除する。
    ''' テンプレートは後で CloneConditionRow / CloneLogicalPanel で使用される。
    ''' </summary>
    ''' <param name="columnNames">検索可能な列名のリスト</param>
    Public Sub New(columnNames As List(Of String))
        ' デザイナーで定義されたコンポーネントを初期化
        InitializeComponent()

        ' パラメータを保存
        _columnNames = New List(Of String)(columnNames)
        _searchCondition = New SearchCondition.ComplexSearchCondition()

        ' テンプレートコンポーネントをコントロール階層から削除
        ' これにより、デザイナーの定義を保持しながら、フォーム上には表示されない
        Me.Controls.Remove(templateConditionRow)
        Me.Controls.Remove(templateLogicalPanel)

        ' ローカライズ適用
        ApplyLocalization()
    End Sub
#End Region

#Region "検索条件セット・復元"
    ''' <summary>
    ''' 前回の検索条件を設定する
    ''' 
    ''' TablePreview から前回の検索条件を受け取り、
    ''' フォーム読み込み時に UI に復元するために使用。
    ''' </summary>
    ''' <param name="condition">復元する検索条件</param>
    Public Sub SetSearchCondition(condition As SearchCondition.ComplexSearchCondition)
        If condition IsNot Nothing AndAlso condition.Conditions.Count > 0 Then
            ' フォーム読み込み後に復元する
            ' ここでは記憶するのみ
            _searchCondition = condition
        End If
    End Sub
#End Region

#Region "フォームイベント・UI初期化"
    ''' <summary>
    ''' フォーム読み込み時のイベントハンドラー
    ''' 
    ''' - ボタンのイベントハンドラーを登録
    ''' - 前回の検索条件があれば復元、なければ初期条件行を1行追加
    ''' </summary>
    Private Sub AdvancedSearchForm_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' ボタンクリックイベントをハンドラーに関連付け
        AddHandler buttonAdd.Click, AddressOf ButtonAdd_Click
        AddHandler buttonClear.Click, AddressOf ButtonClear_Click
        AddHandler buttonSearch.Click, AddressOf ButtonSearch_Click
        AddHandler MyBase.FormClosing, AddressOf AdvancedSearchForm_FormClosing

        ' 前回の検索条件がある場合は復元、なければデフォルト状態を初期化
        If _searchCondition IsNot Nothing AndAlso _searchCondition.Conditions.Count > 0 Then
            ' 前回の検索条件を復元
            RestoreSearchCondition(_searchCondition)
        Else
            ' デフォルトで条件行を1つ追加
            ' これにより、ユーザーは即座に条件を入力できる状態になる
            AddSearchConditionRow()
        End If
    End Sub
#End Region

#Region "検索条件行の追加・削除・UI操作"
    ''' <summary>
    ''' 検索条件行を1行追加する
    ''' 
    ''' フロー:
    ''' 1. 行のインデックスを取得（_conditionRows.Count）
    ''' 2. 最初の行（インデックス 0）か、それ以降か判定
    ''' 3. 最初の行: CreateConditionRowFromTemplate() で新規作成
    ''' 4. 2行目以降: CloneConditionRow() で新規作成
    ''' 5. 2行目以降: AND/OR パネルを追加
    ''' 6. 削除ボタンの表示制御（2行目以降のみ表示）
    ''' 7. flowLayoutPanel に追加
    ''' 8. _conditionRows リストに追加
    ''' </summary>
    Private Sub AddSearchConditionRow()
        Dim row As SearchConditionRow
        Dim rowIndex = _conditionRows.Count

        ' 最初の行か、それ以降かで処理を分岐
        If rowIndex = 0 Then
            ' 最初の行はテンプレートから新規作成
            row = CreateConditionRowFromTemplate()
        Else
            ' 2番目以降は新規作成
            row = CloneConditionRow(templateConditionRow)
        End If

        ' 2行目以降は AND/OR パネルを追加
        If rowIndex > 0 Then
            ' AND/OR 選択用パネルをクローン
            Dim logicalPanel = CloneLogicalPanel()
            ' コンボボックスを row に関連付け
            ' コンボボックスはコントロール配列の2番目（インデックス 1）
            row.LogicalComboBox = CType(logicalPanel.Controls(1), ComboBox)
            ' UI に追加
            flowLayoutPanel.Controls.Add(logicalPanel)
        End If

        ' 削除ボタンの表示制御
        ' 最初の行は削除不可（最低1つの条件は必要）、2行目以降は削除可能
        row.ShowDeleteButton = rowIndex > 0
        ' 削除ボタンのクリックイベントハンドラーを登録
        AddHandler row.DeleteButton.Click, Sub(s, e) RemoveSearchConditionRow(row)

        ' UI に追加
        flowLayoutPanel.Controls.Add(row)
        ' 内部リストに追加
        _conditionRows.Add(row)
    End Sub

    ''' <summary>
    ''' SearchConditionRow を新規作成する（最初の行用）
    ''' </summary>
    ''' <returns>新規作成された SearchConditionRow インスタンス</returns>
    Private Function CreateConditionRowFromTemplate() As SearchConditionRow
        ' 列名リストを渡して新規作成
        Dim row As New SearchConditionRow(_columnNames)
        Return row
    End Function

    ''' <summary>
    ''' SearchConditionRow をクローン作成する（2行目以降用）
    ''' </summary>
    ''' <param name="original">クローン元の SearchConditionRow（使用されていない）</param>
    ''' <returns>新規作成された SearchConditionRow インスタンス</returns>
    Private Function CloneConditionRow(original As SearchConditionRow) As SearchConditionRow
        ' 新規作成
        Dim row As New SearchConditionRow(_columnNames)
        ' 必要に応じて original の状態をコピー
        ' 現在は未実装（全て同じデフォルト状態）
        Return row
    End Function

    ''' <summary>
    ''' AND/OR 選択パネルをクローン作成する
    ''' </summary>
    ''' <returns>新規作成された AND/OR パネル</returns>
    Private Function CloneLogicalPanel() As Panel
        ' パネルを作成
        Dim logicalPanel As New Panel()
        logicalPanel.Height = 40
        logicalPanel.AutoSize = True
        logicalPanel.BackColor = SystemColors.Control

        ' ラベルを作成（"条件:"）
        Dim logicalLabel As New Label()
        logicalLabel.Text = Loc.S("AdvSearch_ConditionLabel")
        logicalLabel.Location = New Point(10, 8)
        logicalLabel.Width = 60

        ' AND/OR コンボボックスを作成
        Dim logicalCombo As New ComboBox()
        logicalCombo.Items.Add("AND")
        logicalCombo.Items.Add("OR")
        logicalCombo.SelectedIndex = 0  ' デフォルトは AND
        logicalCombo.Location = New Point(70, 5)
        logicalCombo.Width = 100
        logicalCombo.DropDownStyle = ComboBoxStyle.DropDownList  ' ドロップダウンのみ

        ' パネルにコントロールを追加
        logicalPanel.Controls.Add(logicalLabel)
        logicalPanel.Controls.Add(logicalCombo)

        Return logicalPanel
    End Function

    ''' <summary>
    ''' 検索条件行を削除する
    ''' </summary>
    ''' <param name="row">削除する検索条件行</param>
    Private Sub RemoveSearchConditionRow(row As SearchConditionRow)
        ' 最低1つの条件は必要なため、条件数が1以下の場合は中止
        If _conditionRows.Count <= 1 Then
            MessageBox.Show(Loc.S("AdvSearch_MinCondition"), Loc.S("Title_Warning"), MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        ' 削除対象行のインデックスを取得
        Dim index = _conditionRows.IndexOf(row)
        ' リストから削除
        _conditionRows.Remove(row)

        ' UI から削除
        flowLayoutPanel.Controls.Remove(row)

        ' AND/OR パネルも削除
        ' 削除対象行の直前の AND/OR パネルが対象
        If index < flowLayoutPanel.Controls.Count Then
            Dim controlToRemove = flowLayoutPanel.Controls(index)
            ' Panel 型かどうかをチェック（AND/OR パネルは Panel 型）
            If controlToRemove.GetType() = GetType(Panel) Then
                flowLayoutPanel.Controls.Remove(controlToRemove)
            End If
        End If
    End Sub


    '-------------------- UI操作イベント --------------------
    ''' <summary>
    ''' 「条件を追加」ボタンのクリックイベントハンドラー
    ''' </summary>
    Private Sub ButtonAdd_Click(sender As Object, e As EventArgs)
        AddSearchConditionRow()
    End Sub

    ''' <summary>
    ''' 「クリア」ボタンのクリックイベントハンドラー
    ''' </summary>
    Private Sub ButtonClear_Click(sender As Object, e As EventArgs)
        ' 既存の条件行をすべてクリア
        _conditionRows.Clear()
        flowLayoutPanel.Controls.Clear()

        ' 検索条件をリセット
        _searchCondition = New SearchCondition.ComplexSearchCondition()

        ' デフォルト状態に戻す（条件1行を追加）
        AddSearchConditionRow()
    End Sub

    ''' <summary>
    ''' 「検索」ボタンのクリックイベントハンドラー
    ''' </summary>
    Private Sub ButtonSearch_Click(sender As Object, e As EventArgs)
        ' Step 1: 入力検証
        For Each row In _conditionRows
            If Not row.IsValid() Then
                ' 無効な条件がある場合はエラーメッセージを表示
                MessageBox.Show(Loc.S("AdvSearch_InvalidConditions"), Loc.S("Title_InputError"), MessageBoxButtons.OK, MessageBoxIcon.Warning)
                Return
            End If
        Next

        ' Step 2-3: 複合検索条件を構築
        _searchCondition = New SearchCondition.ComplexSearchCondition()

        ' すべての条件行を走査
        For i = 0 To _conditionRows.Count - 1
            ' 各行から SearchConditionItem を取得
            Dim condition = _conditionRows(i).GetCondition()
            ' Conditions リストに追加
            _searchCondition.Conditions.Add(condition)

            ' 2行目以降は AND/OR 演算子を追加
            ' LogicalOperators は Conditions より1つ少ない（最初の行には演算子がない）
            If i > 0 Then
                Dim logicalOp = _conditionRows(i).GetLogicalOperator()
                _searchCondition.LogicalOperators.Add(logicalOp)
            End If
        Next

        ' Step 4: DialogResult を OK に設定して、親フォームに返す
        ' Dialog が OK で閉じられたことは、AdvancedSearchForm_FormClosing で処理される
    End Sub
    '------------------------------------------------------
#End Region

#Region "フォームクローズ処理"
    ''' <summary>
    ''' フォームクローズ時のイベントハンドラー
    ''' </summary>
    Private Sub AdvancedSearchForm_FormClosing(sender As Object, e As FormClosingEventArgs)
        ' OK ボタン（検索ボタン）でクローズされた場合
        If Me.DialogResult = DialogResult.OK Then
            ' 検索条件の構築（念のため再実行）
            ButtonSearch_Click(Nothing, Nothing)
        End If
        ' キャンセルボタンの場合は何もしない
    End Sub
#End Region

#Region "検索条件復元用メソッド"

    ''' <summary>
    ''' 前回の検索条件を UI に復元する
    ''' 
    ''' 保存されていた複合検索条件から、各条件行を再構築し、
    ''' フォーム上に復元する。
    ''' 
    ''' 処理フロー:
    ''' 1. 保存条件の数だけループ
    ''' 2. 各条件ごとに AddSearchConditionRow() で行を追加
    ''' 3. 列、演算子、値、大文字小文字区別を復元
    ''' 4. 2行目以降は AND/OR を復元
    ''' </summary>
    ''' <param name="condition">復元する複合検索条件</param>
    Private Sub RestoreSearchCondition(condition As SearchCondition.ComplexSearchCondition)
        ' 保存されている条件の数だけループ
        For i = 0 To condition.Conditions.Count - 1
            ' 条件行を追加
            AddSearchConditionRow()

            ' 追加した条件行を取得
            Dim condRow = _conditionRows(i)
            ' 復元する検索条件を取得
            Dim searchCond = condition.Conditions(i)

            ' 列を復元
            Dim columnIndex = condRow.GetColumnIndex(searchCond.ColumnName)
            If columnIndex >= 0 Then
                condRow.SetColumnIndex(columnIndex)
            End If

            ' 演算子を復元
            Dim operatorIndex = GetOperatorIndex(searchCond.OperatorType)
            condRow.SetOperatorIndex(operatorIndex)

            ' 値を復元
            condRow.SetValue(If(searchCond.Value IsNot Nothing, searchCond.Value.ToString(), ""))

            ' 大文字小文字区別を復元
            condRow.SetCaseSensitive(searchCond.CaseSensitive)

            ' 2行目以降は AND/OR 論理演算子を復元
            If i > 0 Then
                Dim logicalCombo = condRow.LogicalComboBox
                If logicalCombo IsNot Nothing Then
                    ' LogicalOperators は Conditions より1つ少ないため、i-1 でアクセス
                    Dim logicalOp = condition.LogicalOperators(i - 1)
                    If logicalOp = SearchCondition.LogicalOperatorType.And Then
                        logicalCombo.SelectedIndex = 0  ' AND
                    Else
                        logicalCombo.SelectedIndex = 1  ' OR
                    End If
                End If
            End If
        Next
    End Sub

    ''' <summary>
    ''' 演算子タイプをコンボボックスのインデックスに変換する
    ''' 
    ''' SearchCondition.OperatorType から operatorCombo の SelectedIndex に変換。
    ''' マッピング:
    ''' 0 = Contains (含む)
    ''' 1 = NotContains (含まない)
    ''' 2 = Equals (等しい)
    ''' 3 = NotEquals (等しくない)
    ''' 4 = StartsWith (で始まる)
    ''' 5 = EndsWith (で終わる)
    ''' 6 = GreaterThan (>)
    ''' 7 = LessThan (<)
    ''' 8 = GreaterThanOrEqual (>=)
    ''' 9 = LessThanOrEqual (<=)
    ''' 10 = IsNull (Null)
    ''' 11 = IsNotNull (Not Null)
    ''' </summary>
    ''' <param name="operatorType">変換対象の演算子タイプ</param>
    ''' <returns>対応するコンボボックスのインデックス</returns>
    Private Function GetOperatorIndex(operatorType As SearchCondition.OperatorType) As Integer
        Select Case operatorType
            Case SearchCondition.OperatorType.Contains
                Return 0
            Case SearchCondition.OperatorType.NotContains
                Return 1
            Case SearchCondition.OperatorType.Equals
                Return 2
            Case SearchCondition.OperatorType.NotEquals
                Return 3
            Case SearchCondition.OperatorType.StartsWith
                Return 4
            Case SearchCondition.OperatorType.EndsWith
                Return 5
            Case SearchCondition.OperatorType.GreaterThan
                Return 6
            Case SearchCondition.OperatorType.LessThan
                Return 7
            Case SearchCondition.OperatorType.GreaterThanOrEqual
                Return 8
            Case SearchCondition.OperatorType.LessThanOrEqual
                Return 9
            Case SearchCondition.OperatorType.IsNull
                Return 10
            Case SearchCondition.OperatorType.IsNotNull
                Return 11
            Case Else
                Return 0  ' デフォルトは Contains
        End Select
    End Function

#End Region

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("AdvSearch_FormTitle")
        buttonAdd.Text = Loc.S("Button_AddCondition")
        buttonClear.Text = Loc.S("Button_Clear")
        buttonSearch.Text = Loc.S("Button_Search")
        buttonCancel.Text = Loc.S("Button_Cancel")
    End Sub
#End Region

End Class
