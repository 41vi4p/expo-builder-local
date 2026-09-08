; Inno Setup script for the ebl (expo-local-builder) Windows installer.
;
; ebl.exe here is the real, native CLI (built from ../../cli, same source every
; other platform uses) - not a WSL2 forwarder. This installer bundles the
; `cmake --install`ed bin\/share\ tree (see ../../cli/CMakeLists.txt) plus
; install.ps1/uninstall.ps1, and just runs install.ps1 -LocalInstallDir
; -Mode <Native|Docker> to do the actual work (Docker Desktop check + WSL2 tuning in
; Docker mode, or the JDK/Android SDK/Node toolchain setup in Native mode, then the
; PATH update either way) - all the real logic for that lives in exactly one place
; (install.ps1), so the one-line `irm | iex` install and this GUI installer can
; never drift apart. The [Code] section below only adds a wizard page asking which
; mode to pass through - see GetInstallModeArg. UNVERIFIED ON REAL WINDOWS HARDWARE
; (see ../../CLAUDE.md's native-engine section) - this wizard page in particular has
; never actually been rendered/clicked through on a real Windows machine.
;
; Build with: iscc ebl.iss  (from a Windows machine/CI runner with Inno Setup 6
; installed - https://jrsoftware.org/isinfo.php). Expects
; ..\..\cli\build\install\bin\ebl.exe and ..\..\cli\build\install\share\... to
; already exist (see ../../.github/workflows/release.yml's windows-build-and-publish
; job for the `cmake --install` step that produces them).

#define MyAppName "ebl (expo-local-builder)"
#define MyAppVersion "0.16.0"
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
; Versioned like the .deb (ebl_<version>_amd64.deb) so a user grabbing this by hand
; from the Releases page (README.md/ebl_landing_page point at the Releases page, not
; a fixed "latest/download/" URL, so nothing depends on this staying unversioned —
; see release.yml's upload step, which resolves the actual built filename rather than
; hardcoding it) can tell which version they're getting without opening it first.
OutputBaseFilename=ebl-setup-{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
; The installer .exe's own Win32 version resource (Explorer → Properties → Details) —
; distinct from AppVersion above, which only sets the *installed application's*
; registered version in Add/Remove Programs.
VersionInfoVersion={#MyAppVersion}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\..\cli\build\install\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs
Source: "..\..\cli\build\install\share\*"; DestDir: "{app}\share"; Flags: ignoreversion recursesubdirs
Source: "..\install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\uninstall.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Run]
; -LocalInstallDir: the files above are already in place, so this skips the
; network download install.ps1 would otherwise do for the binary itself - Native
; mode's JDK/Android SDK/Node downloads (or Docker mode's image pulls) still happen
; here regardless, via install.ps1's own `ebl setup --runtime <Mode>` step. -Mode
; comes from the wizard page added below (GetInstallModeArg).
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install.ps1"" -LocalInstallDir ""{app}"" {code:GetInstallModeArg}"; \
    Flags: waituntilterminated; \
    StatusMsg: "Finishing setup..."

[UninstallRun]
; -Quiet: this runs hidden (runhidden) with no console attached to prompt on, so it
; only ever does the always-safe part (remove the install dir + PATH entry) - the
; interactive native-toolchain/config cleanup prompts only happen when uninstall.ps1
; is run directly from a terminal instead (see its own header comment).
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall.ps1"" -Quiet"; \
    Flags: waituntilterminated runhidden

[Code]
var
  ModePage: TInputOptionWizardPage;

procedure InitializeWizard;
begin
  ModePage := CreateInputOptionPage(wpWelcome,
    'Choose a build engine',
    'How should ebl build Android apps on this machine? You can switch later with "ebl setup --runtime <docker|native>".',
    '',
    True, False);
  ModePage.Add('Native (recommended) - installs the Android SDK, JDK 17, and Node.js directly on this machine (isolated under %LOCALAPPDATA%\ebl, never touching an existing install). No Docker Desktop or WSL2 required. Builds run directly on your system rather than in an isolated container.');
  ModePage.Add('Docker-based - uses Docker Desktop + WSL2 to run builds in a disposable, fully isolated Linux container, the same engine Linux/macOS use. Requires installing Docker Desktop (and WSL2) separately if you don''t already have them, and uses more disk/memory for the container runtime.');
  ModePage.SelectedValueIndex := 0; // Native pre-selected, per ../CLAUDE.md's "defaults to native on Windows"
end;

function GetInstallModeArg(Param: String): String;
begin
  if ModePage.SelectedValueIndex = 0 then
    Result := '-Mode Native'
  else
    Result := '-Mode Docker';
end;
