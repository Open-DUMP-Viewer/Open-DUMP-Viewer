Imports System.IO
Imports System.Text

Public Class OraDB_DUMP_Viewer

#Region "フォームロード・初期化"
    ''' <summary>
    ''' フォームロードイベント
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub OraDB_DUMP_Viewer_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        '初期化処理をここに記述

        'ステータスラベルのテキストをリセットする
        COMMON.ReSet_StatusLavel()
        'プログレスバーをリセットする
        COMMON.ResetProgressBar()

        '起動時ライセンスチェック（RSA署名方式）
        '認証が完了するまでアプリケーションを使用不可にする
        If Not CheckAndActivateLicense() Then
            Application.Exit()
            Return
        End If

        'ステータスラベルにライセンス保有者名を反映
        COMMON.ReSet_StatusLavel()

    End Sub
#End Region

#Region "ライセンス認証"
    ''' <summary>
    ''' ライセンスを検証し、未認証の場合はユーザーに認証を促す。
    ''' 認証が完了するまでリトライし、キャンセル時は False を返す。
    ''' </summary>
    ''' <returns>認証成功なら True、アプリ終了すべき場合は False</returns>
    Private Function CheckAndActivateLicense() As Boolean
        Try
            Dim appData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OraDBDUMPViewer")
            Dim statusPath = Path.Combine(appData, "license.status")

            ' 既にライセンスが有効ならすぐに通過
            If File.Exists(statusPath) Then
                Dim licenseKey As String = String.Empty
                Dim expiryDate As DateTime
                Dim holder As String = String.Empty
                Dim errMsg As String = String.Empty
                If LICENSE.VerifyLicenseFile(statusPath, licenseKey, expiryDate, holder, errMsg) Then
                    Return True
                End If
            End If

            ' ライセンスが無効または存在しない → 認証ループ
            Do
                Dim msg As String
                If Not File.Exists(statusPath) Then
                    msg = "ライセンスが登録されていません。" & vbCrLf & vbCrLf &
                          "ライセンスの取得は下記サイトから行えます。" & vbCrLf &
                          "https://www.odv.dev/" & vbCrLf & vbCrLf &
                          "ライセンスファイル (.lic.json) を選択して認証しますか？"
                Else
                    Dim errMsg As String = String.Empty
                    Dim dummy1 As String = String.Empty
                    Dim dummy2 As DateTime
                    Dim dummy3 As String = String.Empty
                    LICENSE.VerifyLicenseFile(statusPath, dummy1, dummy2, dummy3, errMsg)
                    msg = "ライセンス検証に失敗しました: " & errMsg & vbCrLf & vbCrLf &
                          "新しいライセンスファイルを選択して認証しますか？"
                End If

                Dim res = MessageBox.Show(msg, "ライセンス認証が必要です",
                                          MessageBoxButtons.YesNo, MessageBoxIcon.Warning)

                If res = DialogResult.Yes Then
                    MenuStripLogics.ライセンス認証ToolStripMenuItem()

                    ' 認証成功したか再確認
                    If File.Exists(statusPath) Then
                        Dim licenseKey As String = String.Empty
                        Dim expiryDate As DateTime
                        Dim holder As String = String.Empty
                        Dim errMsg As String = String.Empty
                        If LICENSE.VerifyLicenseFile(statusPath, licenseKey, expiryDate, holder, errMsg) Then
                            Return True
                        End If
                    End If
                    ' 認証失敗 → ループ継続
                Else
                    ' 「いいえ」を選択 → アプリ終了
                    Return False
                End If
            Loop

        Catch ex As Exception
            MessageBox.Show("ライセンスチェック中にエラーが発生しました: " & ex.Message, "エラー",
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return False
        End Try
    End Function
#End Region

#Region "メニューイベント: ダンプファイル"
    ''' <summary>
    ''' ToolStripMenuItem「ダンプファイル(D)」クリックイベント
    ''' クリックされると、ダンプファイルのパスを取得する
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub ダンプファイルDToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ダンプファイルDToolStripMenuItem.Click
        Dim filePath As String = String.Empty

        'ステータスラベルのテキストを更新する
        COMMON.Set_StatusLavel("ダンプファイルのパスを選択してください...")

        'ダンプファイルのパスを取得する
        filePath = MenuStripLogics.ダンプファイルDToolStripMenuItem()

        If filePath = String.Empty Then
            'キャンセルされた場合、ステータスラベルのテキストをリセットする
            COMMON.ReSet_StatusLavel()
            Return
        End If

        Dim childForm As New Workspace(filePath, "")
        childForm.MdiParent = Me   ' 親フォームを指定
        childForm.Show()

        'ステータスラベルのテキストをリセットする
        COMMON.ReSet_StatusLavel()
    End Sub
