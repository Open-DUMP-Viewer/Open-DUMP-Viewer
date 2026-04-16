Imports System.Globalization
Imports System.Resources
Imports System.Reflection
Imports System.Threading

''' <summary>
''' ローカライズ文字列アクセサ。
''' Loc.S("key") で現在のUI言語に応じた文字列を取得する。
''' Loc.SF("key", args) でフォーマット文字列のパラメータ展開を行う。
''' </summary>
Friend Class Loc
    Private Shared ReadOnly _rm As New ResourceManager("Open_DUMP_Viewer.Strings", Assembly.GetExecutingAssembly())

    ''' <summary>キーに対応するローカライズ文字列を取得</summary>
    Public Shared Function S(key As String) As String
        Dim value = _rm.GetString(key, Thread.CurrentThread.CurrentUICulture)
        Return If(value, key)
    End Function

    ''' <summary>キーに対応するフォーマット文字列にパラメータを適用</summary>
    Public Shared Function SF(key As String, ParamArray args() As Object) As String
        Dim fmt = _rm.GetString(key, Thread.CurrentThread.CurrentUICulture)
        If fmt Is Nothing Then Return key
        Return String.Format(fmt, args)
    End Function
End Class
