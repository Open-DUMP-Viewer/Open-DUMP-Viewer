; ============================================================
; Open DUMP Viewer for Oracle database - Inno Setup Script
; ============================================================
; CI から渡される定義:
;   MyAppVersion     - "2.0.0"
;   MyAppFileVersion - "v2.0.0" or "v2.0.0-beta"
;   MyAppArch        - "x64" or "arm64"
;   MySourceDir      - 発行済みアプリのパス
;   BETA             - ベータビルド時に定義 (任意)
; ============================================================

#define MyAppPublisher "YANAI Taketo"
#define MyAppURL       "https://www.odv.dev/"
#define MyAppExeName   "Open DUMP Viewer.exe"

#ifdef BETA
  #define MyAppName      "Open DUMP Viewer for Oracle database (Beta)"
  #define MyAppId        "{{2e7f53d3-f0d6-4000-8d40-253b4ae66c63}"
  #define MyAppDirName   "Open DUMP Viewer Beta"
  #define MyOldAppId     "{a6f03b60-f9ab-4182-a1fc-aa9801428121}_is1"
#else
  #define MyAppName      "Open DUMP Viewer for Oracle database"
  #define MyAppId        "{{25f04e6a-ad47-47aa-9a66-74f64c772bac}"
  #define MyAppDirName   "Open DUMP Viewer"
  #define MyOldAppId     "{cd3b541d-5df6-4737-9bcc-16a4329a8a54}_is1"
#endif

