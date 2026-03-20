Imports System.IO
Imports System.Net.Http
Imports System.Reflection
Imports System.Text.Json

''' <summary>
''' バージョン情報ダイアログ
''' 現在のバージョンを表示し、GitHubの最新リリースと比較する
''' 新バージョンがある場合はインストーラーをダウンロードして更新できる
''' </summary>
Partial Public Class AboutDialog
    Implements ILocalizable

    Private Const GitHubApiUrl As String = "https://api.github.com/repos/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases/latest"
    Private Const ReleasesPageUrl As String = "https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases/latest"

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

        ' 最新バージョンを非同期で確認
        CheckLatestVersionAsync(currentVersion)
    End Sub

    ''' <summary>
    ''' GitHubの最新リリースを非同期で確認する
    ''' ネットワークエラー時はエラー表示せず、確認不可であることを表示する
    ''' </summary>
    Private Async Sub CheckLatestVersionAsync(currentVersion As String)
        Try
            Using client As New HttpClient()
                client.DefaultRequestHeaders.UserAgent.ParseAdd("OraDB-DUMP-Viewer")
                client.Timeout = TimeSpan.FromSeconds(5)

                Dim response = Await client.GetAsync(GitHubApiUrl)
                If Not response.IsSuccessStatusCode Then
                    lblLatestVersion.Text = Loc.S("About_CheckFailed")
                    Return
                End If

                Dim json = Await response.Content.ReadAsStringAsync()
                Using doc = JsonDocument.Parse(json)
                    Dim root = doc.RootElement

                    ' tag_name を取得
                    Dim tagProp As JsonElement
                    Dim tagName As String = ""
                    If root.TryGetProperty("tag_name", tagProp) Then
                        tagName = tagProp.GetString()
                    End If

                    If String.IsNullOrEmpty(tagName) Then
                        lblLatestVersion.Text = Loc.S("About_CheckFailed")
                        Return
                    End If

                    ' インストーラーダウンロードURLを検索 (実行中のアーキテクチャに合致するものを選択)
                    Dim archSuffix = If(Runtime.InteropServices.RuntimeInformation.OSArchitecture = Runtime.InteropServices.Architecture.Arm64,
                                        "_arm64.exe", "_x64.exe")
                    Dim assetsProp As JsonElement
                    If root.TryGetProperty("assets", assetsProp) Then
                        For Each asset In assetsProp.EnumerateArray()
                            Dim nameProp As JsonElement
                            If asset.TryGetProperty("name", nameProp) Then
                                Dim assetName = nameProp.GetString()
                                If assetName IsNot Nothing AndAlso
                                   assetName.Contains("installer", StringComparison.OrdinalIgnoreCase) AndAlso
                                   assetName.EndsWith(archSuffix, StringComparison.OrdinalIgnoreCase) Then
                                    Dim urlProp As JsonElement
                                    If asset.TryGetProperty("browser_download_url", urlProp) Then
                                        _installerDownloadUrl = urlProp.GetString()
                                    End If
                                    Exit For
                                End If
                            End If
                        Next
                    End If

                    ' "v0.1.1" → "0.1.1"
                    Dim latestVersion = tagName.TrimStart("v"c)

                    ' バージョン比較
                    Dim currentVer As Version = Nothing
                    Dim latestVer As Version = Nothing
                    Dim canParseCurrent = Version.TryParse(currentVersion, currentVer)
                    Dim canParseLatest = Version.TryParse(latestVersion, latestVer)

                    If canParseCurrent AndAlso canParseLatest Then
                        If latestVer > currentVer Then
                            ' 新しいバージョンがある
                            lblLatestVersion.Text = Loc.SF("About_NewVersionAvailable", latestVersion)
                            lblLatestVersion.ForeColor = Color.OrangeRed

                            ' 更新ボタンを表示（インストーラーURLがある場合のみ）
                            If _installerDownloadUrl IsNot Nothing Then
                                btnUpdate.Visible = True
                            End If

                            lnkReleasePage.Text = Loc.S("About_OpenReleasePage")
                            lnkReleasePage.Visible = True
                        Else
                            ' 最新版を使用中
                            lblLatestVersion.Text = Loc.SF("About_LatestInUse", latestVersion)
                            lblLatestVersion.ForeColor = Color.Green
                        End If
                    Else
                        lblLatestVersion.Text = $"{tagName}"
                    End If
                End Using
            End Using
        Catch
            ' ネットワークエラー・タイムアウト等 → エラーにしない
            lblLatestVersion.Text = Loc.S("About_CheckFailedOffline")
        End Try
    End Sub

    ''' <summary>
    ''' 更新ボタンクリック: MSIをダウンロードしてインストーラーを起動する
    ''' </summary>
    Private Async Sub btnUpdate_Click(sender As Object, e As EventArgs) Handles btnUpdate.Click
        If _installerDownloadUrl Is Nothing Then Return

        ' 確認ダイアログ
        Dim res = MessageBox.Show(
            Loc.S("About_UpdateConfirmMessage"),
            Loc.S("About_UpdateConfirmTitle"), MessageBoxButtons.YesNo, MessageBoxIcon.Question)

        If res <> DialogResult.Yes Then Return

        btnUpdate.Enabled = False
        btnUpdate.Text = Loc.S("About_Downloading")
        prgDownload.Visible = True
        prgDownload.Style = ProgressBarStyle.Marquee

        Try
            ' 一時フォルダにインストーラーをダウンロード
            Dim tempDir = Path.Combine(Path.GetTempPath(), "OraDBDumpViewer_Update")
            Directory.CreateDirectory(tempDir)
            Dim installerFileName = Path.GetFileName(New Uri(_installerDownloadUrl).LocalPath)
            Dim installerPath = Path.Combine(tempDir, installerFileName)

            Using client As New HttpClient()
                client.DefaultRequestHeaders.UserAgent.ParseAdd("OraDB-DUMP-Viewer")
                client.Timeout = TimeSpan.FromMinutes(5)

                Using response = Await client.GetAsync(_installerDownloadUrl, HttpCompletionOption.ResponseHeadersRead)
                    response.EnsureSuccessStatusCode()

                    Dim totalBytes = response.Content.Headers.ContentLength
                    If totalBytes.HasValue Then
                        prgDownload.Style = ProgressBarStyle.Continuous
                        prgDownload.Maximum = 100
                    End If

                    Using contentStream = Await response.Content.ReadAsStreamAsync()
                        Using fileStream As New FileStream(installerPath, FileMode.Create, FileAccess.Write, FileShare.None)
                            Dim buffer(8191) As Byte
                            Dim totalRead As Long = 0
                            Dim bytesRead As Integer

                            Do
                                bytesRead = Await contentStream.ReadAsync(buffer, 0, buffer.Length)
                                If bytesRead = 0 Then Exit Do
                                Await fileStream.WriteAsync(buffer, 0, bytesRead)
                                totalRead += bytesRead

                                If totalBytes.HasValue AndAlso totalBytes.Value > 0 Then
                                    prgDownload.Value = CInt(totalRead * 100 \ totalBytes.Value)
                                End If
                            Loop
                        End Using
                    End Using
                End Using
            End Using

            prgDownload.Value = 100
            btnUpdate.Text = Loc.S("About_LaunchingInstaller")

            ' バッチファイルでアプリ終了後にインストーラーを実行
            LaunchInstallerAndExit(installerPath)

        Catch ex As Exception
            prgDownload.Visible = False
            btnUpdate.Text = Loc.S("Button_Update")
            btnUpdate.Enabled = True
            MessageBox.Show(Loc.SF("About_DownloadFailed", ex.Message), Loc.S("Title_Error"),
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>
    ''' バッチファイルを作成し、アプリ終了後にインストーラーを起動する
    ''' </summary>
    Private Sub LaunchInstallerAndExit(installerPath As String)
        ' インストーラーパスを検証（パストラバーサル・コマンドインジェクション防止）
        Dim fullPath = Path.GetFullPath(installerPath)
        If Not fullPath.StartsWith(Path.GetTempPath(), StringComparison.OrdinalIgnoreCase) Then
            Throw New InvalidOperationException("Invalid installer path")
        End If
        If Not File.Exists(fullPath) Then
            Throw New FileNotFoundException("Installer not found", fullPath)
        End If

        Dim batPath = Path.Combine(Path.GetTempPath(), "OraDBDumpViewer_Update", "update.bat")
        Dim escapedPath = fullPath.Replace("^", "^^").Replace("&", "^&").Replace("|", "^|").
                                   Replace("<", "^<").Replace(">", "^>").Replace("%", "%%")
        Dim batContent =
            "@echo off" & vbCrLf &
            Loc.S("About_UpdateBatchMessage") & vbCrLf &
            "timeout /t 2 /nobreak >nul" & vbCrLf &
            $"start """" ""{escapedPath}""" & vbCrLf &
            "exit"

        File.WriteAllText(batPath, batContent, New System.Text.UTF8Encoding(False))

        Dim psi As New ProcessStartInfo()
        psi.FileName = batPath
        psi.CreateNoWindow = True
        psi.WindowStyle = ProcessWindowStyle.Hidden
        Process.Start(psi)

        ' アプリケーションを終了
        Application.Exit()
    End Sub

    Private Sub lnkReleasePage_LinkClicked(sender As Object, e As EventArgs) Handles lnkReleasePage.LinkClicked
        Try
            Process.Start(New ProcessStartInfo(ReleasesPageUrl) With {.UseShellExecute = True})
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
        btnClose.Text = Loc.S("Button_Close")
    End Sub
#End Region

End Class
