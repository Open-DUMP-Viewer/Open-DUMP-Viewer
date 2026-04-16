<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class ErrorReportDialog
    Inherits System.Windows.Forms.Form

    'フォームがコンポーネントの一覧をクリーンアップするために dispose をオーバーライドします。
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
        chkAttachDump = New CheckBox()
        lblTitleCaption = New Label()
        txtTitle = New TextBox()
        lblDescCaption = New Label()
        txtDescription = New TextBox()
        lblContactCaption = New Label()
        txtContact = New TextBox()
        lblContactHint = New Label()
        lblSysInfoHeader = New Label()
        lblVersionCaption = New Label()
        lblVersionValue = New Label()
        lblDllVersionCaption = New Label()
        lblDllVersionValue = New Label()
        lblOSCaption = New Label()
        lblOSValue = New Label()
        lblDotNetCaption = New Label()
        lblDotNetValue = New Label()
        lblArchCaption = New Label()
        lblArchValue = New Label()
        lblLocaleCaption = New Label()
        lblLocaleValue = New Label()
        lblDpiCaption = New Label()
        lblDpiValue = New Label()
        lblMemoryCaption = New Label()
        lblMemoryValue = New Label()
        lblScreenCaption = New Label()
        lblScreenValue = New Label()
        lblDumpInfoCaption = New Label()
        lblDumpInfoValue = New Label()
        pnlButtons = New Panel()
        lnkIssue = New LinkLabel()
        lblStatus = New Label()
        prgSubmit = New ProgressBar()
        btnCancel = New Button()
        btnSubmit = New Button()
        tblMain.SuspendLayout()
        pnlButtons.SuspendLayout()
        SuspendLayout()
        ' 
        ' tblMain
        ' 
        tblMain.ColumnCount = 2
        tblMain.ColumnStyles.Add(New ColumnStyle(SizeType.Absolute, 143F))
        tblMain.ColumnStyles.Add(New ColumnStyle(SizeType.Percent, 100F))
        tblMain.Controls.Add(lblTitleCaption, 0, 0)
        tblMain.Controls.Add(txtTitle, 1, 0)
        tblMain.Controls.Add(lblDescCaption, 0, 1)
        tblMain.Controls.Add(txtDescription, 1, 1)
        tblMain.Controls.Add(lblContactCaption, 0, 2)
        tblMain.Controls.Add(txtContact, 1, 2)
        tblMain.Controls.Add(lblContactHint, 1, 3)
        tblMain.Controls.Add(lblSysInfoHeader, 0, 4)
        tblMain.Controls.Add(lblVersionCaption, 0, 5)
        tblMain.Controls.Add(lblVersionValue, 1, 5)
        tblMain.Controls.Add(lblDllVersionCaption, 0, 6)
        tblMain.Controls.Add(lblDllVersionValue, 1, 6)
        tblMain.Controls.Add(lblOSCaption, 0, 7)
        tblMain.Controls.Add(lblOSValue, 1, 7)
        tblMain.Controls.Add(lblDotNetCaption, 0, 8)
        tblMain.Controls.Add(lblDotNetValue, 1, 8)
        tblMain.Controls.Add(lblArchCaption, 0, 9)
        tblMain.Controls.Add(lblArchValue, 1, 9)
        tblMain.Controls.Add(lblLocaleCaption, 0, 10)
        tblMain.Controls.Add(lblLocaleValue, 1, 10)
        tblMain.Controls.Add(lblDpiCaption, 0, 11)
        tblMain.Controls.Add(lblDpiValue, 1, 11)
        tblMain.Controls.Add(lblMemoryCaption, 0, 12)
        tblMain.Controls.Add(lblMemoryValue, 1, 12)
        tblMain.Controls.Add(lblScreenCaption, 0, 13)
        tblMain.Controls.Add(lblScreenValue, 1, 13)
        tblMain.Controls.Add(lblDumpInfoCaption, 0, 14)
        tblMain.Controls.Add(lblDumpInfoValue, 1, 14)
        tblMain.Controls.Add(chkAttachDump, 1, 15)
        tblMain.Dock = DockStyle.Fill
        tblMain.Location = New Point(0, 0)
        tblMain.Margin = New Padding(4, 5, 4, 5)
        tblMain.Name = "tblMain"
        tblMain.Padding = New Padding(17, 20, 17, 7)
        tblMain.RowCount = 16
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 50F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Percent, 100F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 50F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 33F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 40F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 30F))
        tblMain.RowStyles.Add(New RowStyle(SizeType.Absolute, 35F))
        tblMain.Size = New Size(743, 765)
        tblMain.TabIndex = 0
        ' 
        ' lblTitleCaption
        ' 
        lblTitleCaption.Dock = DockStyle.Fill
        lblTitleCaption.Location = New Point(21, 20)
        lblTitleCaption.Margin = New Padding(4, 0, 4, 0)
        lblTitleCaption.Name = "lblTitleCaption"
        lblTitleCaption.Size = New Size(135, 50)
        lblTitleCaption.TabIndex = 0
        lblTitleCaption.Text = "件名:"
        lblTitleCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' txtTitle
        ' 
        txtTitle.Dock = DockStyle.Fill
        txtTitle.Location = New Point(164, 25)
        txtTitle.Margin = New Padding(4, 5, 4, 5)
        txtTitle.MaxLength = 100
        txtTitle.Name = "txtTitle"
        txtTitle.Size = New Size(558, 31)
        txtTitle.TabIndex = 1
        ' 
        ' lblDescCaption
        ' 
        lblDescCaption.Location = New Point(21, 70)
        lblDescCaption.Margin = New Padding(4, 0, 4, 0)
        lblDescCaption.Name = "lblDescCaption"
        lblDescCaption.Padding = New Padding(0, 7, 0, 0)
        lblDescCaption.Size = New Size(134, 38)
        lblDescCaption.TabIndex = 2
        lblDescCaption.Text = "内容:"
        ' 
        ' txtDescription
        ' 
        txtDescription.Dock = DockStyle.Fill
        txtDescription.Location = New Point(164, 75)
        txtDescription.Margin = New Padding(4, 5, 4, 5)
        txtDescription.AcceptsReturn = True
        txtDescription.MaxLength = 5000
        txtDescription.Multiline = True
        txtDescription.Name = "txtDescription"
        txtDescription.ScrollBars = ScrollBars.Vertical
        txtDescription.Size = New Size(558, 220)
        txtDescription.TabIndex = 3
        ' 
        ' lblContactCaption
        ' 
        lblContactCaption.Dock = DockStyle.Fill
        lblContactCaption.Location = New Point(21, 300)
        lblContactCaption.Margin = New Padding(4, 0, 4, 0)
        lblContactCaption.Name = "lblContactCaption"
        lblContactCaption.Size = New Size(135, 50)
        lblContactCaption.TabIndex = 4
        lblContactCaption.Text = "連絡先:"
        lblContactCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' txtContact
        ' 
        txtContact.Dock = DockStyle.Fill
        txtContact.Location = New Point(164, 305)
        txtContact.Margin = New Padding(4, 5, 4, 5)
        txtContact.MaxLength = 254
        txtContact.Name = "txtContact"
        txtContact.Size = New Size(558, 31)
        txtContact.TabIndex = 5
        ' 
        ' lblContactHint
        ' 
        lblContactHint.Dock = DockStyle.Fill
        lblContactHint.ForeColor = Color.Gray
        lblContactHint.Location = New Point(164, 350)
        lblContactHint.Margin = New Padding(4, 0, 4, 0)
        lblContactHint.Name = "lblContactHint"
        lblContactHint.Size = New Size(558, 33)
        lblContactHint.TabIndex = 6
        lblContactHint.Text = "(任意) メールアドレスなど、返信先をご記入ください"
        ' 
        ' lblSysInfoHeader
        ' 
        tblMain.SetColumnSpan(lblSysInfoHeader, 2)
        lblSysInfoHeader.Dock = DockStyle.Fill
        lblSysInfoHeader.ForeColor = Color.Gray
        lblSysInfoHeader.Location = New Point(21, 383)
        lblSysInfoHeader.Margin = New Padding(4, 0, 4, 0)
        lblSysInfoHeader.Name = "lblSysInfoHeader"
        lblSysInfoHeader.Size = New Size(701, 40)
        lblSysInfoHeader.TabIndex = 7
        lblSysInfoHeader.Text = "── 以下の環境情報が自動的に送信されます (個人情報は含まれません) ──"
        lblSysInfoHeader.TextAlign = ContentAlignment.BottomLeft
        ' 
        ' lblVersionCaption
        ' 
        lblVersionCaption.Dock = DockStyle.Fill
        lblVersionCaption.ForeColor = Color.Gray
        lblVersionCaption.Location = New Point(21, 423)
        lblVersionCaption.Margin = New Padding(4, 0, 4, 0)
        lblVersionCaption.Name = "lblVersionCaption"
        lblVersionCaption.Size = New Size(135, 30)
        lblVersionCaption.TabIndex = 8
        lblVersionCaption.Text = "アプリ:"
        lblVersionCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblVersionValue
        ' 
        lblVersionValue.Dock = DockStyle.Fill
        lblVersionValue.ForeColor = Color.Gray
        lblVersionValue.Location = New Point(164, 423)
        lblVersionValue.Margin = New Padding(4, 0, 4, 0)
        lblVersionValue.Name = "lblVersionValue"
        lblVersionValue.Size = New Size(558, 30)
        lblVersionValue.TabIndex = 9
        lblVersionValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDllVersionCaption
        ' 
        lblDllVersionCaption.Dock = DockStyle.Fill
        lblDllVersionCaption.ForeColor = Color.Gray
        lblDllVersionCaption.Location = New Point(21, 453)
        lblDllVersionCaption.Margin = New Padding(4, 0, 4, 0)
        lblDllVersionCaption.Name = "lblDllVersionCaption"
        lblDllVersionCaption.Size = New Size(135, 30)
        lblDllVersionCaption.TabIndex = 10
        lblDllVersionCaption.Text = "DLL:"
        lblDllVersionCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDllVersionValue
        ' 
        lblDllVersionValue.Dock = DockStyle.Fill
        lblDllVersionValue.ForeColor = Color.Gray
        lblDllVersionValue.Location = New Point(164, 453)
        lblDllVersionValue.Margin = New Padding(4, 0, 4, 0)
        lblDllVersionValue.Name = "lblDllVersionValue"
        lblDllVersionValue.Size = New Size(558, 30)
        lblDllVersionValue.TabIndex = 11
        lblDllVersionValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblOSCaption
        ' 
        lblOSCaption.Dock = DockStyle.Fill
        lblOSCaption.ForeColor = Color.Gray
        lblOSCaption.Location = New Point(21, 483)
        lblOSCaption.Margin = New Padding(4, 0, 4, 0)
        lblOSCaption.Name = "lblOSCaption"
        lblOSCaption.Size = New Size(135, 30)
        lblOSCaption.TabIndex = 12
        lblOSCaption.Text = "OS:"
        lblOSCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblOSValue
        ' 
        lblOSValue.Dock = DockStyle.Fill
        lblOSValue.ForeColor = Color.Gray
        lblOSValue.Location = New Point(164, 483)
        lblOSValue.Margin = New Padding(4, 0, 4, 0)
        lblOSValue.Name = "lblOSValue"
        lblOSValue.Size = New Size(558, 30)
        lblOSValue.TabIndex = 13
        lblOSValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDotNetCaption
        ' 
        lblDotNetCaption.Dock = DockStyle.Fill
        lblDotNetCaption.ForeColor = Color.Gray
        lblDotNetCaption.Location = New Point(21, 513)
        lblDotNetCaption.Margin = New Padding(4, 0, 4, 0)
        lblDotNetCaption.Name = "lblDotNetCaption"
        lblDotNetCaption.Size = New Size(135, 30)
        lblDotNetCaption.TabIndex = 14
        lblDotNetCaption.Text = ".NET:"
        lblDotNetCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDotNetValue
        ' 
        lblDotNetValue.Dock = DockStyle.Fill
        lblDotNetValue.ForeColor = Color.Gray
        lblDotNetValue.Location = New Point(164, 513)
        lblDotNetValue.Margin = New Padding(4, 0, 4, 0)
        lblDotNetValue.Name = "lblDotNetValue"
        lblDotNetValue.Size = New Size(558, 30)
        lblDotNetValue.TabIndex = 15
        lblDotNetValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblArchCaption
        ' 
        lblArchCaption.Dock = DockStyle.Fill
        lblArchCaption.ForeColor = Color.Gray
        lblArchCaption.Location = New Point(21, 543)
        lblArchCaption.Margin = New Padding(4, 0, 4, 0)
        lblArchCaption.Name = "lblArchCaption"
        lblArchCaption.Size = New Size(135, 30)
        lblArchCaption.TabIndex = 16
        lblArchCaption.Text = "CPU:"
        lblArchCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblArchValue
        ' 
        lblArchValue.Dock = DockStyle.Fill
        lblArchValue.ForeColor = Color.Gray
        lblArchValue.Location = New Point(164, 543)
        lblArchValue.Margin = New Padding(4, 0, 4, 0)
        lblArchValue.Name = "lblArchValue"
        lblArchValue.Size = New Size(558, 30)
        lblArchValue.TabIndex = 17
        lblArchValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblLocaleCaption
        ' 
        lblLocaleCaption.Dock = DockStyle.Fill
        lblLocaleCaption.ForeColor = Color.Gray
        lblLocaleCaption.Location = New Point(21, 573)
        lblLocaleCaption.Margin = New Padding(4, 0, 4, 0)
        lblLocaleCaption.Name = "lblLocaleCaption"
        lblLocaleCaption.Size = New Size(135, 30)
        lblLocaleCaption.TabIndex = 18
        lblLocaleCaption.Text = "ロケール:"
        lblLocaleCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblLocaleValue
        ' 
        lblLocaleValue.Dock = DockStyle.Fill
        lblLocaleValue.ForeColor = Color.Gray
        lblLocaleValue.Location = New Point(164, 573)
        lblLocaleValue.Margin = New Padding(4, 0, 4, 0)
        lblLocaleValue.Name = "lblLocaleValue"
        lblLocaleValue.Size = New Size(558, 30)
        lblLocaleValue.TabIndex = 19
        lblLocaleValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDpiCaption
        ' 
        lblDpiCaption.Dock = DockStyle.Fill
        lblDpiCaption.ForeColor = Color.Gray
        lblDpiCaption.Location = New Point(21, 603)
        lblDpiCaption.Margin = New Padding(4, 0, 4, 0)
        lblDpiCaption.Name = "lblDpiCaption"
        lblDpiCaption.Size = New Size(135, 30)
        lblDpiCaption.TabIndex = 20
        lblDpiCaption.Text = "DPI:"
        lblDpiCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDpiValue
        ' 
        lblDpiValue.Dock = DockStyle.Fill
        lblDpiValue.ForeColor = Color.Gray
        lblDpiValue.Location = New Point(164, 603)
        lblDpiValue.Margin = New Padding(4, 0, 4, 0)
        lblDpiValue.Name = "lblDpiValue"
        lblDpiValue.Size = New Size(558, 30)
        lblDpiValue.TabIndex = 21
        lblDpiValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblMemoryCaption
        ' 
        lblMemoryCaption.Dock = DockStyle.Fill
        lblMemoryCaption.ForeColor = Color.Gray
        lblMemoryCaption.Location = New Point(21, 633)
        lblMemoryCaption.Margin = New Padding(4, 0, 4, 0)
        lblMemoryCaption.Name = "lblMemoryCaption"
        lblMemoryCaption.Size = New Size(135, 30)
        lblMemoryCaption.TabIndex = 22
        lblMemoryCaption.Text = "メモリ:"
        lblMemoryCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblMemoryValue
        ' 
        lblMemoryValue.Dock = DockStyle.Fill
        lblMemoryValue.ForeColor = Color.Gray
        lblMemoryValue.Location = New Point(164, 633)
        lblMemoryValue.Margin = New Padding(4, 0, 4, 0)
        lblMemoryValue.Name = "lblMemoryValue"
        lblMemoryValue.Size = New Size(558, 30)
        lblMemoryValue.TabIndex = 23
        lblMemoryValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblScreenCaption
        ' 
        lblScreenCaption.Dock = DockStyle.Fill
        lblScreenCaption.ForeColor = Color.Gray
        lblScreenCaption.Location = New Point(21, 663)
        lblScreenCaption.Margin = New Padding(4, 0, 4, 0)
        lblScreenCaption.Name = "lblScreenCaption"
        lblScreenCaption.Size = New Size(135, 30)
        lblScreenCaption.TabIndex = 24
        lblScreenCaption.Text = "画面:"
        lblScreenCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblScreenValue
        ' 
        lblScreenValue.Dock = DockStyle.Fill
        lblScreenValue.ForeColor = Color.Gray
        lblScreenValue.Location = New Point(164, 663)
        lblScreenValue.Margin = New Padding(4, 0, 4, 0)
        lblScreenValue.Name = "lblScreenValue"
        lblScreenValue.Size = New Size(558, 30)
        lblScreenValue.TabIndex = 25
        lblScreenValue.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDumpInfoCaption
        ' 
        lblDumpInfoCaption.Dock = DockStyle.Fill
        lblDumpInfoCaption.ForeColor = Color.Gray
        lblDumpInfoCaption.Location = New Point(21, 693)
        lblDumpInfoCaption.Margin = New Padding(4, 0, 4, 0)
        lblDumpInfoCaption.Name = "lblDumpInfoCaption"
        lblDumpInfoCaption.Size = New Size(135, 30)
        lblDumpInfoCaption.TabIndex = 26
        lblDumpInfoCaption.Text = "ダンプ:"
        lblDumpInfoCaption.TextAlign = ContentAlignment.MiddleLeft
        ' 
        ' lblDumpInfoValue
        ' 
        lblDumpInfoValue.Dock = DockStyle.Fill
        lblDumpInfoValue.ForeColor = Color.Gray
        lblDumpInfoValue.Location = New Point(164, 693)
        lblDumpInfoValue.Margin = New Padding(4, 0, 4, 0)
        lblDumpInfoValue.Name = "lblDumpInfoValue"
        lblDumpInfoValue.Size = New Size(558, 30)
        lblDumpInfoValue.TabIndex = 27
        lblDumpInfoValue.TextAlign = ContentAlignment.MiddleLeft
        '
        ' chkAttachDump
        '
        chkAttachDump.Dock = DockStyle.Fill
        chkAttachDump.ForeColor = Color.Gray
        chkAttachDump.Location = New Point(164, 727)
        chkAttachDump.Margin = New Padding(4, 0, 4, 0)
        chkAttachDump.Name = "chkAttachDump"
        chkAttachDump.Size = New Size(558, 35)
        chkAttachDump.TabIndex = 28
        chkAttachDump.Text = "ダンプファイルを添付する (50 MB以下)"
        '
        ' pnlButtons
        '
        pnlButtons.Controls.Add(lnkIssue)
        pnlButtons.Controls.Add(lblStatus)
        pnlButtons.Controls.Add(prgSubmit)
        pnlButtons.Controls.Add(btnCancel)
        pnlButtons.Controls.Add(btnSubmit)
        pnlButtons.Dock = DockStyle.Bottom
        pnlButtons.Location = New Point(0, 765)
        pnlButtons.Margin = New Padding(4, 5, 4, 5)
        pnlButtons.Name = "pnlButtons"
        pnlButtons.Padding = New Padding(17, 7, 17, 13)
        pnlButtons.Size = New Size(743, 167)
        pnlButtons.TabIndex = 1
        ' 
        ' lnkIssue
        ' 
        lnkIssue.Anchor = AnchorStyles.Top Or AnchorStyles.Right
        lnkIssue.Location = New Point(457, 113)
        lnkIssue.Margin = New Padding(4, 0, 4, 0)
        lnkIssue.Name = "lnkIssue"
        lnkIssue.Size = New Size(266, 33)
        lnkIssue.TabIndex = 4
        lnkIssue.TextAlign = ContentAlignment.MiddleRight
        lnkIssue.Visible = False
        ' 
        ' lblStatus
        ' 
        lblStatus.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        lblStatus.Location = New Point(21, 113)
        lblStatus.Margin = New Padding(4, 0, 4, 0)
        lblStatus.Name = "lblStatus"
        lblStatus.Size = New Size(557, 33)
        lblStatus.TabIndex = 3
        lblStatus.Visible = False
        ' 
        ' prgSubmit
        ' 
        prgSubmit.Anchor = AnchorStyles.Top Or AnchorStyles.Left Or AnchorStyles.Right
        prgSubmit.Location = New Point(21, 73)
        prgSubmit.Margin = New Padding(4, 5, 4, 5)
        prgSubmit.Name = "prgSubmit"
        prgSubmit.Size = New Size(700, 30)
        prgSubmit.Style = ProgressBarStyle.Marquee
        prgSubmit.TabIndex = 2
        prgSubmit.Visible = False
        ' 
        ' btnCancel
        ' 
        btnCancel.Anchor = AnchorStyles.Top Or AnchorStyles.Right
        btnCancel.DialogResult = DialogResult.Cancel
        btnCancel.Location = New Point(469, 12)
        btnCancel.Margin = New Padding(4, 5, 4, 5)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(123, 50)
        btnCancel.TabIndex = 1
        btnCancel.Text = "キャンセル"
        ' 
        ' btnSubmit
        ' 
        btnSubmit.Anchor = AnchorStyles.Top Or AnchorStyles.Right
        btnSubmit.Location = New Point(600, 12)
        btnSubmit.Margin = New Padding(4, 5, 4, 5)
        btnSubmit.Name = "btnSubmit"
        btnSubmit.Size = New Size(123, 50)
        btnSubmit.TabIndex = 0
        btnSubmit.Text = "送信"
        ' 
        ' ErrorReportDialog
        ' 
        AcceptButton = btnSubmit
        AutoScaleDimensions = New SizeF(10F, 25F)
        AutoScaleMode = AutoScaleMode.Font
        CancelButton = btnCancel
        ClientSize = New Size(743, 932)
        Controls.Add(tblMain)
        Controls.Add(pnlButtons)
        FormBorderStyle = FormBorderStyle.FixedDialog
        Margin = New Padding(4, 5, 4, 5)
        MaximizeBox = False
        MinimizeBox = False
        Name = "ErrorReportDialog"
        StartPosition = FormStartPosition.CenterParent
        Text = "エラー報告"
        tblMain.ResumeLayout(False)
        tblMain.PerformLayout()
        pnlButtons.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents tblMain As TableLayoutPanel
    Friend WithEvents lblTitleCaption As Label
    Friend WithEvents txtTitle As TextBox
    Friend WithEvents lblDescCaption As Label
    Friend WithEvents txtDescription As TextBox
    Friend WithEvents lblContactCaption As Label
    Friend WithEvents txtContact As TextBox
    Friend WithEvents lblContactHint As Label
    Friend WithEvents lblSysInfoHeader As Label
    Friend WithEvents lblVersionCaption As Label
    Friend WithEvents lblVersionValue As Label
    Friend WithEvents lblDllVersionCaption As Label
    Friend WithEvents lblDllVersionValue As Label
    Friend WithEvents lblOSCaption As Label
    Friend WithEvents lblOSValue As Label
    Friend WithEvents lblDotNetCaption As Label
    Friend WithEvents lblDotNetValue As Label
    Friend WithEvents lblArchCaption As Label
    Friend WithEvents lblArchValue As Label
    Friend WithEvents lblLocaleCaption As Label
    Friend WithEvents lblLocaleValue As Label
    Friend WithEvents lblDpiCaption As Label
    Friend WithEvents lblDpiValue As Label
    Friend WithEvents lblMemoryCaption As Label
    Friend WithEvents lblMemoryValue As Label
    Friend WithEvents lblScreenCaption As Label
    Friend WithEvents lblScreenValue As Label
    Friend WithEvents lblDumpInfoCaption As Label
    Friend WithEvents lblDumpInfoValue As Label
    Friend WithEvents pnlButtons As Panel
    Friend WithEvents btnSubmit As Button
    Friend WithEvents btnCancel As Button
    Friend WithEvents prgSubmit As ProgressBar
    Friend WithEvents lblStatus As Label
    Friend WithEvents lnkIssue As LinkLabel
    Friend WithEvents chkAttachDump As CheckBox

End Class
