<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class AboutDialog
    Inherits System.Windows.Forms.Form

    'フォームがコンポーネントの一覧をクリアするために dispose をオーバーライドします。
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

    'Windows フォーム デザイナーで必要です。
    Private components As System.ComponentModel.IContainer

    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        tblMain = New TableLayoutPanel()
        lblProductName = New Label()
        lblVersion = New Label()
        lblCopyright = New Label()
        lblLatestCaption = New Label()
        lblLatestVersion = New Label()
        btnUpdate = New Button()
        prgDownload = New ProgressBar()
        lnkReleasePage = New LinkLabel()
        btnClose = New Button()
        tblMain.SuspendLayout()
        SuspendLayout()
        '
        ' tblMain
        '
        tblMain.ColumnCount = 2
        tblMain.ColumnStyles.Add(New ColumnStyle(SizeType.Absolute, 120F))
        tblMain.ColumnStyles.Add(New ColumnStyle(SizeType.Percent, 100F))
        tblMain.Controls.Add(lblProductName, 0, 0)
        tblMain.Controls.Add(lblVersion, 0, 1)
        tblMain.Controls.Add(lblCopyright, 0, 2)
        tblMain.Controls.Add(lblLatestCaption, 0, 3)
        tblMain.Controls.Add(lblLatestVersion, 1, 3)
        tblMain.Controls.Add(btnUpdate, 0, 4)
        tblMain.Controls.Add(prgDownload, 0, 5)
        tblMain.Controls.Add(lnkReleasePage, 0, 6)
        tblMain.Dock = DockStyle.Fill
        tblMain.Location = New Point(0, 0)
        tblMain.Name = "tblMain"
        tblMain.Padding = New Padding(16)
        tblMain.RowCount = 7
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 36F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 40F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 28F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Percent, 100F))
        tblMain.Size = New Size(450, 265)
        tblMain.TabIndex = 0
        '
        ' lblProductName
        '
        tblMain.SetColumnSpan(lblProductName, 2)
        lblProductName.Dock = DockStyle.Fill
        lblProductName.Font = New Font(lblProductName.Font.FontFamily, 14F, FontStyle.Bold)
        lblProductName.Location = New Point(19, 16)
        lblProductName.Name = "lblProductName"
        lblProductName.Size = New Size(412, 36)
        lblProductName.TabIndex = 0
        lblProductName.Text = "Open DUMP Viewer for Oracle database"
        lblProductName.TextAlign = ContentAlignment.MiddleLeft
        '
        ' lblVersion
        '
        tblMain.SetColumnSpan(lblVersion, 2)
        lblVersion.Dock = DockStyle.Fill
        lblVersion.Location = New Point(19, 52)
        lblVersion.Name = "lblVersion"
        lblVersion.Size = New Size(412, 30)
        lblVersion.TabIndex = 1
        lblVersion.Text = "バージョン:"
        lblVersion.TextAlign = ContentAlignment.MiddleLeft
        '
        ' lblCopyright
        '
        tblMain.SetColumnSpan(lblCopyright, 2)
        lblCopyright.Dock = DockStyle.Fill
        lblCopyright.Location = New Point(19, 82)
        lblCopyright.Name = "lblCopyright"
        lblCopyright.Size = New Size(412, 30)
        lblCopyright.TabIndex = 2
        lblCopyright.Text = "Copyright"
        lblCopyright.TextAlign = ContentAlignment.MiddleLeft
        '
        ' lblLatestCaption
        '
        lblLatestCaption.Dock = DockStyle.Fill
        lblLatestCaption.Location = New Point(19, 112)
        lblLatestCaption.Name = "lblLatestCaption"
        lblLatestCaption.Size = New Size(114, 30)
        lblLatestCaption.TabIndex = 3
        lblLatestCaption.Text = "最新バージョン:"
        lblLatestCaption.TextAlign = ContentAlignment.MiddleRight
        '
        ' lblLatestVersion
        '
        lblLatestVersion.Dock = DockStyle.Fill
        lblLatestVersion.Font = New Font(lblLatestVersion.Font, FontStyle.Bold)
        lblLatestVersion.Location = New Point(139, 112)
        lblLatestVersion.Name = "lblLatestVersion"
        lblLatestVersion.Size = New Size(292, 30)
        lblLatestVersion.TabIndex = 4
        lblLatestVersion.Text = "確認中..."
        lblLatestVersion.TextAlign = ContentAlignment.MiddleLeft
        '
        ' btnUpdate
        '
        tblMain.SetColumnSpan(btnUpdate, 2)
        btnUpdate.Dock = DockStyle.Fill
        btnUpdate.Location = New Point(19, 145)
        btnUpdate.Margin = New Padding(3, 3, 3, 3)
        btnUpdate.Name = "btnUpdate"
        btnUpdate.Size = New Size(412, 34)
        btnUpdate.TabIndex = 6
        btnUpdate.Text = "最新バージョンに更新する"
        btnUpdate.Visible = False
        '
        ' prgDownload
        '
        tblMain.SetColumnSpan(prgDownload, 2)
        prgDownload.Dock = DockStyle.Fill
        prgDownload.Location = New Point(19, 185)
        prgDownload.Name = "prgDownload"
        prgDownload.Size = New Size(412, 22)
        prgDownload.TabIndex = 7
        prgDownload.Visible = False
        '
        ' lnkReleasePage
        '
        tblMain.SetColumnSpan(lnkReleasePage, 2)
        lnkReleasePage.Dock = DockStyle.Fill
        lnkReleasePage.Location = New Point(19, 142)
        lnkReleasePage.Name = "lnkReleasePage"
        lnkReleasePage.Size = New Size(412, 57)
        lnkReleasePage.TabIndex = 5
        lnkReleasePage.TextAlign = ContentAlignment.TopCenter
        lnkReleasePage.Visible = False
        '
        ' btnClose
        '
        btnClose.DialogResult = DialogResult.OK
        btnClose.Dock = DockStyle.Bottom
        btnClose.Location = New Point(0, 265)
        btnClose.Name = "btnClose"
        btnClose.Size = New Size(450, 35)
        btnClose.TabIndex = 1
        btnClose.Text = "閉じる"
        '
        ' AboutDialog
        '
        AcceptButton = btnClose
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        ClientSize = New Size(450, 300)
        Controls.Add(tblMain)
        Controls.Add(btnClose)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "AboutDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "バージョン情報"
        tblMain.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents tblMain As TableLayoutPanel
    Friend WithEvents lblProductName As Label
    Friend WithEvents lblVersion As Label
    Friend WithEvents lblCopyright As Label
    Friend WithEvents lblLatestCaption As Label
    Friend WithEvents lblLatestVersion As Label
    Friend WithEvents btnUpdate As Button
    Friend WithEvents prgDownload As ProgressBar
    Friend WithEvents lnkReleasePage As LinkLabel
    Friend WithEvents btnClose As Button

End Class
