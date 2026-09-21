; Inno Setup script for Dell Toolbox.
; Build on Windows (after a portable build produced stage\dell-toolbox):
;   ISCC res\installer.iss /DStageDir=stage\dell-toolbox /DAppVersion=1.0
; Output: dist\DellToolbox-<version>-setup.exe

#define AppName "Dell Toolbox"
#define AppExe "dell-toolbox.exe"
#ifndef AppVersion
#define AppVersion "1.0"
#endif
#ifndef StageDir
#define StageDir "stage\dell-toolbox"
#endif

[Setup]
AppId={{7A6E2D9B-52C4-4B8A-9C31-D3F0A1B2C701}
AppName={#AppName}
AppVersion={#AppVersion}
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={autopf64}\Dell Toolbox
DefaultGroupName={#AppName}
; the app itself requires administrator (UAC manifest)
PrivilegesRequired=admin
OutputBaseFilename=DellToolbox-{#AppVersion}-setup
OutputDir=dist
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; runascurrentuser: launch with Setup's elevated token - the app's manifest
; requires administrator, and postinstall runs de-elevated by default
; (CreateProcess failed; code 740).
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runascurrentuser

[UninstallRun]
; remove the autostart scheduled task the app may have created
Filename: "schtasks"; Parameters: "/Delete /TN DellToolbox /F"; Flags: runhidden; RunOnceId: "DelAutostartTask"
