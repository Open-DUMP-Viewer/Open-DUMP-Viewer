''' <summary>
''' LOBファイル抽出ダイアログ
'''
''' テーブルのBLOB/CLOB/NCLOBカラムのデータを
''' 個別ファイルとして抽出する。
''' </summary>
Public Class LobExtractDialog
    Implements ILocalizable

#Region "フィールド"
    Private _dumpFilePath As String
    Private _schema As String
    Private _tableName As String
    Private _columnNames As String()
    Private _columnTypes As String()
    Private _dataOffset As Long
#End Region

#Region "初期化"
    ''' <summary>
    ''' LOB抽出ダイアログを初期化
    ''' </summary>
    ''' <param name="dumpFilePath">DUMPファイルパス</param>
    ''' <param name="schema">スキーマ名</param>
    ''' <param name="tableName">テーブル名</param>
    ''' <param name="columnNames">カラム名配列</param>
    ''' <param name="columnTypes">カラム型文字列配列 ("BLOB", "CLOB(4000)" 等)</param>
    ''' <param name="dataOffset">データオフセット (高速シーク用)</param>
    Public Sub New(dumpFilePath As String, schema As String, tableName As String,
                   columnNames As String(), columnTypes As String(),
                   Optional dataOffset As Long = 0)
        InitializeComponent()
        ApplyLocalization()

        _dumpFilePath = dumpFilePath
        _schema = schema
        _tableName = tableName
        _columnNames = columnNames
        _columnTypes = columnTypes
        _dataOffset = dataOffset

        Me.Text = Loc.SF("LobExtract_FormTitle", tableName)

        ' ファイル名方式コンボボックス
        cboFilenameMethod.Items.Add(Loc.S("LobExtract_Sequential"))
        cboFilenameMethod.Items.Add(Loc.S("LobExtract_ColumnValue"))
        cboFilenameMethod.SelectedIndex = 0

        ' LOBカラムと全カラムをコンボに設定
        SetupColumnCombos()
    End Sub

    ''' <summary>
    ''' LOBカラム (BLOB/CLOB/NCLOB) をフィルタしてコンボボックスに設定
    ''' </summary>
    Private Sub SetupColumnCombos()
        If _columnNames Is Nothing OrElse _columnTypes Is Nothing Then Return

        For i As Integer = 0 To _columnNames.Length - 1
            Dim typUpper = If(_columnTypes.Length > i, _columnTypes(i).ToUpperInvariant(), "")
            ' LOBカラムのみ対象
            If typUpper.Contains("BLOB") OrElse typUpper.Contains("CLOB") Then
                cboLobColumn.Items.Add($"{_columnNames(i)} ({_columnTypes(i)})")
            End If
            ' 全カラムをファイル名候補に
            cboFilenameColumn.Items.Add(_columnNames(i))
        Next

        If cboLobColumn.Items.Count > 0 Then
            cboLobColumn.SelectedIndex = 0
        End If
        If cboFilenameColumn.Items.Count > 0 Then
            cboFilenameColumn.SelectedIndex = 0
        End If
    End Sub
#End Region

#Region "イベントハンドラ"
    Private Sub btnBrowse_Click(sender As Object, e As EventArgs) Handles btnBrowse.Click
        Using dlg As New FolderBrowserDialog()
            dlg.Description = Loc.S("LobExtract_BrowseTitle")
            If txtOutputDir.Text.Length > 0 AndAlso IO.Directory.Exists(txtOutputDir.Text) Then
                dlg.SelectedPath = txtOutputDir.Text
            End If
            If dlg.ShowDialog() = DialogResult.OK Then
                txtOutputDir.Text = dlg.SelectedPath
            End If
        End Using
    End Sub

    Private Sub cboFilenameMethod_SelectedIndexChanged(sender As Object, e As EventArgs) Handles cboFilenameMethod.SelectedIndexChanged
        ' カラム値選択時のみカラム名コンボを有効化
        cboFilenameColumn.Enabled = (cboFilenameMethod.SelectedIndex = 1)
    End Sub

    Private Sub btnExtract_Click(sender As Object, e As EventArgs) Handles btnExtract.Click
        ' バリデーション
        If cboLobColumn.SelectedIndex < 0 Then
            MessageBox.Show(Loc.S("LobExtract_SelectLobColumn"), Loc.S("Title_InputError"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        If String.IsNullOrWhiteSpace(txtOutputDir.Text) Then
            MessageBox.Show(Loc.S("LobExtract_SelectOutputFolder"), Loc.S("Title_InputError"),
                           MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If

        ' 出力ディレクトリを作成
        Try
            If Not IO.Directory.Exists(txtOutputDir.Text) Then
                IO.Directory.CreateDirectory(txtOutputDir.Text)
            End If
        Catch ex As Exception
            MessageBox.Show(Loc.SF("LobExtract_CreateFolderError", ex.Message), Loc.S("Title_Error"),
                           MessageBoxButtons.OK, MessageBoxIcon.Error)
            Return
        End Try

        ' LOBカラム名を取得 (表示文字列 "COL_NAME (BLOB)" からカラム名部分を抽出)
        Dim lobColDisplay = cboLobColumn.SelectedItem.ToString()
        Dim lobColName = lobColDisplay.Substring(0, lobColDisplay.LastIndexOf(" ("))

        ' ファイル名カラム
        Dim filenameCol As String = Nothing
        If cboFilenameMethod.SelectedIndex = 1 AndAlso cboFilenameColumn.SelectedIndex >= 0 Then
            filenameCol = cboFilenameColumn.SelectedItem.ToString()
        End If

        ' 拡張子
        Dim ext = txtExtension.Text.Trim()
        If String.IsNullOrEmpty(ext) Then ext = "lob"

        ' ExportProgressDialogで実行
        Dim extractedCount As Long = 0
        Dim outputDir = txtOutputDir.Text

        Using progress As New ExportProgressDialog()
            Dim result = progress.RunExport(
                Sub(worker, args)
                    Dim filesWritten = Open_NativeParser.ExtractLob(
                        _dumpFilePath, _schema, _tableName,
                        lobColName, outputDir,
                        filenameCol, ext, _dataOffset,
                        Sub(rows, tbl, pct)
                            If worker.CancellationPending Then
                                args.Cancel = True
                                Return
                            End If
                            worker.ReportProgress(pct,
                                New ExportProgressDialog.ProgressInfo(tbl, rows, 0))
                        End Sub)

                    If filesWritten < 0 Then
                        Throw New Exception(Loc.S("LobExtract_ExtractError"))
                    End If
                    extractedCount = filesWritten
                End Sub)

            If result Then
                MessageBox.Show(Loc.SF("LobExtract_ExtractComplete", extractedCount),
                               Loc.S("Title_Complete"), MessageBoxButtons.OK, MessageBoxIcon.Information)
            End If
        End Using
    End Sub
#End Region

#Region "ローカライズ"
    Public Sub ApplyLocalization() Implements ILocalizable.ApplyLocalization
        Me.Text = Loc.S("Title_LobExtract")
        lblLobColumn.Text = Loc.S("LobExtract_LobColumnLabel")
        lblOutputDir.Text = Loc.S("LobExtract_OutputFolderLabel")
        lblFilename.Text = Loc.S("LobExtract_FilenameLabel")
        lblExtension.Text = Loc.S("LobExtract_ExtensionLabel")
        grpSettings.Text = Loc.S("LobExtract_SettingsGroup")
        btnExtract.Text = Loc.S("Button_Extract")
        btnClose.Text = Loc.S("Button_Close")
    End Sub
#End Region

End Class
