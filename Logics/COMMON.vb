Public Class COMMON

#Region "ステータスラベル関連"

    ''' <summary>
    ''' ステータスラベル自動リセット用タイマー
    ''' </summary>
    Private Shared _statusResetTimer As Timer

    ''' <summary>
    ''' ToolStripStatusLabelにテキストを設定する
    ''' </summary>
    ''' <param name="text"></param>
    Public Shared Sub Set_StatusLavel(text As String)
        ' 既存のリセットタイマーをキャンセル
        StopStatusResetTimer()
        'ステータスラベルのテキストを更新する
        OraDB_DUMP_Viewer.ToolStripStatusLabel.Text = text
        Application.DoEvents() ' UIの更新を強制
    End Sub

    ''' <summary>
    ''' ToolStripStatusLabelにテキストを設定し、指定秒後にデフォルトに自動リセットする
    ''' </summary>
    ''' <param name="text">表示するテキスト</param>
    ''' <param name="autoResetSeconds">自動リセットまでの秒数 (デフォルト: 5秒)</param>
    Public Shared Sub Set_StatusLavel_AutoReset(text As String, Optional autoResetSeconds As Integer = 5)
        Set_StatusLavel(text)
        StartStatusResetTimer(autoResetSeconds)
    End Sub

    Private Shared Sub StopStatusResetTimer()
        If _statusResetTimer IsNot Nothing Then
            _statusResetTimer.Stop()
            _statusResetTimer.Dispose()
            _statusResetTimer = Nothing
        End If
    End Sub

    Private Shared Sub StartStatusResetTimer(seconds As Integer)
        StopStatusResetTimer()
        _statusResetTimer = New Timer()
        _statusResetTimer.Interval = seconds * 1000
        AddHandler _statusResetTimer.Tick, AddressOf OnStatusResetTimerTick
        _statusResetTimer.Start()
    End Sub

    Private Shared Sub OnStatusResetTimerTick(sender As Object, e As EventArgs)
        StopStatusResetTimer()
        ReSet_StatusLavel()
    End Sub

    ''' <summary>
    ''' ToolStripStatusLabelのテキストをリセットする
    ''' </summary>
    Public Shared Sub ReSet_StatusLavel()
        'ライセンス認証状態に応じてステータスラベルを更新
        Dim holder As String = LICENSE.GetLicenseHolder()
        If String.IsNullOrEmpty(holder) Then
            OraDB_DUMP_Viewer.ToolStripStatusLabel.Text = Loc.S("Status_Unlicensed")
        Else
            OraDB_DUMP_Viewer.ToolStripStatusLabel.Text = Loc.SF("Status_Licensed", holder)
        End If
        Application.DoEvents() ' UIの更新を強制
    End Sub
#End Region

#Region "プログレスバー関連"
    ''' <summary>
    ''' プログレスバー更新処理 (行番号ベース: current/total)
    ''' </summary>
    Public Shared Sub UpdateProgress(current As Long, total As Long, startTime As DateTime)
        Try
            ' 現在の時間と経過時間を取得
            Dim currentTime As DateTime = DateTime.Now
            Dim elapsedTime As TimeSpan = currentTime - startTime

            ' 完了率を計算
            Dim completionPercentage As Double = current / total

            ' 残り時間を計算
            Dim estimatedRemainingTime As TimeSpan = TimeSpan.Zero
            Dim estimatedCompletionTime As DateTime = currentTime

            If completionPercentage > 0 Then
                ' 総予想時間 = 経過時間 / 完了率
                Dim estimatedTotalTime As TimeSpan = TimeSpan.FromTicks(CLng(elapsedTime.Ticks / completionPercentage))
                estimatedRemainingTime = estimatedTotalTime - elapsedTime
                estimatedCompletionTime = currentTime.Add(estimatedRemainingTime)
            End If

            ' 処理速度を計算
            Dim linesPerSecond As Double = If(elapsedTime.TotalSeconds > 0, current / elapsedTime.TotalSeconds, 0)

            ' ステータスラベルに詳細情報を表示
            Dim statusMessage As String = BuildStatusMessage(current, total, elapsedTime,
                                                           estimatedRemainingTime, linesPerSecond,
                                                           estimatedCompletionTime, completionPercentage)

            COMMON.Set_StatusLavel(statusMessage)

            OraDB_DUMP_Viewer.ToolStripProgressBar.Value = current

        Catch ex As Exception
            ' エラーは無視（プログレス表示の失敗でアプリを停止させない）
        End Try
    End Sub

    ''' <summary>
    ''' プログレスバー更新処理 (パーセンテージベース: DLL解析用)
    ''' ファイル位置ベースのパーセンテージ(0-100)と処理行数で更新する。
    ''' 残り時間の推定にもパーセンテージを使用。
    ''' </summary>
    ''' <param name="rowsProcessed">処理済み行数</param>
    ''' <param name="currentTable">現在解析中のテーブル名</param>
    ''' <param name="pct">ファイル位置ベースのパーセンテージ (0-100)</param>
    ''' <param name="startTime">解析開始時刻</param>
    Public Shared Sub UpdateProgress(rowsProcessed As Long, currentTable As String, pct As Integer, startTime As DateTime)
        Try
            Dim elapsed As TimeSpan = DateTime.Now - startTime

            ' 残り時間を推定
            Dim remainingStr As String = ""
            If pct > 0 AndAlso pct < 100 Then
                Dim estimatedTotal As TimeSpan = TimeSpan.FromTicks(CLng(elapsed.Ticks * 100.0 / pct))
                Dim remaining As TimeSpan = estimatedTotal - elapsed
                remainingStr = Loc.SF("Status_Remaining", FormatTimeSpan(remaining))
            End If

            Dim msg As String
            If rowsProcessed = 0 Then
                ' DDLスキャン中（まだレコードが見つかっていない）
                If String.IsNullOrEmpty(currentTable) Then
                    msg = Loc.SF("Status_SearchingTables", pct, FormatTimeSpan(elapsed), remainingStr)
                Else
                    msg = Loc.SF("Status_SearchingTablesWithName", pct, currentTable, FormatTimeSpan(elapsed), remainingStr)
                End If
            Else
                ' レコード読み取り中
                Dim speed As Double = If(elapsed.TotalSeconds > 0, rowsProcessed / elapsed.TotalSeconds, 0)
                msg = Loc.SF("Status_Processing", $"{rowsProcessed:N0}", pct, currentTable, FormatTimeSpan(elapsed), remainingStr, $"{speed:N0}")
            End If

            COMMON.Set_StatusLavel(msg)

            ' プログレスバーを更新
            If pct >= 0 AndAlso pct <= 100 Then
                OraDB_DUMP_Viewer.ToolStripProgressBar.Value = pct
            End If

        Catch ex As Exception
            ' エラーは無視（プログレス表示の失敗でアプリを停止させない）
        End Try
    End Sub

    ''' <summary>
    ''' プログレスバー更新処理 (テーブル一覧取得用)
    ''' </summary>
    ''' <param name="tableCount">発見済みテーブル数</param>
    ''' <param name="currentTable">現在スキャン中のテーブル名</param>
    ''' <param name="pct">ファイル位置ベースのパーセンテージ (0-100)</param>
    ''' <param name="startTime">処理開始時刻</param>
    Public Shared Sub UpdateProgressListTables(tableCount As Long, currentTable As String, pct As Integer, startTime As DateTime)
        Try
            Dim elapsed As TimeSpan = DateTime.Now - startTime

            ' 残り時間を推定
            Dim remainingStr As String = ""
            If pct > 0 AndAlso pct < 100 Then
                Dim estimatedTotal As TimeSpan = TimeSpan.FromTicks(CLng(elapsed.Ticks * 100.0 / pct))
                Dim remaining As TimeSpan = estimatedTotal - elapsed
                remainingStr = Loc.SF("Status_Remaining", FormatTimeSpan(remaining))
            End If

            Dim tableStr As String = If(String.IsNullOrEmpty(currentTable), "", $" | {currentTable}")
            Dim msg = Loc.SF("Status_ListingTables", tableCount, pct, tableStr, FormatTimeSpan(elapsed), remainingStr)

            COMMON.Set_StatusLavel(msg)

            ' プログレスバーを更新
            If pct >= 0 AndAlso pct <= 100 Then
                OraDB_DUMP_Viewer.ToolStripProgressBar.Value = pct
            End If

        Catch ex As Exception
            ' エラーは無視（プログレス表示の失敗でアプリを停止させない）
        End Try
    End Sub

    ''' <summary>
    ''' プログレスバーをパーセンテージモード (0-100%) で初期化する
    ''' </summary>
    Public Shared Sub InitProgressBar()
        SetProgressBarMax(100)
    End Sub

    ''' <summary>
    ''' プログレスバーをリセットする
    ''' </summary>
    Public Shared Sub ResetProgressBar()
        OraDB_DUMP_Viewer.ToolStripProgressBar.Style = ProgressBarStyle.Blocks
        OraDB_DUMP_Viewer.ToolStripProgressBar.Value = 0
        OraDB_DUMP_Viewer.ToolStripProgressBar.Visible = False
    End Sub

    ''' <summary>
    ''' プログレスバーをマーキースタイルに設定する（総行数不明時用）
    ''' </summary>
    Public Shared Sub SetProgressBarMarquee()
        OraDB_DUMP_Viewer.ToolStripProgressBar.Style = ProgressBarStyle.Marquee
        OraDB_DUMP_Viewer.ToolStripProgressBar.Visible = True
    End Sub

    Private Shared Sub SetProgressBarMax(total As Integer)
        OraDB_DUMP_Viewer.ToolStripProgressBar.Style = ProgressBarStyle.Blocks
        OraDB_DUMP_Viewer.ToolStripProgressBar.Maximum = total
        OraDB_DUMP_Viewer.ToolStripProgressBar.Value = 0
        OraDB_DUMP_Viewer.ToolStripProgressBar.Visible = True
    End Sub
#End Region

#Region "ステータスメッセージ構築・補助関数"
    ''' <summary>
    ''' ステータスメッセージを構築
    ''' </summary>
    Private Shared Function BuildStatusMessage(currentLine As Long, totalLines As Long,
                                             elapsedTime As TimeSpan, estimatedRemainingTime As TimeSpan,
                                             linesPerSecond As Double, estimatedCompletionTime As DateTime,
                                             completionPercentage As Double) As String
        Try
            Dim elapsedStr As String = FormatTimeSpan(elapsedTime)
            Dim remainingStr As String = FormatTimeSpan(estimatedRemainingTime)

            ' ステータスメッセージを組み立て
            Dim message As String = Loc.SF("Status_ProcessingFull",
                                          $"{currentLine:N0}", $"{totalLines:N0}", $"{completionPercentage:P1}",
                                          elapsedStr, remainingStr, $"{linesPerSecond:N0}", $"{estimatedCompletionTime:HH:mm:ss}")

            Return message

        Catch ex As Exception
            Return Loc.SF("Status_ProcessingSimple", $"{currentLine:N0}")
        End Try
    End Function

    ''' <summary>
    ''' TimeSpanを読みやすい形式にフォーマット
    ''' </summary>
    Private Shared Function FormatTimeSpan(timeSpan As TimeSpan) As String
        Try
            If timeSpan.TotalDays >= 1 Then
                Return Loc.SF("Time_DayFormat", timeSpan.Days, $"{timeSpan.Hours:D2}:{timeSpan.Minutes:D2}:{timeSpan.Seconds:D2}")
            ElseIf timeSpan.TotalHours >= 1 Then
                Return $"{timeSpan.Hours}:{timeSpan.Minutes:D2}:{timeSpan.Seconds:D2}"
            ElseIf timeSpan.TotalMinutes >= 1 Then
                Return $"{timeSpan.Minutes}:{timeSpan.Seconds:D2}"
            Else
                Return Loc.SF("Time_SecondFormat", timeSpan.Seconds)
            End If
        Catch
            Return Loc.S("Status_Calculating")
        End Try
    End Function
#End Region

End Class