#End Region

#Region "メニューイベント: ステータスバー・エクスポート・ライセンス認証"
    ''' <summary>
    ''' ToolStripMenuItem「ステータスバー(S)」クリックイベント
    ''' クリックされると、ステータスバーの表示/非表示を切り替える
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub ステータスバーSToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ステータスバーSToolStripMenuItem.Click
        'ステータスバーの表示/非表示を切り替える
        MenuStripLogics.ステータスバーSToolStripMenuItem()
    End Sub

    ''' <summary>
    ''' ToolStripMenuItem「エクスポート」クリックイベント
    ''' クリックされると、エクスポートツールバーの表示/非表示を切り替える
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub エクスポートToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles エクスポートToolStripMenuItem.Click
        'エクスポートツールバーの表示/非表示を切り替える
        ToolExport.Visible = Not ToolExport.Visible
        エクスポートToolStripMenuItem.Checked = ToolExport.Visible
    End Sub

    ''' <summary>
    ''' ToolStripMenuItem「ライセンス認証(L)」クリックイベント
    ''' クリックされると、ヘルプを表示する
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub ライセンス認証ToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ライセンス認証ToolStripMenuItem.Click

        ' ライセンス認証ロジックを呼び出し
        MenuStripLogics.ライセンス認証ToolStripMenuItem()

    End Sub
#End Region

#Region "メニューイベント: ウィンドウ操作"
    ''' <summary>
    ''' ToolStripMenuItem「重ねて表示(C)」クリックイベント
    ''' クリックされると、MDI子ウィンドウを重ねて表示する
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub 重ねて表示CToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 重ねて表示CToolStripMenuItem.Click
        'MDI子ウィンドウを重ねて表示する
        Me.LayoutMdi(MdiLayout.Cascade)
    End Sub

    ''' <summary>
    ''' ToolStripMenuItem「並べて表示(T)」クリックイベント
    ''' クリックされると、MDI子ウィンドウを並べて表示する
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub 並べて表示TToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 並べて表示TToolStripMenuItem.Click
        'MDI子ウィンドウを並べて表示する
        Me.LayoutMdi(MdiLayout.TileVertical)
    End Sub

    ''' <summary>
    ''' ToolStripMenuItem「アイコンの整列(I)」クリックイベント
    ''' クリックされると、最小化されたMDI子ウィンドウのアイコンを整列する
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    Private Sub アイコンの整列IToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles アイコンの整列IToolStripMenuItem.Click
        'MDI子ウィンドウのアイコンを整列する
        Me.LayoutMdi(MdiLayout.ArrangeIcons)
    End Sub
#End Region

