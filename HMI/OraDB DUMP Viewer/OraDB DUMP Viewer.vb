Imports System.IO
Imports System.Text

Public Class OraDB_DUMP_Viewer

    Private Const MRU_MAX As Integer = 5

#Region "フォームロード・初期化"
    Private Sub OraDB_DUMP_Viewer_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        COMMON.ReSet_StatusLavel()
        COMMON.ResetProgressBar()
        ExportOptions.Load()

        If Not CheckAndActivateLicense() Then
            Application.Exit()
            Return
        End If

        COMMON.ReSet_StatusLavel()

        ' MRU メニュー構築
        BuildMruMenus()
        ' ワークスペース依存メニューの初期状態
        UpdateWorkspaceMenuState()
    End Sub

    ''' <summary>MDI 子ウィンドウの切替時にメニュー有効化を更新</summary>
    Private Sub OraDB_DUMP_Viewer_MdiChildActivate(sender As Object, e As EventArgs) Handles MyBase.MdiChildActivate
        UpdateWorkspaceMenuState()
    End Sub

    ''' <summary>ワークスペースが開いているかどうかでメニュー有効/無効を制御</summary>
    Private Sub UpdateWorkspaceMenuState()
        Dim hasWorkspace = (TryCast(Me.ActiveMdiChild, Workspace) IsNot Nothing)
        ワークスペースの保存SToolStripMenuItem.Enabled = hasWorkspace
        名前を付けてワークスペースを保存AToolStripMenuItem.Enabled = hasWorkspace
        ワークスペースを閉じるLToolStripMenuItem.Enabled = hasWorkspace
        閉じるCToolStripMenuItem.Enabled = hasWorkspace
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
    Private Sub ダンプファイルDToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ダンプファイルDToolStripMenuItem.Click
        COMMON.Set_StatusLavel("ダンプファイルのパスを選択してください...")
        Dim filePath = MenuStripLogics.ダンプファイルDToolStripMenuItem()
        If String.IsNullOrEmpty(filePath) Then
            COMMON.ReSet_StatusLavel()
            Return
        End If

        OpenDumpFile(filePath)
        COMMON.ReSet_StatusLavel()
    End Sub

    ''' <summary>ダンプファイルを開き、MRU に追加する</summary>
    Private Sub OpenDumpFile(filePath As String, Optional wsData As WorkspaceData = Nothing)
        Dim childForm As New Workspace(filePath, "")
        childForm.MdiParent = Me
        childForm.Show()

        ' ワークスペース状態の復元
        If wsData IsNot Nothing Then
            childForm.LoadWorkspaceState(wsData)
        End If

        ' MRU にダンプファイルを追加
        AddToMru(My.Settings.RecentDumpFiles, filePath)
        BuildMruMenus()
    End Sub
#End Region

#Region "メニューイベント: ワークスペース保存/読込"
    ''' <summary>ワークスペースファイル (.odvw) を開く</summary>
    Private Sub ワークスペースToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ワークスペースToolStripMenuItem.Click
        Using dlg As New OpenFileDialog()
            dlg.Title = "ワークスペースを開く"
            dlg.Filter = "ワークスペースファイル (*.odvw)|*.odvw|すべてのファイル (*.*)|*.*"
            dlg.RestoreDirectory = True
            If dlg.ShowDialog() <> DialogResult.OK Then Return

            Try
                Dim data = WorkspaceData.Load(dlg.FileName)
                If String.IsNullOrEmpty(data.DumpFilePath) OrElse Not File.Exists(data.DumpFilePath) Then
                    MessageBox.Show("ワークスペースに指定されたダンプファイルが見つかりません。" & vbCrLf & data.DumpFilePath,
                                    "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
                    Return
                End If

                OpenDumpFile(data.DumpFilePath, data)

                ' 開いた Workspace に保存パスを設定
                Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
                If ws IsNot Nothing Then ws.WorkspacePath = dlg.FileName

                ' MRU にワークスペースを追加
                AddToMru(My.Settings.RecentWorkspaces, dlg.FileName)
                BuildMruMenus()
            Catch ex As Exception
                MessageBox.Show($"ワークスペースの読み込みに失敗しました。" & vbCrLf & ex.Message,
                                "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
            End Try
        End Using
    End Sub

    ''' <summary>ワークスペースを上書き保存</summary>
    Private Sub ワークスペースの保存SToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ワークスペースの保存SToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then Return

        If String.IsNullOrEmpty(ws.WorkspacePath) Then
            ' まだ保存先がない場合は「名前を付けて保存」
            SaveWorkspaceAs(ws)
        Else
            SaveWorkspace(ws, ws.WorkspacePath)
        End If
    End Sub

    ''' <summary>名前を付けてワークスペースを保存</summary>
    Private Sub 名前を付けてワークスペースを保存AToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 名前を付けてワークスペースを保存AToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then Return
        SaveWorkspaceAs(ws)
    End Sub

    Private Sub SaveWorkspaceAs(ws As Workspace)
        Using dlg As New SaveFileDialog()
            dlg.Title = "ワークスペースを保存"
            dlg.Filter = "ワークスペースファイル (*.odvw)|*.odvw|すべてのファイル (*.*)|*.*"
            dlg.DefaultExt = "odvw"
            dlg.FileName = Path.GetFileNameWithoutExtension(ws.DumpFilePath) & ".odvw"
            dlg.RestoreDirectory = True
            If dlg.ShowDialog() <> DialogResult.OK Then Return
            SaveWorkspace(ws, dlg.FileName)
        End Using
    End Sub

    Private Sub SaveWorkspace(ws As Workspace, savePath As String)
        Try
            Dim data = ws.GetWorkspaceData()
            data.Save(savePath)
            ws.WorkspacePath = savePath

            ' MRU にワークスペースを追加
            AddToMru(My.Settings.RecentWorkspaces, savePath)
            BuildMruMenus()

            MessageBox.Show("ワークスペースを保存しました。" & vbCrLf & savePath,
                            "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
        Catch ex As Exception
            MessageBox.Show($"ワークスペースの保存に失敗しました。" & vbCrLf & ex.Message,
                            "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>ワークスペースを閉じる（保存確認あり）</summary>
    Private Sub ワークスペースを閉じるLToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ワークスペースを閉じるLToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then Return

        Dim result = MessageBox.Show("ワークスペースを保存してから閉じますか？",
                                      "確認", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question)
        Select Case result
            Case DialogResult.Yes
                If String.IsNullOrEmpty(ws.WorkspacePath) Then
                    SaveWorkspaceAs(ws)
                Else
                    SaveWorkspace(ws, ws.WorkspacePath)
                End If
                ws.Close()
            Case DialogResult.No
                ws.Close()
            Case DialogResult.Cancel
                ' キャンセル — 何もしない
        End Select
    End Sub
#End Region

#Region "MRU (最近使ったファイル)"
    ''' <summary>MRU リストにパスを追加 (重複時は先頭に移動)</summary>
    Private Sub AddToMru(ByRef collection As System.Collections.Specialized.StringCollection, path As String)
        If collection Is Nothing Then collection = New System.Collections.Specialized.StringCollection()
        ' 既存エントリを削除して先頭に追加
        If collection.Contains(path) Then collection.Remove(path)
        collection.Insert(0, path)
        ' 最大件数を超えたら末尾を削除
        While collection.Count > MRU_MAX
            collection.RemoveAt(collection.Count - 1)
        End While
        My.Settings.Save()
    End Sub

    ''' <summary>MRU メニューを動的に構築</summary>
    Private Sub BuildMruMenus()
        BuildMruSubmenu(最近使ったワークスペースWToolStripMenuItem, My.Settings.RecentWorkspaces, AddressOf MruWorkspace_Click)
        BuildMruSubmenu(最近使ったダンプファイルDToolStripMenuItem, My.Settings.RecentDumpFiles, AddressOf MruDumpFile_Click)
    End Sub

    Private Sub BuildMruSubmenu(parent As ToolStripMenuItem, collection As System.Collections.Specialized.StringCollection, handler As EventHandler)
        parent.DropDownItems.Clear()
        If collection Is Nothing OrElse collection.Count = 0 Then
            parent.Enabled = False
            Return
        End If

        parent.Enabled = True
        Dim index = 1
        For Each path As String In collection
            If String.IsNullOrEmpty(path) Then Continue For
            Dim item As New ToolStripMenuItem($"&{index} {path}")
            item.Tag = path
            AddHandler item.Click, handler
            parent.DropDownItems.Add(item)
            index += 1
        Next
    End Sub

    Private Sub MruWorkspace_Click(sender As Object, e As EventArgs)
        Dim item = TryCast(sender, ToolStripMenuItem)
        If item Is Nothing Then Return
        Dim path = CStr(item.Tag)
        If Not File.Exists(path) Then
            MessageBox.Show("ファイルが見つかりません。" & vbCrLf & path, "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If
        Try
            Dim data = WorkspaceData.Load(path)
            If String.IsNullOrEmpty(data.DumpFilePath) OrElse Not File.Exists(data.DumpFilePath) Then
                MessageBox.Show("ワークスペースに指定されたダンプファイルが見つかりません。" & vbCrLf & data.DumpFilePath,
                                "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
                Return
            End If
            OpenDumpFile(data.DumpFilePath, data)
            Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
            If ws IsNot Nothing Then ws.WorkspacePath = path
            AddToMru(My.Settings.RecentWorkspaces, path)
            BuildMruMenus()
        Catch ex As Exception
            MessageBox.Show($"ワークスペースの読み込みに失敗しました。" & vbCrLf & ex.Message,
                            "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    Private Sub MruDumpFile_Click(sender As Object, e As EventArgs)
        Dim item = TryCast(sender, ToolStripMenuItem)
        If item Is Nothing Then Return
        Dim path = CStr(item.Tag)
        If Not File.Exists(path) Then
            MessageBox.Show("ファイルが見つかりません。" & vbCrLf & path, "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If
        OpenDumpFile(path)
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

#Region "メニューイベント: 閉じる"
    Private Sub 閉じるCToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 閉じるCToolStripMenuItem.Click
        Dim activeChild = Me.ActiveMdiChild
        If activeChild IsNot Nothing Then activeChild.Close()
    End Sub
#End Region

#Region "メニューイベント: 編集操作"
    Private Sub 元に戻すUToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 元に戻すUToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws IsNot Nothing Then
            ws.UndoExclusion()
        End If
    End Sub

    Private Sub やり直しRToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles やり直しRToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws IsNot Nothing Then
            ws.RedoExclusion()
        End If
    End Sub

    Private Sub 切り取りTToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 切り取りTToolStripMenuItem.Click
        SendKeys.Send("^x")
    End Sub

    Private Sub コピーToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ToolStripMenuItem1.Click
        SendKeys.Send("^c")
    End Sub

    Private Sub 貼り付けPToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 貼り付けPToolStripMenuItem.Click
        SendKeys.Send("^v")
    End Sub

    Private Sub すべて選択AToolStripMenuItem1_Click(sender As Object, e As EventArgs) Handles すべて選択AToolStripMenuItem1.Click
        SendKeys.Send("^a")
    End Sub
#End Region

#Region "メニューイベント: オブジェクト操作"
    Private Sub 開くToolStripMenuItem1_Click(sender As Object, e As EventArgs) Handles 開くToolStripMenuItem1.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show("ワークスペースが開かれていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        ws.OpenSelectedTable()
    End Sub

    Private Sub すべてのフィルタをクリアFToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles すべてのフィルタをクリアFToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws IsNot Nothing Then ws.RestoreAllExcludedTables()
    End Sub

    Private Sub 削除DToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 削除DToolStripMenuItem.Click
        Dim ws = ExportHelper.GetActiveWorkspace()
        If ws IsNot Nothing Then ws.ExcludeSelectedTable()
    End Sub

    Private Sub 更新の取り消しCToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 更新の取り消しCToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws IsNot Nothing Then ws.UndoExclusion()
    End Sub

    Private Sub スクリプトWSToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles スクリプトWSToolStripMenuItem.Click
        ToolStripButton5_Click(sender, e)
    End Sub

    Private Sub データDToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles データDToolStripMenuItem.Click
        ToolStripButton6_Click(sender, e)
    End Sub

    Private Sub オブジェクト一覧ToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles オブジェクト一覧ToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show("ワークスペースが開かれていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        Dim outputPath = ExportHelper.ShowSaveFileDialog("テキストファイル|*.txt|すべてのファイル|*.*", "オブジェクト一覧.txt")
        If outputPath Is Nothing Then Return
        ws.ExportTableListReport(outputPath)
        MessageBox.Show($"オブジェクト一覧を出力しました。" & vbCrLf & outputPath, "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub テーブル定義ToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles テーブル定義ToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show("ワークスペースが開かれていません。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        Dim outputPath = ExportHelper.ShowSaveFileDialog("テキストファイル|*.txt|すべてのファイル|*.*", "テーブル定義.txt")
        If outputPath Is Nothing Then Return
        ws.ExportTableDefinitionReport(outputPath)
        MessageBox.Show($"テーブル定義を出力しました。" & vbCrLf & outputPath, "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub
#End Region

#Region "メニューイベント: オプション"
    Private Sub オプションOToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles オプションOToolStripMenuItem.Click
        Dim dlg As New ExportOptionsDialog()
        dlg.ShowDialog(Me)
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

#Region "ツールバーイベント: テーブル除外"
    Private Sub ToolStripButton1_Click(sender As Object, e As EventArgs) Handles ToolStripButton1.Click
        Dim workspace = ExportHelper.GetActiveWorkspace()
        If workspace Is Nothing Then
            MessageBox.Show("ワークスペースを開いてください。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        workspace.ExcludeSelectedTable()
    End Sub
#End Region

#Region "ツールバーイベント: エクスポート"
    ''' <summary>
    ''' エクスポート操作の共通処理: アクティブなWorkspaceからテーブル情報を取得
    ''' テーブル未選択時は Nothing (一括エクスポートへ分岐)
    ''' </summary>
    Private Function GetExportContext() As ExportHelper.TableExportContext
        Return ExportHelper.GetActiveTableContext()
    End Function

    ''' <summary>
    ''' 一括エクスポート用: 可視テーブル一覧を取得し確認ダイアログを表示
    ''' </summary>
    Private Function GetBulkExportContexts() As List(Of ExportHelper.TableExportContext)
        Dim contexts = ExportHelper.GetActiveVisibleTableContexts()
        If contexts Is Nothing OrElse contexts.Count = 0 Then
            MessageBox.Show("ワークスペースにエクスポート対象のテーブルがありません。", "情報",
                           MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return Nothing
        End If

        Dim result = MessageBox.Show($"{contexts.Count} テーブルを一括エクスポートしますか？",
                                     "一括エクスポート", MessageBoxButtons.YesNo, MessageBoxIcon.Question)
        If result <> DialogResult.Yes Then Return Nothing

        Return contexts
    End Function

    Private Sub ToolStripButton5_Click(sender As Object, e As EventArgs) Handles ToolStripButton5.Click
        Dim ctx = GetExportContext()

        ' DBMS 選択ダイアログ
        Dim sqlDlg As New SqlExportDialog()
        If sqlDlg.ShowDialog(Me) <> DialogResult.OK Then Return
        Dim dbmsType = sqlDlg.SelectedDbmsType

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.sql"
            Dim outputPath = ExportHelper.ShowSaveFileDialog("SQL ファイル|*.sql|すべてのファイル|*.*", defaultName)
            If outputPath Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = SqlExportLogic.ExportFromDump(ctx, outputPath, dbmsType)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"SQL スクリプト出力が完了しました。" & vbCrLf & outputPath,
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Using fbd As New FolderBrowserDialog()
                fbd.Description = "SQL ファイルの出力先フォルダを選択"
                If fbd.ShowDialog() <> DialogResult.OK Then Return

                Using dlg As New ExportProgressDialog()
                    Dim folder = fbd.SelectedPath
                    Dim success = dlg.RunExport(
                        Sub(worker, args)
                            Dim ok = BulkExportLogic.ExportSql(contexts, folder, dbmsType, worker)
                            If Not ok Then args.Cancel = True
                        End Sub)
                    If success Then
                        MessageBox.Show($"SQL 一括エクスポートが完了しました。" & vbCrLf &
                                       $"{contexts.Count} テーブルを出力しました。" & vbCrLf & folder,
                                       "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                    End If
                End Using
            End Using
        End If
    End Sub

    Private Sub ToolStripButton6_Click(sender As Object, e As EventArgs) Handles ToolStripButton6.Click
        Dim ctx = GetExportContext()

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.csv"
            Dim outputPath = ExportHelper.ShowSaveFileDialog("CSV ファイル|*.csv|すべてのファイル|*.*", defaultName)
            If outputPath Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = CsvExportLogic.ExportFromDump(ctx, outputPath)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"CSV エクスポートが完了しました。" & vbCrLf & outputPath,
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Using fbd As New FolderBrowserDialog()
                fbd.Description = "CSV ファイルの出力先フォルダを選択"
                If fbd.ShowDialog() <> DialogResult.OK Then Return

                Using dlg As New ExportProgressDialog()
                    Dim folder = fbd.SelectedPath
                    Dim success = dlg.RunExport(
                        Sub(worker, args)
                            Dim ok = BulkExportLogic.ExportCsv(contexts, folder, worker)
                            If Not ok Then args.Cancel = True
                        End Sub)
                    If success Then
                        MessageBox.Show($"CSV 一括エクスポートが完了しました。" & vbCrLf &
                                       $"{contexts.Count} テーブルを出力しました。" & vbCrLf & folder,
                                       "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                    End If
                End Using
            End Using
        End If
    End Sub

    Private Sub ToolStripButton7_Click(sender As Object, e As EventArgs) Handles ToolStripButton7.Click
        Dim ctx = GetExportContext()

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.xlsx"
            Dim outputPath = ExportHelper.ShowSaveFileDialog("Excel ファイル|*.xlsx|すべてのファイル|*.*", defaultName)
            If outputPath Is Nothing Then Return

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        ' データ取得もBackgroundWorker内で実行 (UIブロック防止)
                        Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                            Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                        Dim rows As List(Of String()) = Nothing
                        If tableData IsNot Nothing AndAlso
                           tableData.ContainsKey(ctx.Schema) AndAlso
                           tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                            rows = tableData(ctx.Schema)(ctx.TableName)
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = ExcelExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                         $"{ctx.Schema}.{ctx.TableName}", outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"Excel エクスポートが完了しました。" & vbCrLf &
                                   $"{exportedRows:#,0} 行を出力しました。" & vbCrLf & outputPath,
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Dim outputPath = ExportHelper.ShowSaveFileDialog("Excel ファイル|*.xlsx|すべてのファイル|*.*", "export.xlsx")
            If outputPath Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = BulkExportLogic.ExportExcel(contexts, outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"Excel 一括エクスポートが完了しました。" & vbCrLf &
                                   $"{contexts.Count} テーブルを出力しました。" & vbCrLf & outputPath,
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub

    Private Sub ToolStripButton8_Click(sender As Object, e As EventArgs) Handles ToolStripButton8.Click
        Dim ctx = GetExportContext()

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.accdb"
            Dim outputPath = ExportHelper.ShowSaveFileDialog("Access データベース|*.accdb|すべてのファイル|*.*", defaultName)
            If outputPath Is Nothing Then Return

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                            Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                        Dim rows As List(Of String()) = Nothing
                        If tableData IsNot Nothing AndAlso
                           tableData.ContainsKey(ctx.Schema) AndAlso
                           tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                            rows = tableData(ctx.Schema)(ctx.TableName)
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = AccessExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                          ctx.TableName, outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"Access エクスポートが完了しました。" & vbCrLf &
                                   $"{exportedRows:#,0} 行を出力しました。" & vbCrLf & outputPath,
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Dim outputPath = ExportHelper.ShowSaveFileDialog("Access データベース|*.accdb|すべてのファイル|*.*", "export.accdb")
            If outputPath Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = BulkExportLogic.ExportAccess(contexts, outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"Access 一括エクスポートが完了しました。" & vbCrLf &
                                   $"{contexts.Count} テーブルを出力しました。" & vbCrLf & outputPath,
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub

    Private Sub ToolStripButton9_Click(sender As Object, e As EventArgs) Handles ToolStripButton9.Click
        Dim ctx = GetExportContext()

        ' DB 接続ダイアログ (SQL Server タブ)
        Dim connDlg As New DatabaseConnectionDialog()
        If connDlg.ShowDialog(Me) <> DialogResult.OK Then Return
        If Not connDlg.IsSqlServer Then
            MessageBox.Show("SQL Server タブから接続してください。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0

            Using dlg As New ExportProgressDialog()
                Dim connStr = connDlg.ConnectionString
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                            Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                        Dim rows As List(Of String()) = Nothing
                        If tableData IsNot Nothing AndAlso
                           tableData.ContainsKey(ctx.Schema) AndAlso
                           tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                            rows = tableData(ctx.Schema)(ctx.TableName)
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = SqlServerExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                             ctx.Schema, ctx.TableName, connStr, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"SQL Server へのエクスポートが完了しました。" & vbCrLf &
                                   $"{exportedRows:#,0} 行を出力しました。",
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim connStr = connDlg.ConnectionString
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = BulkExportLogic.ExportSqlServer(contexts, connStr, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"SQL Server 一括エクスポートが完了しました。" & vbCrLf &
                                   $"{contexts.Count} テーブルを出力しました。",
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub

    Private Sub ToolStripButton3_Click(sender As Object, e As EventArgs) Handles ToolStripButton3.Click
        Dim ctx = GetExportContext()

        ' DB 接続ダイアログ (ODBC タブを初期選択)
        Dim connDlg As New DatabaseConnectionDialog(selectOdbcTab:=True)
        If connDlg.ShowDialog(Me) <> DialogResult.OK Then Return
        If connDlg.IsSqlServer Then
            MessageBox.Show("ODBC タブから接続してください。", "情報", MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0

            Using dlg As New ExportProgressDialog()
                Dim connStr = connDlg.ConnectionString
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                            Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                        Dim rows As List(Of String()) = Nothing
                        If tableData IsNot Nothing AndAlso
                           tableData.ContainsKey(ctx.Schema) AndAlso
                           tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                            rows = tableData(ctx.Schema)(ctx.TableName)
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = OdbcExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                        ctx.TableName, connStr, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"ODBC エクスポートが完了しました。" & vbCrLf &
                                   $"{exportedRows:#,0} 行を出力しました。",
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim connStr = connDlg.ConnectionString
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = BulkExportLogic.ExportOdbc(contexts, connStr, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show($"ODBC 一括エクスポートが完了しました。" & vbCrLf &
                                   $"{contexts.Count} テーブルを出力しました。",
                                   "完了", MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub
#End Region

#Region "メニューイベント: ツール (LOBファイル抽出)"
    Private Sub ファイルの取り出しFToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ファイルの取り出しFToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show("ワークスペースが開かれていません。", "LOBファイル抽出",
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        Dim ctx = ws.GetSelectedTableExportContext()
        If ctx Is Nothing Then
            MessageBox.Show("テーブルを選択してください。", "LOBファイル抽出",
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        ' LOBカラムの有無チェック
        If ctx.ColumnTypes Is Nothing OrElse ctx.ColumnTypes.Length = 0 Then
            MessageBox.Show("カラム型情報がありません。", "LOBファイル抽出",
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        Dim hasLob = ctx.ColumnTypes.Any(Function(t)
                                              Dim u = If(t, "").ToUpperInvariant()
                                              Return u.Contains("BLOB") OrElse u.Contains("CLOB")
                                          End Function)
        If Not hasLob Then
            MessageBox.Show("選択テーブルにLOBカラム (BLOB/CLOB/NCLOB) がありません。", "LOBファイル抽出",
                           MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        Using dlg As New LobExtractDialog(ctx.DumpFilePath, ctx.Schema, ctx.TableName,
                                           ctx.ColumnNames, ctx.ColumnTypes, ctx.DataOffset)
            dlg.ShowDialog(Me)
        End Using
    End Sub
#End Region

#Region "メニューイベント: 終了"
    Private Sub 終了XToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles 終了XToolStripMenuItem.Click
        'アプリケーションを終了する
        Application.Exit()
    End Sub
#End Region

End Class