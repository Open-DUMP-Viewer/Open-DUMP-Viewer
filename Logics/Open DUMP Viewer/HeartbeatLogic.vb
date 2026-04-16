Imports System.IO
Imports System.Net.Http
Imports System.Runtime.InteropServices
Imports System.Security.Cryptography
Imports System.Text
Imports System.Text.Json

''' <summary>
''' ライセンス使用状況のハートビート送信ロジック
''' アプリ起動時に1日1回、使用状況をサーバーへ送信する。
''' 個人を特定する情報は送信しない（マシンハッシュはSHA-256で不可逆化）。
''' </summary>
Public Class HeartbeatLogic

    Private Const HeartbeatEndpoint As String = "https://api.odv.dev/licenses/heartbeat"
    Private Const HeartbeatFileName As String = "heartbeat.last"

    ''' <summary>
    ''' ハートビートを送信する（1日1回制限付き、非同期・非ブロッキング）。
    ''' UI スレッドをブロックしないため、エラーは静かに無視する。
    ''' </summary>
    Public Shared Sub SendIfNeeded()
        Try
            Dim appData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "OpenDUMPViewer")

            ' 1日1回制限: 最終送信日を確認
            Dim lastFile = Path.Combine(appData, HeartbeatFileName)
            If File.Exists(lastFile) Then
                Dim lastText = File.ReadAllText(lastFile).Trim()
                If lastText = DateTime.UtcNow.ToString("yyyy-MM-dd") Then
                    Return ' 今日は既に送信済み
                End If
            End If

            ' ライセンス情報を取得
            Dim statusPath = Path.Combine(appData, "license.status")
            If Not File.Exists(statusPath) Then Return

            Dim licenseKey As String = ""
            Dim expiryDate As DateTime
            Dim holder As String = ""
            Dim errMsg As String = ""
            If Not LICENSE.VerifyLicenseFile(statusPath, licenseKey, expiryDate, holder, errMsg) Then
                Return ' ライセンスが無効なら送信しない
            End If

            ' バックグラウンドで送信（UIをブロックしない）
            Task.Run(Function() SendHeartbeatAsync(licenseKey, appData, lastFile))

        Catch
            ' 起動を妨げない
        End Try
    End Sub

    ''' <summary>
    ''' ハートビートデータを収集しAPIへ送信する
    ''' </summary>
    Private Shared Async Function SendHeartbeatAsync(
        licenseKey As String,
        appData As String,
        lastFile As String
    ) As Task
        Try
            Dim machineHash = ComputeMachineHash()
            Dim isDomainJoined = CheckDomainJoined()
            Dim osVersion = RuntimeInformation.OSDescription
            Dim asm = Reflection.Assembly.GetExecutingAssembly()
            Dim ver = asm.GetName().Version
            Dim appVersion = $"{ver.Major}.{ver.Minor}.{ver.Build}"

            Using client As New HttpClient()
                client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")
                client.Timeout = TimeSpan.FromSeconds(10)

                Dim payload As New Dictionary(Of String, Object) From {
                    {"license_key", licenseKey},
                    {"machine_hash", machineHash},
                    {"is_domain_joined", isDomainJoined},
                    {"os_version", osVersion},
                    {"app_version", appVersion}
                }

                Dim jsonContent As New StringContent(
                    JsonSerializer.Serialize(payload),
                    Encoding.UTF8,
                    "application/json"
                )

                Dim response = Await client.PostAsync(HeartbeatEndpoint, jsonContent)

                If response.IsSuccessStatusCode Then
                    ' 送信成功 → 最終送信日を記録
                    Directory.CreateDirectory(appData)
                    File.WriteAllText(lastFile, DateTime.UtcNow.ToString("yyyy-MM-dd"))
                End If
            End Using

        Catch
            ' ネットワークエラー等は静かに無視（次回起動時にリトライ）
        End Try
    End Function

    ''' <summary>
    ''' マシン固有のSHA-256ハッシュを生成する。
    ''' マシン名・プロセッサ数・OSバージョン・ユーザー名から生成。
    ''' 個人を特定できない不可逆ハッシュとなる。
    ''' </summary>
    Private Shared Function ComputeMachineHash() As String
        Try
            ' マシン固有情報を結合（個人を特定しない範囲で一意性を確保）
            Dim raw = String.Join("|", {
                Environment.MachineName,
                Environment.UserName,
                Environment.ProcessorCount.ToString(),
                RuntimeInformation.OSDescription,
                Environment.SystemDirectory
            })

            Using sha = SHA256.Create()
                Dim hashBytes = sha.ComputeHash(Encoding.UTF8.GetBytes(raw))
                Return BitConverter.ToString(hashBytes).Replace("-", "").ToLowerInvariant()
            End Using
        Catch
            ' フォールバック: ランダムハッシュ（再起動ごとに変わるが、送信は可能）
            Using sha = SHA256.Create()
                Dim fallback = Guid.NewGuid().ToString()
                Dim hashBytes = sha.ComputeHash(Encoding.UTF8.GetBytes(fallback))
                Return BitConverter.ToString(hashBytes).Replace("-", "").ToLowerInvariant()
            End Using
        End Try
    End Function

    ''' <summary>
    ''' マシンがActive Directoryドメインに参加しているか確認する。
    ''' ワークグループ所属の場合はFalse。
    ''' </summary>
    Private Shared Function CheckDomainJoined() As Boolean
        Try
            ' Environment.UserDomainName がマシン名と異なればドメイン参加
            Dim domain = Environment.UserDomainName
            Dim machine = Environment.MachineName
            Return Not String.Equals(domain, machine, StringComparison.OrdinalIgnoreCase)
        Catch
            Return False
        End Try
    End Function

End Class
