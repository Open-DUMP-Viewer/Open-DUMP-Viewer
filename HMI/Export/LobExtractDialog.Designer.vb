<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class LobExtractDialog
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
        lblLobColumn = New Label()
        cboLobColumn = New ComboBox()
        lblOutputDir = New Label()
        txtOutputDir = New TextBox()
        btnBrowse = New Button()
        lblFilename = New Label()
        cboFilenameMethod = New ComboBox()
        cboFilenameColumn = New ComboBox()
        lblExtension = New Label()
        txtExtension = New TextBox()
        btnExtract = New Button()
        btnClose = New Button()
        grpSettings = New GroupBox()
        grpSettings.SuspendLayout()
        SuspendLayout()
        '
        ' lblLobColumn
        '
        lblLobColumn.AutoSize = True
        lblLobColumn.Location = New Point(15, 28)
        lblLobColumn.Name = "lblLobColumn"
        lblLobColumn.Size = New Size(75, 15)
        lblLobColumn.TabIndex = 0
        lblLobColumn.Text = "LOBカラム:"
        '
        ' cboLobColumn
        '
        cboLobColumn.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        cboLobColumn.DropDownStyle = ComboBoxStyle.DropDownList
        cboLobColumn.Location = New Point(120, 25)
        cboLobColumn.Name = "cboLobColumn"
        cboLobColumn.Size = New Size(280, 23)
        cboLobColumn.TabIndex = 1
        '
        ' lblOutputDir
        '
        lblOutputDir.AutoSize = True
        lblOutputDir.Location = New Point(15, 63)
        lblOutputDir.Name = "lblOutputDir"
        lblOutputDir.Size = New Size(81, 15)
        lblOutputDir.TabIndex = 2
        lblOutputDir.Text = "出力フォルダ:"
        '
        ' txtOutputDir
        '
        txtOutputDir.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        txtOutputDir.Location = New Point(120, 60)
        txtOutputDir.Name = "txtOutputDir"
        txtOutputDir.Size = New Size(240, 23)
        txtOutputDir.TabIndex = 3
        '
        ' btnBrowse
        '
        btnBrowse.Anchor = AnchorStyles.Top Or AnchorStyles.Right
        btnBrowse.Location = New Point(366, 59)
        btnBrowse.Name = "btnBrowse"
        btnBrowse.Size = New Size(34, 25)
        btnBrowse.TabIndex = 4
        btnBrowse.Text = "..."
        '
        ' lblFilename
        '
        lblFilename.AutoSize = True
        lblFilename.Location = New Point(15, 98)
        lblFilename.Name = "lblFilename"
        lblFilename.Size = New Size(81, 15)
        lblFilename.TabIndex = 5
        lblFilename.Text = "ファイル名:"
        '
        ' cboFilenameMethod
        '
        cboFilenameMethod.DropDownStyle = ComboBoxStyle.DropDownList
        cboFilenameMethod.Location = New Point(120, 95)
        cboFilenameMethod.Name = "cboFilenameMethod"
        cboFilenameMethod.Size = New Size(120, 23)
        cboFilenameMethod.TabIndex = 6
        '
        ' cboFilenameColumn
        '
        cboFilenameColumn.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        cboFilenameColumn.DropDownStyle = ComboBoxStyle.DropDownList
        cboFilenameColumn.Enabled = False
        cboFilenameColumn.Location = New Point(250, 95)
        cboFilenameColumn.Name = "cboFilenameColumn"
        cboFilenameColumn.Size = New Size(150, 23)
        cboFilenameColumn.TabIndex = 7
        '
        ' lblExtension
        '
        lblExtension.AutoSize = True
        lblExtension.Location = New Point(15, 133)
        lblExtension.Name = "lblExtension"
        lblExtension.Size = New Size(45, 15)
        lblExtension.TabIndex = 8
        lblExtension.Text = "拡張子:"
        '
        ' txtExtension
        '
        txtExtension.Location = New Point(120, 130)
        txtExtension.Name = "txtExtension"
        txtExtension.Size = New Size(100, 23)
        txtExtension.TabIndex = 9
        txtExtension.Text = "lob"
        '
        ' grpSettings
        '
        grpSettings.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        grpSettings.Controls.Add(lblLobColumn)
        grpSettings.Controls.Add(cboLobColumn)
        grpSettings.Controls.Add(lblOutputDir)
        grpSettings.Controls.Add(txtOutputDir)
        grpSettings.Controls.Add(btnBrowse)
        grpSettings.Controls.Add(lblFilename)
        grpSettings.Controls.Add(cboFilenameMethod)
        grpSettings.Controls.Add(cboFilenameColumn)
        grpSettings.Controls.Add(lblExtension)
        grpSettings.Controls.Add(txtExtension)
        grpSettings.Location = New Point(12, 12)
        grpSettings.Name = "grpSettings"
        grpSettings.Size = New Size(416, 170)
        grpSettings.TabIndex = 0
        grpSettings.TabStop = False
        grpSettings.Text = "抽出設定"
        '
        ' btnExtract
        '
        btnExtract.Anchor = AnchorStyles.Bottom Or AnchorStyles.Right
        btnExtract.Location = New Point(222, 195)
        btnExtract.Name = "btnExtract"
        btnExtract.Size = New Size(100, 35)
        btnExtract.TabIndex = 1
        btnExtract.Text = "抽出"
        '
        ' btnClose
        '
        btnClose.Anchor = AnchorStyles.Bottom Or AnchorStyles.Right
        btnClose.DialogResult = DialogResult.Cancel
        btnClose.Location = New Point(328, 195)
        btnClose.Name = "btnClose"
        btnClose.Size = New Size(100, 35)
        btnClose.TabIndex = 2
        btnClose.Text = "閉じる"
        '
        ' LobExtractDialog
        '
        AcceptButton = btnExtract
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        CancelButton = btnClose
        ClientSize = New Size(440, 242)
        Controls.Add(grpSettings)
        Controls.Add(btnExtract)
        Controls.Add(btnClose)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "LobExtractDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "LOBファイル抽出"
        grpSettings.ResumeLayout(False)
        grpSettings.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents lblLobColumn As Label
    Friend WithEvents cboLobColumn As ComboBox
    Friend WithEvents lblOutputDir As Label
    Friend WithEvents txtOutputDir As TextBox
    Friend WithEvents btnBrowse As Button
    Friend WithEvents lblFilename As Label
    Friend WithEvents cboFilenameMethod As ComboBox
    Friend WithEvents cboFilenameColumn As ComboBox
    Friend WithEvents lblExtension As Label
    Friend WithEvents txtExtension As TextBox
    Friend WithEvents grpSettings As GroupBox
    Friend WithEvents btnExtract As Button
    Friend WithEvents btnClose As Button

End Class
