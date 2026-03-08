<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()>
Partial Class SplashScreen
    Inherits System.Windows.Forms.Form

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

    Private components As System.ComponentModel.IContainer

    <System.Diagnostics.DebuggerStepThrough()>
    Private Sub InitializeComponent()
        picLogo = New PictureBox()
        lblAppName = New Label()
        lblVersion = New Label()
        lblLoading = New Label()
        CType(picLogo, System.ComponentModel.ISupportInitialize).BeginInit()
        SuspendLayout()
        '
        ' picLogo
        '
        picLogo.BackColor = Color.Transparent
        picLogo.Location = New Point(150, 30)
        picLogo.Name = "picLogo"
        picLogo.Size = New Size(100, 100)
        picLogo.SizeMode = PictureBoxSizeMode.Zoom
        picLogo.TabIndex = 0
        picLogo.TabStop = False
        '
        ' lblAppName
        '
        lblAppName.BackColor = Color.Transparent
        lblAppName.Dock = DockStyle.None
        lblAppName.Font = New Font("Segoe UI", 18.0F, FontStyle.Bold)
        lblAppName.ForeColor = Color.White
        lblAppName.Location = New Point(0, 145)
        lblAppName.Name = "lblAppName"
        lblAppName.Size = New Size(400, 40)
        lblAppName.TabIndex = 1
        lblAppName.Text = "OraDB DUMP Viewer"
        lblAppName.TextAlign = ContentAlignment.MiddleCenter
        '
        ' lblVersion
        '
        lblVersion.BackColor = Color.Transparent
        lblVersion.Font = New Font("Segoe UI", 10.0F)
        lblVersion.ForeColor = Color.FromArgb(180, 180, 180)
        lblVersion.Location = New Point(0, 185)
        lblVersion.Name = "lblVersion"
        lblVersion.Size = New Size(400, 25)
        lblVersion.TabIndex = 2
        lblVersion.Text = ""
        lblVersion.TextAlign = ContentAlignment.MiddleCenter
        '
        ' lblLoading
        '
        lblLoading.BackColor = Color.Transparent
        lblLoading.Font = New Font("Segoe UI", 9.0F)
        lblLoading.ForeColor = Color.FromArgb(120, 120, 120)
        lblLoading.Location = New Point(0, 225)
        lblLoading.Name = "lblLoading"
        lblLoading.Size = New Size(400, 20)
        lblLoading.TabIndex = 3
        lblLoading.Text = "Loading..."
        lblLoading.TextAlign = ContentAlignment.MiddleCenter
        '
        ' SplashScreen
        '
        AutoScaleDimensions = New SizeF(7.0F, 15.0F)
        AutoScaleMode = AutoScaleMode.Font
        BackColor = Color.FromArgb(30, 30, 30)
        ClientSize = New Size(400, 260)
        Controls.Add(picLogo)
        Controls.Add(lblAppName)
        Controls.Add(lblVersion)
        Controls.Add(lblLoading)
        FormBorderStyle = FormBorderStyle.None
        Name = "SplashScreen"
        ShowInTaskbar = False
        StartPosition = FormStartPosition.CenterScreen
        CType(picLogo, System.ComponentModel.ISupportInitialize).EndInit()
        ResumeLayout(False)
    End Sub

    Friend WithEvents picLogo As PictureBox
    Friend WithEvents lblAppName As Label
    Friend WithEvents lblVersion As Label
    Friend WithEvents lblLoading As Label
End Class
