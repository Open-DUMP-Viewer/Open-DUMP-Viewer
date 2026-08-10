Imports System.IO
Imports System.Net.Http
Imports System.Reflection
Imports System.Text.Json
Imports System.Threading

''' <summary>
''' アップデート共通ロジック。
'''
''' GitHub の最新リリース取得・インストーラーのダウンロード・インストーラー起動を提供する。
''' バージョン情報ダイアログ (AboutDialog) と起動時のアップデート通知 (UpdateChecker) の
''' 双方がこのクラスを使い、更新の手順を一本化する。
''' </summary>
Public Class UpdateLogic

#Region "定数"
    Private Const GitHubApiUrl As String = "https://api.github.com/repos/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/latest"

    ''' <summary>リリースページ（インストーラー資産が見つからない場合の誘導先）</summary>
    Public Const ReleasesPageUrl As String = "https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/latest"

    ''' <summary>ダウンロードしたインストーラーを置く一時フォルダ名</summary>
    Private Const UpdateFolderName As String = "OpenDumpViewer_Update"

    ''' <summary>全体サイズが不明で進捗率を出せないことを示す進捗値</summary>
    Public Const ProgressUnknown As Integer = -1
#End Region

#Region "リリース情報"
    ''' <summary>GitHub の最新リリース情報</summary>
    Public Class ReleaseInfo
        ''' <summary>タグ名（例: "v4.4.1"）</summary>
        Public Property TagName As String

        ''' <summary>タグ名から先頭の "v" を除いたバージョン文字列（例: "4.4.1"）</summary>
        Public Property Version As String

        ''' <summary>実行中のアーキテクチャに合致するインストーラーのURL（見つからない場合は Nothing）</summary>
        Public Property InstallerUrl As String
    End Class
#End Region

#Region "バージョン確認"
    ''' <summary>現在のアプリケーションバージョン (Major.Minor.Build)</summary>
    Public Shared Function GetCurrentVersion() As String
        Dim ver = Assembly.GetExecutingAssembly().GetName().Version
        If ver Is Nothing Then Return Nothing
        Return $"{ver.Major}.{ver.Minor}.{ver.Build}"
    End Function

    ''' <summary>
    ''' GitHub の最新リリースを取得する。
    ''' 応答が得られない・不正な場合は Nothing を返す（通信エラーは呼び出し側へ送出する）。
    ''' </summary>
    Public Shared Async Function GetLatestReleaseAsync() As Task(Of ReleaseInfo)
        Using client As New HttpClient()
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")
            client.Timeout = TimeSpan.FromSeconds(5)

            Dim response = Await client.GetAsync(GitHubApiUrl)
            If Not response.IsSuccessStatusCode Then Return Nothing

            Dim json = Await response.Content.ReadAsStringAsync()
            Using doc = JsonDocument.Parse(json)
                Dim root = doc.RootElement

                ' tag_name を取得
                Dim tagProp As JsonElement
                Dim tagName As String = Nothing
                If root.TryGetProperty("tag_name", tagProp) Then
                    tagName = tagProp.GetString()
                End If
                If String.IsNullOrEmpty(tagName) Then Return Nothing

                ' "v0.1.1" → "0.1.1"
                Return New ReleaseInfo With {
                    .TagName = tagName,
                    .Version = tagName.TrimStart("v"c),
                    .InstallerUrl = FindInstallerUrl(root)
                }
            End Using
        End Using
    End Function

    ''' <summary>
    ''' 実行中のアーキテクチャ (x64 / arm64) に合致するインストーラー資産のURLを探す。
    ''' </summary>
    Private Shared Function FindInstallerUrl(root As JsonElement) As String
        Dim archSuffix = If(Runtime.InteropServices.RuntimeInformation.OSArchitecture = Runtime.InteropServices.Architecture.Arm64,
                            "_arm64.exe", "_x64.exe")

        Dim assetsProp As JsonElement
        If Not root.TryGetProperty("assets", assetsProp) Then Return Nothing

        For Each asset In assetsProp.EnumerateArray()
            Dim nameProp As JsonElement
            If Not asset.TryGetProperty("name", nameProp) Then Continue For

            Dim assetName = nameProp.GetString()
            If assetName Is Nothing Then Continue For
            If Not assetName.Contains("installer", StringComparison.OrdinalIgnoreCase) Then Continue For
            If Not assetName.EndsWith(archSuffix, StringComparison.OrdinalIgnoreCase) Then Continue For

            Dim urlProp As JsonElement
            If Not asset.TryGetProperty("browser_download_url", urlProp) Then Return Nothing

            Dim url = urlProp.GetString()
            Return If(IsTrustedDownloadUrl(url), url, Nothing)
        Next

        Return Nothing
    End Function

    ''' <summary>
    ''' ダウンロード元として信頼できるURLかどうか。
    ''' リリース資産は GitHub 上にあるため、https かつ GitHub のホストに限定する。
    ''' </summary>
    Private Shared Function IsTrustedDownloadUrl(url As String) As Boolean
        If String.IsNullOrEmpty(url) Then Return False

        Dim uri As Uri = Nothing
        If Not Uri.TryCreate(url, UriKind.Absolute, uri) Then Return False
        If uri.Scheme <> Uri.UriSchemeHttps Then Return False

        Return uri.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase) OrElse
               uri.Host.EndsWith(".github.com", StringComparison.OrdinalIgnoreCase) OrElse
               uri.Host.EndsWith(".githubusercontent.com", StringComparison.OrdinalIgnoreCase)
    End Function
