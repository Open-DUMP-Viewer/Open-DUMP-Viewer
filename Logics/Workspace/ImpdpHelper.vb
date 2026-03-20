Imports System.IO
Imports System.Text
Imports System.Text.RegularExpressions

''' <summary>
''' impdp SQLFILE= オプションを使って EXPDP ダンプから DDL テキストを生成し、
''' 制約・INDEX・COMMENT・DEFAULT 情報を抽出するヘルパークラス。
''' Oracle Client (impdp.exe) がインストールされている環境でのみ動作する。
'''
''' 使用方法:
'''   1. ExportOptions.ImpdpPath に impdp.exe のフルパスを設定
'''   2. ImpdpHelper.ExtractMetadata(dumpFilePath) を呼び出し
'''   3. 戻り値の Dictionary から制約情報を取得
'''
''' 制約情報取得には Oracle Client のインストールが必要です。
''' Oracle Instant Client の "Tools" パッケージ、または Oracle Database Client を
''' インストールし、impdp.exe のパスをオプション画面で設定してください。
''' </summary>
Public Class ImpdpHelper

    ''' <summary>
    ''' impdp が利用可能かチェック
    ''' </summary>
    Public Shared Function IsAvailable() As Boolean
        Dim path = ExportOptions.ImpdpPath
        Return Not String.IsNullOrEmpty(path) AndAlso File.Exists(path)
    End Function

    ''' <summary>
    ''' EXPDP ダンプから impdp SQLFILE= で DDL を生成し、制約情報を抽出する。
    ''' テーブル名 → 制約 JSON の辞書を返す。
    ''' </summary>
    Public Shared Function ExtractMetadata(dumpFilePath As String, schemaName As String) As Dictionary(Of String, String)
        Dim result As New Dictionary(Of String, String)

        If Not IsAvailable() Then Return result

        Dim tempDir = Path.Combine(Path.GetTempPath(), $"OraDB_DUMP_Viewer_impdp_{Guid.NewGuid():N}")
        Dim sqlFile = Path.Combine(tempDir, "expdp_ddl.sql")

        Try
            ' 一時ディレクトリ作成
            If Not Directory.Exists(tempDir) Then Directory.CreateDirectory(tempDir)

            ' ダンプファイルを一時ディレクトリにコピー（impdp は DIRECTORY オブジェクト経由でアクセス）
            Dim tempDump = Path.Combine(tempDir, Path.GetFileName(dumpFilePath))
            If Not File.Exists(tempDump) OrElse
               New FileInfo(tempDump).Length <> New FileInfo(dumpFilePath).Length Then
                File.Copy(dumpFilePath, tempDump, True)
            End If

            ' impdp 実行
            Dim success = RunImpdp(tempDir, Path.GetFileName(dumpFilePath), "expdp_ddl.sql", schemaName)
            If Not success OrElse Not File.Exists(sqlFile) Then Return result

            ' DDL テキストを解析
            Dim ddlText = File.ReadAllText(sqlFile, Encoding.UTF8)
            result = ParseDdlText(ddlText)

        Catch
            ' impdp 実行エラーは無視（オプション機能のため）
        Finally
            ' 一時ディレクトリごとクリーンアップ（GUID 付きで一意のため安全に削除可能）
            Try
                If Directory.Exists(tempDir) Then Directory.Delete(tempDir, True)
            Catch
            End Try
        End Try

        Return result
    End Function

    ''' <summary>
    ''' impdp.exe を実行して SQLFILE を生成
    ''' </summary>
    Private Shared Function RunImpdp(workDir As String, dumpFileName As String,
                                      sqlFileName As String, schemaName As String) As Boolean
        Try
            Dim impdpPath = ExportOptions.ImpdpPath

            ' Validate impdp path
            If String.IsNullOrEmpty(impdpPath) Then Return False
            If Not impdpPath.EndsWith("impdp.exe", StringComparison.OrdinalIgnoreCase) Then Return False
            If Not File.Exists(impdpPath) Then Return False

            ' Reject shell metacharacters in file/schema names
            Dim badChars = New Char() {""""c, "'"c, "&"c, "|"c, ">"c, "<"c, ";"c}
            If dumpFileName.IndexOfAny(badChars) >= 0 Then Return False
            If schemaName.IndexOfAny(badChars) >= 0 Then Return False

            Dim oracleHome = Path.GetDirectoryName(Path.GetDirectoryName(impdpPath))

            ' impdp は DIRECTORY オブジェクトが必要だが、SQLFILE モードでは
            ' ローカルファイルパスも使える場合がある。
            ' 最もポータブルな方法: directory パラメータにフルパスを指定
            Dim args = $"""/NOLOG"" USERID=""/ AS SYSDBA"" " &
                       $"DIRECTORY=""{workDir}"" " &
                       $"DUMPFILE=""{dumpFileName}"" " &
                       $"SQLFILE=""{sqlFileName}"" " &
                       $"SCHEMAS=""{schemaName}"""

            ' プロセス実行
            Dim psi As New Diagnostics.ProcessStartInfo()
            psi.FileName = impdpPath
            psi.Arguments = $"\""/\"" AS SYSDBA"" directory=DATA_PUMP_DIR dumpfile={dumpFileName} sqlfile={sqlFileName} schemas={schemaName}"
            psi.WorkingDirectory = workDir
            psi.UseShellExecute = False
            psi.CreateNoWindow = True
            psi.RedirectStandardOutput = True
            psi.RedirectStandardError = True

            ' ORACLE_HOME 環境変数が必要
            If Not String.IsNullOrEmpty(oracleHome) Then
                psi.Environment("ORACLE_HOME") = oracleHome
            End If

            Using proc = Diagnostics.Process.Start(psi)
                proc.WaitForExit(60000) ' 60秒タイムアウト
                Return proc.ExitCode = 0 AndAlso File.Exists(Path.Combine(workDir, sqlFileName))
            End Using

        Catch
            Return False
        End Try
    End Function

    ''' <summary>
    ''' impdp SQLFILE の出力テキストから制約・INDEX・COMMENT 情報を抽出。
    ''' テーブル名 → 制約 JSON 文字列の辞書を返す。
    ''' </summary>
    Public Shared Function ParseDdlText(ddlText As String) As Dictionary(Of String, String)
        Dim result As New Dictionary(Of String, String)
        Dim tableConstraints As New Dictionary(Of String, List(Of String))

        ' Join continuation lines (lines starting with whitespace) into single statements
        Dim rawLines = ddlText.Split({vbLf, vbCr}, StringSplitOptions.RemoveEmptyEntries)
        Dim statements As New List(Of String)
        Dim current As String = ""
        For Each raw In rawLines
            If raw.Length > 0 AndAlso (raw(0) = " "c OrElse raw(0) = vbTab(0)) AndAlso current.Length > 0 Then
                current &= " " & raw.Trim()
            Else
                If current.Length > 0 Then statements.Add(current)
                current = raw.Trim()
            End If
        Next
        If current.Length > 0 Then statements.Add(current)

        For Each line In statements
            line = line.Trim()

            ' ALTER TABLE "schema"."table" ADD CONSTRAINT "name" PRIMARY KEY ("col1", "col2")
            Dim pkMatch = Regex.Match(line, "ALTER TABLE ""[^""]*""\.\""([^""]*)"" ADD CONSTRAINT ""([^""]*)"" PRIMARY KEY \(([^)]+)\)")
            If pkMatch.Success Then
                AddConstraint(tableConstraints, pkMatch.Groups(1).Value,
                    BuildConstraintJson(0, pkMatch.Groups(2).Value, ParseColumnList(pkMatch.Groups(3).Value)))
                Continue For
            End If

            ' ALTER TABLE ... ADD CONSTRAINT "name" UNIQUE ("col1", "col2")
            Dim uqMatch = Regex.Match(line, "ALTER TABLE ""[^""]*""\.\""([^""]*)"" ADD CONSTRAINT ""([^""]*)"" UNIQUE \(([^)]+)\)")
            If uqMatch.Success Then
                AddConstraint(tableConstraints, uqMatch.Groups(1).Value,
                    BuildConstraintJson(1, uqMatch.Groups(2).Value, ParseColumnList(uqMatch.Groups(3).Value)))
                Continue For
            End If

            ' ALTER TABLE ... ADD CONSTRAINT "name" FOREIGN KEY ("col") REFERENCES "schema"."table" ("col")
            Dim fkMatch = Regex.Match(line, "ALTER TABLE ""[^""]*""\.\""([^""]*)"" ADD CONSTRAINT ""([^""]*)"" FOREIGN KEY \(([^)]+)\) REFERENCES ""([^""]*)""\.\""([^""]*)"" \(([^)]+)\)")
            If fkMatch.Success Then
                Dim json = $"{{""type"":2,""name"":""{EscJson(fkMatch.Groups(2).Value)}"",""columns"":[{FormatColArray(ParseColumnList(fkMatch.Groups(3).Value))}]," &
                           $"""ref_schema"":""{EscJson(fkMatch.Groups(4).Value)}"",""ref_table"":""{EscJson(fkMatch.Groups(5).Value)}"",""ref_columns"":[{FormatColArray(ParseColumnList(fkMatch.Groups(6).Value))}]}}"
                AddConstraint(tableConstraints, fkMatch.Groups(1).Value, json)
                Continue For
            End If

            ' ALTER TABLE ... ADD [CONSTRAINT "name"] CHECK (condition) ENABLE
            Dim chkMatch = Regex.Match(line, "ALTER TABLE ""[^""]*""\.\""([^""]*)"" ADD (?:CONSTRAINT ""([^""]*)"" )?CHECK \((.+?)\)\s*(ENABLE|DISABLE)?")
            If chkMatch.Success Then
                Dim cond = $"({chkMatch.Groups(3).Value})"
                Dim chkName = If(chkMatch.Groups(2).Success, chkMatch.Groups(2).Value, "")
                Dim json = $"{{""type"":3,""name"":""{EscJson(chkName)}"",""columns"":[],""condition"":""{EscJson(cond)}""}}"
                AddConstraint(tableConstraints, chkMatch.Groups(1).Value, json)
                Continue For
            End If

            ' CREATE [UNIQUE] INDEX "schema"."name" ON "schema"."table" ("col1", "col2")
            Dim idxMatch = Regex.Match(line, "CREATE\s+(UNIQUE\s+)?INDEX ""[^""]*""\.\""([^""]*)"" ON ""[^""]*""\.\""([^""]*)"" \(([^)]+)\)")
            If idxMatch.Success Then
                Dim isUnique = idxMatch.Groups(1).Success
                ' Skip UNIQUE indexes that back PK/UNIQUE constraints (already captured)
                If Not isUnique Then
                    AddConstraint(tableConstraints, idxMatch.Groups(3).Value,
                        BuildConstraintJson(4, idxMatch.Groups(2).Value, ParseColumnList(idxMatch.Groups(4).Value)))
                End If
                Continue For
            End If

            ' COMMENT ON TABLE "schema"."table" IS 'text'
            Dim tblCmtMatch = Regex.Match(line, "COMMENT ON TABLE ""[^""]*""\.\""([^""]*)""\s+IS '((?:[^']|'')*)'")
            If tblCmtMatch.Success Then
                Dim tblName = tblCmtMatch.Groups(1).Value
                Dim comment = tblCmtMatch.Groups(2).Value.Replace("''", "'")
                ' Store as type=5 comment entry
                Dim json = $"{{""type"":5,""table_comment"":""{EscJson(comment)}"",""col_comments"":{{}}}}"
                AddConstraint(tableConstraints, tblName, json)
                Continue For
            End If

            ' COMMENT ON COLUMN "schema"."table"."column" IS 'text'
            Dim colCmtMatch = Regex.Match(line, "COMMENT ON COLUMN ""[^""]*""\.\""([^""]*)""\.\""([^""]*)""\s+IS '((?:[^']|'')*)'")
            If colCmtMatch.Success Then
                ' Column comments need to be accumulated — handled separately
                Continue For
            End If
        Next

        ' Build final JSON strings per table
        For Each kvp In tableConstraints
            result(kvp.Key) = "[" & String.Join(",", kvp.Value) & "]"
        Next

        Return result
    End Function

    Private Shared Sub AddConstraint(dict As Dictionary(Of String, List(Of String)),
                                      tableName As String, jsonEntry As String)
        If Not dict.ContainsKey(tableName) Then
            dict(tableName) = New List(Of String)
        End If
        dict(tableName).Add(jsonEntry)
    End Sub

    Private Shared Function ParseColumnList(colText As String) As String()
        Return colText.Split(","c).
            Select(Function(c) c.Trim().Trim(""""c).Trim()).
            Where(Function(c) c.Length > 0).
            ToArray()
    End Function

    Private Shared Function BuildConstraintJson(type As Integer, name As String, columns As String()) As String
        Return $"{{""type"":{type},""name"":""{EscJson(name)}"",""columns"":[{FormatColArray(columns)}]}}"
    End Function

    Private Shared Function FormatColArray(cols As String()) As String
        Return String.Join(",", cols.Select(Function(c) $"""{EscJson(c)}"""))
    End Function

    Private Shared Function EscJson(s As String) As String
        Return s.Replace("\", "\\").Replace("""", "\""")
    End Function

End Class
