''' <summary>
''' エクスポートオプション設定ダイアログ
''' 日付フォーマット、CSV オプション、SQL スクリプトオプションを設定
''' </summary>
Public Class ExportOptionsDialog
    Implements ILocalizable

    Public Sub New()
        InitializeComponent()
        ApplyLocalization()
    End Sub

    Private Sub ExportOptionsDialog_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ' ExportOptions から現在の設定をコントロールに反映
        Select Case ExportOptions.DateFormat
            Case Open_NativeParser.DATE_FMT_SLASH : rdoSlash.Checked = True
            Case Open_NativeParser.DATE_FMT_COMPACT : rdoCompact.Checked = True
            Case Open_NativeParser.DATE_FMT_FULL : rdoFull.Checked = True
            Case Open_NativeParser.DATE_FMT_CUSTOM : rdoCustom.Checked = True
            Case Else : rdoSlash.Checked = True
        End Select

        txtCustomFormat.Text = ExportOptions.CustomDateFormat
        txtCustomFormat.Enabled = rdoCustom.Checked

        chkCsvHeader.Checked = ExportOptions.CsvWriteHeader
        chkCsvTypes.Checked = ExportOptions.CsvWriteTypes

        ' デリミタ選択
        cboDelimiter.Items.Clear()
        cboDelimiter.Items.Add(Loc.S("ExportOptions_DelimiterComma"))
        cboDelimiter.Items.Add(Loc.S("ExportOptions_DelimiterTab"))
        cboDelimiter.Items.Add(Loc.S("ExportOptions_DelimiterSemicolon"))
        cboDelimiter.Items.Add(Loc.S("ExportOptions_DelimiterPipe"))
        Select Case ExportOptions.CsvDelimiter
            Case vbTab : cboDelimiter.SelectedIndex = 1
            Case ";" : cboDelimiter.SelectedIndex = 2
            Case "|" : cboDelimiter.SelectedIndex = 3
            Case Else : cboDelimiter.SelectedIndex = 0
        End Select

        chkCreateTable.Checked = ExportOptions.SqlCreateTable
        chkCreateIndex.Checked = ExportOptions.SqlCreateIndex
        chkWriteComments.Checked = ExportOptions.SqlWriteComments
        chkWriteInserts.Checked = ExportOptions.SqlWriteInserts
        chkInferInteger.Checked = ExportOptions.SqlInferInteger

        ' Oracle Client
        txtImpdpPath.Text = ExportOptions.ImpdpPath
    End Sub

    Private Sub btnBrowseImpdp_Click(sender As Object, e As EventArgs) Handles btnBrowseImpdp.Click
        Using dlg As New OpenFileDialog()
            dlg.Title = "impdp.exe"
            dlg.Filter = "impdp.exe|impdp.exe|All files|*.*"
            If Not String.IsNullOrEmpty(txtImpdpPath.Text) Then
                Try
                    dlg.InitialDirectory = IO.Path.GetDirectoryName(txtImpdpPath.Text)
                Catch
                End Try
            End If
            If dlg.ShowDialog() = DialogResult.OK Then
                txtImpdpPath.Text = dlg.FileName
            End If
        End Using
    End Sub

    Private Async Sub btnAutoSetup_Click(sender As Object, e As EventArgs) Handles btnAutoSetup.Click
        ' Oracle Instant Client + Tools を自動ダウンロード・展開
        Dim installDir = IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Open_DUMP_Viewer", "oracle_client")

        Dim result = MessageBox.Show(
            Loc.S("ExportOptions_AutoSetupConfirm"),
            Loc.S("ExportOptions_OracleClient"),
            MessageBoxButtons.YesNo, MessageBoxIcon.Question)
        If result <> DialogResult.Yes Then Return

        btnAutoSetup.Enabled = False
        btnAutoSetup.Text = Loc.S("ExportOptions_AutoSetupProgress")

        Try
            Dim impdpPath = Await Task.Run(Function() InstallOracleClient(installDir))
            If Not String.IsNullOrEmpty(impdpPath) Then
                txtImpdpPath.Text = impdpPath
                MessageBox.Show(Loc.S("ExportOptions_AutoSetupComplete"),
                    Loc.S("ExportOptions_OracleClient"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            End If
        Catch ex As Exception
            MessageBox.Show(ex.Message, Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        Finally
            btnAutoSetup.Enabled = True
            btnAutoSetup.Text = Loc.S("ExportOptions_AutoSetupButton")
        End Try
    End Sub

    ''' <summary>
    ''' Oracle Instant Client を自動ダウンロード・展開する。
    ''' ダウンロード URL は odv.dev の JSON エンドポイントから取得し、
    ''' Oracle のバージョン更新に自動追従する。
    ''' </summary>
    Private Shared Function InstallOracleClient(installDir As String) As String
        IO.Directory.CreateDirectory(installDir)

        ' ダウンロード URL を JSON エンドポイントから取得
        ' （Oracle 公式 URL が変更されても、エンドポイントの更新だけで対応可能）
        Dim basicUrl As String = Nothing
        Dim toolsUrl As String = Nothing

        Using client As New Net.Http.HttpClient()
            client.Timeout = TimeSpan.FromMinutes(10)
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")

            Dim json = client.GetStringAsync("https://oracle-dl.odv.dev/").Result
            Dim doc = System.Text.Json.JsonDocument.Parse(json)

            If Not doc.RootElement.TryGetProperty("basic", Nothing) OrElse
               Not doc.RootElement.TryGetProperty("tools", Nothing) Then
                Throw New Exception("Oracle Client download URLs not found. Please try again later.")
            End If

            basicUrl = doc.RootElement.GetProperty("basic").GetString()
            toolsUrl = doc.RootElement.GetProperty("tools").GetString()

            If String.IsNullOrEmpty(basicUrl) OrElse String.IsNullOrEmpty(toolsUrl) Then
                Throw New Exception("Oracle Client download URLs are empty. Please try again later.")
            End If

            ' Basic パッケージ
            Dim basicZip = IO.Path.Combine(installDir, "basic.zip")
            If Not IO.File.Exists(basicZip) Then
                Dim data = client.GetByteArrayAsync(basicUrl).Result
                IO.File.WriteAllBytes(basicZip, data)
            End If

            ' Tools パッケージ
            Dim toolsZip = IO.Path.Combine(installDir, "tools.zip")
            If Not IO.File.Exists(toolsZip) Then
                Dim data = client.GetByteArrayAsync(toolsUrl).Result
                IO.File.WriteAllBytes(toolsZip, data)
            End If

            ' 展開
            IO.Compression.ZipFile.ExtractToDirectory(basicZip, installDir, True)
            IO.Compression.ZipFile.ExtractToDirectory(toolsZip, installDir, True)
        End Using

        ' impdp.exe を探す
        Dim impdpFiles = IO.Directory.GetFiles(installDir, "impdp.exe", IO.SearchOption.AllDirectories)
        If impdpFiles.Length > 0 Then
            Return impdpFiles(0)
        End If

        Throw New IO.FileNotFoundException("impdp.exe not found after extraction")
    End Function

    Private Sub rdoCustom_CheckedChanged(sender As Object, e As EventArgs) Handles rdoCustom.CheckedChanged
        txtCustomFormat.Enabled = rdoCustom.Checked
    End Sub

    Private Sub btnOK_Click(sender As Object, e As EventArgs) Handles btnOK.Click
        ' コントロールの値を ExportOptions に書き戻す
        If rdoSlash.Checked Then
            ExportOptions.DateFormat = Open_NativeParser.DATE_FMT_SLASH
        ElseIf rdoCompact.Checked Then
            ExportOptions.DateFormat = Open_NativeParser.DATE_FMT_COMPACT
        ElseIf rdoFull.Checked Then
            ExportOptions.DateFormat = Open_NativeParser.DATE_FMT_FULL
        ElseIf rdoCustom.Checked Then
            ExportOptions.DateFormat = Open_NativeParser.DATE_FMT_CUSTOM
        End If

        ExportOptions.CustomDateFormat = txtCustomFormat.Text
        ExportOptions.CsvWriteHeader = chkCsvHeader.Checked
        ExportOptions.CsvWriteTypes = chkCsvTypes.Checked

        ' デリミタ保存
        Select Case cboDelimiter.SelectedIndex
            Case 1 : ExportOptions.CsvDelimiter = vbTab
            Case 2 : ExportOptions.CsvDelimiter = ";"
            Case 3 : ExportOptions.CsvDelimiter = "|"
            Case Else : ExportOptions.CsvDelimiter = ","
        End Select

        ExportOptions.SqlCreateTable = chkCreateTable.Checked
        ExportOptions.SqlCreateIndex = chkCreateIndex.Checked
        ExportOptions.SqlWriteComments = chkWriteComments.Checked
        ExportOptions.SqlWriteInserts = chkWriteInserts.Checked
        ExportOptions.SqlInferInteger = chkInferInteger.Checked
        ExportOptions.ImpdpPath = txtImpdpPath.Text.Trim()

        ' 設定を永続化
        ExportOptions.Save()
    End Sub

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("ExportOptions_FormTitle")
        grpDateFormat.Text = Loc.S("ExportOptions_DateFormat")
        rdoSlash.Text = Loc.S("ExportOptions_DateSlash")
        rdoCustom.Text = Loc.S("ExportOptions_DateCustom")
        grpCsvOptions.Text = Loc.S("ExportOptions_CsvOptions")
        chkCsvHeader.Text = Loc.S("ExportOptions_CsvWriteHeader")
        chkCsvTypes.Text = Loc.S("ExportOptions_CsvWriteTypes")
        lblDelimiter.Text = Loc.S("ExportOptions_CsvDelimiter")
        grpSqlOptions.Text = Loc.S("ExportOptions_SqlOptions")
        chkCreateTable.Text = Loc.S("ExportOptions_SqlCreateTable")
        chkCreateIndex.Text = Loc.S("ExportOptions_SqlCreateIndex")
        chkWriteComments.Text = Loc.S("ExportOptions_SqlWriteComments")
        chkWriteInserts.Text = Loc.S("ExportOptions_SqlWriteInserts")
        chkInferInteger.Text = Loc.S("ExportOptions_SqlInferInteger")
        grpOracleClient.Text = Loc.S("ExportOptions_OracleClient")
        lblImpdpInfo.Text = Loc.S("ExportOptions_ImpdpInfo")
        btnOK.Text = Loc.S("Button_OK")
        btnCancel.Text = Loc.S("Button_Cancel")
    End Sub
#End Region

End Class
