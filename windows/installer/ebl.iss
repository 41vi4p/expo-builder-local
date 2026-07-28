; Inno Setup script for the ebl (expo-local-builder) Windows installer.
;
; ebl.exe here is the real, native CLI (built from ../../cli, same source every
; other platform uses) - not a WSL2 forwarder. This installer bundles the
; `cmake --install`ed bin\/share\ tree (see ../../cli/CMakeLists.txt) plus
; install.ps1/uninstall.ps1, and just runs install.ps1 -LocalInstallDir to do the
; Docker Desktop check + PATH update - all the real logic for that lives in exactly
; one place (install.ps1), so the one-line `irm | iex` install and this GUI
; installer can never drift apart.
;
; Build with: iscc ebl.iss  (from a Windows machine/CI runner with Inno Setup 6
; installed - https://jrsoftware.org/isinfo.php). Expects
; ..\..\cli\build\install\bin\ebl.exe and ..\..\cli\build\install\share\... to
; already exist (see ../../.github/workflows/release.yml's windows-build-and-publish
; job for the `cmake --install` step that produces them).

#define MyAppName "ebl (expo-local-builder)"
#define MyAppVersion "0.9.0"
#define MyAppPublisher "41vi4p"
#define MyAppURL "https://github.com/41vi4p/expo-builder-local"
#define MyAppExeName "ebl.exe"

[Setup]
AppId={{20DF2A08-D7F1-43EB-92F9-39D298B7A69A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={localappdata}\Programs\ebl
DisableProgramGroupPage=yes
; Fixed, not user-choosable: install.ps1 -LocalInstallDir "{app}" below assumes
; {app} is exactly install.ps1's own hardcoded $InstallDir
; (%LOCALAPPDATA%\Programs\ebl) - letting the wizard's directory page override
; {app} would silently desync the two.
DisableDirPage=yes
; Per-user install, matching install.ps1 (%LOCALAPPDATA%\Programs\ebl) - no UAC
; prompt for the installer itself.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputDir=dist
OutputBaseFilename=ebl-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\..\cli\build\install\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs
Source: "..\..\cli\build\install\share\*"; DestDir: "{app}\share"; Flags: ignoreversion recursesubdirs
Source: "..\install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\uninstall.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Run]
; -LocalInstallDir: the files above are already in place, so this only does the
; Docker Desktop check + PATH update - no network access needed.
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install.ps1"" -LocalInstallDir ""{app}"""; \
    Flags: waituntilterminated; \
    StatusMsg: "Finishing setup..."

[UninstallRun]
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall.ps1"""; \
    Flags: waituntilterminated runhidden
