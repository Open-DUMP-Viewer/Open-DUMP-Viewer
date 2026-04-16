Imports System.IO
Imports System.Net.Http
Imports System.Security.Cryptography
Imports System.Text
Imports System.Text.Json

''' <summary>
''' 公開鍵暗号方式によるライセンス検証ロジック
''' サーバー発行のフラットJSON形式ライセンスファイルを検証する。
''' 
''' ライセンスファイル形式:
''' {
'''   "license_key": "ODV-XXXX-XXXX-XXXX-XXXX",
'''   "holder": "企業名 or 個人名",
'''   "license_type": "paid" | "free",
'''   "quantity": 10,
'''   "issued_at": "2026-02-23T...",
'''   "expires_at": "2027-02-23T...",
'''   "signature": "Base64署名"
''' }
''' 
''' 署名対象: signatureフィールドを除いたペイロードの JSON.stringify 結果（UTF-8バイト列）
''' </summary>
Public Class LICENSE

#Region "公開鍵定義"
    ''' <summary>
    ''' 公開鍵（PEM形式 — Base64部分のみ）
    ''' </summary>
    Private Shared ReadOnly PublicKeyBase64 As String =
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA" &
        "u7Phr86EDhhUuKAqZnOL/W4lkby6NIHhaOhCuqBAmEjm" &
        "0Esna3GpEYIup1guwm69UWHEAf5wJSGgDfSrOYuP3agU" &
        "KXl/uQFOXbg23aDidLaH9gf6uuqhBDtDozHlJaT0uc1Y" &
        "AfQEiD+7RKshqCZd8lwK6Z9fLZ9Ae+pFZsBavACI39UC" &
        "8Kgc7bJthZbDBQlMbCTQP9XI0CBXo+X6D3D71DWNuLyD" &
        "0V90IVG01lch19QSmjKCwWWwgy96D+0+5pV22FIcZwCl" &
        "jTyuI9DNpW9ZhMqXsSz3T73YIZxhEl7CQodgeIqstGYR" &
        "4yZTqOD9hn29ACuX2Tp+N/pIZBgW+M5m5QIDAQAB"
#End Region

