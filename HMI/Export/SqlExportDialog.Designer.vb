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
        chkCreateTable = New CheckBox()
        chkInferInteger = New CheckBox()
        lblDatabaseName = New Label()
        txtDatabaseName = New TextBox()
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
        ' chkCreateTable
        '
        chkCreateTable.AutoSize = True
        chkCreateTable.Location = New Point(20, 60)
        chkCreateTable.Name = "chkCreateTable"
        chkCreateTable.Size = New Size(330, 19)
        chkCreateTable.TabIndex = 2
        chkCreateTable.Text = "DROP TABLE + CREATE TABLE を出力"
        '
        ' chkInferInteger
        '
        chkInferInteger.AutoSize = True
        chkInferInteger.Location = New Point(20, 85)
        chkInferInteger.Name = "chkInferInteger"
        chkInferInteger.Size = New Size(330, 19)
        chkInferInteger.TabIndex = 3
        chkInferInteger.Text = "NUMBER を実データから整数型に推定"
        '
        ' lblDatabaseName
        '
        lblDatabaseName.AutoSize = True
        lblDatabaseName.Location = New Point(20, 118)
        lblDatabaseName.Name = "lblDatabaseName"
        lblDatabaseName.Size = New Size(120, 15)
        lblDatabaseName.TabIndex = 4
        lblDatabaseName.Text = "USE [DB名]:"
        '
        ' txtDatabaseName
        '
        txtDatabaseName.Location = New Point(150, 115)
        txtDatabaseName.Name = "txtDatabaseName"
        txtDatabaseName.Size = New Size(200, 23)
        txtDatabaseName.TabIndex = 5
        txtDatabaseName.PlaceholderText = "(省略可)"
        '
        ' btnOK
        '
        btnOK.Location = New Point(150, 155)
        btnOK.Name = "btnOK"
        btnOK.Size = New Size(90, 30)
        btnOK.TabIndex = 6
        btnOK.Text = "OK"
        btnOK.DialogResult = DialogResult.OK
        '
        ' btnCancel
        '
        btnCancel.Location = New Point(260, 155)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(90, 30)
        btnCancel.TabIndex = 7
        btnCancel.Text = "キャンセル"
        btnCancel.DialogResult = DialogResult.Cancel
        '
        ' SqlExportDialog
        '
        AcceptButton = btnOK
        CancelButton = btnCancel
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(380, 200)
        Controls.Add(lblDbms)
        Controls.Add(cboDbms)
        Controls.Add(chkCreateTable)
        Controls.Add(chkInferInteger)
        Controls.Add(lblDatabaseName)
        Controls.Add(txtDatabaseName)
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
    Friend WithEvents chkCreateTable As CheckBox
    Friend WithEvents chkInferInteger As CheckBox
    Friend WithEvents lblDatabaseName As Label
    Friend WithEvents txtDatabaseName As TextBox
    Friend WithEvents btnOK As Button
    Friend WithEvents btnCancel As Button

End Class
