Imports System.IO
Imports System.Net.Http
Imports System.Reflection
Imports System.Text.Json

''' <summary>
''' バージョン情報ダイアログ
''' 現在のバージョンを表示し、GitHubの最新リリースと比較する
''' 新バージョンがある場合はMSIインストーラーをダウンロードして更新できる
''' </summary>
Partial Public Class AboutDialog

    Private Const GitHubApiUrl As String = "https://api.github.com/repos/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases/latest"
    Private Const ReleasesPageUrl As String = "https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases/latest"

    ''' <summary>最新リリースのMSIダウンロードURL（更新ボタン押下時に使用）</summary>
    Private _msiDownloadUrl As String = Nothing

    Public Sub New()
        InitializeComponent()

        ' 現在のバージョン情報を設定
        Dim asm = Assembly.GetExecutingAssembly()
        Dim ver = asm.GetName().Version
        Dim currentVersion = $"{ver.Major}.{ver.Minor}.{ver.Build}"

        lblVersion.Text = $"バージョン: v{currentVersion}"
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
                    lblLatestVersion.Text = "確認できませんでした"
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
                        lblLatestVersion.Text = "確認できませんでした"
                        Return
                    End If

                    ' MSI ダウンロードURLを検索
                    Dim assetsProp As JsonElement
                    If root.TryGetProperty("assets", assetsProp) Then
                        For Each asset In assetsProp.EnumerateArray()
                            Dim nameProp As JsonElement
                            If asset.TryGetProperty("name", nameProp) Then
                                Dim assetName = nameProp.GetString()
                                If assetName IsNot Nothing AndAlso assetName.EndsWith(".msi", StringComparison.OrdinalIgnoreCase) Then
                                    Dim urlProp As JsonElement
                                    If asset.TryGetProperty("browser_download_url", urlProp) Then
                                        _msiDownloadUrl = urlProp.GetString()
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
                            lblLatestVersion.Text = $"v{latestVersion} (新しいバージョンがあります)"
                            lblLatestVersion.ForeColor = Color.OrangeRed

                            ' 更新ボタンを表示（MSI URLがある場合のみ）
                            If _msiDownloadUrl IsNot Nothing Then
                                btnUpdate.Visible = True
                            End If

                            lnkReleasePage.Text = "リリースページを開く"
                            lnkReleasePage.Visible = True
                        Else
                            ' 最新版を使用中
                            lblLatestVersion.Text = $"v{latestVersion} (最新版を利用中)"
                            lblLatestVersion.ForeColor = Color.Green
                        End If
                    Else
                        lblLatestVersion.Text = $"{tagName}"
                    End If
                End Using
            End Using
        Catch
            ' ネットワークエラー・タイムアウト等 → エラーにしない
            lblLatestVersion.Text = "確認できませんでした（オフライン）"
        End Try
    End Sub

    ''' <summary>
    ''' 更新ボタンクリック: MSIをダウンロードしてインストーラーを起動する
    ''' </summary>
    Private Async Sub btnUpdate_Click(sender As Object, e As EventArgs) Handles btnUpdate.Click
        If _msiDownloadUrl Is Nothing Then Return

        ' 確認ダイアログ
        Dim res = MessageBox.Show(
            "最新バージョンのインストーラーをダウンロードして実行します。" & vbCrLf &
            "アプリケーションは自動的に終了します。" & vbCrLf & vbCrLf &
            "続行しますか？",
            "更新の確認", MessageBoxButtons.YesNo, MessageBoxIcon.Question)

        If res <> DialogResult.Yes Then Return

        btnUpdate.Enabled = False
        btnUpdate.Text = "ダウンロード中..."
        prgDownload.Visible = True
        prgDownload.Style = ProgressBarStyle.Marquee

        Try
            ' 一時フォルダにMSIをダウンロード
            Dim tempDir = Path.Combine(Path.GetTempPath(), "OraDBDumpViewer_Update")
            Directory.CreateDirectory(tempDir)
            Dim msiFileName = Path.GetFileName(New Uri(_msiDownloadUrl).LocalPath)
            Dim msiPath = Path.Combine(tempDir, msiFileName)

            Using client As New HttpClient()
                client.DefaultRequestHeaders.UserAgent.ParseAdd("OraDB-DUMP-Viewer")
                client.Timeout = TimeSpan.FromMinutes(5)

                Using response = Await client.GetAsync(_msiDownloadUrl, HttpCompletionOption.ResponseHeadersRead)
                    response.EnsureSuccessStatusCode()

                    Dim totalBytes = response.Content.Headers.ContentLength
                    If totalBytes.HasValue Then
                        prgDownload.Style = ProgressBarStyle.Continuous
                        prgDownload.Maximum = 100
                    End If

                    Using contentStream = Await response.Content.ReadAsStreamAsync()
                        Using fileStream As New FileStream(msiPath, FileMode.Create, FileAccess.Write, FileShare.None)
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
            btnUpdate.Text = "インストーラーを起動中..."

            ' バッチファイルでアプリ終了後にMSIを実行
            LaunchInstallerAndExit(msiPath)

        Catch ex As Exception
            prgDownload.Visible = False
            btnUpdate.Text = "最新バージョンに更新する"
            btnUpdate.Enabled = True
            MessageBox.Show($"ダウンロードに失敗しました: {ex.Message}", "エラー",
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>
    ''' バッチファイルを作成し、アプリ終了後にMSIインストーラーを起動する
    ''' </summary>
    Private Sub LaunchInstallerAndExit(msiPath As String)
        Dim batPath = Path.Combine(Path.GetTempPath(), "OraDBDumpViewer_Update", "update.bat")
        Dim batContent =
            "@echo off" & vbCrLf &
            "echo OraDB DUMP Viewer を更新しています..." & vbCrLf &
            "timeout /t 2 /nobreak >nul" & vbCrLf &
            $"start """" ""{msiPath}""" & vbCrLf &
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

End Class
