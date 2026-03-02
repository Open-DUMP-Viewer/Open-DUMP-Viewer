''' <summary>
''' エクスポート進捗ダイアログ
'''
''' BackgroundWorker でエクスポート処理を非同期実行し、
''' 進捗バー・テーブル名・処理行数・経過時間を表示する。
''' キャンセルボタンで処理を中断可能。
'''
''' 使用例:
'''   Using dlg As New ExportProgressDialog()
'''       dlg.RunExport(Sub(worker, args)
'''           ' エクスポート処理
'''           worker.ReportProgress(50, New ExportProgressDialog.ProgressInfo("TABLE1", 500, 1000))
'''       End Sub)
'''   End Using
''' </summary>
Public Class ExportProgressDialog

#Region "フィールド"
    Private WithEvents _worker As New System.ComponentModel.BackgroundWorker()
    Private _startTime As DateTime
    Private _cancelled As Boolean = False
    Private _exportError As Exception = Nothing
    Private _hasReceivedProgress As Boolean = False
    Private WithEvents _elapsedTimer As New Timer()
#End Region

#Region "進捗情報クラス"
    ''' <summary>
    ''' ReportProgress に渡す進捗情報
    ''' </summary>
    Public Class ProgressInfo
        Public Property TableName As String
        Public Property RowsProcessed As Long
        Public Property TotalRows As Long
        ''' <summary>一括エクスポート時: 現在のテーブル番号 (1-based, 0=単一テーブル)</summary>
        Public Property CurrentTableIndex As Integer = 0
        ''' <summary>一括エクスポート時: 全テーブル数</summary>
        Public Property TotalTableCount As Integer = 0

        Public Sub New(tableName As String, rowsProcessed As Long, totalRows As Long)
            Me.TableName = tableName
            Me.RowsProcessed = rowsProcessed
            Me.TotalRows = totalRows
        End Sub

        Public Sub New(tableName As String, rowsProcessed As Long, totalRows As Long,
                       currentTableIndex As Integer, totalTableCount As Integer)
            Me.TableName = tableName
            Me.RowsProcessed = rowsProcessed
            Me.TotalRows = totalRows
            Me.CurrentTableIndex = currentTableIndex
            Me.TotalTableCount = totalTableCount
        End Sub
    End Class
#End Region

#Region "初期化"
    Public Sub New()
        InitializeComponent()
        _worker.WorkerReportsProgress = True
        _worker.WorkerSupportsCancellation = True

        ' 経過時間を1秒ごとに更新するタイマー
        _elapsedTimer.Interval = 1000
    End Sub
#End Region

#Region "公開メソッド"
    ''' <summary>
    ''' エクスポート処理を実行し、完了まで進捗ダイアログを表示
    ''' </summary>
    ''' <param name="exportAction">
    ''' エクスポート処理。引数: (BackgroundWorker, DoWorkEventArgs)
    ''' worker.ReportProgress(pct, ProgressInfo) で進捗を報告
    ''' worker.CancellationPending で中断チェック
    ''' </param>
    ''' <returns>正常完了なら True、キャンセルまたはエラーなら False</returns>
    Public Function RunExport(exportAction As Action(Of System.ComponentModel.BackgroundWorker, System.ComponentModel.DoWorkEventArgs)) As Boolean
        _exportError = Nothing
        _cancelled = False
        _hasReceivedProgress = False

        AddHandler _worker.DoWork, Sub(s, e)
                                       exportAction(DirectCast(s, System.ComponentModel.BackgroundWorker), e)
                                   End Sub

        ' 進捗報告がない場合に備えてマーキースタイルで開始
        prgExport.Style = ProgressBarStyle.Marquee
        lblTable.Text = "エクスポート処理中..."
        lblRows.Text = ""

        _startTime = DateTime.Now
        _elapsedTimer.Start()
        _worker.RunWorkerAsync()

        ' モーダル表示 (処理完了で自動的に閉じる)
        Me.ShowDialog()

        _elapsedTimer.Stop()

        If _exportError IsNot Nothing Then
            MessageBox.Show($"エクスポートエラー: {_exportError.Message}", "エラー",
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return False
        End If

        Return Not _cancelled
    End Function

    ''' <summary>
    ''' キャンセルが要求されているかどうか
    ''' </summary>
    Public ReadOnly Property IsCancelled As Boolean
        Get
            Return _worker.CancellationPending
        End Get
    End Property
#End Region

#Region "イベントハンドラ"
    Private Sub btnCancel_Click(sender As Object, e As EventArgs) Handles btnCancel.Click
        _cancelled = True
        _worker.CancelAsync()
        btnCancel.Enabled = False
        btnCancel.Text = "中断中..."
    End Sub

    Private Sub _elapsedTimer_Tick(sender As Object, e As EventArgs) Handles _elapsedTimer.Tick
        ' 経過時間を1秒ごとに更新 (進捗報告がない場合でも表示を更新)
        Dim elapsed = DateTime.Now - _startTime
        lblElapsed.Text = $"経過時間: {FormatElapsed(elapsed)}"
    End Sub

    Private Sub _worker_ProgressChanged(sender As Object, e As System.ComponentModel.ProgressChangedEventArgs) Handles _worker.ProgressChanged
        ' 初回の進捗報告でマーキーからブロックスタイルに切り替え
        If Not _hasReceivedProgress Then
            _hasReceivedProgress = True
            prgExport.Style = ProgressBarStyle.Blocks
            prgExport.Value = 0
        End If

        ' プログレスバー更新
        If e.ProgressPercentage >= 0 AndAlso e.ProgressPercentage <= 100 Then
            prgExport.Value = e.ProgressPercentage
        End If

        ' 詳細情報更新
        Dim info = TryCast(e.UserState, ProgressInfo)
        If info IsNot Nothing Then
            If info.TotalTableCount > 0 Then
                lblTable.Text = $"テーブル {info.CurrentTableIndex}/{info.TotalTableCount}: {info.TableName}"
            Else
                lblTable.Text = $"テーブル: {info.TableName}"
            End If
            If info.TotalRows > 0 Then
                lblRows.Text = $"処理行数: {info.RowsProcessed:N0} / {info.TotalRows:N0}"
            Else
                lblRows.Text = $"処理行数: {info.RowsProcessed:N0}"
            End If
        End If

        ' 経過時間
        Dim elapsed = DateTime.Now - _startTime
        lblElapsed.Text = $"経過時間: {FormatElapsed(elapsed)}"
    End Sub

    Private Sub _worker_RunWorkerCompleted(sender As Object, e As System.ComponentModel.RunWorkerCompletedEventArgs) Handles _worker.RunWorkerCompleted
        _elapsedTimer.Stop()
        If e.Error IsNot Nothing Then
            _exportError = e.Error
        End If
        If e.Cancelled Then
            _cancelled = True
        End If
        Me.Close()
    End Sub

    Private Sub ExportProgressDialog_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing
        ' 処理中にXボタンで閉じようとした場合はキャンセル扱い
        If _worker.IsBusy Then
            _cancelled = True
            _worker.CancelAsync()
            e.Cancel = True
        End If
    End Sub
#End Region

#Region "ヘルパー"
    Private Shared Function FormatElapsed(ts As TimeSpan) As String
        If ts.TotalHours >= 1 Then
            Return $"{ts.Hours}:{ts.Minutes:D2}:{ts.Seconds:D2}"
        ElseIf ts.TotalMinutes >= 1 Then
            Return $"{ts.Minutes}:{ts.Seconds:D2}"
        Else
            Return $"{ts.Seconds}秒"
        End If
    End Function
#End Region

End Class
