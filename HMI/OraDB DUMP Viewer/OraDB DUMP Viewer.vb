Imports System.IO
Imports System.Text

Public Class OraDB_DUMP_Viewer
    Implements ILocalizable

    Private Const MRU_MAX As Integer = 5

#Region "フォームロード・初期化"
    Private Sub OraDB_DUMP_Viewer_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        LocaleManager.InitializeLanguage()
        ApplyLocalization()

        COMMON.ReSet_StatusLavel()
        COMMON.ResetProgressBar()
        ExportOptions.Load()

        If Not CheckAndActivateLicense() Then
            Application.Exit()
            Return
        End If

        ' ライセンス使用状況をバックグラウンドで送信（1日1回、UIをブロックしない）
        HeartbeatLogic.SendIfNeeded()

        COMMON.ReSet_StatusLavel()

        ' MRU メニュー構築
        BuildMruMenus()
        ' ワークスペース依存メニューの初期状態
        UpdateWorkspaceMenuState()

        ' ドラッグ&ドロップでダンプファイルを開けるようにする
        Me.AllowDrop = True

        ' コマンドライン引数で指定されたダンプファイルを開く（ファイル関連付け用）
        For Each arg In My.Application.CommandLineArgs
            If File.Exists(arg) AndAlso
               (arg.EndsWith(".dmp", StringComparison.OrdinalIgnoreCase) OrElse
                arg.EndsWith(".DMP", StringComparison.OrdinalIgnoreCase)) Then
                OpenDumpFile(arg)
            End If
        Next
    End Sub

    Private Sub OraDB_DUMP_Viewer_DragEnter(sender As Object, e As DragEventArgs) Handles MyBase.DragEnter
        If e.Data.GetDataPresent(DataFormats.FileDrop) Then
            e.Effect = DragDropEffects.Copy
        Else
            e.Effect = DragDropEffects.None
        End If
    End Sub

    Private Sub OraDB_DUMP_Viewer_DragDrop(sender As Object, e As DragEventArgs) Handles MyBase.DragDrop
        Dim files = TryCast(e.Data.GetData(DataFormats.FileDrop), String())
        If files Is Nothing OrElse files.Length = 0 Then Return

        For Each filePath In files
            If File.Exists(filePath) Then
                OpenDumpFile(filePath)
            End If
        Next

        COMMON.ReSet_StatusLavel()
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

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        ' === メニュー項目 ===
        ' ファイル
        ファイルFToolStripMenuItem.Text = Loc.S("Menu_File")
        開くToolStripMenuItem.Text = Loc.S("Menu_File_Open")
        ワークスペースToolStripMenuItem.Text = Loc.S("Menu_File_Open_Workspace")
        ダンプファイルDToolStripMenuItem.Text = Loc.S("Menu_File_Open_DumpFile")
        閉じるCToolStripMenuItem.Text = Loc.S("Menu_File_Close")
        ワークスペースの保存SToolStripMenuItem.Text = Loc.S("Menu_File_SaveWorkspace")
        名前を付けてワークスペースを保存AToolStripMenuItem.Text = Loc.S("Menu_File_SaveWorkspaceAs")
        ワークスペースを閉じるLToolStripMenuItem.Text = Loc.S("Menu_File_CloseWorkspace")
        ダンプファイルの作成GToolStripMenuItem.Text = Loc.S("Menu_File_CreateDump")
        最近使ったワークスペースWToolStripMenuItem.Text = Loc.S("Menu_File_RecentWorkspaces")
        最近使ったダンプファイルDToolStripMenuItem.Text = Loc.S("Menu_File_RecentDumps")
        終了XToolStripMenuItem.Text = Loc.S("Menu_File_Exit")

        ' 編集
        編集EToolStripMenuItem.Text = Loc.S("Menu_Edit")
        元に戻すUToolStripMenuItem.Text = Loc.S("Menu_Edit_Undo")
        やり直しRToolStripMenuItem.Text = Loc.S("Menu_Edit_Redo")
        切り取りTToolStripMenuItem.Text = Loc.S("Menu_Edit_Cut")
        ToolStripMenuItem1.Text = Loc.S("Menu_Edit_Copy")
        貼り付けPToolStripMenuItem.Text = Loc.S("Menu_Edit_Paste")
        すべて選択AToolStripMenuItem1.Text = Loc.S("Menu_Edit_SelectAll")

        ' 表示
        表示VToolStripMenuItem.Text = Loc.S("Menu_View")
        ツールバーTToolStripMenuItem.Text = Loc.S("Menu_View_Toolbar")
        エクスポートToolStripMenuItem.Text = Loc.S("Menu_View_Export")
        ステータスバーSToolStripMenuItem.Text = Loc.S("Menu_View_StatusBar")

        ' オブジェクト
        オブジェクトOToolStripMenuItem.Text = Loc.S("Menu_Object")
        開くToolStripMenuItem1.Text = Loc.S("Menu_Object_Open")
        すべてのフィルタをクリアFToolStripMenuItem.Text = Loc.S("Menu_Object_ClearFilters")
        削除DToolStripMenuItem.Text = Loc.S("Menu_Object_Delete")
        更新の取り消しCToolStripMenuItem.Text = Loc.S("Menu_Object_UndoChanges")
        プロパティPToolStripMenuItem.Text = Loc.S("Menu_Object_Property")
        エクスポートToolStripMenuItem1.Text = Loc.S("Menu_Object_Export")
        スクリプトWSToolStripMenuItem.Text = Loc.S("Menu_Object_ExportScript")
        データDToolStripMenuItem.Text = Loc.S("Menu_Object_ExportData")
        レポート出力RToolStripMenuItem.Text = Loc.S("Menu_Object_Report")
        オブジェクト一覧ToolStripMenuItem.Text = Loc.S("Menu_Object_ReportObjectList")
        テーブル定義ToolStripMenuItem.Text = Loc.S("Menu_Object_ReportTableDef")

        ' ツール
        ツールTToolStripMenuItem.Text = Loc.S("Menu_Tools")
        ファイルの取り出しFToolStripMenuItem.Text = Loc.S("Menu_Tools_ExtractFile")
        オプションOToolStripMenuItem.Text = Loc.S("Menu_Tools_Options")

        ' ウィンドウ
        ウィンドウWToolStripMenuItem.Text = Loc.S("Menu_Window")
        重ねて表示CToolStripMenuItem.Text = Loc.S("Menu_Window_Cascade")
        並べて表示TToolStripMenuItem.Text = Loc.S("Menu_Window_Tile")
        アイコンの整列IToolStripMenuItem.Text = Loc.S("Menu_Window_ArrangeIcons")

        ' ヘルプ
        ヘルプHToolStripMenuItem.Text = Loc.S("Menu_Help")
        MokujiToolStripMenuItem.Text = Loc.S("Menu_Help_Contents")
        エラー報告RToolStripMenuItem.Text = Loc.S("Menu_Help_ErrorReport")
        ライセンス認証ToolStripMenuItem.Text = Loc.S("Menu_Help_License")
        バージョン情報AToolStripMenuItem.Text = Loc.S("Menu_Help_About")

        ' === ツールバー ===
        tolTablPproperty.Text = Loc.S("Toolbar_Property")
        tolTablPproperty.ToolTipText = Loc.S("Toolbar_Property")
        btnExcludeTable.Text = Loc.S("Toolbar_Delete")
        btnExcludeTable.ToolTipText = Loc.S("Toolbar_Delete")
        btnExportSql.Text = Loc.S("Toolbar_SqlScript")
        btnExportSql.ToolTipText = Loc.S("Toolbar_SqlScript")
        btnExportCsv.Text = Loc.S("Toolbar_CsvExport")
        btnExportCsv.ToolTipText = Loc.S("Toolbar_CsvExport")
        btnExportExcel.Text = Loc.S("Toolbar_ExcelExport")
        btnExportExcel.ToolTipText = Loc.S("Toolbar_ExcelExport")
        btnExportAccess.Text = Loc.S("Toolbar_AccessExport")
        btnExportAccess.ToolTipText = Loc.S("Toolbar_AccessExport")
        btnExportSqlServer.Text = Loc.S("Toolbar_SqlServer")
        btnExportSqlServer.ToolTipText = Loc.S("Toolbar_SqlServer")
        btnExportOdbc.Text = Loc.S("Toolbar_Odbc")
        btnExportOdbc.ToolTipText = Loc.S("Toolbar_Odbc")

        ' Language submenu
        ' Build language menu items dynamically
        Dim langMenu As ToolStripMenuItem = Nothing
        For Each item As ToolStripMenuItem In ヘルプHToolStripMenuItem.DropDownItems.OfType(Of ToolStripMenuItem)()
            If item.Name = "langToolStripMenuItem" Then
                langMenu = item
                Exit For
            End If
        Next
        If langMenu Is Nothing Then
            langMenu = New ToolStripMenuItem()
            langMenu.Name = "langToolStripMenuItem"
            ヘルプHToolStripMenuItem.DropDownItems.Add(New ToolStripSeparator())
            ヘルプHToolStripMenuItem.DropDownItems.Add(langMenu)
        End If
        langMenu.Text = Loc.S("Menu_Help_Language")
        langMenu.DropDownItems.Clear()

        ' 言語定義: (カルチャ名, リソースキー)
        Dim languages() As (cultureName As String, resourceKey As String) = {
            ("ja", "Menu_Help_Language_Japanese"),
            ("en", "Menu_Help_Language_English"),
            ("zh", "Menu_Help_Language_Chinese"),
            ("ko", "Menu_Help_Language_Korean"),
            ("de", "Menu_Help_Language_German"),
            ("fr", "Menu_Help_Language_French"),
            ("es", "Menu_Help_Language_Spanish"),
            ("it", "Menu_Help_Language_Italian"),
            ("ru", "Menu_Help_Language_Russian"),
            ("pt-BR", "Menu_Help_Language_Portuguese")
        }

        Dim currentLang = LocaleManager.CurrentLanguage()
        For Each lang In languages
            Dim item As New ToolStripMenuItem(Loc.S(lang.resourceKey))
            item.Checked = (currentLang = lang.cultureName) OrElse
                           (lang.cultureName = "pt-BR" AndAlso currentLang = "pt")
            Dim culture = lang.cultureName  ' ラムダ用キャプチャ
            AddHandler item.Click, Sub() LocaleManager.SetLanguage(culture)
            langMenu.DropDownItems.Add(item)
        Next
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
                    ' オンライン検証（サーバー到達不可時は許容）
                    If Not LICENSE.VerifyOnline(licenseKey) Then
                        ' サーバーが無効と返した → ライセンスファイルを削除して再認証へ
                        Dim res = MessageBox.Show(Loc.S("License_RevokedByServer"), Loc.S("License_Required"),
                                                  MessageBoxButtons.YesNo, MessageBoxIcon.Warning)
                        If res = DialogResult.Yes Then
                            Try : File.Delete(statusPath) : Catch : End Try
                            ' 再認証ループへ落ちる
                        Else
                            Return False
                        End If
                    Else
                        Return True
                    End If
                End If
            End If

            ' ライセンスが無効または存在しない → 認証ループ
            Do
                Dim msg As String
                If Not File.Exists(statusPath) Then
                    msg = Loc.S("License_NotRegistered")
                Else
                    Dim errMsg As String = String.Empty
                    Dim dummy1 As String = String.Empty
                    Dim dummy2 As DateTime
                    Dim dummy3 As String = String.Empty
                    LICENSE.VerifyLicenseFile(statusPath, dummy1, dummy2, dummy3, errMsg)
                    msg = Loc.SF("License_VerifyFailed", errMsg)
                End If

                Dim res = MessageBox.Show(msg, Loc.S("License_Required"),
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
            MessageBox.Show(Loc.SF("License_CheckError", ex.Message), Loc.S("Title_Error"),
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return False
        End Try
    End Function
#End Region

#Region "メニューイベント: ダンプファイル"
    Private Sub ダンプファイルDToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ダンプファイルDToolStripMenuItem.Click
        COMMON.Set_StatusLavel(Loc.S("Status_SelectDumpFile"))
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
            dlg.Title = Loc.S("Dialog_OpenWorkspaceTitle")
            dlg.Filter = Loc.S("Dialog_WorkspaceFilter")
            dlg.RestoreDirectory = True
            If dlg.ShowDialog() <> DialogResult.OK Then Return

            Try
                Dim data = WorkspaceData.Load(dlg.FileName)
                If String.IsNullOrEmpty(data.DumpFilePath) OrElse Not File.Exists(data.DumpFilePath) Then
                    MessageBox.Show(Loc.SF("Msg_DumpFileNotFound", data.DumpFilePath),
                                    Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
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
                MessageBox.Show(Loc.SF("Msg_WorkspaceLoadFailed", ex.Message),
                                Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
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
            dlg.Title = Loc.S("Dialog_SaveWorkspaceTitle")
            dlg.Filter = Loc.S("Dialog_WorkspaceFilter")
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

            MessageBox.Show(Loc.SF("Msg_WorkspaceSaved", savePath),
                            Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
        Catch ex As Exception
            MessageBox.Show(Loc.SF("Msg_WorkspaceSaveFailed", ex.Message),
                            Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    ''' <summary>ワークスペースを閉じる（保存確認あり）</summary>
    Private Sub ワークスペースを閉じるLToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ワークスペースを閉じるLToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then Return

        Dim result = MessageBox.Show(Loc.S("Msg_CloseWorkspaceConfirm"),
                                      Loc.S("Title_Confirm"), MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question)
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
            MessageBox.Show(Loc.SF("Msg_FileNotFound", path), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End If
        Try
            Dim data = WorkspaceData.Load(path)
            If String.IsNullOrEmpty(data.DumpFilePath) OrElse Not File.Exists(data.DumpFilePath) Then
                MessageBox.Show(Loc.SF("Msg_DumpFileNotFound", data.DumpFilePath),
                                Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
                Return
            End If
            OpenDumpFile(data.DumpFilePath, data)
            Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
            If ws IsNot Nothing Then ws.WorkspacePath = path
            AddToMru(My.Settings.RecentWorkspaces, path)
            BuildMruMenus()
        Catch ex As Exception
            MessageBox.Show(Loc.SF("Msg_WorkspaceLoadFailed", ex.Message),
                            Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    Private Sub MruDumpFile_Click(sender As Object, e As EventArgs)
        Dim item = TryCast(sender, ToolStripMenuItem)
        If item Is Nothing Then Return
        Dim path = CStr(item.Tag)
        If Not File.Exists(path) Then
            MessageBox.Show(Loc.SF("Msg_FileNotFound", path), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
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
            MessageBox.Show(Loc.S("Msg_WorkspaceNotOpen"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        activeChild.ShowTableProperty()
    End Sub

    Private Sub プロパティPToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles プロパティPToolStripMenuItem.Click
        ShowTableProperty()
    End Sub

    Private Sub btnTableProperty_Click(sender As Object, e As EventArgs) Handles tolTablPproperty.Click
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
            MessageBox.Show(Loc.S("Msg_WorkspaceNotOpen"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
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
        btnExportSql_Click(sender, e)
    End Sub

    Private Sub データDToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles データDToolStripMenuItem.Click
        btnExportCsv_Click(sender, e)
    End Sub

    Private Sub オブジェクト一覧ToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles オブジェクト一覧ToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show(Loc.S("Msg_WorkspaceNotOpen"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_TextFilter"), Loc.S("Report_DefaultObjectList"))
        If outputPath Is Nothing Then Return
        ws.ExportTableListReport(outputPath)
        MessageBox.Show(Loc.SF("Msg_ObjectListExported", outputPath), Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub

    Private Sub テーブル定義ToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles テーブル定義ToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show(Loc.S("Msg_WorkspaceNotOpen"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If
        Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_TextFilter"), Loc.S("Report_DefaultTableDef"))
        If outputPath Is Nothing Then Return
        ws.ExportTableDefinitionReport(outputPath)
        MessageBox.Show(Loc.SF("Msg_TableDefExported", outputPath), Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
    End Sub
#End Region

#Region "メニューイベント: オプション"
    Private Sub オプションOToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles オプションOToolStripMenuItem.Click
        Dim dlg As New ExportOptionsDialog()
        dlg.ShowDialog(Me)
    End Sub
#End Region

#Region "メニューイベント: 目次 (ヘルプ)"
    Private Sub MokujiToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles MokujiToolStripMenuItem.Click
        HelpViewerForm.ShowHelp("toc.html")
    End Sub
#End Region

#Region "メニューイベント: エラー報告"
    Private Sub エラー報告RToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles エラー報告RToolStripMenuItem.Click
        Dim ws = Me.MdiChildren.OfType(Of Workspace)().FirstOrDefault()
        Dim dumpPath As String = If(ws IsNot Nothing, ws.DumpFilePath, Nothing)
        Dim dlg As New ErrorReportDialog(dumpPath)
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
    Private Sub btnExcludeTable_Click(sender As Object, e As EventArgs) Handles btnExcludeTable.Click
        Dim workspace = ExportHelper.GetActiveWorkspace()
        If workspace Is Nothing Then
            MessageBox.Show(Loc.S("Msg_OpenWorkspacePlease"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
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
    ''' ActiveMdiChild が TablePreview の場合、そのインスタンスを返す
    ''' </summary>
    Private Function GetActiveTablePreview() As TablePreview
        Return TryCast(Me.ActiveMdiChild, TablePreview)
    End Function

    ''' <summary>
    ''' 一括エクスポート用: 可視テーブル一覧を取得し確認ダイアログを表示
    ''' </summary>
    Private Function GetBulkExportContexts() As List(Of ExportHelper.TableExportContext)
        Dim contexts = ExportHelper.GetActiveVisibleTableContexts()
        If contexts Is Nothing OrElse contexts.Count = 0 Then
            MessageBox.Show(Loc.S("Msg_NoExportTables"), Loc.S("Title_Info"),
                           MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return Nothing
        End If

        Dim result = MessageBox.Show(Loc.SF("Msg_BulkExportConfirm", contexts.Count),
                                     Loc.S("Title_BulkExport"), MessageBoxButtons.YesNo, MessageBoxIcon.Question)
        If result <> DialogResult.Yes Then Return Nothing

        Return contexts
    End Function

    Private Sub btnExportSql_Click(sender As Object, e As EventArgs) Handles btnExportSql.Click
        Dim ctx = GetExportContext()

        ' DBMS 選択ダイアログ
        Dim sqlDlg As New SqlExportDialog()
        If sqlDlg.ShowDialog(Me) <> DialogResult.OK Then Return
        Dim dbmsType = sqlDlg.SelectedDbmsType
        Dim dbName = sqlDlg.DatabaseName

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.sql"
            Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_SqlFilter"), defaultName)
            If outputPath Is Nothing Then Return

            Dim preview = GetActiveTablePreview()
            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok As Boolean
                        If preview IsNot Nothing Then
                            ' TablePreview: インメモリデータから出力
                            ok = SqlExportLogic.ExportFromData(preview.FilteredData,
                                    preview.ExportColumnNames, ctx.ColumnTypes,
                                    ctx.Schema, ctx.TableName, outputPath, dbmsType, worker,
                                    ctx.ColumnNotNulls, ctx.ColumnDefaults, dbName)
                        ElseIf ExportOptions.SqlInferInteger Then
                            ' InferInteger ON: データを読み込んで VB.NET パスで出力
                            Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                                Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                            Dim rows As New List(Of String())
                            If tableData IsNot Nothing AndAlso
                               tableData.ContainsKey(ctx.Schema) AndAlso
                               tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                                rows = tableData(ctx.Schema)(ctx.TableName)
                            End If
                            Dim colNames = New List(Of String)(If(ctx.ColumnNames, Array.Empty(Of String)()))
                            ok = SqlExportLogic.ExportFromData(rows, colNames, ctx.ColumnTypes,
                                    ctx.Schema, ctx.TableName, outputPath, dbmsType, worker,
                                    ctx.ColumnNotNulls, ctx.ColumnDefaults, dbName)
                        Else
                            ' Workspace: DLLから再パース
                            ok = SqlExportLogic.ExportFromDump(ctx, outputPath, dbmsType)
                        End If
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_SqlExportComplete", outputPath),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Using fbd As New FolderBrowserDialog()
                fbd.Description = Loc.S("Dialog_SqlFolderBrowse")
                If fbd.ShowDialog() <> DialogResult.OK Then Return

                Using dlg As New ExportProgressDialog()
                    Dim folder = fbd.SelectedPath
                    Dim success = dlg.RunExport(
                        Sub(worker, args)
                            Dim ok = BulkExportLogic.ExportSql(contexts, folder, dbmsType, worker, dbName)
                            If Not ok Then args.Cancel = True
                        End Sub)
                    If success Then
                        MessageBox.Show(Loc.SF("Msg_BulkSqlExportComplete", contexts.Count, folder),
                                       Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                    End If
                End Using
            End Using
        End If
    End Sub

    Private Sub btnExportCsv_Click(sender As Object, e As EventArgs) Handles btnExportCsv.Click
        Dim ctx = GetExportContext()

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.csv"
            Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_CsvFilter"), defaultName)
            If outputPath Is Nothing Then Return

            Dim preview = GetActiveTablePreview()
            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok As Boolean
                        If preview IsNot Nothing Then
                            ' TablePreview: インメモリデータから出力
                            ok = CsvExportLogic.ExportFromData(preview.FilteredData,
                                    preview.ExportColumnNames, outputPath, worker, ctx.TableName)
                        Else
                            ' Workspace: DLLから再パース
                            ok = CsvExportLogic.ExportFromDump(ctx, outputPath)
                        End If
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_CsvExportComplete", outputPath),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Using fbd As New FolderBrowserDialog()
                fbd.Description = Loc.S("Dialog_CsvFolderBrowse")
                If fbd.ShowDialog() <> DialogResult.OK Then Return

                Using dlg As New ExportProgressDialog()
                    Dim folder = fbd.SelectedPath
                    Dim success = dlg.RunExport(
                        Sub(worker, args)
                            Dim ok = BulkExportLogic.ExportCsv(contexts, folder, worker)
                            If Not ok Then args.Cancel = True
                        End Sub)
                    If success Then
                        MessageBox.Show(Loc.SF("Msg_BulkCsvExportComplete", contexts.Count, folder),
                                       Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                    End If
                End Using
            End Using
        End If
    End Sub

    Private Sub btnExportExcel_Click(sender As Object, e As EventArgs) Handles btnExportExcel.Click
        Dim ctx = GetExportContext()

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.xlsx"
            Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_ExcelFilter"), defaultName)
            If outputPath Is Nothing Then Return

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0
            Dim preview = GetActiveTablePreview()

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim rows As List(Of String()) = Nothing

                        If preview IsNot Nothing Then
                            ' TablePreview: インメモリデータを使用
                            rows = preview.FilteredData
                        Else
                            ' Workspace: DLLから再パース
                            Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                                Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                            If tableData IsNot Nothing AndAlso
                               tableData.ContainsKey(ctx.Schema) AndAlso
                               tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                                rows = tableData(ctx.Schema)(ctx.TableName)
                            End If
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = ExcelExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                         $"{ctx.Schema}.{ctx.TableName}", outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_ExcelExportComplete", exportedRows, outputPath),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_ExcelFilter"), "export.xlsx")
            If outputPath Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = BulkExportLogic.ExportExcel(contexts, outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_BulkExcelExportComplete", contexts.Count, outputPath),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub

    Private Sub btnExportAccess_Click(sender As Object, e As EventArgs) Handles btnExportAccess.Click
        Dim ctx = GetExportContext()

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim defaultName = $"{ctx.Schema}.{ctx.TableName}.accdb"
            Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_AccessFilter"), defaultName)
            If outputPath Is Nothing Then Return

            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0
            Dim preview = GetActiveTablePreview()

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim rows As List(Of String()) = Nothing

                        If preview IsNot Nothing Then
                            ' TablePreview: インメモリデータを使用
                            rows = preview.FilteredData
                        Else
                            ' Workspace: DLLから再パース
                            Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                                Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                            If tableData IsNot Nothing AndAlso
                               tableData.ContainsKey(ctx.Schema) AndAlso
                               tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                                rows = tableData(ctx.Schema)(ctx.TableName)
                            End If
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = AccessExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                          ctx.TableName, outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_AccessExportComplete", exportedRows, outputPath),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        Else
            ' === 一括エクスポート ===
            Dim contexts = GetBulkExportContexts()
            If contexts Is Nothing Then Return

            Dim outputPath = ExportHelper.ShowSaveFileDialog(Loc.S("Dialog_AccessFilter"), "export.accdb")
            If outputPath Is Nothing Then Return

            Using dlg As New ExportProgressDialog()
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim ok = BulkExportLogic.ExportAccess(contexts, outputPath, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_BulkAccessExportComplete", contexts.Count, outputPath),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub

    Private Sub btnExportSqlServer_Click(sender As Object, e As EventArgs) Handles btnExportSqlServer.Click
        Dim ctx = GetExportContext()

        ' DB 接続ダイアログ (SQL Server タブ)
        Dim connDlg As New DatabaseConnectionDialog()
        If connDlg.ShowDialog(Me) <> DialogResult.OK Then Return
        If Not connDlg.IsSqlServer Then
            MessageBox.Show(Loc.S("Msg_ConnectViaSqlServer"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0
            Dim preview = GetActiveTablePreview()

            Using dlg As New ExportProgressDialog()
                Dim connStr = connDlg.ConnectionString
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim rows As List(Of String()) = Nothing

                        If preview IsNot Nothing Then
                            ' TablePreview: インメモリデータを使用
                            rows = preview.FilteredData
                        Else
                            ' Workspace: DLLから再パース
                            Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                                Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                            If tableData IsNot Nothing AndAlso
                               tableData.ContainsKey(ctx.Schema) AndAlso
                               tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                                rows = tableData(ctx.Schema)(ctx.TableName)
                            End If
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = SqlServerExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                             ctx.Schema, ctx.TableName, connStr, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_SqlServerExportComplete", exportedRows),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
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
                    MessageBox.Show(Loc.SF("Msg_BulkSqlServerExportComplete", contexts.Count),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub

    Private Sub btnExportOdbc_Click(sender As Object, e As EventArgs) Handles btnExportOdbc.Click
        Dim ctx = GetExportContext()

        ' DB 接続ダイアログ (ODBC タブを初期選択)
        Dim connDlg As New DatabaseConnectionDialog(selectOdbcTab:=True)
        If connDlg.ShowDialog(Me) <> DialogResult.OK Then Return
        If connDlg.IsSqlServer Then
            MessageBox.Show(Loc.S("Msg_ConnectViaOdbc"), Loc.S("Title_Info"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            Return
        End If

        If ctx IsNot Nothing Then
            ' === 単一テーブル ===
            Dim colNames = If(ctx.ColumnNames, Array.Empty(Of String)())
            Dim columnList = New List(Of String)(colNames)
            Dim exportedRows As Long = 0
            Dim preview = GetActiveTablePreview()

            Using dlg As New ExportProgressDialog()
                Dim connStr = connDlg.ConnectionString
                Dim success = dlg.RunExport(
                    Sub(worker, args)
                        Dim rows As List(Of String()) = Nothing

                        If preview IsNot Nothing Then
                            ' TablePreview: インメモリデータを使用
                            rows = preview.FilteredData
                        Else
                            ' Workspace: DLLから再パース
                            Dim tableData = OraDB_NativeParser.ParseDump(ctx.DumpFilePath,
                                Nothing, ctx.Schema, ctx.TableName, ctx.DataOffset)
                            If tableData IsNot Nothing AndAlso
                               tableData.ContainsKey(ctx.Schema) AndAlso
                               tableData(ctx.Schema).ContainsKey(ctx.TableName) Then
                                rows = tableData(ctx.Schema)(ctx.TableName)
                            End If
                        End If
                        If rows Is Nothing Then rows = New List(Of String())
                        exportedRows = rows.Count

                        Dim ok = OdbcExportLogic.Export(rows, columnList, ctx.ColumnTypes,
                                                        ctx.TableName, connStr, worker)
                        If Not ok Then args.Cancel = True
                    End Sub)
                If success Then
                    MessageBox.Show(Loc.SF("Msg_OdbcExportComplete", exportedRows),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
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
                    MessageBox.Show(Loc.SF("Msg_BulkOdbcExportComplete", contexts.Count),
                                   Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
                End If
            End Using
        End If
    End Sub
#End Region

#Region "メニューイベント: ツール (LOBファイル抽出)"
    Private Sub ファイルの取り出しFToolStripMenuItem_Click(sender As Object, e As EventArgs) Handles ファイルの取り出しFToolStripMenuItem.Click
        Dim ws = TryCast(Me.ActiveMdiChild, Workspace)
        If ws Is Nothing Then
            MessageBox.Show(Loc.S("Msg_WorkspaceNotOpen"), Loc.S("Title_LobExtract"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        Dim ctx = ws.GetSelectedTableExportContext()
        If ctx Is Nothing Then
            MessageBox.Show(Loc.S("Msg_SelectTableForLob"), Loc.S("Title_LobExtract"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        ' LOBカラムの有無チェック
        If ctx.ColumnTypes Is Nothing OrElse ctx.ColumnTypes.Length = 0 Then
            MessageBox.Show(Loc.S("Msg_NoColumnTypeInfo"), Loc.S("Title_LobExtract"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        Dim hasLob = ctx.ColumnTypes.Any(Function(t)
                                             Dim u = If(t, "").ToUpperInvariant()
                                             Return u.Contains("BLOB") OrElse u.Contains("CLOB")
                                         End Function)
        If Not hasLob Then
            MessageBox.Show(Loc.S("Msg_NoLobColumns"), Loc.S("Title_LobExtract"),
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