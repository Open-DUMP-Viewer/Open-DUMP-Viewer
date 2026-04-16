Imports System.IO
Imports Microsoft.Web.WebView2.Core

''' <summary>
''' アプリ内ヘルプビューアー
''' WebView2 で Help フォルダの HTML ファイルを表示する
''' シングルトンで動作し、同時に1つのウィンドウのみ存在する
''' </summary>
Partial Public Class HelpViewerForm
    Implements ILocalizable

    ''' <summary>シングルトンインスタンス</summary>
    Private Shared _instance As HelpViewerForm = Nothing

    ''' <summary>Help フォルダの絶対パス</summary>
    Private ReadOnly _helpBasePath As String

    ''' <summary>ファイル名 → TreeNode のマッピング</summary>
    Private ReadOnly _tocMap As New Dictionary(Of String, TreeNode)(StringComparer.OrdinalIgnoreCase)

    ''' <summary>WebView2 初期化完了フラグ</summary>
    Private _webViewReady As Boolean = False

    ''' <summary>初期化完了後にナビゲートするページ名 (キュー)</summary>
    Private _pendingNavigation As String = Nothing

    ''' <summary>TreeView 選択時の再帰防止フラグ</summary>
    Private _suppressTocSelect As Boolean = False

    ''' <summary>
    ''' ヘルプを表示する (シングルトン)
    ''' 既にウィンドウが開いていればそのウィンドウを前面に出す
    ''' </summary>
    Public Shared Sub ShowHelp(Optional pageName As String = "toc.html")
        If _instance Is Nothing OrElse _instance.IsDisposed Then
            _instance = New HelpViewerForm()
        End If
        _instance.NavigateTo(pageName)
        _instance.Show()
        _instance.BringToFront()
    End Sub

    Public Sub New()
        InitializeComponent()
        ApplyLocalization()

        _helpBasePath = Path.Combine(Application.StartupPath, "Help")
        BuildTocTree()
        InitWebViewAsync()
    End Sub

    ''' <summary>
    ''' WebView2 を非同期で初期化する
    ''' ユーザーデータフォルダを Temp に設定してインストールフォルダを汚さない
    ''' </summary>
    Private Async Sub InitWebViewAsync()
        Try
            Dim env = Await CoreWebView2Environment.CreateAsync(
                Nothing, Path.Combine(Path.GetTempPath(), "OpenDumpViewer_WebView2"), Nothing)
            Await webView.EnsureCoreWebView2Async(env)

            ' 外部リンクをブラウザで開く
            AddHandler webView.CoreWebView2.NavigationStarting, AddressOf WebView_NavigationStarting

            ' ナビゲーション完了時に戻る/進むボタンを更新
            AddHandler webView.CoreWebView2.NavigationCompleted, AddressOf WebView_NavigationCompleted

            ' 新しいウィンドウを開かせない (target="_blank" 等)
            AddHandler webView.CoreWebView2.NewWindowRequested, AddressOf WebView_NewWindowRequested

            _webViewReady = True

            ' 初期化待ちのナビゲーションがあれば実行
            If _pendingNavigation IsNot Nothing Then
                NavigateTo(_pendingNavigation)
                _pendingNavigation = Nothing
            End If
        Catch
            ' WebView2 ランタイムが無い場合はフォールバック
            MessageBox.Show(Loc.S("Help_WebView2NotAvailable"),
                           Loc.S("Help_FormTitle"),
                           MessageBoxButtons.OK, MessageBoxIcon.Information)
        End Try
    End Sub

    ''' <summary>
    ''' 指定ページにナビゲートする
    ''' WebView2 未初期化の場合はキューに入れる
    ''' 言語別ディレクトリ (Help/{lang}/) を優先し、なければ Help/ (日本語) にフォールバック
    ''' </summary>
    Public Sub NavigateTo(pageName As String)
        If Not _webViewReady Then
            _pendingNavigation = pageName
            Return
        End If

        Dim fullPath = GetLocalizedHelpPath(pageName)
        If fullPath Is Nothing Then
            fullPath = GetLocalizedHelpPath("toc.html")
        End If

        If fullPath IsNot Nothing Then
            webView.CoreWebView2.Navigate(New Uri(fullPath).AbsoluteUri)
        End If

        ' TOC のハイライトを更新
        HighlightTocNode(pageName)
    End Sub

    ''' <summary>
    ''' 言語別ヘルプファイルのパスを解決する
    ''' Help/{lang}/pageName → Help/pageName の順にフォールバック
    ''' </summary>
    Private Function GetLocalizedHelpPath(pageName As String) As String
        Dim lang = LocaleManager.CurrentLanguage()

        ' 日本語はデフォルト (Help/ 直下) なのでスキップ
        If lang <> "ja" Then
            Dim langPath = Path.Combine(_helpBasePath, lang, pageName)
            If File.Exists(langPath) Then Return langPath
        End If

        ' フォールバック: Help/ 直下 (日本語)
        Dim defaultPath = Path.Combine(_helpBasePath, pageName)
        If File.Exists(defaultPath) Then Return defaultPath

        Return Nothing
    End Function

