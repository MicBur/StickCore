; Inno Setup script for StickCore (Windows installer)
; Built automatically by .github/workflows/windows.yml, or run locally with
; ISCC after placing the windeployqt output in the "deploy" folder.

#define AppName "StickCore"
#define AppVersion "1.0"
#define AppPublisher "StickCore"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\Output
OutputBaseFilename=StickCoreSetup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\StickCore.exe

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Everything windeployqt produced (the exe plus all required Qt DLLs/plugins).
Source: "..\deploy\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\StickCore"; Filename: "{app}\StickCore.exe"
Name: "{group}\{cm:UninstallProgram,StickCore}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\StickCore"; Filename: "{app}\StickCore.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\StickCore.exe"; Description: "{cm:LaunchProgram,StickCore}"; Flags: nowait postinstall skipifsilent