; ============================================================
; [Setup] - 基本設定
; ============================================================
[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppDirName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\publish
OutputBaseFilename=OpenDumpViewer_{#MyAppFileVersion}_installer_{#MyAppArch}
SetupIconFile=..\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardImageFile=WizardImage.bmp
WizardSmallImageFile=WizardSmallImage.bmp
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
; 実行中のアプリを検出して終了を促す
CloseApplications=force
CloseApplicationsFilter=*.exe
RestartApplications=no
; アーキテクチャ設定
#if MyAppArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif

; ============================================================
; [Languages] - 10言語対応 (OS言語を自動検出)
; ============================================================
[Languages]
Name: "japanese";             MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";              MessagesFile: "compiler:Default.isl"
Name: "german";               MessagesFile: "compiler:Languages\German.isl"
Name: "spanish";              MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french";               MessagesFile: "compiler:Languages\French.isl"
Name: "italian";              MessagesFile: "compiler:Languages\Italian.isl"
Name: "korean";               MessagesFile: "compiler:Languages\Korean.isl"
Name: "brazilianportuguese";  MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "russian";              MessagesFile: "compiler:Languages\Russian.isl"
Name: "chinesesimplified";    MessagesFile: "ChineseSimplified.isl"

; ============================================================
; [CustomMessages] - カスタムメッセージ (各言語)
; ============================================================
[CustomMessages]
; --- 日本語 ---
japanese.LaunchAfterInstall=インストール後に {#MyAppName} を起動する
japanese.DeleteSettings=ユーザー設定を削除しますか？（ライセンス情報等）
japanese.VisitReleasePage=フィードバックを送信する（ブラウザが開きます）
; --- English ---
english.LaunchAfterInstall=Launch {#MyAppName} after installation
english.DeleteSettings=Do you want to delete user settings? (license info, etc.)
english.VisitReleasePage=Send feedback (opens browser)
; --- Deutsch ---
german.LaunchAfterInstall={#MyAppName} nach der Installation starten
german.DeleteSettings=Möchten Sie die Benutzereinstellungen löschen? (Lizenzinformationen usw.)
german.VisitReleasePage=Feedback senden (öffnet Browser)
; --- Español ---
spanish.LaunchAfterInstall=Iniciar {#MyAppName} después de la instalación
spanish.DeleteSettings=¿Desea eliminar la configuración del usuario? (información de licencia, etc.)
spanish.VisitReleasePage=Enviar comentarios (abre el navegador)
; --- Français ---
french.LaunchAfterInstall=Lancer {#MyAppName} après l'installation
french.DeleteSettings=Voulez-vous supprimer les paramètres utilisateur ? (informations de licence, etc.)
french.VisitReleasePage=Envoyer des commentaires (ouvre le navigateur)
; --- Italiano ---
italian.LaunchAfterInstall=Avvia {#MyAppName} dopo l'installazione
italian.DeleteSettings=Eliminare le impostazioni utente? (informazioni sulla licenza, ecc.)
italian.VisitReleasePage=Invia feedback (apre il browser)
; --- 한국어 ---
korean.LaunchAfterInstall=설치 후 {#MyAppName} 실행
korean.DeleteSettings=사용자 설정을 삭제하시겠습니까? (라이선스 정보 등)
korean.VisitReleasePage=피드백 보내기 (브라우저가 열립니다)
; --- Português (BR) ---
brazilianportuguese.LaunchAfterInstall=Iniciar {#MyAppName} após a instalação
brazilianportuguese.DeleteSettings=Deseja excluir as configurações do usuário? (informações de licença, etc.)
brazilianportuguese.VisitReleasePage=Enviar feedback (abre o navegador)
; --- Русский ---
russian.LaunchAfterInstall=Запустить {#MyAppName} после установки
russian.DeleteSettings=Удалить пользовательские настройки? (информация о лицензии и т.д.)
russian.VisitReleasePage=Отправить отзыв (откроется браузер)
; --- 中文 (简体) ---
chinesesimplified.LaunchAfterInstall=安装后启动 {#MyAppName}
chinesesimplified.DeleteSettings=是否删除用户设置？（许可证信息等）
chinesesimplified.VisitReleasePage=发送反馈（将打开浏览器）

; ============================================================
; [Tasks] - インストールオプション
; ============================================================
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "fileassoc";   Description: "{cm:AssocFileExtension,{#MyAppName},.dmp}"

; ============================================================
; [Files] - インストールするファイル
; ============================================================
[Files]
Source: "{#MySourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; ============================================================
; [Icons] - ショートカット
; ============================================================
[Icons]
Name: "{group}\{#MyAppName}";      Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

; ============================================================
; [Registry] - ファイル関連付け (.dmp)
; ============================================================
[Registry]
Root: HKA; Subkey: "Software\Classes\.dmp";                                 ValueType: string; ValueName: ""; ValueData: "OpenDumpViewer.dmp"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\OpenDumpViewer.dmp";                  ValueType: string; ValueName: ""; ValueData: "Oracle DUMP File"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\OpenDumpViewer.dmp\DefaultIcon";      ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\OpenDumpViewer.dmp\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc

; ============================================================
; [Run] - インストール後の実行
; ============================================================
[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchAfterInstall}"; Flags: nowait postinstall skipifsilent

; ============================================================
; [UninstallDelete] - アンインストール時の追加削除
; ============================================================
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

; ============================================================
; [Code] - Pascal Script
; ============================================================
[Code]
/// <summary>
/// インストール開始前: 旧バージョン (OraDB DUMP Viewer) を検出しサイレントアンインストール
/// v4.0.0 の商標準拠リネームに伴う処理。旧設定フォルダ %APPDATA%\OraDBDUMPViewer は
/// 旧アンインストーラーのデフォルト動作で削除される想定（再ライセンス認証が必要）。
/// </summary>
function InitializeSetup(): Boolean;
var
  UninstallKey: String;
  UninstallExe: String;
  ResultCode: Integer;
begin
  UninstallKey := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyOldAppId}';
  if RegQueryStringValue(HKLM, UninstallKey, 'UninstallString', UninstallExe) or
     RegQueryStringValue(HKCU, UninstallKey, 'UninstallString', UninstallExe) then
  begin
    { UninstallString は引用符付きなので直接 Exec に渡す }
    UninstallExe := RemoveQuotes(UninstallExe);
    Exec(UninstallExe, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '',
         SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
  Result := True;
end;

/// <summary>
/// アンインストール時: ユーザー設定の削除を確認
/// %APPDATA%\OpenDUMPViewer を削除するか尋ねる
/// </summary>
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SettingsDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    { Oracle Client キャッシュを無条件削除 }
    DelTree(ExpandConstant('{localappdata}\Open_DUMP_Viewer\oracle_client'), True, True, True);

    SettingsDir := ExpandConstant('{userappdata}\OpenDUMPViewer');
    if DirExists(SettingsDir) then
    begin
      if MsgBox(CustomMessage('DeleteSettings'), mbConfirmation, MB_YESNO) = IDYES then
      begin
        DelTree(SettingsDir, True, True, True);
      end;
    end;
  end;
end;

/// <summary>
/// アンインストール完了後: フィードバックページへの誘導
/// インストール時の言語設定に応じたURLを生成
/// </summary>
procedure DeinitializeUninstall();
var
  ErrorCode: Integer;
  Lang: String;
  URL: String;
begin
  if MsgBox(CustomMessage('VisitReleasePage'), mbConfirmation, MB_YESNO) = IDNO then
    Exit;

  Lang := ActiveLanguage();
  if Lang = 'japanese' then Lang := 'ja'
  else if Lang = 'english' then Lang := 'en'
  else if Lang = 'german' then Lang := 'de'
  else if Lang = 'spanish' then Lang := 'es'
  else if Lang = 'french' then Lang := 'fr'
  else if Lang = 'italian' then Lang := 'it'
  else if Lang = 'korean' then Lang := 'ko'
  else if Lang = 'brazilianportuguese' then Lang := 'pt'
  else if Lang = 'russian' then Lang := 'ru'
  else if Lang = 'chinesesimplified' then Lang := 'zh'
  else Lang := 'en';

  URL := 'https://www.odv.dev/' + Lang + '/feedback?source=uninstall&version={#MyAppVersion}';
  ShellExec('open', URL, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;
