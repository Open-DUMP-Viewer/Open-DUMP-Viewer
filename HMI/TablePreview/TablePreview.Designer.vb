<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class TablePreview
    Inherits System.Windows.Forms.Form

    'フォームがコンポーネントの一覧をクリーンアップするために dispose をオーバーライドします。
    <System.Diagnostics.DebuggerNonUserCode()> _
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    'Windows フォーム デザイナーで必要です。
    Private components As System.ComponentModel.IContainer

    'メモ: 以下のプロシージャは Windows フォーム デザイナーで必要です。
    'Windows フォーム デザイナーを使用して変更できます。  
    'コード エディターを使って変更しないでください。
    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        dataGridViewData = New DataGridView()
        panelSearch = New Panel()
        labelSearch = New Label()
        textBoxSearchValue = New TextBox()
        labelColumns = New Label()
        comboBoxColumns = New ComboBox()
        buttonSearch = New Button()
        buttonReset = New Button()
        buttonAdvancedSearch = New Button()
        buttonExportCsv = New Button()
        labelRowCount = New Label()
        panelPaging = New Panel()
        labelPageSize = New Label()
        numericUpDownPageCount = New NumericUpDown()
        buttonPrev = New Button()
        buttonNext = New Button()
        labelPageInfo = New Label()
        CType(dataGridViewData, ComponentModel.ISupportInitialize).BeginInit()
        panelSearch.SuspendLayout()
        panelPaging.SuspendLayout()
        CType(numericUpDownPageCount, ComponentModel.ISupportInitialize).BeginInit()
        SuspendLayout()
        ' 
        ' dataGridViewData
        ' 
        dataGridViewData.AllowUserToAddRows = False
        dataGridViewData.AllowUserToDeleteRows = False
        dataGridViewData.Anchor = AnchorStyles.Top Or AnchorStyles.Bottom Or AnchorStyles.Left Or AnchorStyles.Right
        dataGridViewData.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize
        dataGridViewData.Location = New Point(20, 208)
        dataGridViewData.Margin = New Padding(5, 6, 5, 6)
        dataGridViewData.Name = "dataGridViewData"
        dataGridViewData.ReadOnly = True
        dataGridViewData.RowHeadersWidth = 62
        dataGridViewData.Size = New Size(1293, 583)
        dataGridViewData.TabIndex = 0
        ' 
        ' panelSearch
        ' 
        panelSearch.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        panelSearch.Controls.Add(labelSearch)
        panelSearch.Controls.Add(textBoxSearchValue)
        panelSearch.Controls.Add(labelColumns)
        panelSearch.Controls.Add(comboBoxColumns)
        panelSearch.Controls.Add(buttonSearch)
        panelSearch.Controls.Add(buttonReset)
        panelSearch.Controls.Add(buttonAdvancedSearch)
        panelSearch.Controls.Add(buttonExportCsv)
        panelSearch.Controls.Add(labelRowCount)
        panelSearch.Location = New Point(20, 25)
        panelSearch.Margin = New Padding(5, 6, 5, 6)
        panelSearch.Name = "panelSearch"
        panelSearch.Size = New Size(1293, 171)
        panelSearch.TabIndex = 1
        ' 
        ' labelSearch
        ' 
        labelSearch.AutoSize = True
        labelSearch.Location = New Point(5, 98)
        labelSearch.Margin = New Padding(5, 0, 5, 0)
        labelSearch.Name = "labelSearch"
        labelSearch.Size = New Size(70, 25)
        labelSearch.TabIndex = 0
        labelSearch.Text = "検索値:"
        ' 
        ' textBoxSearchValue
        ' 
        textBoxSearchValue.Location = New Point(133, 92)
        textBoxSearchValue.Margin = New Padding(5, 6, 5, 6)
        textBoxSearchValue.Name = "textBoxSearchValue"
        textBoxSearchValue.Size = New Size(247, 31)
        textBoxSearchValue.TabIndex = 1
        ' 
        ' labelColumns
        ' 
        labelColumns.AutoSize = True
        labelColumns.Location = New Point(5, 31)
        labelColumns.Margin = New Padding(5, 0, 5, 0)
        labelColumns.Name = "labelColumns"
        labelColumns.Size = New Size(52, 25)
        labelColumns.TabIndex = 2
        labelColumns.Text = "列名:"
        ' 
        ' comboBoxColumns
        ' 
        comboBoxColumns.DropDownStyle = ComboBoxStyle.DropDownList
        comboBoxColumns.FormattingEnabled = True
        comboBoxColumns.Location = New Point(133, 25)
        comboBoxColumns.Margin = New Padding(5, 6, 5, 6)
        comboBoxColumns.Name = "comboBoxColumns"
        comboBoxColumns.Size = New Size(247, 33)
        comboBoxColumns.TabIndex = 3
        ' 
        ' buttonSearch
        ' 
        buttonSearch.Location = New Point(393, 92)
        buttonSearch.Margin = New Padding(5, 6, 5, 6)
        buttonSearch.Name = "buttonSearch"
        buttonSearch.Size = New Size(125, 48)
        buttonSearch.TabIndex = 4
        buttonSearch.Text = "検索"
        buttonSearch.UseVisualStyleBackColor = True
        ' 
        ' buttonReset
        ' 
        buttonReset.Location = New Point(393, 25)
        buttonReset.Margin = New Padding(5, 6, 5, 6)
        buttonReset.Name = "buttonReset"
        buttonReset.Size = New Size(125, 48)
        buttonReset.TabIndex = 5
        buttonReset.Text = "リセット"
        buttonReset.UseVisualStyleBackColor = True
        ' 
        ' buttonAdvancedSearch
        ' 
        buttonAdvancedSearch.Location = New Point(528, 25)
        buttonAdvancedSearch.Margin = New Padding(5, 6, 5, 6)
        buttonAdvancedSearch.Name = "buttonAdvancedSearch"
        buttonAdvancedSearch.Size = New Size(167, 48)
        buttonAdvancedSearch.TabIndex = 6
        buttonAdvancedSearch.Text = "高度な検索"
        buttonAdvancedSearch.UseVisualStyleBackColor = True
        '
        ' buttonExportCsv
        '
        buttonExportCsv.Location = New Point(528, 92)
        buttonExportCsv.Margin = New Padding(5, 6, 5, 6)
        buttonExportCsv.Name = "buttonExportCsv"
        buttonExportCsv.Size = New Size(167, 48)
        buttonExportCsv.TabIndex = 8
        buttonExportCsv.Text = "CSV 出力"
        buttonExportCsv.UseVisualStyleBackColor = True
        '
        ' labelRowCount
        '
        labelRowCount.AutoSize = True
        labelRowCount.Location = New Point(708, 31)
        labelRowCount.Margin = New Padding(5, 0, 5, 0)
        labelRowCount.Name = "labelRowCount"
        labelRowCount.Size = New Size(209, 25)
        labelRowCount.TabIndex = 7
        labelRowCount.Text = "合計行数: 0 (表示行数: 0)"
        ' 
        ' panelPaging
        ' 
        panelPaging.Anchor = AnchorStyles.Bottom Or AnchorStyles.Left Or AnchorStyles.Right
        panelPaging.Controls.Add(labelPageSize)
        panelPaging.Controls.Add(numericUpDownPageCount)
        panelPaging.Controls.Add(buttonPrev)
        panelPaging.Controls.Add(buttonNext)
        panelPaging.Controls.Add(labelPageInfo)
        panelPaging.Location = New Point(20, 804)
        panelPaging.Margin = New Padding(5, 6, 5, 6)
        panelPaging.Name = "panelPaging"
        panelPaging.Size = New Size(1293, 88)
        panelPaging.TabIndex = 2
        ' 
        ' labelPageSize
        ' 
        labelPageSize.AutoSize = True
        labelPageSize.Location = New Point(530, 27)
        labelPageSize.Margin = New Padding(5, 0, 5, 0)
        labelPageSize.Name = "labelPageSize"
        labelPageSize.Size = New Size(68, 25)
        labelPageSize.TabIndex = 4
        labelPageSize.Text = "1ページ:"
        ' 
        ' numericUpDownPageCount
        ' 
        numericUpDownPageCount.Location = New Point(635, 23)
        numericUpDownPageCount.Margin = New Padding(5, 6, 5, 6)
        numericUpDownPageCount.Maximum = Decimal.MaxValue
        numericUpDownPageCount.Minimum = New Decimal(New Integer() {1, 0, 0, 0})
        numericUpDownPageCount.Name = "numericUpDownPageCount"
        numericUpDownPageCount.Size = New Size(100, 31)
        numericUpDownPageCount.TabIndex = 3
        numericUpDownPageCount.Value = New Decimal(New Integer() {100, 0, 0, 0})
        ' 
        ' buttonPrev
        ' 
        buttonPrev.Location = New Point(250, 19)
        buttonPrev.Margin = New Padding(5, 6, 5, 6)
        buttonPrev.Name = "buttonPrev"
        buttonPrev.Size = New Size(125, 48)
        buttonPrev.TabIndex = 1
        buttonPrev.Text = "< 前へ"
        buttonPrev.UseVisualStyleBackColor = True
        ' 
        ' buttonNext
        ' 
        buttonNext.Location = New Point(385, 19)
        buttonNext.Margin = New Padding(5, 6, 5, 6)
        buttonNext.Name = "buttonNext"
        buttonNext.Size = New Size(125, 48)
        buttonNext.TabIndex = 2
        buttonNext.Text = "次へ >"
        buttonNext.UseVisualStyleBackColor = True
        ' 
        ' labelPageInfo
        ' 
        labelPageInfo.AutoSize = True
        labelPageInfo.Location = New Point(5, 25)
        labelPageInfo.Margin = New Padding(5, 0, 5, 0)
        labelPageInfo.Name = "labelPageInfo"
        labelPageInfo.Size = New Size(90, 25)
        labelPageInfo.TabIndex = 0
        labelPageInfo.Text = "ページ: 1/1"
        ' 
        ' TablePreview
        ' 
        AutoScaleDimensions = New SizeF(10F, 25F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(1333, 917)
        Controls.Add(dataGridViewData)
        Controls.Add(panelSearch)
        Controls.Add(panelPaging)
        Margin = New Padding(5, 6, 5, 6)
        Name = "TablePreview"
        Text = "テーブルデータプレビュー"
        WindowState = FormWindowState.Maximized
        CType(dataGridViewData, ComponentModel.ISupportInitialize).EndInit()
        panelSearch.ResumeLayout(False)
        panelSearch.PerformLayout()
        panelPaging.ResumeLayout(False)
        panelPaging.PerformLayout()
        CType(numericUpDownPageCount, ComponentModel.ISupportInitialize).EndInit()
        ResumeLayout(False)

    End Sub

    Friend WithEvents dataGridViewData As System.Windows.Forms.DataGridView
    Friend WithEvents panelSearch As System.Windows.Forms.Panel
    Friend WithEvents buttonSearch As System.Windows.Forms.Button
    Friend WithEvents comboBoxColumns As System.Windows.Forms.ComboBox
    Friend WithEvents textBoxSearchValue As System.Windows.Forms.TextBox
    Friend WithEvents labelSearch As System.Windows.Forms.Label
    Friend WithEvents labelColumns As System.Windows.Forms.Label
    Friend WithEvents labelRowCount As System.Windows.Forms.Label
    Friend WithEvents buttonReset As System.Windows.Forms.Button
    Friend WithEvents buttonAdvancedSearch As System.Windows.Forms.Button
    Friend WithEvents buttonExportCsv As System.Windows.Forms.Button
    Friend WithEvents panelPaging As System.Windows.Forms.Panel
    Friend WithEvents labelPageInfo As System.Windows.Forms.Label
    Friend WithEvents buttonNext As System.Windows.Forms.Button
    Friend WithEvents buttonPrev As System.Windows.Forms.Button
    Friend WithEvents numericUpDownPageCount As System.Windows.Forms.NumericUpDown
    Friend WithEvents labelPageSize As System.Windows.Forms.Label
End Class
