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
        chkCreateIndex = New CheckBox()
        chkWriteComments = New CheckBox()
        sepLine = New Label()
        chkWriteInserts = New CheckBox()
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
        ' chkCreateIndex
        '
        chkCreateIndex.AutoSize = True
        chkCreateIndex.Location = New Point(20, 85)
        chkCreateIndex.Name = "chkCreateIndex"
        chkCreateIndex.Size = New Size(330, 19)
        chkCreateIndex.TabIndex = 3
        chkCreateIndex.Text = "CREATE INDEX を出力"
        '
        ' chkWriteComments
        '
        chkWriteComments.AutoSize = True
        chkWriteComments.Location = New Point(20, 110)
        chkWriteComments.Name = "chkWriteComments"
        chkWriteComments.Size = New Size(330, 19)
        chkWriteComments.TabIndex = 4
        chkWriteComments.Text = "COMMENT ON を出力"
        '
        ' sepLine
        '
        sepLine.BorderStyle = BorderStyle.Fixed3D
        sepLine.Location = New Point(20, 138)
        sepLine.Name = "sepLine"
        sepLine.Size = New Size(330, 2)
        '
        ' chkWriteInserts
        '
        chkWriteInserts.AutoSize = True
        chkWriteInserts.Location = New Point(20, 148)
        chkWriteInserts.Name = "chkWriteInserts"
        chkWriteInserts.Size = New Size(330, 19)
        chkWriteInserts.TabIndex = 5
        chkWriteInserts.Text = "INSERT INTO データを出力"
        '
        ' chkInferInteger
        '
        chkInferInteger.AutoSize = True
        chkInferInteger.Location = New Point(20, 173)
        chkInferInteger.Name = "chkInferInteger"
        chkInferInteger.Size = New Size(330, 19)
        chkInferInteger.TabIndex = 6
        chkInferInteger.Text = "NUMBER を実データから整数型に推定"
        '
        ' lblDatabaseName
        '
        lblDatabaseName.AutoSize = True
        lblDatabaseName.Location = New Point(20, 206)
        lblDatabaseName.Name = "lblDatabaseName"
        lblDatabaseName.Size = New Size(120, 15)
        lblDatabaseName.TabIndex = 6
        lblDatabaseName.Text = "USE [DB名]:"
        '
        ' txtDatabaseName
        '
        txtDatabaseName.Location = New Point(150, 203)
        txtDatabaseName.Name = "txtDatabaseName"
        txtDatabaseName.Size = New Size(200, 23)
        txtDatabaseName.TabIndex = 8
        txtDatabaseName.PlaceholderText = "(省略可)"
        '
        ' btnOK
        '
        btnOK.Location = New Point(150, 243)
        btnOK.Name = "btnOK"
        btnOK.Size = New Size(90, 30)
        btnOK.TabIndex = 9
        btnOK.Text = "OK"
        btnOK.DialogResult = DialogResult.OK
        '
        ' btnCancel
        '
        btnCancel.Location = New Point(260, 243)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(90, 30)
        btnCancel.TabIndex = 10
        btnCancel.Text = "キャンセル"
        btnCancel.DialogResult = DialogResult.Cancel
        '
        ' SqlExportDialog
        '
        AcceptButton = btnOK
        CancelButton = btnCancel
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(380, 288)
        Controls.Add(lblDbms)
        Controls.Add(cboDbms)
        Controls.Add(chkCreateTable)
        Controls.Add(chkCreateIndex)
        Controls.Add(chkWriteComments)
        Controls.Add(sepLine)
        Controls.Add(chkWriteInserts)
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
    Friend WithEvents chkCreateIndex As CheckBox
    Friend WithEvents chkWriteComments As CheckBox
    Friend WithEvents sepLine As Label
    Friend WithEvents chkWriteInserts As CheckBox
    Friend WithEvents chkInferInteger As CheckBox
    Friend WithEvents lblDatabaseName As Label
    Friend WithEvents txtDatabaseName As TextBox
    Friend WithEvents btnOK As Button
    Friend WithEvents btnCancel As Button

End Class
