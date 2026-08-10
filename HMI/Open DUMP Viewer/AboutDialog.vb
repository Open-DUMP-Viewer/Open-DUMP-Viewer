Imports System.Reflection
Imports System.Threading

''' <summary>
''' バージョン情報ダイアログ
''' 現在のバージョンを表示し、GitHubの最新リリースと比較する
''' 新バージョンがある場合はインストーラーをダウンロードして更新できる
''' </summary>
Partial Public Class AboutDialog
    Implements ILocalizable

    Private Const SponsorPageUrl As String = "https://github.com/sponsors/Open-DUMP-Viewer"

    ''' <summary>最新リリースのインストーラーダウンロードURL（更新ボタン押下時に使用）</summary>
    Private _installerDownloadUrl As String = Nothing

    Public Sub New()
        InitializeComponent()
        ApplyLocalization()

        ' 現在のバージョン情報を設定
        Dim asm = Assembly.GetExecutingAssembly()
        Dim ver = asm.GetName().Version
        Dim currentVersion = $"{ver.Major}.{ver.Minor}.{ver.Build}"

        lblVersion.Text = $"{Loc.S("About_VersionLabel")} v{currentVersion}"
        lblCopyright.Text = $"Copyright (C) {DateTime.Now.Year} YANAI Taketo"

        ' MSIX (Microsoft Store) 配布版では Store が更新を担当するため、
        ' 最新バージョン確認 UI を非表示にする
        If PackageContext.IsPackaged Then
            lblLatestCaption.Visible = False
            lblLatestVersion.Visible = False
            btnUpdate.Visible = False
            prgDownload.Visible = False
            lnkReleasePage.Visible = False
        Else
            ' 最新バージョンを非同期で確認
            CheckLatestVersionAsync(currentVersion)
        End If
    End Sub

    ''' <summary>
    ''' GitHubの最新リリースを非同期で確認する
    ''' ネットワークエラー時はエラー表示せず、確認不可であることを表示する
    ''' </summary>
    Private Async Sub CheckLatestVersionAsync(currentVersion As String)
        Try
            Dim latest = Await UpdateLogic.GetLatestReleaseAsync()

            ' 確認中にダイアログが閉じられていた場合は何もしない
            If IsDisposed Then Return

            If latest Is Nothing Then
                lblLatestVersion.Text = Loc.S("About_CheckFailed")
                Return
            End If

            ' インストーラーダウンロードURL（実行中のアーキテクチャに合致するもの）
            _installerDownloadUrl = latest.InstallerUrl

            ' バージョン比較
            Dim currentVer As Version = Nothing
            Dim latestVer As Version = Nothing
            Dim canParseCurrent = Version.TryParse(currentVersion, currentVer)
            Dim canParseLatest = Version.TryParse(latest.Version, latestVer)

            If canParseCurrent AndAlso canParseLatest Then
                If latestVer > currentVer Then
                    ' 新しいバージョンがある
                    lblLatestVersion.Text = Loc.SF("About_NewVersionAvailable", latest.Version)
                    lblLatestVersion.ForeColor = Color.OrangeRed

                    ' 更新ボタンを表示（インストーラーURLがある場合のみ）
                    If _installerDownloadUrl IsNot Nothing Then
                        btnUpdate.Visible = True
                    End If

                    lnkReleasePage.Text = Loc.S("About_OpenReleasePage")
                    lnkReleasePage.Visible = True
                Else
                    ' 最新版を使用中
                    lblLatestVersion.Text = Loc.SF("About_LatestInUse", latest.Version)
                    lblLatestVersion.ForeColor = Color.Green
                End If
            Else
                lblLatestVersion.Text = latest.TagName
            End If

        Catch
            ' ネットワークエラー・タイムアウト等 → エラーにしない
            If Not IsDisposed Then lblLatestVersion.Text = Loc.S("About_CheckFailedOffline")
        End Try
    End Sub

    ''' <summary>
    ''' 更新ボタンクリック: インストーラーをダウンロードして起動する
    ''' 画面上で明示的に押された操作なので、確認ダイアログは挟まない
    ''' </summary>
    Private Async Sub btnUpdate_Click(sender As Object, e As EventArgs) Handles btnUpdate.Click
        If _installerDownloadUrl Is Nothing Then Return

        btnUpdate.Enabled = False
        btnUpdate.Text = Loc.S("About_Downloading")
        prgDownload.Visible = True
        prgDownload.Style = ProgressBarStyle.Marquee

        Try
            Dim progress As New Progress(Of Integer)(AddressOf OnDownloadProgress)
            Dim installerPath = Await UpdateLogic.DownloadInstallerAsync(_installerDownloadUrl, progress, CancellationToken.None)

            prgDownload.Style = ProgressBarStyle.Continuous
            prgDownload.Value = 100
            btnUpdate.Text = Loc.S("About_LaunchingInstaller")

            ' バッチファイルでアプリ終了後にインストーラーを実行
            UpdateLogic.LaunchInstallerAndExit(installerPath)

        Catch ex As Exception
            prgDownload.Visible = False
            btnUpdate.Text = Loc.S("Button_Update")
            btnUpdate.Enabled = True
            MessageBox.Show(Loc.SF("About_DownloadFailed", ex.Message), Loc.S("Title_Error"),
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>ダウンロード進捗の反映（UIスレッドで呼ばれる）</summary>
    Private Sub OnDownloadProgress(percent As Integer)
        ' 閉じた後に届いた通知は無視する
        If IsDisposed Then Return

        If percent = UpdateLogic.ProgressUnknown Then
            ' 全体サイズが不明 → 進捗率を出せないのでマーキー表示のままにする
            prgDownload.Style = ProgressBarStyle.Marquee
            Return
        End If

        prgDownload.Style = ProgressBarStyle.Continuous
        prgDownload.Maximum = 100
        prgDownload.Value = Math.Max(0, Math.Min(percent, 100))
    End Sub

    Private Sub lnkReleasePage_LinkClicked(sender As Object, e As EventArgs) Handles lnkReleasePage.LinkClicked
        UpdateLogic.OpenReleasesPage()
    End Sub

    Private Sub lnkSponsor_LinkClicked(sender As Object, e As EventArgs) Handles lnkSponsor.LinkClicked
        Try
            Process.Start(New ProcessStartInfo(SponsorPageUrl) With {.UseShellExecute = True})
        Catch
            ' ブラウザ起動失敗は無視
        End Try
    End Sub

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("About_FormTitle")
        lblLatestCaption.Text = Loc.S("About_LatestVersionLabel")
        lblLatestVersion.Text = Loc.S("About_Checking")
        btnUpdate.Text = Loc.S("Button_Update")
        lnkSponsor.Text = "❤ " & Loc.S("About_SponsorOnGitHub")
        btnClose.Text = Loc.S("Button_Close")
    End Sub
#End Region

End Class
