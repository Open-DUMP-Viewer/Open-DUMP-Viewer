<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class DatabaseConnectionDialog
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
        tabControl = New TabControl()
        tabSqlServer = New TabPage()
        lblServer = New Label()
        txtServer = New TextBox()
        lblAuth = New Label()
        cboAuth = New ComboBox()
        lblUser = New Label()
        txtUser = New TextBox()
        lblPassword = New Label()
        txtPassword = New TextBox()
        lblDatabase = New Label()
        txtDatabase = New TextBox()
        btnTest = New Button()
        lblTestResult = New Label()
        tabOdbc = New TabPage()
        lblDsn = New Label()
        cboDsn = New ComboBox()
        lblConnStr = New Label()
        txtConnStr = New TextBox()
        btnTestOdbc = New Button()
        lblTestResultOdbc = New Label()
        btnOK = New Button()
        btnCancel = New Button()
        tabControl.SuspendLayout()
        tabSqlServer.SuspendLayout()
        tabOdbc.SuspendLayout()
        SuspendLayout()
        '
        ' tabControl
        '
        tabControl.Controls.Add(tabSqlServer)
        tabControl.Controls.Add(tabOdbc)
        tabControl.Location = New Point(12, 12)
        tabControl.Name = "tabControl"
        tabControl.SelectedIndex = 0
        tabControl.Size = New Size(460, 290)
        tabControl.TabIndex = 0
        '
        ' tabSqlServer
        '
        tabSqlServer.Controls.Add(lblServer)
        tabSqlServer.Controls.Add(txtServer)
        tabSqlServer.Controls.Add(lblAuth)
        tabSqlServer.Controls.Add(cboAuth)
        tabSqlServer.Controls.Add(lblUser)
        tabSqlServer.Controls.Add(txtUser)
        tabSqlServer.Controls.Add(lblPassword)
        tabSqlServer.Controls.Add(txtPassword)
        tabSqlServer.Controls.Add(lblDatabase)
        tabSqlServer.Controls.Add(txtDatabase)
        tabSqlServer.Controls.Add(btnTest)
        tabSqlServer.Controls.Add(lblTestResult)
        tabSqlServer.Location = New Point(4, 24)
        tabSqlServer.Name = "tabSqlServer"
        tabSqlServer.Padding = New Padding(10)
        tabSqlServer.Size = New Size(452, 262)
        tabSqlServer.TabIndex = 0
        tabSqlServer.Text = "SQL Server"
        '
        ' lblServer
        '
        lblServer.AutoSize = True
        lblServer.Location = New Point(15, 15)
        lblServer.Name = "lblServer"
        lblServer.Size = New Size(70, 15)
        lblServer.Text = "サーバー名:"
        '
        ' txtServer
        '
        txtServer.Location = New Point(130, 12)
        txtServer.Name = "txtServer"
        txtServer.Size = New Size(300, 23)
        txtServer.Text = "localhost"
        '
        ' lblAuth
        '
        lblAuth.AutoSize = True
        lblAuth.Location = New Point(15, 48)
        lblAuth.Name = "lblAuth"
        lblAuth.Size = New Size(70, 15)
        lblAuth.Text = "認証方式:"
        '
        ' cboAuth
        '
        cboAuth.DropDownStyle = ComboBoxStyle.DropDownList
        cboAuth.Location = New Point(130, 45)
        cboAuth.Name = "cboAuth"
        cboAuth.Size = New Size(200, 23)
        '
        ' lblUser
        '
        lblUser.AutoSize = True
        lblUser.Location = New Point(15, 81)
        lblUser.Name = "lblUser"
        lblUser.Size = New Size(70, 15)
        lblUser.Text = "ユーザー名:"
        '
        ' txtUser
        '
        txtUser.Location = New Point(130, 78)
        txtUser.Name = "txtUser"
        txtUser.Size = New Size(200, 23)
        txtUser.Text = "sa"
        '
        ' lblPassword
        '
        lblPassword.AutoSize = True
        lblPassword.Location = New Point(15, 114)
        lblPassword.Name = "lblPassword"
        lblPassword.Size = New Size(70, 15)
        lblPassword.Text = "パスワード:"
        '
        ' txtPassword
        '
        txtPassword.Location = New Point(130, 111)
        txtPassword.Name = "txtPassword"
        txtPassword.PasswordChar = "*"c
        txtPassword.Size = New Size(200, 23)
        '
        ' lblDatabase
        '
        lblDatabase.AutoSize = True
        lblDatabase.Location = New Point(15, 147)
        lblDatabase.Name = "lblDatabase"
        lblDatabase.Size = New Size(90, 15)
        lblDatabase.Text = "データベース名:"
        '
        ' txtDatabase
        '
        txtDatabase.Location = New Point(130, 144)
        txtDatabase.Name = "txtDatabase"
        txtDatabase.Size = New Size(200, 23)
        '
        ' btnTest
        '
        btnTest.Location = New Point(15, 185)
        btnTest.Name = "btnTest"
        btnTest.Size = New Size(100, 30)
        btnTest.Text = "接続テスト"
        '
        ' lblTestResult
        '
        lblTestResult.AutoSize = True
        lblTestResult.Location = New Point(130, 192)
        lblTestResult.Name = "lblTestResult"
        lblTestResult.Size = New Size(0, 15)
        '
        ' tabOdbc
        '
        tabOdbc.Controls.Add(lblDsn)
        tabOdbc.Controls.Add(cboDsn)
        tabOdbc.Controls.Add(lblConnStr)
        tabOdbc.Controls.Add(txtConnStr)
        tabOdbc.Controls.Add(btnTestOdbc)
        tabOdbc.Controls.Add(lblTestResultOdbc)
        tabOdbc.Location = New Point(4, 24)
        tabOdbc.Name = "tabOdbc"
        tabOdbc.Padding = New Padding(10)
        tabOdbc.Size = New Size(452, 262)
        tabOdbc.TabIndex = 1
        tabOdbc.Text = "ODBC"
        '
        ' lblDsn
        '
        lblDsn.AutoSize = True
        lblDsn.Location = New Point(15, 15)
        lblDsn.Name = "lblDsn"
        lblDsn.Size = New Size(80, 15)
        lblDsn.Text = "システム DSN:"
        '
        ' cboDsn
        '
        cboDsn.DropDownStyle = ComboBoxStyle.DropDownList
        cboDsn.Location = New Point(130, 12)
        cboDsn.Name = "cboDsn"
        cboDsn.Size = New Size(300, 23)
        '
        ' lblConnStr
        '
        lblConnStr.AutoSize = True
        lblConnStr.Location = New Point(15, 55)
        lblConnStr.Name = "lblConnStr"
        lblConnStr.Size = New Size(100, 15)
        lblConnStr.Text = "接続文字列 (直接入力):"
        '
        ' txtConnStr
        '
        txtConnStr.Location = New Point(15, 78)
        txtConnStr.Multiline = True
        txtConnStr.Name = "txtConnStr"
        txtConnStr.Size = New Size(415, 80)
        txtConnStr.ScrollBars = ScrollBars.Vertical
        '
        ' btnTestOdbc
        '
        btnTestOdbc.Location = New Point(15, 175)
        btnTestOdbc.Name = "btnTestOdbc"
        btnTestOdbc.Size = New Size(100, 30)
        btnTestOdbc.Text = "接続テスト"
        '
        ' lblTestResultOdbc
        '
        lblTestResultOdbc.AutoSize = True
        lblTestResultOdbc.Location = New Point(130, 182)
        lblTestResultOdbc.Name = "lblTestResultOdbc"
        lblTestResultOdbc.Size = New Size(0, 15)
        '
        ' btnOK
        '
        btnOK.Location = New Point(280, 315)
        btnOK.Name = "btnOK"
        btnOK.Size = New Size(90, 30)
        btnOK.Text = "OK"
        btnOK.DialogResult = DialogResult.OK
        '
        ' btnCancel
        '
        btnCancel.Location = New Point(382, 315)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(90, 30)
        btnCancel.Text = "キャンセル"
        btnCancel.DialogResult = DialogResult.Cancel
        '
        ' DatabaseConnectionDialog
        '
        AcceptButton = btnOK
        CancelButton = btnCancel
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(484, 360)
        Controls.Add(tabControl)
        Controls.Add(btnOK)
        Controls.Add(btnCancel)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "DatabaseConnectionDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "データベース接続"
        tabControl.ResumeLayout(False)
        tabSqlServer.ResumeLayout(False)
        tabSqlServer.PerformLayout()
        tabOdbc.ResumeLayout(False)
        tabOdbc.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents tabControl As TabControl
    Friend WithEvents tabSqlServer As TabPage
    Friend WithEvents lblServer As Label
    Friend WithEvents txtServer As TextBox
    Friend WithEvents lblAuth As Label
    Friend WithEvents cboAuth As ComboBox
    Friend WithEvents lblUser As Label
    Friend WithEvents txtUser As TextBox
    Friend WithEvents lblPassword As Label
    Friend WithEvents txtPassword As TextBox
    Friend WithEvents lblDatabase As Label
    Friend WithEvents txtDatabase As TextBox
    Friend WithEvents btnTest As Button
    Friend WithEvents lblTestResult As Label
    Friend WithEvents tabOdbc As TabPage
    Friend WithEvents lblDsn As Label
    Friend WithEvents cboDsn As ComboBox
    Friend WithEvents lblConnStr As Label
    Friend WithEvents txtConnStr As TextBox
    Friend WithEvents btnTestOdbc As Button
    Friend WithEvents lblTestResultOdbc As Label
    Friend WithEvents btnOK As Button
    Friend WithEvents btnCancel As Button

End Class
