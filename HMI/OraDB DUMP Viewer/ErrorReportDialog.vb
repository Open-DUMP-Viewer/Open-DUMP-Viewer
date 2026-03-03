''' <summary>
''' エラー報告ダイアログ
''' Cloudflare Worker 経由で GitHub Issue を自動作成する
''' 個人情報は収集しない（ユーザー名、PC名、ファイルパス等は含まない）
''' </summary>
Partial Public Class ErrorReportDialog

    Private _sysInfo As ErrorReportLogic.SystemInfo

    ''' <summary>
    ''' ダンプファイルのパスを指定してエラー報告ダイアログを生成する
    ''' パス自体は送信しない (サイズ・形式のみ収集)
    ''' </summary>
    Public Sub New(Optional dumpFilePath As String = Nothing)
        InitializeComponent()

        ' 個人情報を含まない環境情報を自動収集
        _sysInfo = ErrorReportLogic.CollectSystemInfo(dumpFilePath)

        ' 収集した情報をユーザーに透明に表示
        lblVersionValue.Text = _sysInfo.AppVersion
        lblDllVersionValue.Text = _sysInfo.DllVersion
        lblOSValue.Text = _sysInfo.OsVersion
        lblDotNetValue.Text = _sysInfo.DotNetVersion
        lblArchValue.Text = $"{_sysInfo.Architecture} (プロセス: {_sysInfo.ProcessArchitecture})"
        lblLocaleValue.Text = _sysInfo.Locale
        lblDpiValue.Text = _sysInfo.DpiScale
        lblMemoryValue.Text = _sysInfo.MemoryMB
        lblScreenValue.Text = _sysInfo.ScreenResolution
        lblDumpInfoValue.Text = _sysInfo.DumpFileInfo
    End Sub

    ''' <summary>
    ''' 送信ボタンクリック: エラー報告を Cloudflare Worker に送信
    ''' </summary>
    Private Async Sub btnSubmit_Click(sender As Object, e As EventArgs) Handles btnSubmit.Click
        ' ローカルバリデーション
        If String.IsNullOrWhiteSpace(txtTitle.Text) Then
            MessageBox.Show("件名を入力してください。", "入力エラー",
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            txtTitle.Focus()
            Return
        End If

        If String.IsNullOrWhiteSpace(txtDescription.Text) Then
            MessageBox.Show("内容を入力してください。", "入力エラー",
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
                _sysInfo
            )

            prgSubmit.Visible = False

            If result.Success Then
                lblStatus.Text = "送信が完了しました。ご報告ありがとうございます。"
                lblStatus.ForeColor = Color.Green
                lblStatus.Visible = True

                If Not String.IsNullOrEmpty(result.IssueUrl) Then
                    lnkIssue.Text = $"Issue #{result.IssueNumber} を確認する"
                    lnkIssue.Tag = result.IssueUrl
                    lnkIssue.Visible = True
                End If

                ' 送信成功後は「閉じる」ボタンのみ有効
                btnCancel.Text = "閉じる"
                btnCancel.Enabled = True
                btnSubmit.Visible = False
            Else
                lblStatus.Text = $"送信に失敗しました: {result.ErrorMessage}"
                lblStatus.ForeColor = Color.Red
                lblStatus.Visible = True
                SetSubmitting(False)
            End If

        Catch ex As Exception
            prgSubmit.Visible = False
            lblStatus.Text = $"送信エラー: {ex.Message}"
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
            lblStatus.Text = "送信中..."
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

End Class
