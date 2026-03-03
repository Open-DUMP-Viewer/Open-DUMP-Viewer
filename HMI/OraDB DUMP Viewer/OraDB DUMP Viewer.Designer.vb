<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()>
Partial Class OraDB_DUMP_Viewer
    Inherits System.Windows.Forms.Form

    'フォームがコンポーネントの一覧をクリーンアップするために dispose をオーバーライドします。
    <System.Diagnostics.DebuggerNonUserCode()>
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    'Windows フォーム デザイナーで必要です。
    Private components As System.ComponentModel.IContainer

    'メモ: 以下のプロシージャは Windows フォーム デザイナーで必要です。
    'Windows フォーム デザイナーを使用して変更できます。  
    'コード エディターを使って変更しないでください。
    <System.Diagnostics.DebuggerStepThrough()>
    Private Sub InitializeComponent()
        Dim resources As System.ComponentModel.ComponentResourceManager = New System.ComponentModel.ComponentResourceManager(GetType(OraDB_DUMP_Viewer))
        MenuStrip = New MenuStrip()
        ファイルFToolStripMenuItem = New ToolStripMenuItem()
        開くToolStripMenuItem = New ToolStripMenuItem()
        ワークスペースToolStripMenuItem = New ToolStripMenuItem()
        ダンプファイルDToolStripMenuItem = New ToolStripMenuItem()
        閉じるCToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator1 = New ToolStripSeparator()
        ワークスペースの保存SToolStripMenuItem = New ToolStripMenuItem()
        名前を付けてワークスペースを保存AToolStripMenuItem = New ToolStripMenuItem()
        ワークスペースを閉じるLToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator2 = New ToolStripSeparator()
        ダンプファイルの作成GToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator3 = New ToolStripSeparator()
        最近使ったワークスペースWToolStripMenuItem = New ToolStripMenuItem()
        最近使ったダンプファイルDToolStripMenuItem = New ToolStripMenuItem()
        終了XToolStripMenuItem = New ToolStripMenuItem()
        編集EToolStripMenuItem = New ToolStripMenuItem()
        元に戻すUToolStripMenuItem = New ToolStripMenuItem()
        やり直しRToolStripMenuItem = New ToolStripMenuItem()
        コピーCToolStripMenuItem = New ToolStripSeparator()
        切り取りTToolStripMenuItem = New ToolStripMenuItem()
        ToolStripMenuItem1 = New ToolStripMenuItem()
        貼り付けPToolStripMenuItem = New ToolStripMenuItem()
        すべて選択AToolStripMenuItem = New ToolStripSeparator()
        すべて選択AToolStripMenuItem1 = New ToolStripMenuItem()
        表示VToolStripMenuItem = New ToolStripMenuItem()
        ツールバーTToolStripMenuItem = New ToolStripMenuItem()
        エクスポートToolStripMenuItem = New ToolStripMenuItem()
        ステータスバーSToolStripMenuItem = New ToolStripMenuItem()
        オブジェクトOToolStripMenuItem = New ToolStripMenuItem()
        開くToolStripMenuItem1 = New ToolStripMenuItem()
        ToolStripSeparator5 = New ToolStripSeparator()
        すべてのフィルタをクリアFToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator6 = New ToolStripSeparator()
        削除DToolStripMenuItem = New ToolStripMenuItem()
        更新の取り消しCToolStripMenuItem = New ToolStripMenuItem()
        プロパティPToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator7 = New ToolStripSeparator()
        エクスポートToolStripMenuItem1 = New ToolStripMenuItem()
        スクリプトWSToolStripMenuItem = New ToolStripMenuItem()
        データDToolStripMenuItem = New ToolStripMenuItem()
        レポート出力RToolStripMenuItem = New ToolStripMenuItem()
        オブジェクト一覧ToolStripMenuItem = New ToolStripMenuItem()
        テーブル定義ToolStripMenuItem = New ToolStripMenuItem()
        ツールTToolStripMenuItem = New ToolStripMenuItem()
        ファイルの取り出しFToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator8 = New ToolStripSeparator()
        オプションOToolStripMenuItem = New ToolStripMenuItem()
        ウィンドウWToolStripMenuItem = New ToolStripMenuItem()
        重ねて表示CToolStripMenuItem = New ToolStripMenuItem()
        並べて表示TToolStripMenuItem = New ToolStripMenuItem()
        アイコンの整列IToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator13 = New ToolStripSeparator()
        ヘルプHToolStripMenuItem = New ToolStripMenuItem()
        MokujiToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator9 = New ToolStripSeparator()
        エラー報告RToolStripMenuItem = New ToolStripMenuItem()
        ToolStripSeparator10 = New ToolStripSeparator()
        ライセンス認証ToolStripMenuItem = New ToolStripMenuItem()
        バージョン情報AToolStripMenuItem = New ToolStripMenuItem()
        StatusStrip = New StatusStrip()
        ToolStripProgressBar = New ToolStripProgressBar()
        ToolStripStatusLabel = New ToolStripStatusLabel()
        ToolExport = New ToolStrip()
        tolTablPproperty = New ToolStripButton()
        btnExcludeTable = New ToolStripButton()
        ToolStripSeparator11 = New ToolStripSeparator()
        btnExportSql = New ToolStripButton()
        btnExportCsv = New ToolStripButton()
        ToolStripSeparator12 = New ToolStripSeparator()
        btnExportExcel = New ToolStripButton()
        btnExportAccess = New ToolStripButton()
        btnExportSqlServer = New ToolStripButton()
        btnExportOdbc = New ToolStripButton()
        MenuStrip.SuspendLayout()
        StatusStrip.SuspendLayout()
        ToolExport.SuspendLayout()
        SuspendLayout()
        ' 
        ' MenuStrip
        ' 
        MenuStrip.ImageScalingSize = New Size(24, 24)
        MenuStrip.Items.AddRange(New ToolStripItem() {ファイルFToolStripMenuItem, 編集EToolStripMenuItem, 表示VToolStripMenuItem, オブジェクトOToolStripMenuItem, ツールTToolStripMenuItem, ウィンドウWToolStripMenuItem, ヘルプHToolStripMenuItem})
        MenuStrip.Location = New Point(0, 0)
        MenuStrip.MdiWindowListItem = ウィンドウWToolStripMenuItem
        MenuStrip.Name = "MenuStrip"
        MenuStrip.Padding = New Padding(4, 1, 0, 1)
        MenuStrip.Size = New Size(1125, 24)
        MenuStrip.TabIndex = 0
        MenuStrip.Text = "MenuStrip1"
        ' 
        ' ファイルFToolStripMenuItem
        ' 
        ファイルFToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {開くToolStripMenuItem, 閉じるCToolStripMenuItem, ToolStripSeparator1, ワークスペースの保存SToolStripMenuItem, 名前を付けてワークスペースを保存AToolStripMenuItem, ワークスペースを閉じるLToolStripMenuItem, ToolStripSeparator2, ダンプファイルの作成GToolStripMenuItem, ToolStripSeparator3, 最近使ったワークスペースWToolStripMenuItem, 最近使ったダンプファイルDToolStripMenuItem, 終了XToolStripMenuItem})
        ファイルFToolStripMenuItem.Name = "ファイルFToolStripMenuItem"
        ファイルFToolStripMenuItem.Size = New Size(67, 22)
        ファイルFToolStripMenuItem.Text = "ファイル(F)"
        ' 
        ' 開くToolStripMenuItem
        ' 
        開くToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {ワークスペースToolStripMenuItem, ダンプファイルDToolStripMenuItem})
        開くToolStripMenuItem.Name = "開くToolStripMenuItem"
        開くToolStripMenuItem.Size = New Size(257, 22)
        開くToolStripMenuItem.Text = "開く(&O)"
        ' 
        ' ワークスペースToolStripMenuItem
        ' 
        ワークスペースToolStripMenuItem.Name = "ワークスペースToolStripMenuItem"
        ワークスペースToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.Shift Or Keys.O
        ワークスペースToolStripMenuItem.Size = New Size(238, 22)
        ワークスペースToolStripMenuItem.Text = "ワークスペース(&W)..."
        ' 
        ' ダンプファイルDToolStripMenuItem
        ' 
        ダンプファイルDToolStripMenuItem.Name = "ダンプファイルDToolStripMenuItem"
        ダンプファイルDToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.O
        ダンプファイルDToolStripMenuItem.Size = New Size(238, 22)
        ダンプファイルDToolStripMenuItem.Text = "ダンプファイル(&D)..."
        ' 
        ' 閉じるCToolStripMenuItem
        ' 
        閉じるCToolStripMenuItem.Name = "閉じるCToolStripMenuItem"
        閉じるCToolStripMenuItem.Size = New Size(257, 22)
        閉じるCToolStripMenuItem.Text = "閉じる(&C)"
        ' 
        ' ToolStripSeparator1
        ' 
        ToolStripSeparator1.Name = "ToolStripSeparator1"
        ToolStripSeparator1.Size = New Size(254, 6)
        ' 
        ' ワークスペースの保存SToolStripMenuItem
        ' 
        ワークスペースの保存SToolStripMenuItem.Name = "ワークスペースの保存SToolStripMenuItem"
        ワークスペースの保存SToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.S
        ワークスペースの保存SToolStripMenuItem.Size = New Size(257, 22)
        ワークスペースの保存SToolStripMenuItem.Text = "ワークスペースの保存(&S)"
        ' 
        ' 名前を付けてワークスペースを保存AToolStripMenuItem
        ' 
        名前を付けてワークスペースを保存AToolStripMenuItem.Name = "名前を付けてワークスペースを保存AToolStripMenuItem"
        名前を付けてワークスペースを保存AToolStripMenuItem.Size = New Size(257, 22)
        名前を付けてワークスペースを保存AToolStripMenuItem.Text = "名前を付けてワークスペースを保存(&A)..."
        ' 
        ' ワークスペースを閉じるLToolStripMenuItem
        ' 
        ワークスペースを閉じるLToolStripMenuItem.Name = "ワークスペースを閉じるLToolStripMenuItem"
        ワークスペースを閉じるLToolStripMenuItem.Size = New Size(257, 22)
        ワークスペースを閉じるLToolStripMenuItem.Text = "ワークスペースを閉じる(&L)"
        ' 
        ' ToolStripSeparator2
        ' 
        ToolStripSeparator2.Name = "ToolStripSeparator2"
        ToolStripSeparator2.Size = New Size(254, 6)
        ' 
        ' ダンプファイルの作成GToolStripMenuItem
        ' 
        ダンプファイルの作成GToolStripMenuItem.Enabled = False
        ダンプファイルの作成GToolStripMenuItem.Name = "ダンプファイルの作成GToolStripMenuItem"
        ダンプファイルの作成GToolStripMenuItem.ShowShortcutKeys = False
        ダンプファイルの作成GToolStripMenuItem.Size = New Size(257, 22)
        ダンプファイルの作成GToolStripMenuItem.Text = "ダンプファイルの作成(&G)..."
        ' 
        ' ToolStripSeparator3
        ' 
        ToolStripSeparator3.Name = "ToolStripSeparator3"
        ToolStripSeparator3.Size = New Size(254, 6)
        ' 
        ' 最近使ったワークスペースWToolStripMenuItem
        ' 
        最近使ったワークスペースWToolStripMenuItem.Name = "最近使ったワークスペースWToolStripMenuItem"
        最近使ったワークスペースWToolStripMenuItem.Size = New Size(257, 22)
        最近使ったワークスペースWToolStripMenuItem.Text = "最近使ったワークスペース(&W)"
        ' 
        ' 最近使ったダンプファイルDToolStripMenuItem
        ' 
        最近使ったダンプファイルDToolStripMenuItem.Name = "最近使ったダンプファイルDToolStripMenuItem"
        最近使ったダンプファイルDToolStripMenuItem.Size = New Size(257, 22)
        最近使ったダンプファイルDToolStripMenuItem.Text = "最近使ったダンプファイル(&D)"
        ' 
        ' 終了XToolStripMenuItem
        ' 
        終了XToolStripMenuItem.Name = "終了XToolStripMenuItem"
        終了XToolStripMenuItem.Size = New Size(257, 22)
        終了XToolStripMenuItem.Text = "終了(&X)"
        ' 
        ' 編集EToolStripMenuItem
        ' 
        編集EToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {元に戻すUToolStripMenuItem, やり直しRToolStripMenuItem, コピーCToolStripMenuItem, 切り取りTToolStripMenuItem, ToolStripMenuItem1, 貼り付けPToolStripMenuItem, すべて選択AToolStripMenuItem, すべて選択AToolStripMenuItem1})
        編集EToolStripMenuItem.Name = "編集EToolStripMenuItem"
        編集EToolStripMenuItem.Size = New Size(57, 22)
        編集EToolStripMenuItem.Text = "編集(E)"
        ' 
        ' 元に戻すUToolStripMenuItem
        ' 
        元に戻すUToolStripMenuItem.Name = "元に戻すUToolStripMenuItem"
        元に戻すUToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.Z
        元に戻すUToolStripMenuItem.Size = New Size(184, 22)
        元に戻すUToolStripMenuItem.Text = "元に戻す(&U)"
        ' 
        ' やり直しRToolStripMenuItem
        ' 
        やり直しRToolStripMenuItem.Name = "やり直しRToolStripMenuItem"
        やり直しRToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.Y
        やり直しRToolStripMenuItem.Size = New Size(184, 22)
        やり直しRToolStripMenuItem.Text = "やり直し(&R)"
        ' 
        ' コピーCToolStripMenuItem
        ' 
        コピーCToolStripMenuItem.Name = "コピーCToolStripMenuItem"
        コピーCToolStripMenuItem.Size = New Size(181, 6)
        ' 
        ' 切り取りTToolStripMenuItem
        ' 
        切り取りTToolStripMenuItem.Name = "切り取りTToolStripMenuItem"
        切り取りTToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.X
        切り取りTToolStripMenuItem.Size = New Size(184, 22)
        切り取りTToolStripMenuItem.Text = "切り取り(&T)"
        ' 
        ' ToolStripMenuItem1
        ' 
        ToolStripMenuItem1.Name = "ToolStripMenuItem1"
        ToolStripMenuItem1.ShortcutKeys = Keys.Control Or Keys.C
        ToolStripMenuItem1.Size = New Size(184, 22)
        ToolStripMenuItem1.Text = "コピー(&C)"
        ' 
        ' 貼り付けPToolStripMenuItem
        ' 
        貼り付けPToolStripMenuItem.Name = "貼り付けPToolStripMenuItem"
        貼り付けPToolStripMenuItem.ShortcutKeys = Keys.Control Or Keys.V
        貼り付けPToolStripMenuItem.Size = New Size(184, 22)
        貼り付けPToolStripMenuItem.Text = "貼り付け(&P)"
        ' 
        ' すべて選択AToolStripMenuItem
        ' 
        すべて選択AToolStripMenuItem.Name = "すべて選択AToolStripMenuItem"
        すべて選択AToolStripMenuItem.Size = New Size(181, 6)
        ' 
        ' すべて選択AToolStripMenuItem1
        ' 
        すべて選択AToolStripMenuItem1.Name = "すべて選択AToolStripMenuItem1"
        すべて選択AToolStripMenuItem1.ShortcutKeys = Keys.Control Or Keys.A
        すべて選択AToolStripMenuItem1.Size = New Size(184, 22)
        すべて選択AToolStripMenuItem1.Text = "すべて選択(&A)"
        ' 
        ' 表示VToolStripMenuItem
        ' 
        表示VToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {ツールバーTToolStripMenuItem, ステータスバーSToolStripMenuItem})
        表示VToolStripMenuItem.Name = "表示VToolStripMenuItem"
        表示VToolStripMenuItem.Size = New Size(58, 22)
        表示VToolStripMenuItem.Text = "表示(&V)"
        ' 
        ' ツールバーTToolStripMenuItem
        ' 
        ツールバーTToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {エクスポートToolStripMenuItem})
        ツールバーTToolStripMenuItem.Name = "ツールバーTToolStripMenuItem"
        ツールバーTToolStripMenuItem.Size = New Size(199, 22)
        ツールバーTToolStripMenuItem.Text = "ツールバー(&T)"
        ' 
        ' エクスポートToolStripMenuItem
        ' 
        エクスポートToolStripMenuItem.Checked = True
        エクスポートToolStripMenuItem.CheckState = CheckState.Checked
        エクスポートToolStripMenuItem.Name = "エクスポートToolStripMenuItem"
        エクスポートToolStripMenuItem.Size = New Size(127, 22)
        エクスポートToolStripMenuItem.Text = "エクスポート"
        ' 
        ' ステータスバーSToolStripMenuItem
        ' 
        ステータスバーSToolStripMenuItem.Checked = True
        ステータスバーSToolStripMenuItem.CheckState = CheckState.Checked
        ステータスバーSToolStripMenuItem.Name = "ステータスバーSToolStripMenuItem"
        ステータスバーSToolStripMenuItem.Size = New Size(199, 22)
        ステータスバーSToolStripMenuItem.Text = "ステータスバー(&S)"
        ' 
        ' オブジェクトOToolStripMenuItem
        ' 
        オブジェクトOToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {開くToolStripMenuItem1, ToolStripSeparator5, すべてのフィルタをクリアFToolStripMenuItem, ToolStripSeparator6, 削除DToolStripMenuItem, 更新の取り消しCToolStripMenuItem, プロパティPToolStripMenuItem, ToolStripSeparator7, エクスポートToolStripMenuItem1, レポート出力RToolStripMenuItem})
        オブジェクトOToolStripMenuItem.Name = "オブジェクトOToolStripMenuItem"
        オブジェクトOToolStripMenuItem.Size = New Size(88, 22)
        オブジェクトOToolStripMenuItem.Text = "オブジェクト(&O)"
        ' 
        ' 開くToolStripMenuItem1
        ' 
        開くToolStripMenuItem1.Name = "開くToolStripMenuItem1"
        開くToolStripMenuItem1.Size = New Size(196, 22)
        開くToolStripMenuItem1.Text = "開く(&O)"
        ' 
        ' ToolStripSeparator5
        ' 
        ToolStripSeparator5.Name = "ToolStripSeparator5"
        ToolStripSeparator5.Size = New Size(193, 6)
        ' 
        ' すべてのフィルタをクリアFToolStripMenuItem
        ' 
        すべてのフィルタをクリアFToolStripMenuItem.Name = "すべてのフィルタをクリアFToolStripMenuItem"
        すべてのフィルタをクリアFToolStripMenuItem.Size = New Size(196, 22)
        すべてのフィルタをクリアFToolStripMenuItem.Text = "すべてのフィルタをクリア(&F)"
        ' 
        ' ToolStripSeparator6
        ' 
        ToolStripSeparator6.Name = "ToolStripSeparator6"
        ToolStripSeparator6.Size = New Size(193, 6)
        ' 
        ' 削除DToolStripMenuItem
        ' 
        削除DToolStripMenuItem.Name = "削除DToolStripMenuItem"
        削除DToolStripMenuItem.ShortcutKeys = Keys.Delete
        削除DToolStripMenuItem.Size = New Size(196, 22)
        削除DToolStripMenuItem.Text = "削除(&D)"
        ' 
        ' 更新の取り消しCToolStripMenuItem
        ' 
        更新の取り消しCToolStripMenuItem.Name = "更新の取り消しCToolStripMenuItem"
        更新の取り消しCToolStripMenuItem.Size = New Size(196, 22)
        更新の取り消しCToolStripMenuItem.Text = "更新の取り消し(&C)"
        ' 
        ' プロパティPToolStripMenuItem
        ' 
        プロパティPToolStripMenuItem.Name = "プロパティPToolStripMenuItem"
        プロパティPToolStripMenuItem.Size = New Size(196, 22)
        プロパティPToolStripMenuItem.Text = "プロパティ(&P)"
        ' 
        ' ToolStripSeparator7
        ' 
        ToolStripSeparator7.Name = "ToolStripSeparator7"
        ToolStripSeparator7.Size = New Size(193, 6)
        ' 
        ' エクスポートToolStripMenuItem1
        ' 
        エクスポートToolStripMenuItem1.DropDownItems.AddRange(New ToolStripItem() {スクリプトWSToolStripMenuItem, データDToolStripMenuItem})
        エクスポートToolStripMenuItem1.Name = "エクスポートToolStripMenuItem1"
        エクスポートToolStripMenuItem1.Size = New Size(196, 22)
        エクスポートToolStripMenuItem1.Text = "エクスポート(&E)"
        ' 
        ' スクリプトWSToolStripMenuItem
        ' 
        スクリプトWSToolStripMenuItem.Name = "スクリプトWSToolStripMenuItem"
        スクリプトWSToolStripMenuItem.Size = New Size(131, 22)
        スクリプトWSToolStripMenuItem.Text = "スクリプト(&S)"
        ' 
        ' データDToolStripMenuItem
        ' 
        データDToolStripMenuItem.Name = "データDToolStripMenuItem"
        データDToolStripMenuItem.Size = New Size(131, 22)
        データDToolStripMenuItem.Text = "データ(&D)"
        ' 
        ' レポート出力RToolStripMenuItem
        ' 
        レポート出力RToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {オブジェクト一覧ToolStripMenuItem, テーブル定義ToolStripMenuItem})
        レポート出力RToolStripMenuItem.Name = "レポート出力RToolStripMenuItem"
        レポート出力RToolStripMenuItem.Size = New Size(196, 22)
        レポート出力RToolStripMenuItem.Text = "レポート出力(&R)"
        ' 
        ' オブジェクト一覧ToolStripMenuItem
        ' 
        オブジェクト一覧ToolStripMenuItem.Name = "オブジェクト一覧ToolStripMenuItem"
        オブジェクト一覧ToolStripMenuItem.Size = New Size(150, 22)
        オブジェクト一覧ToolStripMenuItem.Text = "オブジェクト一覧"
        ' 
        ' テーブル定義ToolStripMenuItem
        ' 
        テーブル定義ToolStripMenuItem.Name = "テーブル定義ToolStripMenuItem"
        テーブル定義ToolStripMenuItem.Size = New Size(150, 22)
        テーブル定義ToolStripMenuItem.Text = "テーブル定義"
        ' 
        ' ツールTToolStripMenuItem
        ' 
        ツールTToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {ファイルの取り出しFToolStripMenuItem, ToolStripSeparator8, オプションOToolStripMenuItem})
        ツールTToolStripMenuItem.Name = "ツールTToolStripMenuItem"
        ツールTToolStripMenuItem.Size = New Size(60, 22)
        ツールTToolStripMenuItem.Text = "ツール(&T)"
        ' 
        ' ファイルの取り出しFToolStripMenuItem
        ' 
        ファイルの取り出しFToolStripMenuItem.Name = "ファイルの取り出しFToolStripMenuItem"
        ファイルの取り出しFToolStripMenuItem.Size = New Size(172, 22)
        ファイルの取り出しFToolStripMenuItem.Text = "ファイルの取り出し(&F)"
        ' 
        ' ToolStripSeparator8
        ' 
        ToolStripSeparator8.Name = "ToolStripSeparator8"
        ToolStripSeparator8.Size = New Size(169, 6)
        ' 
        ' オプションOToolStripMenuItem
        ' 
        オプションOToolStripMenuItem.Name = "オプションOToolStripMenuItem"
        オプションOToolStripMenuItem.Size = New Size(172, 22)
        オプションOToolStripMenuItem.Text = "オプション(&O)"
        ' 
        ' ウィンドウWToolStripMenuItem
        ' 
        ウィンドウWToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {重ねて表示CToolStripMenuItem, 並べて表示TToolStripMenuItem, アイコンの整列IToolStripMenuItem, ToolStripSeparator13})
        ウィンドウWToolStripMenuItem.Name = "ウィンドウWToolStripMenuItem"
        ウィンドウWToolStripMenuItem.Size = New Size(80, 22)
        ウィンドウWToolStripMenuItem.Text = "ウィンドウ(&W)"
        ' 
        ' 重ねて表示CToolStripMenuItem
        ' 
        重ねて表示CToolStripMenuItem.Name = "重ねて表示CToolStripMenuItem"
        重ねて表示CToolStripMenuItem.Size = New Size(154, 22)
        重ねて表示CToolStripMenuItem.Text = "重ねて表示(&C)"
        ' 
        ' 並べて表示TToolStripMenuItem
        ' 
        並べて表示TToolStripMenuItem.Name = "並べて表示TToolStripMenuItem"
        並べて表示TToolStripMenuItem.Size = New Size(154, 22)
        並べて表示TToolStripMenuItem.Text = "並べて表示($T)"
        ' 
        ' アイコンの整列IToolStripMenuItem
        ' 
        アイコンの整列IToolStripMenuItem.Name = "アイコンの整列IToolStripMenuItem"
        アイコンの整列IToolStripMenuItem.Size = New Size(154, 22)
        アイコンの整列IToolStripMenuItem.Text = "アイコンの整列(&I)"
        ' 
        ' ToolStripSeparator13
        ' 
        ToolStripSeparator13.Name = "ToolStripSeparator13"
        ToolStripSeparator13.Size = New Size(151, 6)
        ' 
        ' ヘルプHToolStripMenuItem
        ' 
        ヘルプHToolStripMenuItem.DropDownItems.AddRange(New ToolStripItem() {MokujiToolStripMenuItem, ToolStripSeparator9, エラー報告RToolStripMenuItem, ToolStripSeparator10, ライセンス認証ToolStripMenuItem, バージョン情報AToolStripMenuItem})
        ヘルプHToolStripMenuItem.Name = "ヘルプHToolStripMenuItem"
        ヘルプHToolStripMenuItem.Size = New Size(65, 22)
        ヘルプHToolStripMenuItem.Text = "ヘルプ(&H)"
        ' 
        ' MokujiToolStripMenuItem
        ' 
        MokujiToolStripMenuItem.Name = "MokujiToolStripMenuItem"
        MokujiToolStripMenuItem.ShortcutKeys = Keys.F1
        MokujiToolStripMenuItem.Size = New Size(158, 22)
        MokujiToolStripMenuItem.Text = "目次(&I)"
        ' 
        ' ToolStripSeparator9
        ' 
        ToolStripSeparator9.Name = "ToolStripSeparator9"
        ToolStripSeparator9.Size = New Size(155, 6)
        ' 
        ' エラー報告RToolStripMenuItem
        ' 
        エラー報告RToolStripMenuItem.Name = "エラー報告RToolStripMenuItem"
        エラー報告RToolStripMenuItem.Size = New Size(158, 22)
        エラー報告RToolStripMenuItem.Text = "エラー報告(&R)"
        ' 
        ' ToolStripSeparator10
        ' 
        ToolStripSeparator10.Name = "ToolStripSeparator10"
        ToolStripSeparator10.Size = New Size(155, 6)
        ' 
        ' ライセンス認証ToolStripMenuItem
        ' 
        ライセンス認証ToolStripMenuItem.Name = "ライセンス認証ToolStripMenuItem"
        ライセンス認証ToolStripMenuItem.Size = New Size(158, 22)
        ライセンス認証ToolStripMenuItem.Text = "ライセンス認証(&L)"
        ' 
        ' バージョン情報AToolStripMenuItem
        ' 
        バージョン情報AToolStripMenuItem.Name = "バージョン情報AToolStripMenuItem"
        バージョン情報AToolStripMenuItem.Size = New Size(158, 22)
        バージョン情報AToolStripMenuItem.Text = "バージョン情報(&A)"
        ' 
        ' StatusStrip
        ' 
        StatusStrip.ImageScalingSize = New Size(24, 24)
        StatusStrip.Items.AddRange(New ToolStripItem() {ToolStripProgressBar, ToolStripStatusLabel})
        StatusStrip.Location = New Point(0, 613)
        StatusStrip.Name = "StatusStrip"
        StatusStrip.Padding = New Padding(1, 0, 10, 0)
        StatusStrip.Size = New Size(1125, 22)
        StatusStrip.TabIndex = 1
        StatusStrip.Text = "StatusStrip1"
        ' 
        ' ToolStripProgressBar
        ' 
        ToolStripProgressBar.Name = "ToolStripProgressBar"
        ToolStripProgressBar.Size = New Size(70, 16)
        ' 
        ' ToolStripStatusLabel
        ' 
        ToolStripStatusLabel.Name = "ToolStripStatusLabel"
        ToolStripStatusLabel.Size = New Size(116, 17)
        ToolStripStatusLabel.Text = "OraDB DUMP Viewer"
        ' 
        ' ToolExport
        ' 
        ToolExport.ImageScalingSize = New Size(24, 24)
        ToolExport.Items.AddRange(New ToolStripItem() {tolTablPproperty, btnExcludeTable, ToolStripSeparator11, btnExportSql, btnExportCsv, ToolStripSeparator12, btnExportExcel, btnExportAccess, btnExportSqlServer, btnExportOdbc})
        ToolExport.Location = New Point(0, 24)
        ToolExport.Name = "ToolExport"
        ToolExport.Size = New Size(1125, 31)
        ToolExport.TabIndex = 4
        ToolExport.Text = "ToolStrip2"
        ' 
        ' tolTablPproperty
        ' 
        tolTablPproperty.DisplayStyle = ToolStripItemDisplayStyle.Image
        tolTablPproperty.Image = CType(resources.GetObject("tolTablPproperty.Image"), Image)
        tolTablPproperty.ImageTransparentColor = Color.Magenta
        tolTablPproperty.Name = "tolTablPproperty"
        tolTablPproperty.Size = New Size(28, 28)
        tolTablPproperty.Text = "プロパティ"
        ' 
        ' btnExcludeTable
        ' 
        btnExcludeTable.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExcludeTable.Image = CType(resources.GetObject("btnExcludeTable.Image"), Image)
        btnExcludeTable.ImageTransparentColor = Color.Magenta
        btnExcludeTable.Name = "btnExcludeTable"
        btnExcludeTable.Size = New Size(28, 28)
        btnExcludeTable.Text = "削除"
        ' 
        ' ToolStripSeparator11
        ' 
        ToolStripSeparator11.Name = "ToolStripSeparator11"
        ToolStripSeparator11.Size = New Size(6, 31)
        ' 
        ' btnExportSql
        ' 
        btnExportSql.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExportSql.Image = CType(resources.GetObject("btnExportSql.Image"), Image)
        btnExportSql.ImageTransparentColor = Color.Magenta
        btnExportSql.Name = "btnExportSql"
        btnExportSql.Size = New Size(28, 28)
        btnExportSql.Text = "SQL スクリプト"
        ' 
        ' btnExportCsv
        ' 
        btnExportCsv.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExportCsv.Image = CType(resources.GetObject("btnExportCsv.Image"), Image)
        btnExportCsv.ImageTransparentColor = Color.Magenta
        btnExportCsv.Name = "btnExportCsv"
        btnExportCsv.Size = New Size(28, 28)
        btnExportCsv.Text = "CSV 出力"
        ' 
        ' ToolStripSeparator12
        ' 
        ToolStripSeparator12.Name = "ToolStripSeparator12"
        ToolStripSeparator12.Size = New Size(6, 31)
        ' 
        ' btnExportExcel
        ' 
        btnExportExcel.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExportExcel.Image = CType(resources.GetObject("btnExportExcel.Image"), Image)
        btnExportExcel.ImageTransparentColor = Color.Magenta
        btnExportExcel.Name = "btnExportExcel"
        btnExportExcel.Size = New Size(28, 28)
        btnExportExcel.Text = "Excel 出力"
        ' 
        ' btnExportAccess
        ' 
        btnExportAccess.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExportAccess.Image = CType(resources.GetObject("btnExportAccess.Image"), Image)
        btnExportAccess.ImageTransparentColor = Color.Magenta
        btnExportAccess.Name = "btnExportAccess"
        btnExportAccess.Size = New Size(28, 28)
        btnExportAccess.Text = "Access 出力"
        '
        ' btnExportSqlServer
        ' 
        btnExportSqlServer.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExportSqlServer.Image = CType(resources.GetObject("btnExportSqlServer.Image"), Image)
        btnExportSqlServer.ImageTransparentColor = Color.Magenta
        btnExportSqlServer.Name = "btnExportSqlServer"
        btnExportSqlServer.Size = New Size(28, 28)
        btnExportSqlServer.Text = "SQL Server"
        ' 
        ' btnExportOdbc
        ' 
        btnExportOdbc.DisplayStyle = ToolStripItemDisplayStyle.Image
        btnExportOdbc.Image = CType(resources.GetObject("btnExportOdbc.Image"), Image)
        btnExportOdbc.ImageTransparentColor = Color.Magenta
        btnExportOdbc.Name = "btnExportOdbc"
        btnExportOdbc.Size = New Size(28, 28)
        btnExportOdbc.Text = "ODBC"
        '
        ' OraDB_DUMP_Viewer
        '
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(1125, 635)
        Controls.Add(ToolExport)
        Controls.Add(StatusStrip)
        Controls.Add(MenuStrip)
        Icon = System.Drawing.Icon.ExtractAssociatedIcon(System.Reflection.Assembly.GetExecutingAssembly().Location)
        IsMdiContainer = True
        MainMenuStrip = MenuStrip
        Margin = New Padding(2, 2, 2, 2)
        Name = "OraDB_DUMP_Viewer"
        Text = "OraDB DUMP Viewer"
        MenuStrip.ResumeLayout(False)
        MenuStrip.PerformLayout()
        StatusStrip.ResumeLayout(False)
        StatusStrip.PerformLayout()
        ToolExport.ResumeLayout(False)
        ToolExport.PerformLayout()
        ResumeLayout(False)
        PerformLayout()
    End Sub

    Friend WithEvents MenuStrip As MenuStrip
    Friend WithEvents ファイルFToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 開くToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ワークスペースToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ダンプファイルDToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 閉じるCToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator1 As ToolStripSeparator
    Friend WithEvents ワークスペースの保存SToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 名前を付けてワークスペースを保存AToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ワークスペースを閉じるLToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator2 As ToolStripSeparator
    Friend WithEvents ダンプファイルの作成GToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator3 As ToolStripSeparator
    Friend WithEvents 最近使ったワークスペースWToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 最近使ったダンプファイルDToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 終了XToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 編集EToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 元に戻すUToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents やり直しRToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 切り取りTToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents コピーCToolStripMenuItem As ToolStripSeparator
    Friend WithEvents 貼り付けPToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents すべて選択AToolStripMenuItem As ToolStripSeparator
    Friend WithEvents すべて選択AToolStripMenuItem1 As ToolStripMenuItem
    Friend WithEvents 表示VToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ツールバーTToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents エクスポートToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ステータスバーSToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents オブジェクトOToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 開くToolStripMenuItem1 As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator5 As ToolStripSeparator
    Friend WithEvents すべてのフィルタをクリアFToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator6 As ToolStripSeparator
    Friend WithEvents 削除DToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 更新の取り消しCToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents プロパティPToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator7 As ToolStripSeparator
    Friend WithEvents エクスポートToolStripMenuItem1 As ToolStripMenuItem
    Friend WithEvents スクリプトWSToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents データDToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents レポート出力RToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents オブジェクト一覧ToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents テーブル定義ToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ツールTToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ファイルの取り出しFToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator8 As ToolStripSeparator
    Friend WithEvents オプションOToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ウィンドウWToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 重ねて表示CToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents 並べて表示TToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents アイコンの整列IToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ヘルプHToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents MokujiToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator9 As ToolStripSeparator
    Friend WithEvents エラー報告RToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripSeparator10 As ToolStripSeparator
    Friend WithEvents バージョン情報AToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolStripMenuItem1 As ToolStripMenuItem
    Friend WithEvents StatusStrip As StatusStrip
    Friend WithEvents ToolStripStatusLabel As ToolStripStatusLabel
    Friend WithEvents ToolStripProgressBar As ToolStripProgressBar
    Friend WithEvents ライセンス認証ToolStripMenuItem As ToolStripMenuItem
    Friend WithEvents ToolEdit As ToolStrip
    Friend WithEvents ToolExport As ToolStrip
    Friend WithEvents btnExportSql As ToolStripButton
    Friend WithEvents btnExportCsv As ToolStripButton
    Friend WithEvents ToolStripSeparator12 As ToolStripSeparator
    Friend WithEvents btnExportExcel As ToolStripButton
    Friend WithEvents btnExportAccess As ToolStripButton
    Friend WithEvents btnExportSqlServer As ToolStripButton
    Friend WithEvents tolTablPproperty As ToolStripButton
    Friend WithEvents btnExcludeTable As ToolStripButton
    Friend WithEvents ToolStripSeparator11 As ToolStripSeparator
    Friend WithEvents btnExportOdbc As ToolStripButton
    Friend WithEvents ToolStripSeparator13 As ToolStripSeparator
End Class
