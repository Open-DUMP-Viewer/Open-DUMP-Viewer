<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class UpdateDownloadDialog
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
        lblStatus = New Label()
        prgDownload = New ProgressBar()
        btnCancel = New Button()
        SuspendLayout()
        '
        ' lblStatus
        '
        lblStatus.Location = New Point(16, 16)
        lblStatus.Name = "lblStatus"
        lblStatus.Size = New Size(408, 24)
        lblStatus.TabIndex = 0
        lblStatus.Text = "インストーラーをダウンロードしています..."
        lblStatus.TextAlign = ContentAlignment.MiddleLeft
        '
        ' prgDownload
        '
        prgDownload.Location = New Point(16, 46)
        prgDownload.Name = "prgDownload"
        prgDownload.Size = New Size(408, 22)
        prgDownload.Style = ProgressBarStyle.Marquee
        prgDownload.TabIndex = 1
        '
        ' btnCancel
        '
        btnCancel.Location = New Point(334, 80)
        btnCancel.Name = "btnCancel"
        btnCancel.Size = New Size(90, 30)
        btnCancel.TabIndex = 2
        btnCancel.Text = "キャンセル"
        '
        ' UpdateDownloadDialog
        '
        AutoScaleDimensions = New SizeF(7F, 15F)
        AutoScaleMode = AutoScaleMode.Font
        CancelButton = btnCancel
        ClientSize = New Size(440, 124)
        Controls.Add(lblStatus)
        Controls.Add(prgDownload)
        Controls.Add(btnCancel)
        FormBorderStyle = FormBorderStyle.FixedDialog
        MaximizeBox = False
        MinimizeBox = False
        Name = "UpdateDownloadDialog"
        ShowInTaskbar = False
        StartPosition = FormStartPosition.CenterScreen
        Text = "アップデート"
        ResumeLayout(False)
    End Sub

    Friend WithEvents lblStatus As Label
    Friend WithEvents prgDownload As ProgressBar
    Friend WithEvents btnCancel As Button

End Class
