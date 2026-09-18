#define AppName "DannyDee Track Manager"
#ifndef AppVersion
  #define AppVersion "0.11.1"
#endif
#ifndef StageDir
  #define StageDir "..\build\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{17A7C12D-B85F-493D-9520-9985DB52E766}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=DannyDee
DefaultDirName={localappdata}\Programs\DannyDee Track Manager
DefaultGroupName=DannyDee Track Manager
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=DannyDee-Track-Manager-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\DannyDeeTrackManager.exe
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany=DannyDee
VersionInfoDescription=DannyDee Track Manager Installer
VersionInfoProductName={#AppName}

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Desktop-Verknüpfung erstellen"; GroupDescription: "Zusätzliche Aufgaben:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\DannyDee Track Manager"; Filename: "{app}\DannyDeeTrackManager.exe"
Name: "{autodesktop}\DannyDee Track Manager"; Filename: "{app}\DannyDeeTrackManager.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\DannyDeeTrackManager.exe"; Description: "DannyDee Track Manager starten"; Flags: nowait postinstall skipifsilent
