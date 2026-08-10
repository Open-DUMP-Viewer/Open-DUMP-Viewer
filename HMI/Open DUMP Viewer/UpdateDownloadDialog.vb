Imports System.Threading

''' <summary>
''' アップデート用インストーラーのダウンロード進捗ダイアログ。
'''
''' 起動時のアップデート通知で「はい」を選んだときに表示する。
''' バージョン情報ダイアログはダイアログ内に進捗バーを持つため、こちらは使わない。
'''
''' 使用例:
'''   Using dlg As New UpdateDownloadDialog(installerUrl)
'''       If dlg.ShowDialog() = DialogResult.OK Then
'''           UpdateLogic.LaunchInstallerAndExit(dlg.InstallerPath)
'''       End If
'''   End Using
''' </summary>
Public Class UpdateDownloadDialog
    Implements ILocalizable

#Region "フィールド"
    Private ReadOnly _installerUrl As String
    Private ReadOnly _cts As New CancellationTokenSource()

    ''' <summary>ダウンロードが終了した（成功・キャンセル・失敗のいずれか）かどうか</summary>
    Private _finished As Boolean = False

    Private _installerPath As String = Nothing
    Private _downloadError As Exception = Nothing
#End Region

#Region "公開プロパティ"
    ''' <summary>ダウンロードしたインストーラーのフルパス（DialogResult.OK のときのみ有効）</summary>
    Public ReadOnly Property InstallerPath As String
        Get
            Return _installerPath
        End Get
    End Property

    ''' <summary>ダウンロードに失敗した場合の例外（成功・キャンセル時は Nothing）</summary>
    Public ReadOnly Property DownloadError As Exception
        Get
            Return _downloadError
        End Get
    End Property
#End Region

#Region "初期化"
    Public Sub New(installerUrl As String)
        InitializeComponent()
        ApplyLocalization()
        _installerUrl = installerUrl
    End Sub
#End Region

#Region "イベントハンドラ"
    ''' <summary>表示直後にダウンロードを開始し、終了したらダイアログを閉じる</summary>
    Private Async Sub UpdateDownloadDialog_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        Dim progress As New Progress(Of Integer)(AddressOf OnProgressChanged)

        Try
            _installerPath = Await UpdateLogic.DownloadInstallerAsync(_installerUrl, progress, _cts.Token)
            _finished = True
            lblStatus.Text = Loc.S("About_LaunchingInstaller")
            DialogResult = DialogResult.OK

        Catch ex As OperationCanceledException When _cts.IsCancellationRequested
            ' 利用者によるキャンセル（タイムアウトは下の Catch で失敗として扱う）
            _finished = True
            DialogResult = DialogResult.Cancel

        Catch ex As Exception
            _downloadError = ex
            _finished = True
            DialogResult = DialogResult.Abort
        End Try
    End Sub

    Private Sub btnCancel_Click(sender As Object, e As EventArgs) Handles btnCancel.Click
        RequestCancel()
    End Sub

    Private Sub UpdateDownloadDialog_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing
        ' ダウンロード中に閉じようとした場合はキャンセルを要求し、終了を待ってから閉じる
        If Not _finished Then
            RequestCancel()
            e.Cancel = True
        End If
    End Sub

    Private Sub UpdateDownloadDialog_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        ' 閉じる時点でダウンロードは終了している (FormClosing で待たせている)
        _cts.Dispose()
    End Sub
#End Region

#Region "ヘルパー"
    ''' <summary>ダウンロード進捗の反映（UIスレッドで呼ばれる）</summary>
    Private Sub OnProgressChanged(percent As Integer)
        ' 閉じた後に届いた通知は無視する
        If IsDisposed OrElse _finished Then Return

        If percent = UpdateLogic.ProgressUnknown Then
            ' 全体サイズが不明 → 進捗率を出せないのでマーキー表示のままにする
            prgDownload.Style = ProgressBarStyle.Marquee
            Return
        End If

        prgDownload.Style = ProgressBarStyle.Continuous
        prgDownload.Maximum = 100
        prgDownload.Value = Math.Max(0, Math.Min(percent, 100))

        If Not _cts.IsCancellationRequested Then
            lblStatus.Text = Loc.SF("UpdateDownload_Progress", prgDownload.Value)
        End If
    End Sub

    ''' <summary>ダウンロードの中断を要求する</summary>
    Private Sub RequestCancel()
        If _finished OrElse _cts.IsCancellationRequested Then Return

        _cts.Cancel()
        btnCancel.Enabled = False
        lblStatus.Text = Loc.S("UpdateDownload_Cancelling")
    End Sub
#End Region

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("UpdateDownload_FormTitle")
        lblStatus.Text = Loc.S("UpdateDownload_Downloading")
        btnCancel.Text = Loc.S("Button_Cancel")
    End Sub
#End Region

End Class
