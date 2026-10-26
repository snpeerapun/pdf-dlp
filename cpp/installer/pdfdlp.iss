; PDF DLP — Windows installer (Inno Setup 6). Built by .github/workflows/build.yml; version passed with /DAppVersion=x.y.z
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define AppName "PDF DLP"
#define AppPublisher "oneable"
#define AppURL "https://oneable.co.th"
#define AppExe "PDF DLP.exe"

[Setup]
AppId={{7D3B1F2A-5C84-4F1E-9B0C-PDFDLP000001}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=PDF-DLP-Setup-{#AppVersion}
SetupIconFile=..\assets\icon\pdfdlp.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "thai"; MessagesFile: "compiler:Languages\Thai.isl"; LicenseFile: ""

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "pdfassoc"; Description: "Open PDF files with {#AppName} (adds to 'Open with')"; GroupDescription: "File association:"
Name: "contextmenu"; Description: "Explorer right-click menu: Open / Set classification / Set password"; GroupDescription: "File association:"

[Files]
Source: "..\dist\PDF-DLP-win64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; "Open with" entry for .pdf (does not steal the default viewer)
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: pdfassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: pdfassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""; Tasks: pdfassoc
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithList\{#AppExe}"; Flags: uninsdeletekey; Tasks: pdfassoc
; Explorer context menu (same keys the app manages from Settings)
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.open"; ValueType: string; ValueData: "Open with {#AppName}"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.open"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" --action open ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.classify"; ValueType: string; ValueData: "Set classification ({#AppName})"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.classify"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.classify\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" --action classify ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.encrypt"; ValueType: string; ValueData: "Set PDF password ({#AppName})"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.encrypt"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.encrypt\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" --action encrypt ""%1"""; Tasks: contextmenu

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
