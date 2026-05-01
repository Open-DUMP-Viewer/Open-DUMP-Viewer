Imports System.Runtime.InteropServices

''' <summary>
''' MSIX (Microsoft Store) パッケージ実行コンテキストの判定。
''' Store 版では自動更新を Store に委ねるため、アプリ内の更新チェックを抑制する。
''' </summary>
Public Module PackageContext

    Private Const APPMODEL_ERROR_NO_PACKAGE As Integer = 15700

    <DllImport("kernel32.dll", EntryPoint:="GetCurrentPackageFullName", CharSet:=CharSet.Unicode, SetLastError:=False)>
    Private Function GetCurrentPackageFullName(ByRef packageFullNameLength As Integer, packageFullName As IntPtr) As Integer
    End Function

    Private _isPackaged As Boolean? = Nothing

    ''' <summary>
    ''' 現在のプロセスが MSIX (AppX) パッケージとして実行されているかを返す。
    ''' Win32 API GetCurrentPackageFullName で判定し、結果はキャッシュする。
    ''' </summary>
    Public ReadOnly Property IsPackaged As Boolean
        Get
            If Not _isPackaged.HasValue Then
                _isPackaged = DetectPackaged()
            End If
            Return _isPackaged.Value
        End Get
    End Property

    Private Function DetectPackaged() As Boolean
        Try
            Dim length As Integer = 0
            Dim result = GetCurrentPackageFullName(length, IntPtr.Zero)
            Return result <> APPMODEL_ERROR_NO_PACKAGE
        Catch
            Return False
        End Try
    End Function

End Module
