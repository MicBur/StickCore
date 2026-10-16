; Inno Setup script for StickCore (Windows installer)
; Built automatically by .github/workflows/windows.yml, or run locally with
; ISCC after placing the windeployqt output in the "deploy" folder.

#define AppName "StickCore"
#define AppVersion "1.0.0"
#define AppPublisher "MicBur"
#define AppURL "https://github.com/MicBur/StickCore"
#define AppExeName "StickCore.exe"

[Setup]
AppId={{D37E6F90-7F89-4D62-8924-11883F205C9A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\Output
OutputBaseFilename=StickCoreSetup
SetupIconFile=..\media\stickcore.ico
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "installfonts"; Description: "Alle College- & Stickschriften in Windows registrieren"; GroupDescription: "Schriften:"

[Files]
Source: "..\deploy\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\deploy\fonts\*.ttf"; DestDir: "{autofonts}"; Flags: onlyifdoesntexist uninsneveruninstall; Tasks: installfonts

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\stickcore.ico"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\stickcore.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
