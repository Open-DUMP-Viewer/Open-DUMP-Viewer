Imports System.Net.Http
Imports System.Runtime.InteropServices
Imports System.Text
Imports System.Text.Json

''' <summary>
''' エラー報告の送信ロジック
''' Cloudflare Worker (report.odv.dev) へ HTTP POST でエラー報告を送信する
''' </summary>
Public Class ErrorReportLogic

    Private Const ReportEndpoint As String = "https://report.odv.dev/"

    ''' <summary>
    ''' エラー報告の送信結果
    ''' </summary>
    Public Class ReportResult
        Public Property Success As Boolean
        Public Property IssueUrl As String
        Public Property IssueNumber As Integer
        Public Property ErrorMessage As String
    End Class

    ''' <summary>
    ''' 個人情報を含まないシステム環境情報
    ''' </summary>
    Public Class SystemInfo
        Public Property AppVersion As String
        Public Property DllVersion As String
        Public Property OsVersion As String
        Public Property DotNetVersion As String
        Public Property Architecture As String
        Public Property ProcessArchitecture As String
        Public Property Locale As String
        Public Property DpiScale As String
        Public Property MemoryMB As String
        Public Property ScreenResolution As String
        ''' <summary>ダンプファイル情報 (サイズ・形式のみ、ファイル名は含まない)</summary>
        Public Property DumpFileInfo As String
        ''' <summary>直前のスタックトレース (個人パスはマスク済み)</summary>
        Public Property StackTrace As String
        ''' <summary>ダンプファイルのパス (送信せず添付時のみ使用)</summary>
        Public Property DumpFilePath As String
    End Class

    ''' <summary>
    ''' 個人情報を含まない環境情報を収集する
    ''' ユーザー名、コンピューター名、ファイルパス等は一切収集しない
    ''' </summary>
    ''' <param name="dumpFilePath">現在開いているダンプファイルのパス (サイズ・形式のみ取得、パス自体は送信しない)</param>
    Public Shared Function CollectSystemInfo(Optional dumpFilePath As String = Nothing) As SystemInfo
        Dim info As New SystemInfo()

        ' アプリバージョン
        Dim asm = Reflection.Assembly.GetExecutingAssembly()
        Dim ver = asm.GetName().Version
        info.AppVersion = $"v{ver.Major}.{ver.Minor}.{ver.Build}"

        ' ネイティブ DLL バージョン
        Try
            info.DllVersion = Open_NativeParser.GetVersion()
        Catch
            info.DllVersion = "N/A"
        End Try

        ' OS バージョン (RuntimeInformation.OSDescription はユーザー名を含まない)
        info.OsVersion = RuntimeInformation.OSDescription

        ' .NET バージョン
        info.DotNetVersion = RuntimeInformation.FrameworkDescription

        ' OS アーキテクチャ (x64 / ARM64)
        info.Architecture = RuntimeInformation.OSArchitecture.ToString()

        ' プロセスアーキテクチャ (エミュレーション検出用)
        info.ProcessArchitecture = RuntimeInformation.ProcessArchitecture.ToString()

        ' ロケール (文字コード問題の診断用)
        info.Locale = $"{Globalization.CultureInfo.CurrentCulture.Name} / CP{Text.Encoding.Default.CodePage}"

        ' DPI スケーリング (UI レイアウト問題の診断用)
        Try
            Using g = Drawing.Graphics.FromHwnd(IntPtr.Zero)
                Dim dpiX = g.DpiX
                info.DpiScale = $"{CInt(dpiX / 96 * 100)}% ({dpiX} DPI)"
            End Using
        Catch
            info.DpiScale = "N/A"
        End Try

        ' メモリ使用量 (大規模ファイル問題の診断用)
        Try
            Dim proc = Process.GetCurrentProcess()
            Dim workingMB = proc.WorkingSet64 \ (1024 * 1024)
            Dim gcMB = GC.GetTotalMemory(False) \ (1024 * 1024)
            info.MemoryMB = $"Working: {workingMB} MB / GC: {gcMB} MB"
        Catch
            info.MemoryMB = "N/A"
        End Try

        ' 画面解像度 (UI レイアウト問題の診断用)
        Try
            Dim screen = System.Windows.Forms.Screen.PrimaryScreen
            If screen IsNot Nothing Then
                info.ScreenResolution = $"{screen.Bounds.Width}x{screen.Bounds.Height}"
            Else
                info.ScreenResolution = "N/A"
            End If
        Catch
            info.ScreenResolution = "N/A"
        End Try

        ' ダンプファイル情報 (ファイル名・パスは送信しない、サイズと形式のみ)
        If dumpFilePath IsNot Nothing AndAlso IO.File.Exists(dumpFilePath) Then
            Try
                Dim fi As New IO.FileInfo(dumpFilePath)
                Dim sizeMB = fi.Length / (1024.0 * 1024.0)
                Dim dumpType = Open_NativeParser.CheckDumpKind(dumpFilePath)
                Dim typeName = DumpTypeName(dumpType)
                info.DumpFileInfo = $"{typeName} / {sizeMB:F1} MB"
                info.DumpFilePath = dumpFilePath
            Catch
                info.DumpFileInfo = Loc.S("ErrorReport_RetrievalFailed")
            End Try
        Else
            info.DumpFileInfo = Loc.S("ErrorReport_NotOpened")
        End If

        Return info
    End Function

    ''' <summary>
    ''' スタックトレースからファイルパス中の個人情報をマスクする
    ''' "C:\Users\John\..." → "C:\Users\***\..." のように置換
    ''' </summary>
    Public Shared Function SanitizeStackTrace(stackTrace As String) As String
        If String.IsNullOrEmpty(stackTrace) Then Return ""
        ' ファイルパス中のユーザー名をマスク
        Return Text.RegularExpressions.Regex.Replace(
            stackTrace,
            "(?i)([A-Z]:\\Users\\)[^\\]+",
            "$1***")
    End Function

    ''' <summary>
    ''' ダンプ種別コードを表示名に変換
    ''' </summary>
    Private Shared Function DumpTypeName(dumpType As Integer) As String
        Select Case dumpType
            Case Open_NativeParser.DUMP_EXPDP : Return "EXPDP"
            Case Open_NativeParser.DUMP_EXPDP_COMPRESS : Return Loc.S("ErrorReport_DumpType_ExpdpCompress")
            Case Open_NativeParser.DUMP_EXP : Return "EXP"
            Case Open_NativeParser.DUMP_EXP_DIRECT : Return "EXP (Direct)"
            Case Else : Return Loc.S("ErrorReport_DumpType_Unknown")
        End Select
    End Function

    ''' <summary>
    ''' エラー報告を Cloudflare Worker に送信する
    ''' </summary>
    Public Shared Async Function SubmitReportAsync(
        title As String,
        description As String,
        contact As String,
        sysInfo As SystemInfo,
        Optional attachDump As Boolean = False
    ) As Task(Of ReportResult)

        Using client As New HttpClient()
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")
            client.Timeout = TimeSpan.FromSeconds(If(attachDump, 120, 15))

            Dim payload As New Dictionary(Of String, String) From {
                {"title", title},
                {"description", description},
                {"contact", contact},
                {"app_version", sysInfo.AppVersion},
                {"dll_version", sysInfo.DllVersion},
                {"os_version", sysInfo.OsVersion},
                {"dotnet_version", sysInfo.DotNetVersion},
                {"architecture", sysInfo.Architecture},
                {"process_architecture", sysInfo.ProcessArchitecture},
                {"locale", sysInfo.Locale},
                {"dpi_scale", sysInfo.DpiScale},
                {"memory", sysInfo.MemoryMB},
                {"screen_resolution", sysInfo.ScreenResolution},
                {"dump_file_info", If(sysInfo.DumpFileInfo, "")},
                {"stack_trace", If(sysInfo.StackTrace, "")}
            }

            ' ユーザーが許可した場合のみダンプファイルを添付
            If attachDump AndAlso sysInfo.DumpFilePath IsNot Nothing AndAlso IO.File.Exists(sysInfo.DumpFilePath) Then
                Try
                    Dim fi As New IO.FileInfo(sysInfo.DumpFilePath)
                    If fi.Length <= 50L * 1024 * 1024 Then
                        Dim bytes = IO.File.ReadAllBytes(sysInfo.DumpFilePath)
                        payload("dump_file") = Convert.ToBase64String(bytes)
                        payload("dump_file_name") = fi.Name
                    End If
                Catch
                    ' ファイル読み取り失敗は無視
                End Try
            End If

            Dim jsonContent As New StringContent(
                JsonSerializer.Serialize(payload),
                Encoding.UTF8,
                "application/json"
            )

            Dim response = Await client.PostAsync(ReportEndpoint, jsonContent)
            Dim responseBody = Await response.Content.ReadAsStringAsync()

            Using doc = JsonDocument.Parse(responseBody)
                Dim root = doc.RootElement
                Dim result As New ReportResult()

                Dim successProp As JsonElement
                If root.TryGetProperty("success", successProp) Then
                    result.Success = successProp.GetBoolean()
                End If

                If result.Success Then
                    Dim urlProp As JsonElement
                    If root.TryGetProperty("issue_url", urlProp) Then
                        result.IssueUrl = urlProp.GetString()
                    End If

                    Dim numProp As JsonElement
                    If root.TryGetProperty("issue_number", numProp) Then
                        result.IssueNumber = numProp.GetInt32()
                    End If
                Else
                    Dim errProp As JsonElement
                    If root.TryGetProperty("error", errProp) Then
                        result.ErrorMessage = TranslateError(errProp.GetString(), response.StatusCode)
                    Else
                        result.ErrorMessage = Loc.SF("ErrorReport_ServerError", CInt(response.StatusCode))
                    End If
                End If

                Return result
            End Using
        End Using
    End Function

    ''' <summary>
    ''' エラーコードを日本語メッセージに変換する
    ''' </summary>
    Private Shared Function TranslateError(errorCode As String, statusCode As Net.HttpStatusCode) As String
        Select Case errorCode
            Case "rate_limit_exceeded"
                Return Loc.S("ErrorReport_RateLimitExceeded")
            Case "invalid_json"
                Return Loc.S("ErrorReport_InvalidRequest")
            Case "internal_error"
                Return Loc.S("ErrorReport_InternalError")
            Case Else
                If errorCode IsNot Nothing AndAlso errorCode.Length > 0 Then
                    Return errorCode
                End If
                Return Loc.SF("ErrorReport_UnknownError", CInt(statusCode))
        End Select
    End Function

End Class
