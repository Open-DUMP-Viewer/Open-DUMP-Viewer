Imports System.IO
Imports System.Security.Cryptography
Imports System.Text

Public Class MenuStripLogics
    Public Shared Function ダンプファイルDToolStripMenuItem() As String
        Try
            'OpenFileDialogのインスタンスを作成
            Using OpenFileDialog As New OpenFileDialog
                OpenFileDialog.Title = Loc.S("Dialog_OpenDumpTitle")
                OpenFileDialog.Filter = Loc.S("Dialog_DumpFilter")
                OpenFileDialog.FilterIndex = 1
                OpenFileDialog.RestoreDirectory = True
                OpenFileDialog.CheckFileExists = True
                OpenFileDialog.CheckPathExists = True

                '初期フォルダをデスクトップに設定
                OpenFileDialog.InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Desktop)

                'ダイアログを表示
                If OpenFileDialog.ShowDialog() = DialogResult.OK Then
                    Dim dumpFilePath As String = OpenFileDialog.FileName

                    'ファイルパスを返す
                    Return dumpFilePath

                End If

            End Using
        Catch ex As Exception
            MessageBox.Show(Loc.SF("Dialog_FileSelectionError", ex.Message),
                            Loc.S("Title_Error"),
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error)
        End Try

        'キャンセルまたはエラー時は空文字を返す
        Return String.Empty
    End Function

    Public Shared Sub ステータスバーSToolStripMenuItem()
        'ステータスバーの表示・非表示を切り替える
        If Open_DUMP_Viewer.ToolStripStatusLabel.Visible = True Then
            Open_DUMP_Viewer.ToolStripStatusLabel.Visible = False
            Open_DUMP_Viewer.ステータスバーSToolStripMenuItem.Checked = False
        Else
            Open_DUMP_Viewer.ToolStripStatusLabel.Visible = True
            Open_DUMP_Viewer.ステータスバーSToolStripMenuItem.Checked = True
        End If
    End Sub

    ' Use COMMON for shared license logic



    Public Shared Sub ライセンス認証ToolStripMenuItem()
        Try
            Using OpenFileDialog As New OpenFileDialog
                OpenFileDialog.Title = Loc.S("License_FileDialogTitle")
                OpenFileDialog.Filter = Loc.S("License_FileFilter")
                OpenFileDialog.FilterIndex = 1
                OpenFileDialog.RestoreDirectory = True
                OpenFileDialog.CheckFileExists = True
                OpenFileDialog.CheckPathExists = True
                OpenFileDialog.InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Desktop)

                If OpenFileDialog.ShowDialog() = DialogResult.OK Then
                    Dim licPath As String = OpenFileDialog.FileName
                    Dim licenseKey As String = String.Empty
                    Dim expiryDate As DateTime
                    Dim holder As String = String.Empty
                    Dim errMsg As String = String.Empty
                    If Not LICENSE.VerifyLicenseFile(licPath, licenseKey, expiryDate, holder, errMsg) Then
                        MessageBox.Show(errMsg, Loc.S("Title_AuthFailed"), MessageBoxButtons.OK, MessageBoxIcon.Error)
                        Return
                    End If
                    ' 認証成功時はAppDataにそのまま保存
                    Dim appData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OpenDUMPViewer")
                    If Not Directory.Exists(appData) Then Directory.CreateDirectory(appData)
                    Dim statusPath = Path.Combine(appData, "license.status")
                    File.Copy(licPath, statusPath, True)
                    MessageBox.Show(Loc.S("License_Success"), Loc.S("Title_AuthSuccess"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                    ' ステータスバーにHolder名を反映
                    COMMON.ReSet_StatusLavel()
                End If

            End Using
        Catch ex As Exception
            MessageBox.Show(Loc.SF("License_AuthError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub
End Class