#Region "ライセンス検証メイン処理"
    ''' <summary>
    ''' ライセンスファイル（フラットJSON+署名）を検証し、情報を抽出
    ''' </summary>
    ''' <param name="licensePath">ライセンスファイルパス</param>
    ''' <param name="licenseKey">[out] ライセンスキー</param>
    ''' <param name="expiryDate">[out] 有効期限</param>
    ''' <param name="holder">[out] 使用者名（企業名 or 個人名）</param>
    ''' <param name="errMsg">[out] エラー内容</param>
    ''' <returns>有効な場合True</returns>
    Public Shared Function VerifyLicenseFile(licensePath As String, ByRef licenseKey As String, ByRef expiryDate As DateTime, ByRef holder As String, ByRef errMsg As String) As Boolean
        licenseKey = "" : expiryDate = Date.MinValue : holder = "" : errMsg = ""
        Try
            Dim jsonText = File.ReadAllText(licensePath, Encoding.UTF8)
            Dim doc = JsonDocument.Parse(jsonText).RootElement

            ' 署名を取得
            Dim sigB64 = doc.GetProperty("signature").GetString()
            Dim sigBytes = Convert.FromBase64String(sigB64)

            ' 署名対象のペイロードを再構築（signatureフィールドを除いたJSON）
            ' Node.js の JSON.stringify({ license_key, holder, license_type, quantity, issued_at, expires_at }) と同一の文字列を生成
            Dim lk = doc.GetProperty("license_key").GetString()
            Dim hd = doc.GetProperty("holder").GetString()
            Dim lt = doc.GetProperty("license_type").GetString()
            Dim qt = doc.GetProperty("quantity").GetInt32()
            Dim ia = doc.GetProperty("issued_at").GetString()
            Dim ea = doc.GetProperty("expires_at").GetString()

            ' JSON.stringify と同じ出力を生成（キー順序を維持、スペースなし）
            Dim payloadJson = "{" &
                """license_key"":""" & EscapeJsonString(lk) & """," &
                """holder"":""" & EscapeJsonString(hd) & """," &
                """license_type"":""" & EscapeJsonString(lt) & """," &
                """quantity"":" & qt.ToString() & "," &
                """issued_at"":""" & EscapeJsonString(ia) & """," &
                """expires_at"":""" & EscapeJsonString(ea) & """}"
            Dim payloadBytes = Encoding.UTF8.GetBytes(payloadJson)

            ' 公開鍵で署名検証（PEM形式）
            Using rsa As RSA = RSA.Create()
                rsa.ImportSubjectPublicKeyInfo(Convert.FromBase64String(PublicKeyBase64), Nothing)
                If Not rsa.VerifyData(payloadBytes, sigBytes, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1) Then
                    errMsg = Loc.S("License_SignatureVerifyFailed") : Return False
                End If
            End Using

            ' 情報を抽出
            licenseKey = lk
            holder = hd

            If Not DateTime.TryParse(ea, expiryDate) Then errMsg = Loc.S("License_ExpiryParseFailed") : Return False
            If DateTime.Now.Date > expiryDate.Date Then errMsg = Loc.S("License_Expired") : Return False
            Return True
        Catch ex As Exception
            errMsg = Loc.SF("License_VerifyError", ex.Message)
            Return False
        End Try
    End Function

    ''' <summary>
    ''' JSON文字列のエスケープ（Node.js JSON.stringify互換）
    ''' </summary>
    Private Shared Function EscapeJsonString(s As String) As String
        If s Is Nothing Then Return ""
        Dim sb As New StringBuilder()
        For Each c In s
            Select Case c
                Case """"c : sb.Append("\""")
                Case "\"c : sb.Append("\\")
                Case ChrW(8) : sb.Append("\b")
                Case ChrW(12) : sb.Append("\f")
                Case ChrW(10) : sb.Append("\n")
                Case ChrW(13) : sb.Append("\r")
                Case ChrW(9) : sb.Append("\t")
                Case Else
                    If Char.IsControl(c) Then
                        sb.Append("\u" & AscW(c).ToString("x4"))
                    Else
                        sb.Append(c)
                    End If
            End Select
        Next
        Return sb.ToString()
    End Function
#End Region

#Region "オンラインライセンス検証"
    Private Const CheckStatusEndpoint As String = "https://api.odv.dev/licenses/check/"

    ''' <summary>
    ''' サーバーにライセンスキーの有効性を問い合わせる（起動時チェック）。
    ''' サーバーに到達できない場合はTrue（オフライン許容）。
    ''' </summary>
    ''' <param name="licenseKey">検証するライセンスキー</param>
    ''' <returns>有効ならTrue、サーバーが無効と返した場合False、通信失敗時True</returns>
    Public Shared Function VerifyOnline(licenseKey As String) As Boolean
        Try
            Using client As New HttpClient()
                client.DefaultRequestHeaders.UserAgent.ParseAdd("Open-DUMP-Viewer")
                client.Timeout = TimeSpan.FromSeconds(5)

                Dim task = client.GetAsync(CheckStatusEndpoint & Uri.EscapeDataString(licenseKey))
                task.Wait()
                Dim response = task.Result

                If Not response.IsSuccessStatusCode Then
                    ' 404 = ライセンスが見つからない → 無効
                    If response.StatusCode = Net.HttpStatusCode.NotFound Then Return False
                    ' その他のエラー（500等）→ オフラインと同等に扱い許容
                    Return True
                End If

                Dim bodyTask = response.Content.ReadAsStringAsync()
                bodyTask.Wait()
                Dim doc = JsonDocument.Parse(bodyTask.Result).RootElement

                ' サーバーが valid: false を返した場合
                Dim validProp As JsonElement
                If doc.TryGetProperty("valid", validProp) Then
                    Return validProp.GetBoolean()
                End If

                ' valid プロパティが無い場合は許容
                Return True
            End Using
        Catch
            ' ネットワークエラー・タイムアウト → オフライン環境として許容
            Return True
        End Try
    End Function
#End Region

#Region "ライセンス情報取得"
    ''' <summary>
    ''' 現在のライセンスファイルからHolder（使用者名/企業名）を取得
    ''' </summary>
    Public Shared Function GetLicenseHolder() As String
        Try
            Dim appData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OpenDUMPViewer")
            Dim statusPath = Path.Combine(appData, "license.status")
            If Not File.Exists(statusPath) Then Return ""
            Dim licenseKey As String = ""
            Dim expiryDate As DateTime
            Dim holder As String = ""
            Dim errMsg As String = ""
            If LICENSE.VerifyLicenseFile(statusPath, licenseKey, expiryDate, holder, errMsg) Then
                Return holder
            End If
            Return ""
        Catch
            Return ""
        End Try
    End Function
#End Region

End Class
