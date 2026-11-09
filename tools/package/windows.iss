; Inno Setup script for the Windows installer (DECISIONS.md "Releases"). Run through
; tools/package/windows.ps1, which passes Version, Artefacts and OutDir.
; Installs the standalone app into Program Files\VJN6 and the VST3 plugin into
; Common Files\VST3. AppId must never change: it ties upgrades and the
; uninstaller to earlier installs.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Artefacts
  #error Pass /DArtefacts=<VJN6_artefacts\Release>
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{0B26E3F6-0AAC-4159-865D-EA047C1C8467}
AppName=VJN6
AppVersion={#Version}
AppVerName=VJN6 {#Version}
AppPublisher=Marco Funk
AppPublisherURL=https://github.com/wiz4rd-de/VJN6
AppSupportURL=https://github.com/wiz4rd-de/VJN6/issues
DefaultDirName={autopf}\VJN6
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
OutputDir={#OutDir}
OutputBaseFilename=VJN6-{#Version}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\VJN6.exe
UninstallDisplayName=VJN6

[Types]
Name: "full"; Description: "Standalone app and VST3 plugin"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "app"; Description: "VJN6 standalone app"; Types: full custom
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full custom

[Files]
Source: "{#Artefacts}\Standalone\VJN6.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Artefacts}\VST3\VJN6.vst3\*"; DestDir: "{commoncf64}\VST3\VJN6.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\VJN6"; Filename: "{app}\VJN6.exe"; Components: app

[Run]
Filename: "{app}\VJN6.exe"; Description: "Start VJN6"; Flags: nowait postinstall skipifsilent; Components: app
