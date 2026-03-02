<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class ExportProgressDialog
    Inherits System.Windows.Forms.Form

    'フォームがコンポーネントの一覧をクリアするために dispose をオーバーライドします。
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

    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        prgExport = New ProgressBar()
        lblTable = New Label()
        lblRows = New Label()
        lblElapsed = New Label()
        btnCancel = New Button()
        SuspendLayout()
        '
        ' prgExport
        '
        prgExport.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        prgExport.Location = New Point(20, 20)
        prgExport.Name = "prgExport"
        prgExport.Size = New Size(440, 28)
        prgExport.TabIndex = 0
        '
        ' lblTable
        '
        lblTable.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        lblTable.Location = New Point(20, 58)
        lblTable.Name = "lblTable"
        lblTable.Size = New Size(440, 25)
        lblTable.TabIndex = 1
        lblTable.Text = ""
        '
        ' lblRows
        '
        lblRows.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        lblRows.Location = New Point(20, 83)
        lblRows.Name = "lblRows"
        lblRows.Size = New Size(440, 25)
        lblRows.TabIndex = 2
        lblRows.Text = ""
        '
        ' lblElapsed
        '
        lblElapsed.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        lblElapsed.Location = New Point(20, 108)
        lblElapsed.Name = "lblElapsed"
        lblElapsed.Size = New Size(440, 25)
        lblElapsed.TabIndex = 3
        lblElapsed.Text = ""
        '
        ' btnCancel
        '
        btnCancel.Anchor = AnchorStyles.Bottom Or AnchorStyles.Right
        btnCancel.Location = New Point(340, 145)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(120, 35)
        btnCancel.TabIndex = 4
        btnCancel.Text = "キャンセル"
        '
        ' ExportProgressDialog
        '
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(480, 195)
        Controls.Add(prgExport)
        Controls.Add(lblTable)
        Controls.Add(lblRows)
        Controls.Add(lblElapsed)
        Controls.Add(btnCancel)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "ExportProgressDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "エクスポート中..."
        ResumeLayout(False)
    End Sub

    Friend WithEvents prgExport As ProgressBar
    Friend WithEvents lblTable As Label
    Friend WithEvents lblRows As Label
    Friend WithEvents lblElapsed As Label
    Friend WithEvents btnCancel As Button

End Class
