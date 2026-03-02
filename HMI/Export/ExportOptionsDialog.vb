''' <summary>
''' エクスポートオプション設定ダイアログ
''' 日付フォーマット、CSV オプション、SQL スクリプトオプションを設定
''' </summary>
Public Class ExportOptionsDialog

    Public Sub New()
        InitializeComponent()
    End Sub

    Private Sub ExportOptionsDialog_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' ExportOptions から現在の設定をコントロールに反映
        Select Case ExportOptions.DateFormat
            Case OraDB_NativeParser.DATE_FMT_SLASH : rdoSlash.Checked = True
            Case OraDB_NativeParser.DATE_FMT_COMPACT : rdoCompact.Checked = True
            Case OraDB_NativeParser.DATE_FMT_FULL : rdoFull.Checked = True
            Case OraDB_NativeParser.DATE_FMT_CUSTOM : rdoCustom.Checked = True
            Case Else : rdoSlash.Checked = True
        End Select

        txtCustomFormat.Text = ExportOptions.CustomDateFormat
        txtCustomFormat.Enabled = rdoCustom.Checked

        chkCsvHeader.Checked = ExportOptions.CsvWriteHeader
        chkCsvTypes.Checked = ExportOptions.CsvWriteTypes
        chkCreateTable.Checked = ExportOptions.SqlCreateTable
    End Sub

    Private Sub rdoCustom_CheckedChanged(sender As Object, e As EventArgs) Handles rdoCustom.CheckedChanged
        txtCustomFormat.Enabled = rdoCustom.Checked
    End Sub

    Private Sub btnOK_Click(sender As Object, e As EventArgs) Handles btnOK.Click
        ' コントロールの値を ExportOptions に書き戻す
        If rdoSlash.Checked Then
            ExportOptions.DateFormat = OraDB_NativeParser.DATE_FMT_SLASH
        ElseIf rdoCompact.Checked Then
            ExportOptions.DateFormat = OraDB_NativeParser.DATE_FMT_COMPACT
        ElseIf rdoFull.Checked Then
            ExportOptions.DateFormat = OraDB_NativeParser.DATE_FMT_FULL
        ElseIf rdoCustom.Checked Then
            ExportOptions.DateFormat = OraDB_NativeParser.DATE_FMT_CUSTOM
        End If

        ExportOptions.CustomDateFormat = txtCustomFormat.Text
        ExportOptions.CsvWriteHeader = chkCsvHeader.Checked
        ExportOptions.CsvWriteTypes = chkCsvTypes.Checked
        ExportOptions.SqlCreateTable = chkCreateTable.Checked

        ' 設定を永続化
        ExportOptions.Save()
    End Sub

End Class
