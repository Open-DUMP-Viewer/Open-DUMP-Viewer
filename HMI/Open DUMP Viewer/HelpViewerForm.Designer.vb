<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class HelpViewerForm
    Inherits System.Windows.Forms.Form

    <System.Diagnostics.DebuggerNonUserCode()> _
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    Private components As System.ComponentModel.IContainer

    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        toolStrip = New ToolStrip()
        btnBack = New ToolStripButton()
        btnForward = New ToolStripButton()
        btnHome = New ToolStripButton()
        toolSep1 = New ToolStripSeparator()
        txtSearch = New ToolStripTextBox()
        btnSearch = New ToolStripButton()
        splitContainer = New SplitContainer()
        tvToc = New TreeView()
        webView = New Microsoft.Web.WebView2.WinForms.WebView2()
        toolStrip.SuspendLayout()
        CType(splitContainer, ComponentModel.ISupportInitialize).BeginInit()
        splitContainer.Panel1.SuspendLayout()
        splitContainer.Panel2.SuspendLayout()
        splitContainer.SuspendLayout()
        CType(webView, ComponentModel.ISupportInitialize).BeginInit()
        SuspendLayout()
        '
        ' toolStrip
        '
        toolStrip.ImageScalingSize = New Size(20, 20)
        toolStrip.Items.AddRange(New ToolStripItem() {btnBack, btnForward, btnHome, toolSep1, txtSearch, btnSearch})
        toolStrip.Location = New Point(0, 0)
        toolStrip.Name = "toolStrip"
        toolStrip.Size = New Size(900, 27)
        toolStrip.TabIndex = 0
        '
        ' btnBack
        '
        btnBack.DisplayStyle = ToolStripItemDisplayStyle.Text
        btnBack.Enabled = False
        btnBack.Name = "btnBack"
        btnBack.Size = New Size(30, 24)
        btnBack.Text = "<<"
        btnBack.ToolTipText = "戻る"
        '
        ' btnForward
        '
        btnForward.DisplayStyle = ToolStripItemDisplayStyle.Text
        btnForward.Enabled = False
        btnForward.Name = "btnForward"
        btnForward.Size = New Size(30, 24)
        btnForward.Text = ">>"
        btnForward.ToolTipText = "進む"
        '
        ' btnHome
        '
        btnHome.DisplayStyle = ToolStripItemDisplayStyle.Text
        btnHome.Name = "btnHome"
        btnHome.Size = New Size(40, 24)
        btnHome.Text = "目次"
        '
        ' toolSep1
        '
        toolSep1.Name = "toolSep1"
        toolSep1.Size = New Size(6, 27)
        '
        ' txtSearch
        '
        txtSearch.Name = "txtSearch"
        txtSearch.Size = New Size(200, 27)
        '
        ' btnSearch
        '
        btnSearch.DisplayStyle = ToolStripItemDisplayStyle.Text
        btnSearch.Name = "btnSearch"
        btnSearch.Size = New Size(40, 24)
        btnSearch.Text = "検索"
        '
        ' splitContainer
        '
        splitContainer.Dock = DockStyle.Fill
        splitContainer.FixedPanel = FixedPanel.Panel1
        splitContainer.Location = New Point(0, 27)
        splitContainer.Name = "splitContainer"
        '
        ' splitContainer.Panel1
        '
        splitContainer.Panel1.Controls.Add(tvToc)
        splitContainer.Panel1MinSize = 150
        '
        ' splitContainer.Panel2
        '
        splitContainer.Panel2.Controls.Add(webView)
        splitContainer.Size = New Size(900, 573)
        splitContainer.SplitterDistance = 220
        splitContainer.TabIndex = 1
        '
        ' tvToc
        '
        tvToc.Dock = DockStyle.Fill
        tvToc.Font = New Font(tvToc.Font.FontFamily, 9F)
        tvToc.FullRowSelect = True
        tvToc.HideSelection = False
        tvToc.ItemHeight = 22
        tvToc.Location = New Point(0, 0)
        tvToc.Name = "tvToc"
        tvToc.Size = New Size(220, 573)
        tvToc.TabIndex = 0
        '
        ' webView
        '
        webView.AllowExternalDrop = False
        webView.Dock = DockStyle.Fill
        webView.Location = New Point(0, 0)
        webView.Name = "webView"
        webView.Size = New Size(676, 573)
        webView.TabIndex = 0
        '
        ' HelpViewerForm
        '
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(900, 600)
        Controls.Add(splitContainer)
        Controls.Add(toolStrip)
        MinimumSize = New Size(600, 400)
        Name = "HelpViewerForm"
        StartPosition = FormStartPosition.CenterScreen
        Text = "ヘルプ"
        toolStrip.ResumeLayout(False)
        toolStrip.PerformLayout()
        splitContainer.Panel1.ResumeLayout(False)
        splitContainer.Panel2.ResumeLayout(False)
        CType(splitContainer, ComponentModel.ISupportInitialize).EndInit()
        splitContainer.ResumeLayout(False)
        CType(webView, ComponentModel.ISupportInitialize).EndInit()
        ResumeLayout(False)
        PerformLayout()
    End Sub

    Friend WithEvents toolStrip As ToolStrip
    Friend WithEvents btnBack As ToolStripButton
    Friend WithEvents btnForward As ToolStripButton
    Friend WithEvents btnHome As ToolStripButton
    Friend WithEvents toolSep1 As ToolStripSeparator
    Friend WithEvents txtSearch As ToolStripTextBox
    Friend WithEvents btnSearch As ToolStripButton
    Friend WithEvents splitContainer As SplitContainer
    Friend WithEvents tvToc As TreeView
    Friend WithEvents webView As Microsoft.Web.WebView2.WinForms.WebView2

End Class
