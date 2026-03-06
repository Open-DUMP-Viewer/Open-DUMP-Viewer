Imports System.Collections.Generic

''' <summary>
''' 単一の検索条件行UI
''' 
''' 高度な検索フォームで使用される検索条件を入力するUIコンポーネント。
''' 列名、演算子、検索値、大文字小文字区別の指定、および AND/OR の論理演算子を管理する。
''' 
''' デザイナーで定義されたコンポーネント:
''' - columnCombo: 検索対象の列を選択
''' - operatorCombo: 検索演算子を選択（12種類）
''' - valueTextBox: 検索値を入力
''' - caseSensitiveCheckBox: 大文字小文字区別をチェック
''' - btnDelete: 条件行を削除するボタン
''' 
''' 使用例:
''' Dim row As New SearchConditionRow(columnNames)
''' If row.IsValid() Then
'''     Dim condition = row.GetCondition()
''' End If
''' </summary>
Public Class SearchConditionRow
    Inherits Panel

    ''' <summary>
    ''' 2行目以降の AND/OR コンボボックス
    ''' 前の条件との論理演算子を指定するために使用
    ''' </summary>
    Public LogicalComboBox As ComboBox

    ''' <summary>
    ''' 削除ボタンの表示/非表示を制御するプロパティ
    ''' 最初の条件行（1行目）では非表示、2行目以降は表示
    ''' </summary>
    <System.ComponentModel.Browsable(False)>
    <System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)>
    Public Property ShowDeleteButton As Boolean
        Get
            Return btnDelete.Visible
        End Get
        Set(value As Boolean)
            btnDelete.Visible = value
        End Set
    End Property

    ''' <summary>
    ''' 削除ボタンへのアクセスを提供するプロパティ
    ''' 外部からイベントハンドラーを追加する際に使用
    ''' </summary>
    Public ReadOnly Property DeleteButton As Button
        Get
            Return btnDelete
        End Get
    End Property

    ''' <summary>
    ''' コンストラクタ
    ''' 
    ''' デザイナーで自動生成されたコンポーネントを初期化し、
    ''' 列名一覧をコンボボックスに設定します。
    ''' </summary>
    ''' <param name="columnNames">検索可能な列名のリスト</param>
    Public Sub New(columnNames As List(Of String))
        ' デザイナーで定義されたコンポーネントを初期化
        InitializeComponent()

        ' 列名をコンボボックスに追加
        For Each colName In columnNames
            columnCombo.Items.Add(colName)
        Next

        ' 最初の列を選択状態にする
        If columnCombo.Items.Count > 0 Then
            columnCombo.SelectedIndex = 0
        End If

        ' 演算子コンボボックスをローカライズ済みテキストで再設定
        operatorCombo.Items.Clear()
        operatorCombo.Items.AddRange(New Object() {
            Loc.S("Operator_Contains"), Loc.S("Operator_NotContains"),
            Loc.S("Operator_Equals"), Loc.S("Operator_NotEquals"),
            Loc.S("Operator_StartsWith"), Loc.S("Operator_EndsWith"),
            ">", "<", ">=", "<=", "Null", "Not Null"})
        operatorCombo.SelectedIndex = 0

        ' チェックボックス・ボタンをローカライズ
        caseSensitiveCheckBox.Text = Loc.S("Search_CaseSensitive")
        btnDelete.Text = Loc.S("Button_Delete")

        ' 列が変更されたときに検索値をクリアするイベントハンドラーを追加
        ' 異なる列に変更した際に、前の列の値が残らないようにするため
        AddHandler columnCombo.SelectedIndexChanged, AddressOf ColumnCombo_SelectedIndexChanged
    End Sub

    ''' <summary>
    ''' 列が選択されたときのイベントハンドラー
    ''' 列を変更した場合、前の列の検索値をクリアする
    ''' </summary>
    Private Sub ColumnCombo_SelectedIndexChanged(sender As Object, e As EventArgs)
        ' 列が変更されたときに値をクリア
        valueTextBox.Clear()
    End Sub

    ''' <summary>
    ''' 検索条件が有効かどうかを判定する
    ''' 
    ''' 以下の条件をチェック：
    ''' 1. 列が選択されている（SelectedIndex >= 0）
    ''' 2. 演算子が選択されている（SelectedIndex >= 0）
    ''' 3. 検索値が入力されている（Null/Not Null 以外）
    ''' 
    ''' Null/Not Null 演算子の場合は検索値が不要なため別処理
    ''' </summary>
    ''' <returns>すべての必須項目が入力されている場合 True、そうでない場合 False</returns>
    Public Function IsValid() As Boolean
        ' 列と演算子が両方選択されているかチェック
        Dim columnSelected = columnCombo.SelectedIndex >= 0
        Dim operatorSelected = operatorCombo.SelectedIndex >= 0

        ' 値の入力チェック
        ' Null/Not Null 演算子の場合は値が不要なため、それ以外は値が必須
        Dim valueValid = IsNullOperator() OrElse Not String.IsNullOrEmpty(valueTextBox.Text.Trim())

        Return columnSelected AndAlso operatorSelected AndAlso valueValid
    End Function

    ''' <summary>
    ''' Null または Not Null 演算子が選択されているかを判定する
    ''' 
    ''' これらの演算子では検索値が不要なため、
    ''' IsValid() メソッドで値のチェックをスキップするために使用
    ''' </summary>
    ''' <returns>Null（インデックス10）または Not Null（インデックス11）の場合 True</returns>
    Private Function IsNullOperator() As Boolean
        ' インデックス 10 = "Null", 11 = "Not Null"
        Return operatorCombo.SelectedIndex = 10 OrElse operatorCombo.SelectedIndex = 11
    End Function

    ''' <summary>
    ''' UI から検索条件オブジェクトを生成する
    ''' 
    ''' コンボボックスのインデックスを SearchCondition.OperatorType にマッピングし、
    ''' SearchConditionItem オブジェクトを生成する。
    ''' 
    ''' マッピング:
    ''' 0 = 含む (Contains)
    ''' 1 = 含まない (NotContains)
    ''' 2 = 等しい (Equals)
    ''' 3 = 等しくない (NotEquals)
    ''' 4 = で始まる (StartsWith)
    ''' 5 = で終わる (EndsWith)
    ''' 6 = > (GreaterThan)
    ''' 7 = < (LessThan)
    ''' 8 = >= (GreaterThanOrEqual)
    ''' 9 = <= (LessThanOrEqual)
    ''' 10 = Null (IsNull)
    ''' 11 = Not Null (IsNotNull)
    ''' </summary>
    ''' <returns>UI の値から生成された SearchConditionItem オブジェクト</returns>
    Public Function GetCondition() As SearchCondition.SearchConditionItem
        ' インデックスから演算子タイプにマッピング
        Dim opType As SearchCondition.OperatorType
        Select Case operatorCombo.SelectedIndex
            Case 0
                opType = SearchCondition.OperatorType.Contains
            Case 1
                opType = SearchCondition.OperatorType.NotContains
            Case 2
                opType = SearchCondition.OperatorType.Equals
            Case 3
                opType = SearchCondition.OperatorType.NotEquals
            Case 4
                opType = SearchCondition.OperatorType.StartsWith
            Case 5
                opType = SearchCondition.OperatorType.EndsWith
            Case 6
                opType = SearchCondition.OperatorType.GreaterThan
            Case 7
                opType = SearchCondition.OperatorType.LessThan
            Case 8
                opType = SearchCondition.OperatorType.GreaterThanOrEqual
            Case 9
                opType = SearchCondition.OperatorType.LessThanOrEqual
            Case 10
                opType = SearchCondition.OperatorType.IsNull
            Case 11
                opType = SearchCondition.OperatorType.IsNotNull
            Case Else
                ' デフォルトは「含む」
                opType = SearchCondition.OperatorType.Contains
        End Select

        ' SearchConditionItem を生成して返す
        Return New SearchCondition.SearchConditionItem(
            columnCombo.SelectedItem.ToString(),
            opType,
            valueTextBox.Text.Trim(),
            caseSensitiveCheckBox.Checked
        )
    End Function

    ''' <summary>
    ''' AND/OR 論理演算子を取得する
    ''' 
    ''' 2行目以降の条件行では LogicalComboBox が設定される。
    ''' SelectedIndex が 0 なら AND、1 なら OR。
    ''' LogicalComboBox が未設定の場合はデフォルト AND を返す。
    ''' </summary>
    ''' <returns>AND または OR の論理演算子タイプ</returns>
    Public Function GetLogicalOperator() As SearchCondition.LogicalOperatorType
        If LogicalComboBox IsNot Nothing Then
            If LogicalComboBox.SelectedIndex = 0 Then
                Return SearchCondition.LogicalOperatorType.And
            Else
                Return SearchCondition.LogicalOperatorType.Or
            End If
        End If
        ' デフォルトは AND を返す
        Return SearchCondition.LogicalOperatorType.And
    End Function

#Region "検索条件復元用メソッド"

    ''' <summary>
    ''' 列のインデックスを取得する
    ''' 
    ''' 列名から対応するコンボボックスのインデックスを検索する。
    ''' 検索条件を復元する際に使用。
    ''' </summary>
    ''' <param name="columnName">検索する列名</param>
    ''' <returns>見つかったインデックス、見つからない場合 -1</returns>
    Public Function GetColumnIndex(columnName As String) As Integer
        ' コンボボックスのすべてのアイテムを走査
        For i = 0 To columnCombo.Items.Count - 1
            If columnCombo.Items(i).ToString() = columnName Then
                Return i
            End If
        Next
        ' 見つからない場合
        Return -1
    End Function

    ''' <summary>
    ''' 列のインデックスを設定する
    ''' 
    ''' 前回の検索条件から列を復元するために使用。
    ''' インデックスが有効な範囲内であることをチェック。
    ''' </summary>
    ''' <param name="index">設定するインデックス</param>
    Public Sub SetColumnIndex(index As Integer)
        ' インデックスが有効な範囲内かチェック
        If index >= 0 AndAlso index < columnCombo.Items.Count Then
            columnCombo.SelectedIndex = index
        End If
    End Sub

    ''' <summary>
    ''' 演算子のインデックスを設定する
    ''' 
    ''' 前回の検索条件から演算子を復元するために使用。
    ''' インデックスが有効な範囲内であることをチェック。
    ''' </summary>
    ''' <param name="index">設定するインデックス（0～11）</param>
    Public Sub SetOperatorIndex(index As Integer)
        ' インデックスが有効な範囲内かチェック
        If index >= 0 AndAlso index < operatorCombo.Items.Count Then
            operatorCombo.SelectedIndex = index
        End If
    End Sub

    ''' <summary>
    ''' 検索値を設定する
    ''' 
    ''' 前回の検索条件から値を復元するために使用。
    ''' </summary>
    ''' <param name="value">設定する検索値</param>
    Public Sub SetValue(value As String)
        valueTextBox.Text = value
    End Sub

    ''' <summary>
    ''' 大文字小文字区別のチェック状態を設定する
    ''' 
    ''' 前回の検索条件から大文字小文字区別の設定を復元するために使用。
    ''' </summary>
    ''' <param name="caseSensitive">大文字小文字を区別する場合 True</param>
    Public Sub SetCaseSensitive(caseSensitive As Boolean)
        caseSensitiveCheckBox.Checked = caseSensitive
    End Sub

#End Region

End Class