#End Region

#Region "ダウンロード・インストーラー起動"
    ''' <summary>
    ''' インストーラーを一時フォルダへダウンロードする。
    ''' </summary>
    ''' <param name="installerUrl">インストーラーのURL（GitHub のリリース資産）</param>
    ''' <param name="progress">進捗 (0-100)。全体サイズが不明な場合は <see cref="ProgressUnknown"/> を通知する</param>
    ''' <param name="cancellationToken">中断トークン</param>
    ''' <returns>ダウンロードしたインストーラーのフルパス</returns>
    Public Shared Async Function DownloadInstallerAsync(installerUrl As String,
                                                        progress As IProgress(Of Integer),
                                                        cancellationToken As CancellationToken) As Task(Of String)
        If Not IsTrustedDownloadUrl(installerUrl) Then
            Throw New InvalidOperationException("Invalid installer URL")
        End If

        ' 一時フォルダにインストーラーをダウンロード
        Dim tempDir = Path.Combine(Path.GetTempPath(), UpdateFolderName)
        Directory.CreateDirectory(tempDir)

        Dim installerFileName = Path.GetFileName(New Uri(installerUrl).LocalPath)
        If String.IsNullOrEmpty(installerFileName) Then
            Throw New InvalidOperationException("Invalid installer file name")
        End If
        Dim installerPath = Path.Combine(tempDir, installerFileName)

        Try
            Using client As New HttpClient()
                client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")
                client.Timeout = TimeSpan.FromMinutes(5)

                Using response = Await client.GetAsync(installerUrl, HttpCompletionOption.ResponseHeadersRead, cancellationToken)
                    response.EnsureSuccessStatusCode()

                    Dim totalBytes = response.Content.Headers.ContentLength
                    Dim hasTotal = totalBytes.HasValue AndAlso totalBytes.Value > 0
                    progress?.Report(If(hasTotal, 0, ProgressUnknown))

                    Using contentStream = Await response.Content.ReadAsStreamAsync(cancellationToken)
                        Using fileStream As New FileStream(installerPath, FileMode.Create, FileAccess.Write, FileShare.None)
                            Dim buffer(8191) As Byte
                            Dim totalRead As Long = 0
                            Dim lastPercent As Integer = -1
                            Dim bytesRead As Integer

                            Do
                                bytesRead = Await contentStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken)
                                If bytesRead = 0 Then Exit Do

                                Await fileStream.WriteAsync(buffer, 0, bytesRead, cancellationToken)
                                totalRead += bytesRead

                                If hasTotal Then
                                    ' 応答が Content-Length を超えても進捗が 100 を超えないようにする
                                    Dim percent = CInt(Math.Min(totalRead * 100L \ totalBytes.Value, 100L))
                                    If percent <> lastPercent Then
                                        lastPercent = percent
                                        progress?.Report(percent)
                                    End If
                                End If
                            Loop
                        End Using
                    End Using
                End Using
            End Using

        Catch
            ' 中断・失敗時に中途半端なファイルを残さない
            DeleteQuietly(installerPath)
            Throw
        End Try

        Return installerPath
    End Function

    ''' <summary>ファイルを削除する（削除できなくても無視する）</summary>
    Private Shared Sub DeleteQuietly(filePath As String)
        Try
            If File.Exists(filePath) Then File.Delete(filePath)
        Catch
            ' 一時ファイルの削除失敗は無視
        End Try
    End Sub

    ''' <summary>
    ''' バッチファイルを作成し、アプリ終了後にインストーラーを起動する。
    ''' </summary>
    Public Shared Sub LaunchInstallerAndExit(installerPath As String)
        ' インストーラーパスを検証（パストラバーサル・コマンドインジェクション防止）
        Dim fullPath = Path.GetFullPath(installerPath)
        If Not fullPath.StartsWith(Path.GetTempPath(), StringComparison.OrdinalIgnoreCase) Then
            Throw New InvalidOperationException("Invalid installer path")
        End If
        If Not File.Exists(fullPath) Then
            Throw New FileNotFoundException("Installer not found", fullPath)
        End If

        Dim batPath = Path.Combine(Path.GetTempPath(), UpdateFolderName, "update.bat")
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

    ''' <summary>リリースページをブラウザで開く</summary>
    Public Shared Sub OpenReleasesPage()
        Try
            Process.Start(New ProcessStartInfo(ReleasesPageUrl) With {.UseShellExecute = True})
        Catch
            ' ブラウザ起動失敗は無視
        End Try
    End Sub
#End Region

End Class
