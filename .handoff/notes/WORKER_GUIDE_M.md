# 中程度修正の作業者向け手引き(群8〜14 共通)

R = /tmp/claude-0/-home-user-Open-DUMP-Viewer/6f7e1e96-1edb-5288-8b7c-135e457959e9/scratchpad/odvtest
製品: Open DUMP Viewer。C エンジン Logics/DumpParser(信頼できない .dmp を解析)+ VB.NET WinForms。
リポジトリのルールは AGENTS.md(C11・MSVC 互換・GNU 拡張禁止、odv_ 接頭辞、VB 規約)。

## 入力
- 指摘の全文: $R/fix/brief_<群>.md(修正前コードの行番号。現行では行がずれている)
- 現状判定: $R/fix/triage_results.json の該当群(status / evidence / current_code_location / assets / fixer_notes)
  と $R/fix/triage_<群>/(判定時の出力)
- 基準: 高修正後の最終 = $R/fix/bin_final, $R/fix/regress_final.jsonl。修正前 = bin_base / regress_base.jsonl

## 道具
- ビルド: bash $R/fix/build_at.sh <あなたの作業ツリー>/Logics/DumpParser $R/fix/bin_<タグ>
  警告の基準は win 11 / native 8 / asan 1。増やさないこと(build_<タグ>.log の件数で確認)
- 解析: bash $R/fix/bin_<タグ>/odv.sh <dump> <outdir> [opts](wine の Windows 版=正)。odv_native.sh は文字コード変換なし
- 回帰: cd $R/fix && python3 regress.py --tag <タグ> --bin bin_<タグ> --jobs 3 [--cases 正規表現]
  比較: python3 cmp.py final <タグ>(群の前の基準と比べる)。per-dump は regress_<タグ>.jsonl の keys
  regress.py は中断しても再実行で続きから(--force で全やり直し)
- ASan: bash $R/fix/asan_sweep.sh $R/fix/bin_<タグ> <タグ> <パスの部分一致>(群に関係するダンプに絞ってよい)
  既知の指摘は odv_datetime.c:371/410/415 の負 INTERVAL の UBSan のみ。これ以外が出たら直す
- VB: cd <作業ツリー> && dotnet build "Open DUMP Viewer.vbproj" -p:EnableWindowsTargeting=true(0 警告 0 エラー)
- DB(新しいダンプが要るとき): $R/tools/README-infra.md(mkuser.sh / mkdump.sh / sqlrun.sh)、正解値 $R/tools/README-compare.md(truth.py)。
  ケースは $R/cases/<名前>/case.conf と run_case.sh の流儀に合わせる。DB は 11/21/23 が稼働(落ちていたら下記で復旧)
  復旧: setsid nohup dockerd > /tmp/claude-0/dockerd.log 2>&1 < /dev/null & ; docker start ora11 ora21 ora23 ; README.md §2 の接続確認

## ゲート(群を終える条件)
1. ビルド警告が増えない。VB を触ったら dotnet build 0/0
2. 全ケース回帰で、対象の指摘が直ったことを証拠ダンプで示す
3. 単一ファイルでも退行ゼロ: cmp.py final <タグ> の「新たな差異」を 1 件ずつ説明できること。
   説明できる非退行の型: (a) 以前欠けていた表・行が読めるようになり既存の別課題が見えた (b) 比較ツールが新しい正しい出力を
   読めない(例: '...'||CHR(10)||'...' は compare.py が解釈できない既知の盲点。値の保全を自分で確かめること)
   (c) 出力の形式を正しく変えた結果、正解値の比較規則側の更新が要る → その場合は $R/tools/compare.py / truth.py を直してよい
   (正しさを緩めるのではなく、正しい出力を正しく評価する変更に限る。変更点を NOTES に書く)
4. ASan: 新しい指摘ゼロ
5. 触っていない形式(例: expdp だけ直したなら exp)の結果が完全不変

## 作業の決まり
- git の commit はあなたの作業ツリーのブランチにだけ行う(push はしない)。最終のコミットメッセージは統括側が書く
- 製品の挙動に関わる設計判断で迷ったら、推測で決めず NOTES に選択肢を書いて報告する
- 再起動対策: 途中経過を $R/fix/NOTES_<群>.md に随時書く(何を直したか・どこまで検証したか・次に何をするか)。
  作業者プロセスが再起動で止まっても、次の担当がそこから再開できるようにする
- ディスクの空きは約 2GB。大きな中間ファイルを作らない。回帰 1 タグ ≈ 38MB
- CPU は 4。回帰は --jobs 3 まで。他の群の作業者が同時に動いていることがある
- 最終報告: 変更ファイル、指摘ごとの修正内容と証拠、ゲート 1〜5 の結果(数値)、残った課題、NOTES のパス

## 比較ツールの扱い(2026-10-17 追加・必ず守る)
- 共有の $R/tools/compare.py・truth.py を直接編集しない(同時編集で他の群の照合が壊れた)。
- 自分専用の写しを作り、ODV_COMPARE=<写しのパス> python3 regress.py ... で使う。
- ゲートの比較は、基準側と候補側を同じ写しで回す(写しを変えたら基準も別タグで回し直す)。
- 比較ツールへの変更は最終報告に diff -u と理由を書く。共有版への反映は統括が行う。

## 容量(2026-10-17 追加・必ず守る)
- 全件回帰は ODV_REGRESS_DISCARD=1 を付けて回す(jsonl に記録した後、1 ダンプごとの成果物を消す。cmp.py は jsonl だけを使う)。
  個別に中身を見たいダンプは odv.sh で別に回す。空きが 500MB を切ったら新しい大きなファイルを作らず、片付けてから続ける。
- 警告数の基準は最新の統合ブランチで取り直すこと(native は群10 以降 5)。
