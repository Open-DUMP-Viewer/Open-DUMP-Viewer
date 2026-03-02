''' <summary>
''' SQL スクリプト出力の DBMS 選択ダイアログ
''' Oracle, PostgreSQL, MySQL, SQL Server から選択
''' </summary>
Public Class SqlExportDialog

    ''' <summary>選択された DBMS タイプ</summary>
    <ComponentModel.DesignerSerializationVisibility(ComponentModel.DesignerSerializationVisibility.Hidden)>
    <ComponentModel.Browsable(False)>
    Public Property SelectedDbmsType As Integer = ExportHelper.DBMS_ORACLE

    Public Sub New()
        InitializeComponent()

        ' DBMS 一覧を設定
        cboDbms.Items.Add("Oracle")
        cboDbms.Items.Add("PostgreSQL")
        cboDbms.Items.Add("MySQL")
        cboDbms.Items.Add("SQL Server")
        cboDbms.SelectedIndex = 0
    End Sub

    Private Sub btnOK_Click(sender As Object, e As EventArgs) Handles btnOK.Click
        Select Case cboDbms.SelectedIndex
            Case 0 : SelectedDbmsType = ExportHelper.DBMS_ORACLE
            Case 1 : SelectedDbmsType = ExportHelper.DBMS_POSTGRES
            Case 2 : SelectedDbmsType = ExportHelper.DBMS_MYSQL
            Case 3 : SelectedDbmsType = ExportHelper.DBMS_SQLSERVER
        End Select
    End Sub

End Class
