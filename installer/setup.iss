; ============================================================
; OraDB DUMP Viewer - Inno Setup Script
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
#define MyAppExeName   "OraDB DUMP Viewer.exe"

#ifdef BETA
  #define MyAppName    "OraDB DUMP Viewer (Beta)"
  #define MyAppId      "{{a6f03b60-f9ab-4182-a1fc-aa9801428121}"
  #define MyAppDirName "OraDB DUMP Viewer Beta"
#else
  #define MyAppName    "OraDB DUMP Viewer"
  #define MyAppId      "{{cd3b541d-5df6-4737-9bcc-16a4329a8a54}"
  #define MyAppDirName "OraDB DUMP Viewer"
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
OutputBaseFilename=OraDBDumpViewer_{#MyAppFileVersion}_installer_{#MyAppArch}
SetupIconFile=..\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
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
japanese.VisitReleasePage=最新リリースページを開く
; --- English ---
english.LaunchAfterInstall=Launch {#MyAppName} after installation
english.DeleteSettings=Do you want to delete user settings? (license info, etc.)
english.VisitReleasePage=Open the latest release page
; --- Deutsch ---
german.LaunchAfterInstall={#MyAppName} nach der Installation starten
german.DeleteSettings=Möchten Sie die Benutzereinstellungen löschen? (Lizenzinformationen usw.)
german.VisitReleasePage=Neueste Release-Seite öffnen
; --- Español ---
spanish.LaunchAfterInstall=Iniciar {#MyAppName} después de la instalación
spanish.DeleteSettings=¿Desea eliminar la configuración del usuario? (información de licencia, etc.)
spanish.VisitReleasePage=Abrir la página de la última versión
; --- Français ---
french.LaunchAfterInstall=Lancer {#MyAppName} après l'installation
french.DeleteSettings=Voulez-vous supprimer les paramètres utilisateur ? (informations de licence, etc.)
french.VisitReleasePage=Ouvrir la page de la dernière version
; --- Italiano ---
italian.LaunchAfterInstall=Avvia {#MyAppName} dopo l'installazione
italian.DeleteSettings=Eliminare le impostazioni utente? (informazioni sulla licenza, ecc.)
italian.VisitReleasePage=Apri la pagina dell'ultima versione
; --- 한국어 ---
korean.LaunchAfterInstall=설치 후 {#MyAppName} 실행
korean.DeleteSettings=사용자 설정을 삭제하시겠습니까? (라이선스 정보 등)
korean.VisitReleasePage=최신 릴리스 페이지 열기
; --- Português (BR) ---
brazilianportuguese.LaunchAfterInstall=Iniciar {#MyAppName} após a instalação
brazilianportuguese.DeleteSettings=Deseja excluir as configurações do usuário? (informações de licença, etc.)
brazilianportuguese.VisitReleasePage=Abrir a página da versão mais recente
; --- Русский ---
russian.LaunchAfterInstall=Запустить {#MyAppName} после установки
russian.DeleteSettings=Удалить пользовательские настройки? (информация о лицензии и т.д.)
russian.VisitReleasePage=Открыть страницу последнего релиза
; --- 中文 (简体) ---
chinesesimplified.LaunchAfterInstall=安装后启动 {#MyAppName}
chinesesimplified.DeleteSettings=是否删除用户设置？（许可证信息等）
chinesesimplified.VisitReleasePage=打开最新版本页面

; ============================================================
; [Tasks] - インストールオプション
; ============================================================
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checked

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
/// アンインストール時: ユーザー設定の削除を確認
/// %APPDATA%\OraDBDUMPViewer を削除するか尋ねる
/// </summary>
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SettingsDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    SettingsDir := ExpandConstant('{userappdata}\OraDBDUMPViewer');
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
/// アンインストール完了後: 最新リリースページへの誘導
/// </summary>
procedure DeinitializeUninstall();
var
  ErrorCode: Integer;
begin
  if MsgBox(CustomMessage('VisitReleasePage'), mbConfirmation, MB_YESNO) = IDNO then
    Exit;
  ShellExec('open', 'https://github.com/OraDB-DUMP-Viewer/OraDB-DUMP-Viewer/releases/latest', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;
