; AnyCapture installer (Inno Setup 6.7 or later) — packages dist\AnyCapture\ produced by build.bat.
; build.bat compiles it automatically when it finds Inno Setup, passing the version from rust\Cargo.toml:
;   ISCC.exe /DAppVersion=0.5.0 packaging\installer\AnyCapture.iss   →   dist\AnyCapture-0.5.0-setup.exe
;
; Installs for the current user only (no administrator prompt, in %LOCALAPPDATA%\Programs\AnyCapture) or, if the user
; chooses, for all users (in Program Files). Captures, presets and settings live in the user profile
; and stay there after uninstalling.

#define AppName      "AnyCapture"
#ifndef AppVersion
  #define AppVersion "0.5.0"
#endif
#define AppPublisher "Antonio Rezende"
#define AppURL       "https://github.com/toim22/AnyCapture"
#define SrcDir       "..\..\dist\AnyCapture"
; the program's former name: an install made under it is replaced (its folder, shortcuts and Start entry go away;
; the user's settings, library and captures move over when AnyCapture first starts)
#define OldName      "Snapnab"

[Setup]
AppId={{C59F0FFC-FF4A-4107-8483-9934772DA4EB}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
AppCopyright=Copyright © 2026 {#AppPublisher}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup
VersionInfoProductName={#AppName}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=AnyCapture-{#AppVersion}-setup
SetupIconFile=..\..\rust\resources\AnyCapture.ico
UninstallDisplayIcon={app}\AnyCapture.exe
UninstallDisplayName={#AppName}
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
; the modern wizard in Windows 11's style, light or dark as Windows is, with the Welcome page and the AnyCapture pictures
; (one file per display scale, drawn by wizard_images.py)
WizardStyle=modern dynamic windows11 hidebevels
DisableWelcomePage=no
WizardImageFile=WizardLarge-100.png,WizardLarge-125.png,WizardLarge-150.png,WizardLarge-175.png,WizardLarge-200.png,WizardLarge-250.png
WizardImageFileDynamicDark=WizardLarge-dark-100.png,WizardLarge-dark-125.png,WizardLarge-dark-150.png,WizardLarge-dark-175.png,WizardLarge-dark-200.png,WizardLarge-dark-250.png
WizardSmallImageFile=WizardSmall-100.png,WizardSmall-125.png,WizardSmall-150.png,WizardSmall-175.png,WizardSmall-200.png,WizardSmall-250.png
WizardSmallImageFileDynamicDark=WizardSmall-100.png,WizardSmall-125.png,WizardSmall-150.png,WizardSmall-175.png,WizardSmall-200.png,WizardSmall-250.png
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
; installs for the current user only (no UAC); the dialog lets the user choose "all users". If the user picks
; a protected folder (such as Program Files) in per-user mode, [Code] offers to restart as administrator
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
; an update installs where the previous version is (the AppId is the same since the first version, so every earlier
; version is replaced in place), for all users or only this one as before, without asking again
UsePreviousPrivileges=yes
UsePreviousAppDir=yes
; the installer/uninstaller closes AnyCapture if it is running (and reopens it after updating); the last two are the
; versions released under the program's former name, which this one replaces
AppMutex=AnyCapture-Capture-{{41F1BF9C-5A4E-4B6F-9C33-1D2E3F405A61},AnyCapture-Editor-{{800F2EB3-7C1D-4E2A-A3B4-C5D6E7F80912},{#OldName}-Capture-{{41F1BF9C-5A4E-4B6F-9C33-1D2E3F405A61},{#OldName}-Editor-{{800F2EB3-7C1D-4E2A-A3B4-C5D6E7F80912}
CloseApplications=yes
; the wizard language follows Windows; the list only appears if it is not one of the 12
ShowLanguageDialog=auto

[Languages]
; the same 12 languages as the program: 7 with official Inno Setup translations and 5 in Languages\ (made for AnyCapture)
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "hindi"; MessagesFile: "Languages\Hindi.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "bengali"; MessagesFile: "Languages\Bengali.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "indonesian"; MessagesFile: "Languages\Indonesian.isl"
Name: "urdu"; MessagesFile: "Languages\Urdu.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[CustomMessages]
; chosen folder requires administrator ("just for me" mode) / folder not writable even as administrator
brazilianportuguese.DirNeedsAdmin=A pasta escolhida exige permissão de administrador:%n%n%1%n%nPara instalar nela, o AnyCapture precisa ser instalado para todos os usuários. Deseja reiniciar a instalação como administrador?
brazilianportuguese.DirNotWritable=Não é possível gravar na pasta escolhida:%n%n%1%n%nEscolha outra pasta.
english.DirNeedsAdmin=The selected folder requires administrator permission:%n%n%1%n%nTo install there, AnyCapture must be installed for all users. Restart Setup as administrator?
english.DirNotWritable=Setup cannot write to the selected folder:%n%n%1%n%nPlease choose another folder.
chinesesimplified.DirNeedsAdmin=所选文件夹需要管理员权限：%n%n%1%n%n要安装到此处，必须为所有用户安装 AnyCapture。是否以管理员身份重新启动安装程序？
chinesesimplified.DirNotWritable=无法写入所选文件夹：%n%n%1%n%n请选择其他文件夹。
hindi.DirNeedsAdmin=चुने गए फ़ोल्डर के लिए व्यवस्थापक अनुमति आवश्यक है:%n%n%1%n%nवहाँ इंस्टॉल करने के लिए AnyCapture को सभी उपयोगकर्ताओं के लिए इंस्टॉल करना होगा। क्या सेटअप को व्यवस्थापक के रूप में फिर से शुरू करें?
hindi.DirNotWritable=चुने गए फ़ोल्डर में लिखा नहीं जा सकता:%n%n%1%n%nकृपया कोई दूसरा फ़ोल्डर चुनें।
spanish.DirNeedsAdmin=La carpeta seleccionada requiere permisos de administrador:%n%n%1%n%nPara instalar allí, AnyCapture debe instalarse para todos los usuarios. ¿Desea reiniciar la instalación como administrador?
spanish.DirNotWritable=No se puede escribir en la carpeta seleccionada:%n%n%1%n%nElija otra carpeta.
arabic.DirNeedsAdmin=يتطلب المجلد المحدد صلاحيات المسؤول:%n%n%1%n%nللتثبيت فيه، يجب تثبيت AnyCapture لجميع المستخدمين. هل تريد إعادة تشغيل الإعداد كمسؤول؟
arabic.DirNotWritable=تتعذّر الكتابة في المجلد المحدد:%n%n%1%n%nيُرجى اختيار مجلد آخر.
french.DirNeedsAdmin=Le dossier choisi nécessite les droits d'administrateur :%n%n%1%n%nPour l'installer à cet emplacement, AnyCapture doit être installé pour tous les utilisateurs. Redémarrer l'installation en tant qu'administrateur ?
french.DirNotWritable=Impossible d'écrire dans le dossier choisi :%n%n%1%n%nVeuillez choisir un autre dossier.
bengali.DirNeedsAdmin=নির্বাচিত ফোল্ডারের জন্য প্রশাসকের অনুমতি প্রয়োজন:%n%n%1%n%nসেখানে ইনস্টল করতে AnyCapture সব ব্যবহারকারীর জন্য ইনস্টল করতে হবে। প্রশাসক হিসেবে সেটআপ আবার শুরু করবেন?
bengali.DirNotWritable=নির্বাচিত ফোল্ডারে লেখা যাচ্ছে না:%n%n%1%n%nঅন্য একটি ফোল্ডার বেছে নিন।
russian.DirNeedsAdmin=Для выбранной папки требуются права администратора:%n%n%1%n%nЧтобы установить AnyCapture в эту папку, его нужно установить для всех пользователей. Перезапустить установку от имени администратора?
russian.DirNotWritable=Невозможно выполнить запись в выбранную папку:%n%n%1%n%nВыберите другую папку.
indonesian.DirNeedsAdmin=Folder yang dipilih memerlukan izin administrator:%n%n%1%n%nUntuk memasang di sana, AnyCapture harus dipasang untuk semua pengguna. Mulai ulang penyiapan sebagai administrator?
indonesian.DirNotWritable=Tidak dapat menulis ke folder yang dipilih:%n%n%1%n%nSilakan pilih folder lain.
urdu.DirNeedsAdmin=منتخب فولڈر کے لیے منتظم کی اجازت درکار ہے:%n%n%1%n%nوہاں انسٹال کرنے کے لیے AnyCapture کو تمام صارفین کے لیے انسٹال کرنا ہوگا۔ کیا سیٹ اپ کو بطور منتظم دوبارہ شروع کریں؟
urdu.DirNotWritable=منتخب فولڈر میں لکھا نہیں جا سکتا:%n%n%1%n%nبراہ کرم کوئی دوسرا فولڈر منتخب کریں۔
german.DirNeedsAdmin=Der ausgewählte Ordner erfordert Administratorrechte:%n%n%1%n%nUm AnyCapture dort zu installieren, muss es für alle Benutzer installiert werden. Setup als Administrator neu starten?
german.DirNotWritable=In den ausgewählten Ordner kann nicht geschrieben werden:%n%n%1%n%nBitte wählen Sie einen anderen Ordner.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "{cm:AutoStartProgram,{#AppName}}"; GroupDescription: "{cm:AutoStartProgramGroupDescription}"

[InstallDelete]
; files of earlier versions that this one no longer ships
Type: files; Name: "{app}\LEIA-ME.txt"

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\AnyCapture"; Filename: "{app}\AnyCapture.exe"
Name: "{autoprograms}\AnyCapture Editor"; Filename: "{app}\AnyCaptureEditor.exe"
Name: "{autoprograms}\AnyCapture Manual"; Filename: "{app}\help\AnyCapture-Manual.html"
Name: "{autodesktop}\AnyCapture"; Filename: "{app}\AnyCapture.exe"; Tasks: desktopicon

[Registry]
; same value that Options > "Start with Windows" writes; removed on uninstall
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "AnyCapture"; \
    ValueData: """{app}\AnyCapture.exe"" /hide"; Tasks: startup; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "AnyCapture"; \
    Flags: dontcreatekey uninsdeletevalue

[Run]
Filename: "{app}\AnyCapture.exe"; Description: "{cm:LaunchProgram,AnyCapture}"; Flags: nowait postinstall skipifsilent
; an update started by AnyCapture (/UPDATE, silent): the programs it closed start again
Filename: "{app}\AnyCapture.exe"; Flags: nowait runasoriginaluser; Check: IsUpdate
Filename: "{app}\AnyCaptureEditor.exe"; Flags: nowait runasoriginaluser; Check: IsUpdateWithEditor

[Code]
// In "just for me" mode Setup runs without administrator rights; if the user switches to a protected folder (Program
// Files, the root of C:\…), copying would fail with "access denied". When leaving the folder page, Setup checks
// whether it can write there and, if not, offers to restart as administrator (/ALLUSERS) with the chosen folder.
var
  Relaunching: Boolean;

// true in the instance restarted as administrator (the license was already accepted)
function IsRelaunch: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/RELAUNCHED') = 0 then
      Result := True;
end;

function HasParam(Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
      Result := True;
end;

// An update AnyCapture downloaded and started (Help > Check for updates): /UPDATE, and /EDITOR when the Editor was
// open. AnyCapture exits right after starting Setup; Setup waits for both programs to end (up to a minute) before
// its AppMutex check, and starts them again when it is done ([Run]).
function IsUpdate: Boolean;
begin
  Result := HasParam('/UPDATE');
end;

function IsUpdateWithEditor: Boolean;
begin
  Result := IsUpdate and HasParam('/EDITOR');
end;

function InitializeSetup: Boolean;
var
  I: Integer;
begin
  Result := True;
  if not IsUpdate then
    Exit;
  for I := 1 to 120 do
  begin
    if not CheckForMutexes('AnyCapture-Capture-{41F1BF9C-5A4E-4B6F-9C33-1D2E3F405A61},AnyCapture-Editor-{800F2EB3-7C1D-4E2A-A3B4-C5D6E7F80912}') then
      Exit;
    Sleep(500);
  end;
  Log('Update: AnyCapture is still running after a minute');
end;

// nearest folder that already exists (the install folder may not exist yet)
function ExistingAncestor(Dir: String): String;
var
  Parent: String;
begin
  Result := RemoveBackslashUnlessRoot(Dir);
  while not DirExists(Result) do
  begin
    Parent := ExtractFileDir(Result);
    if (Parent = '') or (Parent = Result) then
    begin
      Result := '';
      Exit;
    end;
    Result := Parent;
  end;
end;

function CanWriteTo(Dir: String): Boolean;
var
  Probe: String;
begin
  Dir := ExistingAncestor(Dir);
  Result := Dir <> '';
  if not Result then
    Exit;
  Probe := AddBackslash(Dir) + '~anycapture-setup-' + GetDateTimeString('hhnnsszzz', #0, #0) + '.tmp';
  Result := SaveStringToFile(Probe, '', False);
  if Result then
    DeleteFile(Probe);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = wpLicense) and IsRelaunch;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
  ErrorCode: Integer;
begin
  Result := True;
  if (CurPageID <> wpSelectDir) or WizardSilent then
    Exit;
  Dir := WizardDirValue;
  if CanWriteTo(Dir) then
    Exit;
  Result := False;
  if IsAdminInstallMode then
  begin
    MsgBox(FmtMessage(CustomMessage('DirNotWritable'), [Dir]), mbError, MB_OK);
    Exit;
  end;
  if MsgBox(FmtMessage(CustomMessage('DirNeedsAdmin'), [Dir]), mbConfirmation, MB_YESNO) <> IDYES then
    Exit;
  if ShellExec('runas', ExpandConstant('{srcexe}'),
               '/ALLUSERS /RELAUNCHED /LANG=' + ActiveLanguage + ' /DIR="' + Dir + '"',
               '', SW_SHOW, ewNoWait, ErrorCode) then
  begin
    Relaunching := True;
    WizardForm.Close;
  end
  else if ErrorCode <> 1223 then  // 1223: the user declined UAC; stay on this page
    MsgBox(SysErrorMessage(ErrorCode), mbError, MB_OK);
end;

// An earlier version installed in the other mode would stay next to this one. Installing for all users, a copy that
// was installed only for this user is removed first (its uninstaller, silently, before the files are copied). A copy
// for all users can only be removed by an administrator, so a per-user install leaves it.
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C59F0FFC-FF4A-4107-8483-9934772DA4EB}_is1';

procedure RemoveOtherInstall(Root: Integer);
var
  Cmd: String;
  Code: Integer;
begin
  if not RegQueryStringValue(Root, UninstallKey, 'UninstallString', Cmd) then
    Exit;
  Cmd := RemoveQuotes(Cmd);
  Log('Removing the per-user install of an earlier version: ' + Cmd);
  if not Exec(Cmd, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Log('Could not run it: ' + SysErrorMessage(Code));
end;

// An install made under the program's former name sits in a folder of that name. The update goes to a folder with the
// new name (in the same parent folder), and the old install is removed with its own uninstaller before the files are
// copied; its "Start with Windows" entry comes back under the new name.
var
  HadOldStartup: Boolean;

procedure InitializeWizard;
var
  Dir: String;
begin
  Dir := RemoveBackslashUnlessRoot(WizardForm.DirEdit.Text);
  if CompareText(ExtractFileName(Dir), '{#OldName}') = 0 then
    WizardForm.DirEdit.Text := AddBackslash(ExtractFileDir(Dir)) + '{#AppName}';
end;

procedure RemoveRenamedInstall;
var
  Root: Integer;
  Location, Cmd: String;
  Code: Integer;
begin
  if IsAdminInstallMode then
    Root := HKLM
  else
    Root := HKCU;
  if not RegQueryStringValue(Root, UninstallKey, 'InstallLocation', Location) then
    Exit;
  if CompareText(ExtractFileName(RemoveBackslashUnlessRoot(Location)), '{#OldName}') <> 0 then
    Exit;
  if not RegQueryStringValue(Root, UninstallKey, 'UninstallString', Cmd) then
    Exit;
  HadOldStartup := RegValueExists(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#OldName}');
  Cmd := RemoveQuotes(Cmd);
  Log('Removing the install made under the former name: ' + Cmd);
  if not Exec(Cmd, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Log('Could not run it: ' + SysErrorMessage(Code));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    RemoveRenamedInstall;
    if IsAdminInstallMode then
      RemoveOtherInstall(HKCU);
  end;
  if (CurStep = ssPostInstall) and HadOldStartup and not WizardIsTaskSelected('startup') then
    RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#AppName}',
                        '"' + ExpandConstant('{app}') + '\{#AppName}.exe" /hide');
end;

// closes this instance without asking "Exit Setup?" once the elevated one has opened
procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if Relaunching then
    Confirm := False;
end;
