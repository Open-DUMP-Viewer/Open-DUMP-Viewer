''' <summary>
''' 起動時のアップデートチェック。
''' GitHub の最新リリースと現在のバージョンを比較し、
''' 新バージョンがある場合にユーザーに通知する（強制更新なし）。
'''
''' 通知で「はい」を選んだ場合は、バージョン情報ダイアログと同じく
''' インストーラーをダウンロードして更新する（リリースページは開かない）。
''' 実行中のアーキテクチャ向けインストーラーが見つからない場合に限り、
''' 従来どおりリリースページへ誘導する。
''' </summary>
Public Class UpdateChecker

    ''' <summary>
    ''' バックグラウンドでアップデートチェックを実行し、新バージョンがあれば通知する。
    ''' UIスレッドをブロックしない。エラー時は無視。
    ''' </summary>
    Public Shared Async Sub CheckOnStartupAsync()
        Try
            Dim currentVersion = UpdateLogic.GetCurrentVersion()
            If String.IsNullOrEmpty(currentVersion) Then Return

            Dim latest = Await UpdateLogic.GetLatestReleaseAsync()
            If latest Is Nothing OrElse String.IsNullOrEmpty(latest.Version) Then Return

            Dim currentVer As Version = Nothing
            Dim latestVer As Version = Nothing
            If Not Version.TryParse(currentVersion, currentVer) Then Return
            If Not Version.TryParse(latest.Version, latestVer) Then Return
            If latestVer <= currentVer Then Return

            If String.IsNullOrEmpty(latest.InstallerUrl) Then
                ' インストーラーが見つからない場合のみリリースページへ誘導する
                If Confirm(latest.Version, currentVersion, Loc.S("UpdateCheck_OpenReleasePage")) Then
                    UpdateLogic.OpenReleasesPage()
                End If
                Return
            End If

            If Confirm(latest.Version, currentVersion, Loc.S("UpdateCheck_DownloadAndUpdate")) Then
                DownloadAndInstall(latest.InstallerUrl)
            End If

        Catch
            ' アップデートチェック失敗は無視（ネットワーク不通等）
        End Try
    End Sub

    ''' <summary>新バージョンを通知し、続行してよいかを尋ねる</summary>
    Private Shared Function Confirm(latestVersion As String, currentVersion As String, question As String) As Boolean
        Dim result = MessageBox.Show(
            Loc.SF("UpdateCheck_NewVersionAvailable", latestVersion, currentVersion) & vbCrLf & vbCrLf & question,
            Loc.S("UpdateCheck_Title"),
            MessageBoxButtons.YesNo, MessageBoxIcon.Information)
        Return result = DialogResult.Yes
    End Function

    ''' <summary>
    ''' インストーラーをダウンロードし、完了したらアプリを終了してインストーラーを起動する。
    ''' キャンセル時は何もせず戻る。
    ''' </summary>
    Private Shared Sub DownloadAndInstall(installerUrl As String)
        Dim installerPath As String = Nothing

        Using dlg As New UpdateDownloadDialog(installerUrl)
            If dlg.ShowDialog() <> DialogResult.OK Then
                If dlg.DownloadError IsNot Nothing Then ShowFailure(dlg.DownloadError)
                Return
            End If
            installerPath = dlg.InstallerPath
        End Using

        Try
            UpdateLogic.LaunchInstallerAndExit(installerPath)
        Catch ex As Exception
            ShowFailure(ex)
        End Try
    End Sub

    Private Shared Sub ShowFailure(ex As Exception)
        MessageBox.Show(Loc.SF("Update_Failed", ex.Message), Loc.S("Title_Error"),
                        MessageBoxButtons.OK, MessageBoxIcon.Error)
    End Sub

End Class
