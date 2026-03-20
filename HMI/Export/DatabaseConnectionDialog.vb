Imports Microsoft.Data.SqlClient

''' <summary>
''' データベース接続ダイアログ
''' SQL Server タブと ODBC タブを持つ。
''' 接続テスト機能付き。
''' </summary>
Public Class DatabaseConnectionDialog
    Implements ILocalizable

    ''' <summary>接続種別: True=SQL Server, False=ODBC</summary>
    <ComponentModel.DesignerSerializationVisibility(ComponentModel.DesignerSerializationVisibility.Hidden)>
    <ComponentModel.Browsable(False)>
    Public Property IsSqlServer As Boolean = True

    ''' <summary>構築された接続文字列</summary>
    <ComponentModel.DesignerSerializationVisibility(ComponentModel.DesignerSerializationVisibility.Hidden)>
    <ComponentModel.Browsable(False)>
    Public Property ConnectionString As String = ""

    Public Sub New(Optional selectOdbcTab As Boolean = False)
        InitializeComponent()
        ApplyLocalization()

        ' 認証方式の選択肢
        cboAuth.Items.Add(Loc.S("DbConn_AuthWindows"))
        cboAuth.Items.Add(Loc.S("DbConn_AuthSqlServer"))
        cboAuth.SelectedIndex = 1

        ' ODBC DSN 一覧を取得
        LoadOdbcDsnList()

        ' ODBC タブを初期選択
        If selectOdbcTab Then
            tabControl.SelectedTab = tabOdbc
        End If
    End Sub

    Private Sub DatabaseConnectionDialog_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        UpdateAuthUI()
    End Sub

    ''' <summary>認証方式変更時にユーザー名/パスワードの有効/無効を切り替え</summary>
    Private Sub cboAuth_SelectedIndexChanged(sender As Object, e As EventArgs) Handles cboAuth.SelectedIndexChanged
        UpdateAuthUI()
    End Sub

    Private Sub UpdateAuthUI()
        Dim isSqlAuth = (cboAuth.SelectedIndex = 1)
        txtUser.Enabled = isSqlAuth
        txtPassword.Enabled = isSqlAuth
    End Sub

    ''' <summary>ODBC DSN 一覧を読み込み</summary>
    Private Sub LoadOdbcDsnList()
        Try
            cboDsn.Items.Clear()
            ' システム DSN をレジストリから取得
            Dim key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey("SOFTWARE\ODBC\ODBC.INI\ODBC Data Sources")
            If key IsNot Nothing Then
                For Each dsnName As String In key.GetValueNames()
                    cboDsn.Items.Add(dsnName)
                Next
                key.Close()
            End If
            If cboDsn.Items.Count > 0 Then cboDsn.SelectedIndex = 0
        Catch
        End Try
    End Sub

    ''' <summary>SQL Server 接続文字列を構築</summary>
    Private Function BuildSqlServerConnectionString() As String
        Dim builder As New SqlConnectionStringBuilder()
        builder.DataSource = txtServer.Text.Trim()
        builder.InitialCatalog = txtDatabase.Text.Trim()
        builder.TrustServerCertificate = False
        builder.Encrypt = True

        If cboAuth.SelectedIndex = 0 Then
            ' Windows 認証
            builder.IntegratedSecurity = True
        Else
            ' SQL Server 認証
            builder.IntegratedSecurity = False
            builder.UserID = txtUser.Text.Trim()
            builder.Password = txtPassword.Text
        End If

        Return builder.ConnectionString
    End Function

    ''' <summary>ODBC 接続文字列を構築</summary>
    Private Function BuildOdbcConnectionString() As String
        If Not String.IsNullOrWhiteSpace(txtConnStr.Text) Then
            Return txtConnStr.Text.Trim()
        End If
        If cboDsn.SelectedItem IsNot Nothing Then
            Return $"DSN={cboDsn.SelectedItem}"
        End If
        Return ""
    End Function

    ''' <summary>SQL Server 接続テスト</summary>
    Private Sub btnTest_Click(sender As Object, e As EventArgs) Handles btnTest.Click
        lblTestResult.ForeColor = Color.Black
        lblTestResult.Text = Loc.S("DbConn_Connecting")
        Application.DoEvents()

        Try
            Dim connStr = BuildSqlServerConnectionString()
            Using conn As New SqlConnection(connStr)
                conn.Open()
            End Using
            lblTestResult.ForeColor = Color.Green
            lblTestResult.Text = Loc.S("DbConn_Success")
        Catch ex As Exception
            lblTestResult.ForeColor = Color.Red
            lblTestResult.Text = Loc.SF("DbConn_Failed", ex.Message)
        End Try
    End Sub

    ''' <summary>ODBC 接続テスト</summary>
    Private Sub btnTestOdbc_Click(sender As Object, e As EventArgs) Handles btnTestOdbc.Click
        lblTestResultOdbc.ForeColor = Color.Black
        lblTestResultOdbc.Text = Loc.S("DbConn_Connecting")
        Application.DoEvents()

        Try
            Dim connStr = BuildOdbcConnectionString()
            Using conn As New System.Data.Odbc.OdbcConnection(connStr)
                conn.Open()
            End Using
            lblTestResultOdbc.ForeColor = Color.Green
            lblTestResultOdbc.Text = Loc.S("DbConn_Success")
        Catch ex As Exception
            lblTestResultOdbc.ForeColor = Color.Red
            lblTestResultOdbc.Text = Loc.SF("DbConn_Failed", ex.Message)
        End Try
    End Sub

    ''' <summary>OK ボタン</summary>
    Private Sub btnOK_Click(sender As Object, e As EventArgs) Handles btnOK.Click
        If tabControl.SelectedTab Is tabSqlServer Then
            IsSqlServer = True
            ConnectionString = BuildSqlServerConnectionString()
            If String.IsNullOrWhiteSpace(txtServer.Text) OrElse String.IsNullOrWhiteSpace(txtDatabase.Text) Then
                MessageBox.Show(Loc.S("DbConn_EnterServerAndDb"), Loc.S("Title_InputError"),
                               MessageBoxButtons.OK, MessageBoxIcon.Warning)
                DialogResult = DialogResult.None
            End If
        Else
            IsSqlServer = False
            ConnectionString = BuildOdbcConnectionString()
            If String.IsNullOrEmpty(ConnectionString) Then
                MessageBox.Show(Loc.S("DbConn_EnterDsnOrConnStr"), Loc.S("Title_InputError"),
                               MessageBoxButtons.OK, MessageBoxIcon.Warning)
                DialogResult = DialogResult.None
            End If
        End If
    End Sub

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("DbConn_FormTitle")
        lblServer.Text = Loc.S("DbConn_ServerNameLabel")
        lblAuth.Text = Loc.S("DbConn_AuthLabel")
        lblUser.Text = Loc.S("DbConn_UserNameLabel")
        lblPassword.Text = Loc.S("DbConn_PasswordLabel")
        lblDatabase.Text = Loc.S("DbConn_DatabaseNameLabel")
        btnTest.Text = Loc.S("Button_ConnectionTest")
        btnTestOdbc.Text = Loc.S("Button_ConnectionTest")
        lblDsn.Text = Loc.S("DbConn_SystemDsnLabel")
        lblConnStr.Text = Loc.S("DbConn_ConnectionStringLabel")
        btnOK.Text = Loc.S("Button_OK")
        btnCancel.Text = Loc.S("Button_Cancel")
    End Sub
#End Region

End Class
