''' <summary>
''' SQL スクリプト出力の DBMS 選択ダイアログ
''' Oracle, PostgreSQL, MySQL, SQL Server から選択
''' </summary>
Public Class SqlExportDialog
    Implements ILocalizable

    ''' <summary>選択された DBMS タイプ</summary>
    <ComponentModel.DesignerSerializationVisibility(ComponentModel.DesignerSerializationVisibility.Hidden)>
    <ComponentModel.Browsable(False)>
    Public Property SelectedDbmsType As Integer = ExportHelper.DBMS_ORACLE

    ''' <summary>入力されたデータベース名 (USE [DB] 用、空の場合は出力しない)</summary>
    <ComponentModel.DesignerSerializationVisibility(ComponentModel.DesignerSerializationVisibility.Hidden)>
    <ComponentModel.Browsable(False)>
    Public Property DatabaseName As String = ""

    Public Sub New()
        InitializeComponent()
        ApplyLocalization()

        ' DBMS 一覧を設定
        cboDbms.Items.Add("Oracle")
        cboDbms.Items.Add("PostgreSQL")
        cboDbms.Items.Add("MySQL")
        cboDbms.Items.Add("SQL Server")
        cboDbms.SelectedIndex = 0

        ' ExportOptions の現在値を反映
        chkCreateTable.Checked = ExportOptions.SqlCreateTable
        chkInferInteger.Checked = ExportOptions.SqlInferInteger
    End Sub

    Private Sub btnOK_Click(sender As Object, e As EventArgs) Handles btnOK.Click
        Select Case cboDbms.SelectedIndex
            Case 0 : SelectedDbmsType = ExportHelper.DBMS_ORACLE
            Case 1 : SelectedDbmsType = ExportHelper.DBMS_POSTGRES
            Case 2 : SelectedDbmsType = ExportHelper.DBMS_MYSQL
            Case 3 : SelectedDbmsType = ExportHelper.DBMS_SQLSERVER
        End Select

        ' オプションを反映・保存
        ExportOptions.SqlCreateTable = chkCreateTable.Checked
        ExportOptions.SqlInferInteger = chkInferInteger.Checked
        DatabaseName = txtDatabaseName.Text.Trim()
        ExportOptions.Save()
    End Sub

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("SqlExport_FormTitle")
        lblDbms.Text = Loc.S("SqlExport_DatabaseLabel")
        chkCreateTable.Text = Loc.S("ExportOptions_SqlCreateTable")
        chkInferInteger.Text = Loc.S("ExportOptions_SqlInferInteger")
        lblDatabaseName.Text = Loc.S("SqlExport_DatabaseName")
        btnOK.Text = Loc.S("Button_OK")
        btnCancel.Text = Loc.S("Button_Cancel")
    End Sub
#End Region

End Class
