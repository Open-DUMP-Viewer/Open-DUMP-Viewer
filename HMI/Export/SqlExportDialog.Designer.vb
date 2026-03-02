<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class SqlExportDialog
    Inherits System.Windows.Forms.Form

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

    Private components As System.ComponentModel.IContainer

    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        lblDbms = New Label()
        cboDbms = New ComboBox()
        btnOK = New Button()
        btnCancel = New Button()
        SuspendLayout()
        '
        ' lblDbms
        '
        lblDbms.AutoSize = True
        lblDbms.Location = New Point(20, 25)
        lblDbms.Name = "lblDbms"
        lblDbms.Size = New Size(120, 15)
        lblDbms.TabIndex = 0
        lblDbms.Text = "出力先 DB:"
        '
        ' cboDbms
        '
        cboDbms.DropDownStyle = ComboBoxStyle.DropDownList
        cboDbms.FormattingEnabled = True
        cboDbms.Location = New Point(150, 22)
        cboDbms.Name = "cboDbms"
        cboDbms.Size = New Size(200, 23)
        cboDbms.TabIndex = 1
        '
        ' btnOK
        '
        btnOK.Location = New Point(150, 65)
        btnOK.Name = "btnOK"
        btnOK.Size = New Size(90, 30)
        btnOK.TabIndex = 2
        btnOK.Text = "OK"
        btnOK.DialogResult = DialogResult.OK
        '
        ' btnCancel
        '
        btnCancel.Location = New Point(260, 65)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(90, 30)
        btnCancel.TabIndex = 3
        btnCancel.Text = "キャンセル"
        btnCancel.DialogResult = DialogResult.Cancel
        '
        ' SqlExportDialog
        '
        AcceptButton = btnOK
        CancelButton = btnCancel
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(380, 110)
        Controls.Add(lblDbms)
        Controls.Add(cboDbms)
        Controls.Add(btnOK)
        Controls.Add(btnCancel)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "SqlExportDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "SQL スクリプト出力"
        ResumeLayout(False)
        PerformLayout()
    End Sub

    Friend WithEvents lblDbms As Label
    Friend WithEvents cboDbms As ComboBox
    Friend WithEvents btnOK As Button
    Friend WithEvents btnCancel As Button

End Class