#Region "TOC ツリー構築"

    ''' <summary>
    ''' 目次ツリーを構築する
    ''' toc.html の構造に対応したノードを作成
    ''' </summary>
    Private Sub BuildTocTree()
        tvToc.BeginUpdate()
        tvToc.Nodes.Clear()

        AddTocNode(Nothing, Loc.S("Help_TOC_GettingStarted"), "getting_started.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_OpenDump"), "open_dump.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_Workspace"), "workspace.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_TablePreview"), "table_preview.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_Search"), "search.html")

        ' エクスポート グループ
        Dim exportNode = New TreeNode(Loc.S("Help_TOC_Export"))
        tvToc.Nodes.Add(exportNode)
        AddTocNode(exportNode, Loc.S("Help_TOC_ExportCsv"), "export_csv.html")
        AddTocNode(exportNode, Loc.S("Help_TOC_ExportSql"), "export_sql.html")
        AddTocNode(exportNode, Loc.S("Help_TOC_ExportBatch"), "export_batch.html")
        AddTocNode(exportNode, Loc.S("Help_TOC_ExportOptions"), "export_options.html")
        exportNode.Expand()

        AddTocNode(Nothing, Loc.S("Help_TOC_LobExtract"), "lob_extract.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_ShortcutKeys"), "shortcut_keys.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_SupportedFormats"), "supported_formats.html")
        AddTocNode(Nothing, Loc.S("Help_TOC_About"), "about.html")

        tvToc.EndUpdate()
    End Sub

    ''' <summary>
    ''' TOC ノードを追加し、マップに登録する
    ''' </summary>
    Private Sub AddTocNode(parent As TreeNode, text As String, fileName As String)
        Dim node As New TreeNode(text)
        node.Tag = fileName

        If parent IsNot Nothing Then
            parent.Nodes.Add(node)
        Else
            tvToc.Nodes.Add(node)
        End If

        _tocMap(fileName) = node
    End Sub

    ''' <summary>
    ''' 現在のページに対応する TOC ノードをハイライトする
    ''' </summary>
    Private Sub HighlightTocNode(pageName As String)
        If _tocMap.ContainsKey(pageName) Then
            _suppressTocSelect = True
            tvToc.SelectedNode = _tocMap(pageName)
            _suppressTocSelect = False
        End If
    End Sub

#End Region

