Imports System.Collections.Generic

Public Class TablePreviewLogic

    ''' <summary>
    ''' テーブルデータをTablePreviewフォームで表示する
    ''' 親フォーム参照から新しいTablePreviewを作成して表示する
    ''' </summary>
    ''' <param name="parentForm">親となるMDIフォーム</param>
    ''' <param name="tableData">テーブルデータ（行ごとにDictionary&lt;列名, 値&gt;のリスト）</param>
    ''' <param name="columnNames">列名のリスト</param>
    ''' <param name="tableName">テーブル名</param>
    Public Shared Sub DisplayTableData(parentForm As Form, tableData As List(Of String()),
                                       columnNames As List(Of String), tableName As String,
                                       Optional schema As String = Nothing,
                                       Optional columnTypes As String() = Nothing,
                                       Optional columnNotNulls As Boolean() = Nothing,
                                       Optional columnDefaults As String() = Nothing)
        Try
            ' 列名リストが空の場合はエラーダイアログを表示して終了
            If columnNames Is Nothing OrElse columnNames.Count = 0 Then
                MessageBox.Show(Loc.S("TablePreview_NoColumns"), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
                Return
            End If

            ' データがNullの場合は空リストに置換（列ヘッダーのみ表示）
            If tableData Is Nothing Then
                tableData = New List(Of String())
            End If

            ' TablePreviewフォームを作成し、MDI親フォームに表示
            Dim previewForm As New TablePreview(tableData, columnNames, tableName, schema, columnTypes, columnNotNulls, columnDefaults)
            previewForm.MdiParent = parentForm
            previewForm.Show()

        Catch ex As Exception
            ' 例外発生時はエラーダイアログを表示
            MessageBox.Show(Loc.SF("TablePreview_DisplayError", ex.Message), Loc.S("Title_Error"), MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

End Class
