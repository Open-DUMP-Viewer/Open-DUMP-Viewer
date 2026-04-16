Imports System.Reflection

''' <summary>
''' 起動時スプラッシュスクリーン
''' アプリアイコン・アプリ名・バージョンを表示する
''' </summary>
Public Class SplashScreen

    Private Sub SplashScreen_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' バージョン情報を設定
        Dim asm = Assembly.GetExecutingAssembly()
        Dim ver = asm.GetName().Version
        lblVersion.Text = $"v{ver.Major}.{ver.Minor}.{ver.Build}"

        ' アプリアイコンを PictureBox に設定
        Try
            Dim exePath = asm.Location
            If String.IsNullOrEmpty(exePath) Then
                exePath = Environment.ProcessPath
            End If
            If Not String.IsNullOrEmpty(exePath) Then
                Dim ico = Icon.ExtractAssociatedIcon(exePath)
                If ico IsNot Nothing Then
                    picLogo.Image = ico.ToBitmap()
                End If
            End If
        Catch
            ' アイコン取得失敗時は何もしない
        End Try
    End Sub

End Class
