; Midi-Fundus Setup - Inno Setup Skript (frueher MIDIARCHIV; AppId, Plugin-ID und Datenordner %APPDATA%\MIDIARCHIV bleiben)
;
; Installiert das VST3-Plugin nach C:\Program Files\Common Files\VST3 und das
; Standalone-Programm nach C:\Program Files\Midi-Fundus (beides waehlbar).
; Die im Setup gewaehlte Sprache (Deutsch/English/Francais) wird als
; HKLM\Software\Yakobo\MIDIARCHIV\Language an Midi-Fundus uebergeben; spaeter
; laesst sie sich in der App unter Einstellungen umschalten.
;
; Vorher Release bauen:
;   cmake --build build --config Release --target MIDIARCHIV_VST3 MIDIARCHIV_Standalone
; Dann:
;   "C:\Program Files\Inno Setup 7\ISCC.exe" installer\MIDIARCHIV.iss

#define AppName "Midi-Fundus"
#define AppVersion "1.0.0"
#define AppPublisher "Kay 'Yakobo' Fiedler (Yakobo1970)"
#define AppURL "https://www.undabtanzbar.de"
#define AppExeName "Midi-Fundus.exe"
#define BuildDir "..\build\MIDIARCHIV_artefacts\Release"

[Setup]
AppId={{29D5639D-9155-4EE5-AC4F-4297F0DEB3E6}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppComments={cm:AppComment}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=..\dist_installer
OutputBaseFilename=Midi-Fundus-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
LicenseFile=..\LICENSE
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
SetupIconFile=MIDIARCHIV.ico
ShowLanguageDialog=yes
UsePreviousLanguage=no

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
german.AppComment=MIDI-Archiv mit Dublettenfinder - VST3 und Standalone
english.AppComment=MIDI archive with duplicate finder - VST3 and standalone
french.AppComment=Archive MIDI avec détecteur de doublons - VST3 et autonome
german.TypeFull=VST3 + Standalone
english.TypeFull=VST3 + standalone
french.TypeFull=VST3 + autonome
german.TypeCustom=Benutzerdefiniert
english.TypeCustom=Custom
french.TypeCustom=Personnalisée
german.CompVst3=VST3-Plugin (C:\Program Files\Common Files\VST3)
english.CompVst3=VST3 plug-in (C:\Program Files\Common Files\VST3)
french.CompVst3=Plug-in VST3 (C:\Program Files\Common Files\VST3)
german.CompStandalone=Standalone-Programm
english.CompStandalone=Standalone program
french.CompStandalone=Programme autonome

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full custom
Name: "standalone"; Description: "{cm:CompStandalone}"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone

[Files]
Source: "{#BuildDir}\VST3\Midi-Fundus.vst3\*"; DestDir: "{commoncf64}\VST3\Midi-Fundus.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#BuildDir}\Standalone\{#AppExeName}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#BuildDir}\Standalone\GeneralUser-GS.sf2"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#BuildDir}\Standalone\LICENSE_GeneralUser.txt"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\Source\ThirdParty\LICENSE_tsf.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; language chosen in this setup -> Midi-Fundus takes it over once at the next start (afterwards the in-app setting counts)
Root: HKLM; Subkey: "Software\Yakobo\MIDIARCHIV"; ValueType: string; ValueName: "Language"; ValueData: "{language}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Yakobo"; Flags: uninsdeletekeyifempty

[InstallDelete]
; old name: same plug-in ID, so the old bundle must go (otherwise the DAW sees the plug-in twice)
Type: filesandordirs; Name: "{commoncf64}\VST3\MIDIARCHIV.vst3"
Type: filesandordirs; Name: "{autopf}\MIDIARCHIV"
Type: files; Name: "{autodesktop}\MIDIARCHIV.lnk"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Components: standalone
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent; Components: standalone
