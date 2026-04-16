Imports System.Net.Http
Imports System.Reflection
Imports System.Text.Json

''' <summary>
''' 起動時のアップデートチェック。
''' GitHub の最新リリースと現在のバージョンを比較し、
''' 新バージョンがある場合にユーザーに通知する（強制更新なし）。
''' </summary>
Public Class UpdateChecker

    Private Const GitHubApiUrl As String = "https://api.github.com/repos/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/latest"
    Private Const ReleasesPageUrl As String = "https://github.com/Open-DUMP-Viewer/Open-DUMP-Viewer/releases/latest"

    ''' <summary>
    ''' バックグラウンドでアップデートチェックを実行し、新バージョンがあれば通知する。
    ''' UIスレッドをブロックしない。エラー時は無視。
    ''' </summary>
    Public Shared Async Sub CheckOnStartupAsync()
        Try
            Dim currentVersion = GetCurrentVersion()
            If String.IsNullOrEmpty(currentVersion) Then Return

            Dim latestVersion = Await GetLatestVersionAsync()
            If String.IsNullOrEmpty(latestVersion) Then Return

            Dim currentVer As Version = Nothing
            Dim latestVer As Version = Nothing
            If Not Version.TryParse(currentVersion, currentVer) Then Return
            If Not Version.TryParse(latestVersion, latestVer) Then Return

            If latestVer > currentVer Then
                Dim result = MessageBox.Show(
                    Loc.SF("UpdateCheck_NewVersionAvailable", latestVersion, currentVersion) & vbCrLf & vbCrLf &
                    Loc.S("UpdateCheck_OpenReleasePage"),
                    Loc.S("UpdateCheck_Title"),
                    MessageBoxButtons.YesNo, MessageBoxIcon.Information)
                If result = DialogResult.Yes Then
                    Process.Start(New Diagnostics.ProcessStartInfo(ReleasesPageUrl) With {.UseShellExecute = True})
                End If
            End If

        Catch
            ' アップデートチェック失敗は無視（ネットワーク不通等）
        End Try
    End Sub

    Private Shared Function GetCurrentVersion() As String
        Dim asm = Assembly.GetEntryAssembly()
        If asm Is Nothing Then Return Nothing
        Dim ver = asm.GetName().Version
        Return $"{ver.Major}.{ver.Minor}.{ver.Build}"
    End Function

    Private Shared Async Function GetLatestVersionAsync() As Task(Of String)
        Using client As New HttpClient()
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")
            client.Timeout = TimeSpan.FromSeconds(5)

            Dim response = Await client.GetAsync(GitHubApiUrl)
            If Not response.IsSuccessStatusCode Then Return Nothing

            Dim json = Await response.Content.ReadAsStringAsync()
            Using doc = JsonDocument.Parse(json)
                Dim tagProp As JsonElement
                If doc.RootElement.TryGetProperty("tag_name", tagProp) Then
                    Return tagProp.GetString()?.TrimStart("v"c)
                End If
            End Using
        End Using
        Return Nothing
    End Function

End Class
