Imports System.Globalization
Imports System.Threading

''' <summary>
''' アプリケーションの言語設定を管理するモジュール。
''' OS言語に自動追従し、手動切替の結果をSettingsに永続化する。
''' </summary>
Public Module LocaleManager

    ''' <summary>
    ''' アプリ起動時に呼び出す。保存済み設定があればそれを適用する。
    ''' 設定がなければOSのCurrentUICultureをそのまま使用する。
    ''' </summary>
    Public Sub InitializeLanguage()
        Dim saved As String = My.Settings.UILanguage
        If Not String.IsNullOrEmpty(saved) Then
            Try
                Thread.CurrentThread.CurrentUICulture = New CultureInfo(saved)
            Catch ex As CultureNotFoundException
                ' 無効なカルチャ名は無視 — OS既定を使用
            End Try
        End If
    End Sub

    ''' <summary>
    ''' 言語を切り替え、開いている全フォームに再適用する。
    ''' </summary>
    ''' <param name="cultureName">カルチャ名 (例: "ja", "en")</param>
    Public Sub SetLanguage(cultureName As String)
        Dim culture = New CultureInfo(cultureName)
        Thread.CurrentThread.CurrentUICulture = culture

        ' 設定を永続化
        My.Settings.UILanguage = cultureName
        My.Settings.Save()

        ' 開いている全フォームのローカライズを再適用
        For Each frm As Form In Application.OpenForms
            Dim localizable = TryCast(frm, ILocalizable)
            If localizable IsNot Nothing Then
                localizable.ApplyLocalization()
            End If
        Next
    End Sub

    ''' <summary>
    ''' 現在のUI言語が日本語かどうかを返す。
    ''' </summary>
    Public Function IsJapanese() As Boolean
        Return Thread.CurrentThread.CurrentUICulture.TwoLetterISOLanguageName = "ja"
    End Function

    ''' <summary>
    ''' 現在のUI言語のカルチャ名を返す (例: "ja", "en", "pt-BR")。
    ''' pt-BR のようにリージョン付きカルチャの場合はそのまま返す。
    ''' </summary>
    Public Function CurrentLanguage() As String
        Dim culture = Thread.CurrentThread.CurrentUICulture
        ' pt-BR はリージョン込みで識別する必要がある
        If culture.Name.Equals("pt-BR", StringComparison.OrdinalIgnoreCase) Then
            Return "pt-BR"
        End If
        Return culture.TwoLetterISOLanguageName
    End Function

End Module
