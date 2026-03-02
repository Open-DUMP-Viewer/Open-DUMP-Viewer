Imports System.IO
Imports System.Text.Json

''' <summary>
''' ワークスペース状態をJSONファイル (.odvw) として永続化するためのデータクラス
''' </summary>
Public Class WorkspaceData
    Public Property DumpFilePath As String = String.Empty
    Public Property ExcludedTables As List(Of String) = New List(Of String)
    Public Property SearchFilter As String = String.Empty
    Public Property CurrentSchema As String = String.Empty
    Public Property ExpandedNodes As List(Of String) = New List(Of String)

    Private Shared ReadOnly _jsonOptions As New JsonSerializerOptions() With {
        .WriteIndented = True
    }

    Public Shared Function Load(path As String) As WorkspaceData
        Dim json = File.ReadAllText(path, System.Text.Encoding.UTF8)
        Dim data = JsonSerializer.Deserialize(Of WorkspaceData)(json, _jsonOptions)
        If data Is Nothing Then data = New WorkspaceData()
        Return data
    End Function

    Public Sub Save(path As String)
        Dim json = JsonSerializer.Serialize(Me, _jsonOptions)
        File.WriteAllText(path, json, System.Text.Encoding.UTF8)
    End Sub
End Class
