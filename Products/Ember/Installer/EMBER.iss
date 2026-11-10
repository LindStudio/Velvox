; EMBER De-Esser - Windows installer (Inno Setup 6). Built by scripts\make-installer.ps1, which passes
;   /DBuildDir=<build\Ember_artefacts\Release>  /DAppVersion=<x.y.z>  /DOutDir=<output folder>
; Installs: VST3 (C:\Program Files\Common Files\VST3), standalone app, manuals (ProgramData\Lind Audio\EMBER).
; User presets in Documents\Lind Audio\EMBER\Presets are never removed by the uninstaller.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\..\build\Ember_artefacts\Release"
#endif
#ifndef OutDir
  #define OutDir "..\..\..\build\installer"
#endif

#define AppName "EMBER De-Esser"
#define Publisher "Lind Audio"

[Setup]
; NEVER change the AppId: Windows uses it to find and update the installed product
AppId={{221D840B-F3A0-49B4-9D1E-1B79E856608F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL=https://lind-audio.com
AppSupportURL=https://lind-audio.com/ember
AppCopyright=© 2026 {#Publisher}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Lind Audio\EMBER
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutDir}
OutputBaseFilename=EMBER_De-Esser_{#AppVersion}_Windows_Setup
SetupIconFile=ember.ico
UninstallDisplayIcon={app}\EMBER De-Esser.exe
UninstallDisplayName={#AppName}
LicenseFile=EULA.txt
WizardStyle=modern
WizardImageFile=wizard_large.bmp
WizardSmallImageFile=wizard_small.bmp
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
de.CompVst3=VST3-Plugin (für FL Studio, Ableton, Cubase, Reaper …)
en.CompVst3=VST3 plugin (for FL Studio, Ableton, Cubase, Reaper …)
de.CompApp=Standalone-App (ohne DAW nutzbar)
en.CompApp=Standalone app (works without a DAW)
de.CompManual=Handbuch
en.CompManual=Manual
de.OpenManual=Handbuch jetzt öffnen
en.OpenManual=Open the manual now

[Types]
Name: "full"; Description: "Full"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full custom; Flags: fixed
Name: "app"; Description: "{cm:CompApp}"; Types: full
Name: "manual"; Description: "{cm:CompManual}"; Types: full custom

[Files]
Source: "{#BuildDir}\VST3\EMBER De-Esser.vst3\*"; DestDir: "{commoncf64}\VST3\EMBER De-Esser.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#BuildDir}\Standalone\EMBER De-Esser.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app
Source: "EMBER_Handbuch.html"; DestDir: "{commonappdata}\Lind Audio\EMBER"; Flags: ignoreversion; Components: manual
Source: "EMBER_Manual.html"; DestDir: "{commonappdata}\Lind Audio\EMBER"; Flags: ignoreversion; Components: manual
Source: "EULA.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\EMBER De-Esser.exe"; Components: app
Name: "{autoprograms}\{#AppName} Handbuch"; Filename: "{commonappdata}\Lind Audio\EMBER\EMBER_Handbuch.html"; Components: manual; Languages: de
Name: "{autoprograms}\{#AppName} Manual"; Filename: "{commonappdata}\Lind Audio\EMBER\EMBER_Manual.html"; Components: manual; Languages: en

[Run]
Filename: "{commonappdata}\Lind Audio\EMBER\EMBER_Handbuch.html"; Description: "{cm:OpenManual}"; Flags: postinstall shellexec skipifsilent unchecked; Components: manual; Languages: de
Filename: "{commonappdata}\Lind Audio\EMBER\EMBER_Manual.html"; Description: "{cm:OpenManual}"; Flags: postinstall shellexec skipifsilent unchecked; Components: manual; Languages: en

[UninstallDelete]
; the VST3 bundle folder carries desktop.ini (system attribute) and is not removed as an empty folder otherwise
Type: filesandordirs; Name: "{commoncf64}\VST3\EMBER De-Esser.vst3"