#Region "メニューイベント: テーブルプロパティ"
    ''' <summary>
    ''' テーブルプロパティを表示する共通処理
    ''' アクティブなWorkspaceフォームの選択テーブルのプロパティを表示
    ''' </summary>
    Private Sub ShowTableProperty()
        Dim activeChild = TryCast(Me.ActiveMdiChild, Workspace)
        If activeChild Is Nothing Then
            MessageBox.Show("ワークスペースが開かれていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        activeChild.ShowTableProperty()
    End Sub

    Private Sub プロパティPToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles プロパティPToolStripMenuItem.Click
        ShowTableProperty()
    End Sub

    Private Sub ToolStripButton2_Click(sender As Object, e As EventArgs) Handles tolTablPproperty.Click
        ShowTableProperty()
    End Sub
#End Region

#Region "メニューイベント: エラー報告"
    Private Sub エラー報告RToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles エラー報告RToolStripMenuItem.Click
        Dim dlg As New ErrorReportDialog()
        dlg.ShowDialog(Me)
    End Sub
#End Region

#Region "メニューイベント: バージョン情報"
    Private Sub バージョン情報AToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles バージョン情報AToolStripMenuItem.Click
        Dim dlg As New AboutDialog()
        dlg.ShowDialog(Me)
    End Sub
#End Region

#Region "ツールバーイベント: エクスポート"
    ''' <summary>
    ''' エクスポート操作の共通処理: アクティブなWorkspaceからテーブル情報を取得
    ''' </summary>
    Private Function GetExportContext() As ExportHelper.TableExportContext
        Dim ctx = ExportHelper.GetActiveTableContext()
        If ctx Is Nothing Then
            MessageBox.Show("ワークスペースでテーブルを選択してください。", "情報",
                           MessageBoxButtons.OK, MessageBoxIcon.Information)
        End If
        Return ctx
    End Function

    Private Sub ToolStripButton5_Click(sender As Object, e As EventArgs) Handles ToolStripButton5.Click
        Dim ctx = GetExportContext()
        If ctx Is Nothing Then Return
        MessageBox.Show("SQL スクリプト出力は準備中です。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub ToolStripButton6_Click(sender As Object, e As EventArgs) Handles ToolStripButton6.Click
        Dim ctx = GetExportContext()
        If ctx Is Nothing Then Return

        ' SaveFileDialog
        Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.csv"
        Dim outputPath = ExportHelper.ShowSaveFileDialog("CSV ファイル|*.csv|すべてのファイル|*.*", defaultName)
        If outputPath Is Nothing Then Return

        ' C DLL でストリーミング CSV エクスポート (進捗ダイアログ付き)
        Using dlg As New ExportProgressDialog()
            Dim success = dlg.RunExport(
                Sub(worker, args)
                    Dim ok = CsvExportLogic.ExportFromDump(ctx, outputPath)
                    If Not ok Then
                        args.Cancel = True
                    End If
                End Sub)

            If success Then
                MessageBox.Show($"CSV エクスポートが完了しました。" & vbCrLf & outputPath,
                               "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
            End If
        End Using
    End Sub

    Private Sub ToolStripButton7_Click(sender As Object, e As EventArgs) Handles ToolStripButton7.Click
        Dim ctx = GetExportContext()
        If ctx Is Nothing Then Return
        MessageBox.Show("Excel 出力は準備中です。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub ToolStripButton8_Click(sender As Object, e As EventArgs) Handles ToolStripButton8.Click
        Dim ctx = GetExportContext()
        If ctx Is Nothing Then Return
        MessageBox.Show("Access 出力は準備中です。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub ToolStripButton9_Click(sender As Object, e As EventArgs) Handles ToolStripButton9.Click
        Dim ctx = GetExportContext()
        If ctx Is Nothing Then Return
        MessageBox.Show("SQL Server 出力は準備中です。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub ToolStripButton3_Click(sender As Object, e As EventArgs) Handles ToolStripButton3.Click
        Dim ctx = GetExportContext()
        If ctx Is Nothing Then Return
        MessageBox.Show("ODBC 出力は準備中です。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub
#End Region

#Region "メニューイベント: 終了"
    Private Sub 終了XToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 終了XToolStripMenuItem.Click
        'アプリケーションを終了する
        Application.Exit()
    End Sub
#End Region

End Class