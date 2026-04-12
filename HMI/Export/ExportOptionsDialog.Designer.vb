<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class ExportOptionsDialog
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
        grpDateFormat = New GroupBox()
        rdoSlash = New RadioButton()
        rdoCompact = New RadioButton()
        rdoFull = New RadioButton()
        rdoCustom = New RadioButton()
        txtCustomFormat = New TextBox()
        grpCsvOptions = New GroupBox()
        chkCsvHeader = New CheckBox()
        chkCsvTypes = New CheckBox()
        lblDelimiter = New Label()
        cboDelimiter = New ComboBox()
        grpSqlOptions = New GroupBox()
        chkCreateTable = New CheckBox()
        chkCreateIndex = New CheckBox()
        chkWriteComments = New CheckBox()
        chkWriteInserts = New CheckBox()
        chkInferInteger = New CheckBox()
        btnOK = New Button()
        btnCancel = New Button()
        grpOracleClient = New GroupBox()
        txtImpdpPath = New TextBox()
        btnBrowseImpdp = New Button()
        btnAutoSetup = New Button()
        lblImpdpInfo = New Label()
        grpDateFormat.SuspendLayout()
        grpCsvOptions.SuspendLayout()
        grpSqlOptions.SuspendLayout()
        grpOracleClient.SuspendLayout()
        SuspendLayout()
        '
        ' grpDateFormat
        '
        grpDateFormat.Controls.Add(rdoSlash)
        grpDateFormat.Controls.Add(rdoCompact)
        grpDateFormat.Controls.Add(rdoFull)
        grpDateFormat.Controls.Add(rdoCustom)
        grpDateFormat.Controls.Add(txtCustomFormat)
        grpDateFormat.Location = New Point(15, 15)
        grpDateFormat.Name = "grpDateFormat"
        grpDateFormat.Size = New Size(380, 145)
        grpDateFormat.TabIndex = 0
        grpDateFormat.TabStop = False
        grpDateFormat.Text = "日付フォーマット"
        '
        ' rdoSlash
        '
        rdoSlash.AutoSize = True
        rdoSlash.Checked = True
        rdoSlash.Location = New Point(15, 25)
        rdoSlash.Name = "rdoSlash"
        rdoSlash.Size = New Size(250, 19)
        rdoSlash.TabIndex = 0
        rdoSlash.TabStop = True
        rdoSlash.Text = "YYYY/MM/DD HH:MI:SS (デフォルト)"
        '
        ' rdoCompact
        '
        rdoCompact.AutoSize = True
        rdoCompact.Location = New Point(15, 50)
        rdoCompact.Name = "rdoCompact"
        rdoCompact.Size = New Size(120, 19)
        rdoCompact.TabIndex = 1
        rdoCompact.Text = "YYYYMMDD"
        '
        ' rdoFull
        '
        rdoFull.AutoSize = True
        rdoFull.Location = New Point(15, 75)
        rdoFull.Name = "rdoFull"
        rdoFull.Size = New Size(160, 19)
        rdoFull.TabIndex = 2
        rdoFull.Text = "YYYYMMDDHHMMSS"
        '
        ' rdoCustom
        '
        rdoCustom.AutoSize = True
        rdoCustom.Location = New Point(15, 100)
        rdoCustom.Name = "rdoCustom"
        rdoCustom.Size = New Size(80, 19)
        rdoCustom.TabIndex = 3
        rdoCustom.Text = "カスタム:"
        '
        ' txtCustomFormat
        '
        txtCustomFormat.Enabled = False
        txtCustomFormat.Location = New Point(105, 98)
        txtCustomFormat.Name = "txtCustomFormat"
        txtCustomFormat.Size = New Size(255, 23)
        txtCustomFormat.TabIndex = 4
        txtCustomFormat.Text = "YYYY-MM-DD HH24:MI:SS"
        '
        ' grpCsvOptions
        '
        grpCsvOptions.Controls.Add(chkCsvHeader)
        grpCsvOptions.Controls.Add(chkCsvTypes)
        grpCsvOptions.Controls.Add(lblDelimiter)
        grpCsvOptions.Controls.Add(cboDelimiter)
        grpCsvOptions.Location = New Point(15, 170)
        grpCsvOptions.Name = "grpCsvOptions"
        grpCsvOptions.Size = New Size(380, 105)
        grpCsvOptions.TabIndex = 1
        grpCsvOptions.TabStop = False
        grpCsvOptions.Text = "CSV オプション"
        '
        ' chkCsvHeader
        '
        chkCsvHeader.AutoSize = True
        chkCsvHeader.Checked = True
        chkCsvHeader.CheckState = CheckState.Checked
        chkCsvHeader.Location = New Point(15, 25)
        chkCsvHeader.Name = "chkCsvHeader"
        chkCsvHeader.Size = New Size(180, 19)
        chkCsvHeader.TabIndex = 0
        chkCsvHeader.Text = "カラム名ヘッダを出力"
        '
        ' chkCsvTypes
        '
        chkCsvTypes.AutoSize = True
        chkCsvTypes.Location = New Point(15, 50)
        chkCsvTypes.Name = "chkCsvTypes"
        chkCsvTypes.Size = New Size(180, 19)
        chkCsvTypes.TabIndex = 1
        chkCsvTypes.Text = "カラム型行を出力"
        '
        ' lblDelimiter
        '
        lblDelimiter.AutoSize = True
        lblDelimiter.Location = New Point(15, 75)
        lblDelimiter.Name = "lblDelimiter"
        lblDelimiter.Size = New Size(60, 15)
        lblDelimiter.TabIndex = 2
        lblDelimiter.Text = "デリミタ:"
        '
        ' cboDelimiter
        '
        cboDelimiter.DropDownStyle = ComboBoxStyle.DropDownList
        cboDelimiter.Location = New Point(105, 72)
        cboDelimiter.Name = "cboDelimiter"
        cboDelimiter.Size = New Size(180, 23)
        cboDelimiter.TabIndex = 3
        '
        ' grpSqlOptions
        '
        grpSqlOptions.Controls.Add(chkCreateTable)
        grpSqlOptions.Controls.Add(chkCreateIndex)
        grpSqlOptions.Controls.Add(chkWriteComments)
        grpSqlOptions.Controls.Add(chkWriteInserts)
        grpSqlOptions.Controls.Add(chkInferInteger)
        grpSqlOptions.Location = New Point(15, 285)
        grpSqlOptions.Name = "grpSqlOptions"
        grpSqlOptions.Size = New Size(380, 155)
        grpSqlOptions.TabIndex = 2
        grpSqlOptions.TabStop = False
        grpSqlOptions.Text = "SQL スクリプトオプション"
        '
        ' chkCreateTable
        '
        chkCreateTable.AutoSize = True
        chkCreateTable.Location = New Point(15, 25)
        chkCreateTable.Name = "chkCreateTable"
        chkCreateTable.Size = New Size(180, 19)
        chkCreateTable.TabIndex = 0
        chkCreateTable.Text = "CREATE TABLE を出力"
        '
        ' chkCreateIndex
        '
        chkCreateIndex.AutoSize = True
        chkCreateIndex.Location = New Point(15, 50)
        chkCreateIndex.Name = "chkCreateIndex"
        chkCreateIndex.Size = New Size(180, 19)
        chkCreateIndex.TabIndex = 1
        chkCreateIndex.Text = "CREATE INDEX を出力"
        '
        ' chkWriteComments
        '
        chkWriteComments.AutoSize = True
        chkWriteComments.Location = New Point(15, 75)
        chkWriteComments.Name = "chkWriteComments"
        chkWriteComments.Size = New Size(180, 19)
        chkWriteComments.TabIndex = 2
        chkWriteComments.Text = "COMMENT ON を出力"
        '
        ' chkWriteInserts
        '
        chkWriteInserts.AutoSize = True
        chkWriteInserts.Location = New Point(15, 100)
        chkWriteInserts.Name = "chkWriteInserts"
        chkWriteInserts.Size = New Size(330, 19)
        chkWriteInserts.TabIndex = 3
        chkWriteInserts.Text = "INSERT INTO データを出力"
        '
        ' chkInferInteger
        '
        chkInferInteger.AutoSize = True
        chkInferInteger.Location = New Point(15, 125)
        chkInferInteger.Name = "chkInferInteger"
        chkInferInteger.Size = New Size(330, 19)
        chkInferInteger.TabIndex = 4
        chkInferInteger.Text = "NUMBER を実データから整数型に推定"
        '
        ' grpOracleClient
        '
        grpOracleClient.Controls.Add(txtImpdpPath)
        grpOracleClient.Controls.Add(btnBrowseImpdp)
        grpOracleClient.Controls.Add(btnAutoSetup)
        grpOracleClient.Controls.Add(lblImpdpInfo)
        grpOracleClient.Location = New Point(15, 450)
        grpOracleClient.Name = "grpOracleClient"
        grpOracleClient.Size = New Size(380, 130)
        grpOracleClient.TabIndex = 5
        grpOracleClient.TabStop = False
        grpOracleClient.Text = "Oracle Client (オプション)"
        '
        ' txtImpdpPath
        '
        txtImpdpPath.Location = New Point(15, 25)
        txtImpdpPath.Name = "txtImpdpPath"
        txtImpdpPath.Size = New Size(290, 23)
        txtImpdpPath.TabIndex = 0
        txtImpdpPath.PlaceholderText = "impdp.exe のパス"
        '
        ' btnBrowseImpdp
        '
        btnBrowseImpdp.Location = New Point(315, 24)
        btnBrowseImpdp.Name = "btnBrowseImpdp"
        btnBrowseImpdp.Size = New Size(50, 25)
        btnBrowseImpdp.TabIndex = 1
        btnBrowseImpdp.Text = "..."
        '
        ' btnAutoSetup
        '
        btnAutoSetup.Location = New Point(15, 55)
        btnAutoSetup.Name = "btnAutoSetup"
        btnAutoSetup.Size = New Size(350, 28)
        btnAutoSetup.TabIndex = 2
        btnAutoSetup.Text = "Oracle Instant Client を自動セットアップ..."
        '
        ' lblImpdpInfo
        '
        lblImpdpInfo.AutoSize = False
        lblImpdpInfo.Location = New Point(15, 88)
        lblImpdpInfo.Name = "lblImpdpInfo"
        lblImpdpInfo.Size = New Size(350, 36)
        lblImpdpInfo.TabIndex = 2
        lblImpdpInfo.ForeColor = Drawing.SystemColors.GrayText
        lblImpdpInfo.Text = "EXPDP ダンプの制約カラム情報取得に必要です。Oracle Database Client または Instant Client Tools をインストールしてください。"
        '
        ' btnOK
        '
        btnOK.Location = New Point(195, 595)
        btnOK.Name = "btnOK"
        btnOK.Size = New Size(90, 30)
        btnOK.TabIndex = 6
        btnOK.Text = "OK"
        btnOK.DialogResult = DialogResult.OK
        '
        ' btnCancel
        '
        btnCancel.Location = New Point(305, 595)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(90, 30)
        btnCancel.TabIndex = 7
        btnCancel.Text = "キャンセル"
        btnCancel.DialogResult = DialogResult.Cancel
        '
        ' ExportOptionsDialog
        '
        AcceptButton = btnOK
        CancelButton = btnCancel
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(410, 640)
        Controls.Add(grpDateFormat)
        Controls.Add(grpCsvOptions)
        Controls.Add(grpSqlOptions)
        Controls.Add(grpOracleClient)
        Controls.Add(btnOK)
        Controls.Add(btnCancel)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "ExportOptionsDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "エクスポートオプション"
        grpDateFormat.ResumeLayout(False)
        grpDateFormat.PerformLayout()
        grpCsvOptions.ResumeLayout(False)
        grpCsvOptions.PerformLayout()
        grpSqlOptions.ResumeLayout(False)
        grpSqlOptions.PerformLayout()
        grpOracleClient.ResumeLayout(False)
        grpOracleClient.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents grpDateFormat As GroupBox
    Friend WithEvents rdoSlash As RadioButton
    Friend WithEvents rdoCompact As RadioButton
    Friend WithEvents rdoFull As RadioButton
    Friend WithEvents rdoCustom As RadioButton
    Friend WithEvents txtCustomFormat As TextBox
    Friend WithEvents grpCsvOptions As GroupBox
    Friend WithEvents chkCsvHeader As CheckBox
    Friend WithEvents chkCsvTypes As CheckBox
    Friend WithEvents lblDelimiter As Label
    Friend WithEvents cboDelimiter As ComboBox
    Friend WithEvents grpSqlOptions As GroupBox
    Friend WithEvents chkCreateTable As CheckBox
    Friend WithEvents chkCreateIndex As CheckBox
    Friend WithEvents chkWriteComments As CheckBox
    Friend WithEvents chkWriteInserts As CheckBox
    Friend WithEvents chkInferInteger As CheckBox
    Friend WithEvents grpOracleClient As GroupBox
    Friend WithEvents txtImpdpPath As TextBox
    Friend WithEvents btnBrowseImpdp As Button
    Friend WithEvents btnAutoSetup As Button
    Friend WithEvents lblImpdpInfo As Label
    Friend WithEvents btnOK As Button
    Friend WithEvents btnCancel As Button

End Class
