''' <summary>
''' エラー報告ダイアログ
''' Cloudflare Worker 経由で GitHub Issue を自動作成する
''' 個人情報は収集しない（ユーザー名、PC名、ファイルパス等は含まない）
''' </summary>
Partial Public Class ErrorReportDialog
    Implements ILocalizable

    Private _sysInfo As ErrorReportLogic.SystemInfo

    ''' <summary>
    ''' ダンプファイルのパスを指定してエラー報告ダイアログを生成する
    ''' パス自体は送信しない (サイズ・形式のみ収集)
    ''' </summary>
    Public Sub New(Optional dumpFilePath As String = Nothing)
        InitializeComponent()
        ApplyLocalization()

        ' 個人情報を含まない環境情報を自動収集
        _sysInfo = ErrorReportLogic.CollectSystemInfo(dumpFilePath)

        ' 収集した情報をユーザーに透明に表示
        lblVersionValue.Text = _sysInfo.AppVersion
        lblDllVersionValue.Text = _sysInfo.DllVersion
        lblOSValue.Text = _sysInfo.OsVersion
        lblDotNetValue.Text = _sysInfo.DotNetVersion
        lblArchValue.Text = $"{_sysInfo.Architecture} ({Loc.S("ErrorReport_Process")} {_sysInfo.ProcessArchitecture})"
        lblLocaleValue.Text = _sysInfo.Locale
        lblDpiValue.Text = _sysInfo.DpiScale
        lblMemoryValue.Text = _sysInfo.MemoryMB
        lblScreenValue.Text = _sysInfo.ScreenResolution
        lblDumpInfoValue.Text = _sysInfo.DumpFileInfo

        ' ダンプファイルが開かれていて50MB以下の場合のみ添付チェックボックスを有効化
        If _sysInfo.DumpFilePath IsNot Nothing AndAlso IO.File.Exists(_sysInfo.DumpFilePath) Then
            Dim fi As New IO.FileInfo(_sysInfo.DumpFilePath)
            If fi.Length <= 50L * 1024 * 1024 Then
                chkAttachDump.Enabled = True
                Dim sizeMB = fi.Length / (1024.0 * 1024.0)
                chkAttachDump.Text = Loc.SF("ErrorReport_AttachDump", $"{sizeMB:F1}")
            Else
                chkAttachDump.Enabled = False
                chkAttachDump.Text = Loc.S("ErrorReport_AttachDumpTooLarge")
            End If
        Else
            chkAttachDump.Enabled = False
            chkAttachDump.Text = Loc.S("ErrorReport_AttachDumpNoFile")
        End If
    End Sub

    ''' <summary>
    ''' 送信ボタンクリック: エラー報告を Cloudflare Worker に送信
    ''' </summary>
    Private Async Sub btnSubmit_Click(sender As Object, e As EventArgs) Handles btnSubmit.Click
        ' ローカルバリデーション
        If String.IsNullOrWhiteSpace(txtTitle.Text) Then
            MessageBox.Show(Loc.S("ErrorReport_SubjectRequired"), Loc.S("Title_InputError"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            txtTitle.Focus()
            Return
        End If

        If String.IsNullOrWhiteSpace(txtDescription.Text) Then
            MessageBox.Show(Loc.S("ErrorReport_DescriptionRequired"), Loc.S("Title_InputError"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            txtDescription.Focus()
            Return
        End If

        ' UI を送信中状態に切り替え
        SetSubmitting(True)

        Try
            Dim result = Await ErrorReportLogic.SubmitReportAsync(
                txtTitle.Text.Trim(),
                txtDescription.Text.Trim(),
                txtContact.Text.Trim(),
                _sysInfo,
                attachDump:=chkAttachDump.Checked
            )

            prgSubmit.Visible = False

            If result.Success Then
                lblStatus.Text = Loc.S("ErrorReport_SubmitSuccess")
                lblStatus.ForeColor = Color.Green
                lblStatus.Visible = True

                If Not String.IsNullOrEmpty(result.IssueUrl) Then
                    lnkIssue.Text = Loc.SF("ErrorReport_ViewIssue", result.IssueNumber)
                    lnkIssue.Tag = result.IssueUrl
                    lnkIssue.Visible = True
                End If

                ' 送信成功後は「閉じる」ボタンのみ有効
                btnCancel.Text = Loc.S("Button_Close")
                btnCancel.Enabled = True
                btnSubmit.Visible = False
            Else
                lblStatus.Text = Loc.SF("ErrorReport_SubmitFailed", result.ErrorMessage)
                lblStatus.ForeColor = Color.Red
                lblStatus.Visible = True
                SetSubmitting(False)
            End If

        Catch ex As Exception
            prgSubmit.Visible = False
            lblStatus.Text = Loc.SF("ErrorReport_SubmitError", ex.Message)
            lblStatus.ForeColor = Color.Red
            lblStatus.Visible = True
            SetSubmitting(False)
        End Try
    End Sub

    ''' <summary>
    ''' 送信中/送信完了時の UI 状態を切り替える
    ''' </summary>
    Private Sub SetSubmitting(submitting As Boolean)
        btnSubmit.Enabled = Not submitting
        btnCancel.Enabled = Not submitting
        txtTitle.ReadOnly = submitting
        txtDescription.ReadOnly = submitting
        txtContact.ReadOnly = submitting
        prgSubmit.Visible = submitting

        If submitting Then
            lblStatus.Text = Loc.S("ErrorReport_Submitting")
            lblStatus.ForeColor = SystemColors.ControlText
            lblStatus.Visible = True
        End If
    End Sub

    ''' <summary>
    ''' Issue リンククリック: ブラウザで GitHub Issue を開く
    ''' </summary>
    Private Sub lnkIssue_LinkClicked(sender As Object, e As LinkLabelLinkClickedEventArgs) Handles lnkIssue.LinkClicked
        Try
            Dim url = TryCast(lnkIssue.Tag, String)
            If url IsNot Nothing Then
                Process.Start(New ProcessStartInfo(url) With {.UseShellExecute = True})
            End If
        Catch
            ' ブラウザ起動失敗は無視
        End Try
    End Sub

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("ErrorReport_FormTitle")
        lblTitleCaption.Text = Loc.S("ErrorReport_SubjectLabel")
        lblDescCaption.Text = Loc.S("ErrorReport_DescriptionLabel")
        lblContactCaption.Text = Loc.S("ErrorReport_ContactLabel")
        lblContactHint.Text = Loc.S("ErrorReport_ContactHint")
        lblSysInfoHeader.Text = Loc.S("ErrorReport_SysInfoHeader")
        lblVersionCaption.Text = Loc.S("ErrorReport_AppLabel")
        lblDllVersionCaption.Text = Loc.S("ErrorReport_DllLabel")
        lblOSCaption.Text = Loc.S("ErrorReport_OsLabel")
        lblDotNetCaption.Text = Loc.S("ErrorReport_DotNetLabel")
        lblArchCaption.Text = Loc.S("ErrorReport_CpuLabel")
        lblLocaleCaption.Text = Loc.S("ErrorReport_LocaleLabel")
        lblDpiCaption.Text = Loc.S("ErrorReport_DpiLabel")
        lblMemoryCaption.Text = Loc.S("ErrorReport_MemoryLabel")
        lblScreenCaption.Text = Loc.S("ErrorReport_ScreenLabel")
        lblDumpInfoCaption.Text = Loc.S("ErrorReport_DumpLabel")
        btnSubmit.Text = Loc.S("Button_Submit")
        btnCancel.Text = Loc.S("Button_Cancel")
    End Sub
#End Region

End Class