#Region "WebView2 イベント"

    ''' <summary>
    ''' ナビゲーション開始時: 外部リンクはブラウザにリダイレクト
    ''' </summary>
    Private Sub WebView_NavigationStarting(sender As Object, e As CoreWebView2NavigationStartingEventArgs)
        Dim uri As Uri = Nothing
        If Uri.TryCreate(e.Uri, UriKind.Absolute, uri) Then
            ' http/https リンクはブラウザで開く
            If uri.Scheme = "http" OrElse uri.Scheme = "https" Then
                e.Cancel = True
                Try
                    Process.Start(New ProcessStartInfo(e.Uri) With {.UseShellExecute = True})
                Catch
                End Try
            End If
        End If
    End Sub

    ''' <summary>
    ''' ナビゲーション完了時: ボタン状態と TOC ハイライトを更新
    ''' </summary>
    Private Sub WebView_NavigationCompleted(sender As Object, e As CoreWebView2NavigationCompletedEventArgs)
        btnBack.Enabled = webView.CanGoBack
        btnForward.Enabled = webView.CanGoForward

        ' 現在の URL からファイル名を取得して TOC をハイライト
        Try
            Dim currentUri As New Uri(webView.Source.AbsoluteUri)
            If currentUri.Scheme = "file" Then
                Dim fileName = Path.GetFileName(currentUri.LocalPath)
                HighlightTocNode(fileName)
            End If
        Catch
        End Try
    End Sub

    ''' <summary>
    ''' 新しいウィンドウの要求を抑制し、同一ウィンドウ内で表示
    ''' </summary>
    Private Sub WebView_NewWindowRequested(sender As Object, e As CoreWebView2NewWindowRequestedEventArgs)
        e.Handled = True
        webView.CoreWebView2.Navigate(e.Uri)
    End Sub

#End Region

#Region "ツールバーイベント"

    Private Sub btnBack_Click(sender As Object, e As EventArgs) Handles btnBack.Click
        If webView.CanGoBack Then webView.GoBack()
    End Sub

    Private Sub btnForward_Click(sender As Object, e As EventArgs) Handles btnForward.Click
        If webView.CanGoForward Then webView.GoForward()
    End Sub

    Private Sub btnHome_Click(sender As Object, e As EventArgs) Handles btnHome.Click
        NavigateTo("toc.html")
    End Sub

    Private Sub btnSearch_Click(sender As Object, e As EventArgs) Handles btnSearch.Click
        ExecuteSearch()
    End Sub

    Private Sub txtSearch_KeyDown(sender As Object, e As KeyEventArgs) Handles txtSearch.KeyDown
        If e.KeyCode = Keys.Enter Then
            e.SuppressKeyPress = True
            ExecuteSearch()
        End If
    End Sub

    Private Sub ExecuteSearch()
        If Not _webViewReady Then Return
        Dim searchText = txtSearch.Text.Trim()
        If String.IsNullOrEmpty(searchText) Then Return

        ' WebView2 の組み込み検索を使用
        Dim escaped = searchText.Replace("'", "\'").Replace("\", "\\")
        webView.CoreWebView2.ExecuteScriptAsync($"window.find('{escaped}', false, false, true)")
    End Sub

#End Region

#Region "TreeView イベント"

    Private Sub tvToc_AfterSelect(sender As Object, e As TreeViewEventArgs) Handles tvToc.AfterSelect
        If _suppressTocSelect Then Return
        If e.Node?.Tag IsNot Nothing Then
            NavigateTo(e.Node.Tag.ToString())
        End If
    End Sub

    Private Sub tvToc_NodeMouseDoubleClick(sender As Object, e As TreeNodeMouseClickEventArgs) Handles tvToc.NodeMouseDoubleClick
        If e.Node?.Tag IsNot Nothing Then
            NavigateTo(e.Node.Tag.ToString())
        End If
    End Sub

#End Region

#Region "フォームイベント"

    ''' <summary>
    ''' 閉じるボタンでは非表示にしてシングルトンを維持する
    ''' </summary>
    Protected Overrides Sub OnFormClosing(e As FormClosingEventArgs)
        If e.CloseReason = CloseReason.UserClosing Then
            e.Cancel = True
            Me.Hide()
            Return
        End If
        MyBase.OnFormClosing(e)
    End Sub

#End Region

#Region "ローカライズ"

    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("Help_FormTitle")
        btnBack.ToolTipText = Loc.S("Help_Back")
        btnForward.ToolTipText = Loc.S("Help_Forward")
        btnHome.Text = Loc.S("Help_Home")
        btnHome.ToolTipText = Loc.S("Help_Home")
        btnSearch.Text = Loc.S("Help_Search")
        btnSearch.ToolTipText = Loc.S("Help_Search")
        txtSearch.ToolTipText = Loc.S("Help_SearchPlaceholder")

        ' TOC を再構築
        BuildTocTree()
    End Sub

#End Region

End Class
