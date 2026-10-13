; Inno Setup script for the Windows installer. CI runs:
;   ISCC /DAppVersion=1.3.0 /DSourceDir=path\to\deployed\files packaging\windows\dq9.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\deploy"
#endif

[Setup]
AppId={{6F1C2B7E-3D4A-4E8B-9C21-D9A1C4E0B9A7}
AppName=DQ9 Alchemy Helper
AppVersion={#AppVersion}
AppPublisher=WeirdWally-dev
AppPublisherURL=https://github.com/WeirdWally-dev/dq9-alchemy-tool
DefaultDirName={autopf}\DQ9 Alchemy Helper
DefaultGroupName=DQ9 Alchemy Helper
DisableProgramGroupPage=yes
; Installs for the current user only, so no admin prompt.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputBaseFilename=DQ9-Alchemy-Helper-Setup
OutputDir=..\..\dist
SetupIconFile=dq9.ico
UninstallDisplayIcon={app}\DQ9AlchemyHelper.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\DQ9 Alchemy Helper"; Filename: "{app}\DQ9AlchemyHelper.exe"
Name: "{autodesktop}\DQ9 Alchemy Helper"; Filename: "{app}\DQ9AlchemyHelper.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\DQ9AlchemyHelper.exe"; Description: "{cm:LaunchProgram,DQ9 Alchemy Helper}"; Flags: nowait postinstall skipifsilent
