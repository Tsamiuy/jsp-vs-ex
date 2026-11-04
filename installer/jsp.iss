; J-Stars Victory VS EX: Fan Revival: installer (Inno Setup 6.7).
; Layout (PARAM.SFO reader, per-user install, portable task) adapted by hand from the maintainer's installer for another PS3
; port (same MIT licence), without its update page, RPCS3 import, ISO reading, version check, crash report or any online
; piece: this installer and the program it installs contact no server and send nothing anywhere.
;
; Per-user, no admin. It copies nothing of the game and downloads nothing. Wizard: welcome, "Use your RPCS3 install?" (optional: pre-fills
; the next pages from the player's RPCS3, read only; may offer to import the RPCS3 save after a shown dry run), your disc folder (PS3_GAME
; with PARAM.SFO whose TITLE_ID is BLUS31519 or BLES02136), your decrypted EBOOT.elf, install folder,
; shortcuts and the Portable task, ready, install, finish. The compiler (llvm-mingw, CMake, Ninja) is bundled in the
; payload, so there is no build tools page and no Visual Studio is needed. After the files are copied, firstrun.py (bundled
; Python) builds this repository's port on the player's PC: lift, SPU jobs, compile.
;
; Compile on Windows: ISCC /DPayload=<dir made by installer\tools\assemble_payload.py> [/DArt=<art dir>] installer\jsp.iss
; The art comes from installer\tools\installer_art.py (build\installer\art, never committed). See installer\README.md.
#ifndef Payload
  #error Pass /DPayload=<dir made by installer\tools\assemble_payload.py>
#endif
#ifndef Art
  #define Art "..\build\installer\art"
#endif
#ifndef Music
  #define Music "..\build\installer\music"
#endif
; Opt-in music: an "Enable Music?" box, off by default, plays one of the tracks in installer\music\tracks.tsv at random.
; installer\tools\installer_music.py renders them to build\installer\music (never committed). Silent installs never play.
#define MusicCount 6
#define AppTitle "J-Stars Victory VS EX: Fan Revival"
#define AppDirName "JSP Fan Revival"
#define LauncherExe "jsp-launcher.exe"
#define AppVersion Trim(FileRead(FileOpen(AddBackslash(Payload) + "VERSION.txt")))

[Setup]
AppId={{6F0B8A52-3C41-4D7E-9B1A-5E2C7D40A9F3}
AppName={#AppTitle}
AppVersion={#AppVersion}
AppPublisher=unofficial fan project
DefaultDirName={autopf}\{#AppDirName}
DefaultGroupName={#AppDirName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableDirPage=no
PrivilegesRequired=lowest
; Files keep their UTC time stamps: without this a setup built in one time zone sets the sources' times in local time on a PC west of UTC,
; i.e. in the future, and ninja/CMake then rebuild them on every run.
TimeStampsInUTC=yes
; PRIVATE TEST BUILD only (not for a PR): the setup always writes its own log to %TEMP%
SetupLogging=yes
PrivilegesRequiredOverridesAllowed=
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
OutputBaseFilename=jsp-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern dark
WizardImageFile={#Art}\wizard-100.bmp,{#Art}\wizard-125.bmp,{#Art}\wizard-150.bmp,{#Art}\wizard-200.bmp
WizardSmallImageFile={#Art}\header-100.bmp,{#Art}\header-125.bmp,{#Art}\header-150.bmp,{#Art}\header-200.bmp
SetupIconFile={#Art}\jsp-setup.ico
UninstallDisplayIcon={app}\jsp-uninstall.ico
UninstallDisplayName={#AppTitle}
AllowNoIcons=yes
CloseApplications=no

[Messages]
WelcomeLabel1=J-Stars Victory VS EX: Fan Revival
WelcomeLabel2=An unofficial fan project. Nothing of the game is included or downloaded: setup builds the port on this PC from your own copy of the game.%n%nNext, tell setup where your disc folder and your decrypted EBOOT.elf are.
FinishedHeadingLabel=Setup is done

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Excludes: "\tools\jsp-settings.cs"; Flags: recursesubdirs ignoreversion
; The settings window (launcher\JStarsLauncher.cs): compiled right after it is copied with the C# compiler that comes with Windows
; (.NET Framework 4), so the shortcuts below can open it. Without that compiler the shortcuts start the game directly.
Source: "{#Payload}\tools\jsp-settings.cs"; DestDir: "{app}\tools"; Flags: ignoreversion; AfterInstall: BuildSettings
Source: "{#Music}\music-*.wav"; Flags: dontcopy
; "Use your RPCS3 install?" page: the bundled Python and the two helper scripts are unpacked to {tmp} while the wizard runs (setup
; has not copied anything yet). The compiler stores identical files once, so the Python is not in the setup twice.
Source: "{#Payload}\toolchain\python\*"; Flags: dontcopy
Source: "{#Payload}\tools\rpcs3_*.py"; Flags: dontcopy

[Tasks]
Name: "desktopicon"; Description: "A shortcut on the desktop"; Flags: unchecked
; ENHANCEMENT: portable task (layout from the maintainer's installer for another PS3 port): saves and settings in the install folder, not in
; %APPDATA%\JSP Fan Revival. Off by default; silent installs use /TASKS=portable.
Name: "portable"; Description: "&Portable: saves and settings in the install folder (else in %APPDATA%)"; Flags: unchecked

[INI]
Filename: "{app}\jsp-install.ini"; Section: "install"; Key: "portable"; String: "1"; Tasks: portable
Filename: "{app}\jsp-install.ini"; Section: "install"; Key: "portable"; String: "0"; Tasks: not portable

[Icons]
Name: "{group}\{#AppDirName}"; Filename: "{app}\jsp-settings.exe"; WorkingDir: "{app}"; IconFilename: "{app}\jsp-launcher.ico"; Check: SettingsBuilt
Name: "{group}\{#AppDirName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; IconFilename: "{app}\jsp-launcher.ico"; Check: not SettingsBuilt
Name: "{group}\Build again ({#AppDirName})"; Filename: "{app}\build-again.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\jsp-launcher.ico"
Name: "{group}\Make a support bundle ({#AppDirName})"; Filename: "{app}\support-bundle.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\jsp-launcher.ico"
Name: "{group}\Uninstall {#AppDirName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\jsp-uninstall.ico"
Name: "{autodesktop}\{#AppDirName}"; Filename: "{app}\jsp-settings.exe"; WorkingDir: "{app}"; IconFilename: "{app}\jsp-launcher.ico"; Tasks: desktopicon; Check: SettingsBuilt
Name: "{autodesktop}\{#AppDirName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; IconFilename: "{app}\jsp-launcher.ico"; Tasks: desktopicon; Check: not SettingsBuilt

[UninstallDelete]
Type: files; Name: "{app}\jsp-settings.exe"
Type: filesandordirs; Name: "{app}\build"
Type: filesandordirs; Name: "{app}\__pycache__"
Type: filesandordirs; Name: "{app}\toolchain\python\__pycache__"
Type: filesandordirs; Name: "{app}\ps3recomp\tools\__pycache__"
; firstrun.py's empty stand-in dev_flash (the game reads nothing from it)
Type: dirifempty; Name: "{app}\dev_flash"
Type: files; Name: "{app}\*.log"
Type: files; Name: "{app}\jsp-install.ini"
Type: files; Name: "{app}\build-again.cmd"
Type: files; Name: "{app}\jsp-support-*.zip"
Type: files; Name: "{app}\paths.txt"
Type: filesandordirs; Name: "{app}\game"
Type: files; Name: "{app}\logs\*"
Type: dirifempty; Name: "{app}\logs"

[Code]
var
  DiscPage: TInputDirWizardPage;
  EbootFilePage: TInputFileWizardPage;
  RpcsPage: TWizardPage;
  RpcsEdit: TEdit;
  RpcsText: TNewStaticText;
  RpcsBrowse: TNewButton;
  RpcsLabel: TNewStaticText;
  RpcsButton: TNewButton;
  SavePage: TWizardPage;
  SaveMemo: TNewMemo;
  SaveBox: TNewCheckBox;
  PyReady, RpcsAuto, ImportSave: Boolean;
  RpcsLastDir, RpcsFoundDir, PreDisc, PreEboot, SavePlan, SaveNote: String;
  SaveFound, SaveCopies, SaveReplaces: Integer;
  DiscId, DiscPath, EbootPath, FlashPath: String;
  SkipBuild: Boolean;
  BuildRc: Integer;

{ ---- PARAM.SFO (magic NUL P S F, key table start at 8, data table start at 12, entry count at
  16, 16-byte entries from 20: key offset u16, format u16, length u32, max length u32, data offset u32) ---- }
function SfoInt(S: AnsiString; P, Bytes: Integer): Integer;
var
  B: Integer;
begin
  Result := 0;
  if (P < 0) or (P + Bytes > Length(S)) then Exit;
  for B := Bytes - 1 downto 0 do Result := Result * 256 + Ord(S[P + B + 1]);
end;

function SfoText(S: AnsiString; P, Max: Integer): String;
var
  I: Integer;
begin
  Result := '';
  I := P + 1;
  while (I <= Length(S)) and (I <= P + Max) and (Ord(S[I]) <> 0) do begin
    Result := Result + Chr(Ord(S[I]));
    I := I + 1;
  end;
end;

function SfoField(Path, Key: String): String;
var
  S: AnsiString;
  KT, DT, N, I, E: Integer;
begin
  Result := '';
  if not LoadStringFromFile(Path, S) then Exit;
  if (Length(S) < 20) or (Ord(S[1]) <> 0) or (Ord(S[2]) <> 80) or (Ord(S[3]) <> 83) or (Ord(S[4]) <> 70) then Exit;
  KT := SfoInt(S, 8, 4);
  DT := SfoInt(S, 12, 4);
  N := SfoInt(S, 16, 4);
  if N > 200 then N := 200;
  for I := 0 to N - 1 do begin
    E := 20 + 16 * I;
    if SfoText(S, KT + SfoInt(S, E, 2), 32) = Key then begin
      Result := SfoText(S, DT + SfoInt(S, E + 12, 4), SfoInt(S, E + 4, 4));
      Exit;
    end;
  end;
end;

{ Same rule as firstrun.py (checks P1 and P2): [ ] ; break CMake's configure; & ^ % ! " break the generated .cmd files. '' when fine. }
function PathProblem(Path, What: String; Script: Boolean): String;
var
  I: Integer;
  C: String;
begin
  Result := '';
  for I := 1 to Length(Path) do begin
    C := Copy(Path, I, 1);
    if (Pos(C, '[];') > 0) or (Script and (Pos(C, '&^%!"') > 0)) then begin
      Result := 'The ' + What + ' path contains "' + C + '", which the build cannot handle. Choose a folder without any of these characters: [ ] ; ' +
        'and, for the install folder, & ^ % ! "' + #13#10 + #13#10 + Path;
      Exit;
    end;
  end;
end;

{ '' when Dir is a disc folder of a supported region (US BLUS31519, EU BLES02136), else the message. Dir may be the
  disc folder or its PS3_GAME folder. Id and Root come back for the install ini. }
function DiscProblem(Dir: String; var Id, Root: String): String;
begin
  Id := '';
  Root := RemoveBackslash(Dir);
  if Root = '' then begin
    Result := 'Select the folder of your game disc (the one that holds PS3_GAME).';
    Exit;
  end;
  Result := PathProblem(Root, 'disc folder', False);
  if Result <> '' then Exit;
  if (not FileExists(Root + '\PS3_GAME\PARAM.SFO')) and FileExists(Root + '\PARAM.SFO') and
     (CompareText(ExtractFileName(Root), 'PS3_GAME') = 0) then
    Root := RemoveBackslash(ExtractFileDir(Root));   { the drive root of a mounted image comes back as "G:\" }
  if not FileExists(Root + '\PS3_GAME\PARAM.SFO') then begin
    Result := 'This folder does not look like a PS3 game disc folder: PS3_GAME\PARAM.SFO is missing. Select the folder that holds PS3_GAME.';
    Exit;
  end;
  Id := SfoField(Root + '\PS3_GAME\PARAM.SFO', 'TITLE_ID');
  if Id = '' then
    Result := 'PS3_GAME\PARAM.SFO has no readable TITLE_ID, so it is not a game folder.'
  else if (Id <> 'BLUS31519') and (Id <> 'BLES02136') then
    Result := 'This is the game ' + Id + ', not J-Stars Victory VS+. Supported: BLUS31519 (US) and BLES02136 (EU).'
  else
    Result := '';
end;

function GetDriveTypeW(RootPath: String): Cardinal;
  external 'GetDriveTypeW@kernel32 stdcall';

{ True when the chosen disc folder is on a read-only drive, so the decrypted EBOOT.elf cannot be created beside the EBOOT.BIN there.
  Two tests, in this order: (1) GetDriveType says CD/DVD (DRIVE_CDROM = 5): a disc, or an image mounted as a drive letter; nothing is
  written. (2) For any other drive, only now that the EBOOT.elf is missing: a file with a name that does not exist yet is created in
  PS3_GAME\USRDIR and deleted at once; if it cannot be created the drive is write-protected (a read-only share or card). Not used: a
  folder's read-only attribute, because Explorer sets it on ordinary writable folders. An existing file of that name is never touched. }
function DiscIsReadOnly(Folder: String): Boolean;
var
  Drive, Dir, Probe: String;
begin
  Result := False;
  if Folder = '' then Exit;
  Drive := ExtractFileDrive(Folder);
  if (Drive <> '') and (GetDriveTypeW(AddBackslash(Drive)) = 5) then begin
    Result := True;
    Exit;
  end;
  Dir := RemoveBackslash(Folder) + '\PS3_GAME\USRDIR';
  if not DirExists(Dir) then Exit;
  Probe := Dir + '\jsp-setup-write-test.tmp';
  if FileExists(Probe) then Exit;
  if SaveStringToFile(Probe, '', False) then
    DeleteFile(Probe)
  else
    Result := True;
end;

{ '' when Path is a decrypted PowerPC64 ELF (7F 45 4C 46, 64-bit, big endian, machine 21), else the message. An
  encrypted EBOOT.BIN (SELF, "SCE") gets its own message. Reads 20 bytes. When the file is not there and the disc folder is on a
  read-only drive, the message says how to get the file (no steps: the decryption itself is RPCS3's documentation). }
function EbootProblem(Path: String): String;
var
  F: TFileStream;
  H: AnsiString;
begin
  if not FileExists(Path) then begin
    Result := 'Select your decrypted EBOOT.elf.';
    if DiscIsReadOnly(DiscPath) then
      Result := Result + #13#10#13#10 + 'The disc folder is on a read-only drive (a disc or a mounted image), so the decrypted EBOOT.elf cannot be made there. ' +
        'Copy EBOOT.BIN from PS3_GAME\USRDIR to a normal folder, make EBOOT.elf from that copy (from your own copy, see RPCS3 and its documentation), then select it here.';
    Exit;
  end;
  Result := 'Could not read ' + Path + '.';
  try
    F := TFileStream.Create(Path, fmOpenRead or fmShareDenyNone);
    try
      if F.Size < 20 then begin
        Result := 'This file is too small to be an EBOOT.elf.';
        Exit;
      end;
      H := StringOfChar(' ', 20);
      F.ReadBuffer(H, 20);  { Inno's ReadBuffer takes an AnsiString, not an array }
    finally
      F.Free;
    end;
  except
    Exit;
  end;
  if (Ord(H[1]) = $53) and (Ord(H[2]) = $43) and (Ord(H[3]) = $45) and (Ord(H[4]) = 0) then
    Result := 'That is the encrypted EBOOT.BIN. Setup needs the decrypted EBOOT.elf, made from your own copy: see RPCS3 and its documentation.'
  else if (Ord(H[1]) <> $7F) or (Ord(H[2]) <> $45) or (Ord(H[3]) <> $4C) or (Ord(H[4]) <> $46) then
    Result := 'This file is not an ELF program, so it is not a decrypted EBOOT.elf (see RPCS3 and its documentation).'
  else if (Ord(H[5]) <> 2) or (Ord(H[6]) <> 2) or (Ord(H[19]) <> 0) or (Ord(H[20]) <> 21) then
    Result := 'This ELF is not a PlayStation 3 (PowerPC64) program.'
  else
    Result := '';
end;

{ '' when Dir is the dev_flash folder of an RPCS3 install (it holds sys), else the message. Only for /DEVFLASH=: the game reads
  nothing from dev_flash (tested 2026-11-04: menus, a fight and sound with an empty folder), so the wizard no longer asks for it. }
function FlashProblem(Dir: String): String;
begin
  Result := '';
  if RemoveBackslash(Dir) = '' then Result := 'Select the dev_flash folder of your RPCS3 install (see RPCS3 and its documentation).'
  else if not DirExists(RemoveBackslash(Dir) + '\sys') then
    Result := 'That folder has no "sys" folder inside, so it is not a dev_flash folder. Select the dev_flash folder of your RPCS3 install.';
end;

{ ---- "Use your RPCS3 install?" (optional): rpcs3_wizard.py with the bundled Python looks in the player's RPCS3 folders, read only, and
  fills in the disc folder and an already decrypted EBOOT.elf; every field can still be changed. It can also
  copy the game's save from RPCS3 into this port's save folder, but only after a list of what it would do is shown and the player
  ticks the box. Real paths go only into the fields; the log gets the helper's --json --redacted output. ---- }
function UnpackHelper: Boolean;
begin
  if not PyReady then begin
    try
      ExtractTemporaryFiles('{tmp}\python*');
      ExtractTemporaryFiles('{tmp}\*.dll');
      ExtractTemporaryFiles('{tmp}\*.pyd');
      ExtractTemporaryFiles('{tmp}\rpcs3_*.py');
      // the pattern is matched against the whole destination name, so it starts with the temp folder
      PyReady := FileExists(ExpandConstant('{tmp}\python.exe')) and FileExists(ExpandConstant('{tmp}\rpcs3_wizard.py'));
    except
      Log('rpcs3: could not unpack the helper: ' + GetExceptionMessage);
    end;
  end;
  Result := PyReady;
end;

{ One command-line argument in quotes, read back by Python exactly. A trailing backslash would escape the closing quote
  (Windows' rule: backslashes before a quote are halved), so they are doubled: a disc folder picked as the PS3_GAME folder
  of a mounted image becomes "G:\" and split the rest of the line ("unrecognized arguments"). }
function ArgQ(S: String): String;
var
  N: Integer;
begin
  N := Length(S);
  Result := S;
  while (N > 0) and (S[N] = '\') do begin
    Result := Result + '\';
    N := N - 1;
  end;
  Result := '"' + Result + '"';
end;

{ Runs the helper; its stdout (redacted JSON) goes to the log. Args must hold no cmd metacharacters (the RPCS3 folder is checked by PathProblem first). }
function RunHelper(Args: String): Integer;
var
  OutF: String;
  Lines: TArrayOfString;
  I, Rc: Integer;
begin
  Result := -1;
  if not UnpackHelper then Exit;
  OutF := ExpandConstant('{tmp}\rpcs3_out.txt');
  DeleteFile(OutF);
  if not Exec(ExpandConstant('{cmd}'), '/C ""' + ExpandConstant('{tmp}\python.exe') + '" -X utf8 "' + ExpandConstant('{tmp}\rpcs3_wizard.py') + '" ' +
      Args + ' --json --redacted > "' + OutF + '" 2>&1"', '', SW_HIDE, ewWaitUntilTerminated, Rc) then Exit;
  Result := Rc;
  Log('rpcs3_wizard ' + Copy(Args, 1, Pos(' ', Args) - 1) + ' exit ' + IntToStr(Rc));
  if LoadStringsFromFile(OutF, Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do Log('rpcs3_wizard> ' + Lines[I]);
end;

function HddDir: String;
begin
  Result := ExpandConstant('{userappdata}\{#AppDirName}\hdd0');
end;

{ Finds RPCS3 (Dir '' = the usual places) and keeps what it found in RpcsFoundDir, PreDisc, PreEboot and SaveFound.
  Returns the text for the page: what was filled in, and each problem in plain words. }
function RpcsSearch(Dir: String): String;
var
  Ini, Args, Id: String;
  I, N: Integer;
begin
  Result := '';
  RpcsFoundDir := ''; PreDisc := ''; PreEboot := ''; SaveFound := 0;
  Ini := ExpandConstant('{tmp}\rpcs3_find.ini');
  DeleteFile(Ini);
  Args := 'find --ini ' + ArgQ(Ini);
  if Dir <> '' then Args := Args + ' --rpcs3 ' + ArgQ(Dir);
  RunHelper(Args);
  if not FileExists(Ini) then begin
    Result := 'The search could not run. Fill in the next pages yourself.';
    Exit;
  end;
  if GetIniString('rpcs3', 'found', '0', Ini) = '1' then begin
    RpcsFoundDir := GetIniString('rpcs3', 'root', '', Ini);
    PreDisc := GetIniString('rpcs3', 'disc', '', Ini);
    PreEboot := GetIniString('rpcs3', 'eboot', '', Ini);
    SaveFound := StrToIntDef(GetIniString('rpcs3', 'saves', '0', Ini), 0);
    Id := GetIniString('rpcs3', 'title_id', '', Ini);
    Result := 'RPCS3 was found. Filled in on the next pages:';
    if PreDisc <> '' then Result := Result + #13#10 + '  - disc folder (' + Id + ')';
    if PreEboot <> '' then Result := Result + #13#10 + '  - the decrypted EBOOT.elf';
    if (PreDisc = '') and (PreEboot = '') then Result := 'RPCS3 was found, but nothing could be filled in.';
    if SaveFound > 0 then Result := Result + #13#10 + 'A save of the game was found; a later page offers to import it.';
  end;
  N := StrToIntDef(GetIniString('rpcs3', 'problems', '0', Ini), 0);
  for I := 1 to N do
    Result := Result + #13#10#13#10 + GetIniString('rpcs3', 'problem' + IntToStr(I), '', Ini);
  DeleteFile(Ini);
end;

procedure ApplyPre;
begin
  if PreDisc <> '' then DiscPage.Values[0] := PreDisc;
  if PreEboot <> '' then EbootFilePage.Values[0] := PreEboot;
end;

procedure RpcsRun;
var
  Dir, Msg: String;
begin
  RpcsFoundDir := ''; PreDisc := ''; PreEboot := ''; SaveFound := 0; ImportSave := False;
  Dir := RemoveBackslash(RpcsEdit.Text);
  if Dir <> '' then begin
    Msg := PathProblem(Dir, 'RPCS3 folder', True);
    if Msg <> '' then begin
      RpcsLabel.Caption := Msg;
      Exit;
    end;
  end;
  RpcsLabel.Caption := 'Looking...';
  RpcsLabel.Caption := RpcsSearch(Dir);
  if RpcsFoundDir <> '' then RpcsEdit.Text := RpcsFoundDir;
  ApplyPre;
  RpcsLastDir := RemoveBackslash(RpcsEdit.Text);
end;

procedure RpcsBrowseClick(Sender: TObject);
var
  Dir: String;
begin
  Dir := RemoveBackslash(RpcsEdit.Text);
  if BrowseForFolder('Select the folder of your RPCS3 install (it holds rpcs3.exe)', Dir, False) then RpcsEdit.Text := Dir;
end;

procedure RpcsButtonClick(Sender: TObject);
begin
  RpcsRun;
end;

{ The dry run of the save import: writes what would be copied, replaced and left to the list file and keeps the plan hash.
  Nothing is written to the game's save folder. }
function SavePlanMake(Lst: String): Boolean;
var
  Ini: String;
begin
  SavePlan := '';
  Ini := ExpandConstant('{tmp}\rpcs3_save.ini');
  DeleteFile(Ini);
  DeleteFile(Lst);
  RunHelper('save-plan --rpcs3 ' + ArgQ(RpcsFoundDir) + ' --hdd0 ' + ArgQ(HddDir) + ' --ini ' + ArgQ(Ini) + ' --list ' + ArgQ(Lst));
  if FileExists(Ini) and FileExists(Lst) then begin
    SavePlan := GetIniString('save', 'plan', '', Ini);
    SaveCopies := StrToIntDef(GetIniString('save', 'copies', '0', Ini), 0);
    SaveReplaces := StrToIntDef(GetIniString('save', 'replaces', '0', Ini), 0);
  end;
  DeleteFile(Ini);
  Result := SavePlan <> '';
end;

procedure SaveShowPlan;
begin
  SaveMemo.Lines.Clear;
  SaveBox.Checked := False;
  SaveBox.Enabled := False;
  if SavePlanMake(ExpandConstant('{tmp}\rpcs3_save.txt')) then begin
    SaveMemo.Lines.LoadFromFile(ExpandConstant('{tmp}\rpcs3_save.txt'));
    SaveMemo.Lines.Insert(0, '');
    SaveMemo.Lines.Insert(0, 'These files are copied into the game''s save folder only if you tick the box below. A line marked REPLACES overwrites the file already there; nothing is deleted. This is only a list: nothing has been copied yet.');
    SaveBox.Enabled := True;
  end else
    SaveMemo.Lines.Add('The list could not be made, so nothing can be imported.');
end;

{ Silent: the list goes to the log (file names only), then the plan is the one that is copied. }
procedure SavePlanSilent;
var
  Lst: String;
  Lines: TArrayOfString;
  I: Integer;
begin
  Lst := ExpandConstant('{tmp}\rpcs3_save.txt');
  if SavePlanMake(Lst) then begin
    if LoadStringsFromFile(Lst, Lines) then
      for I := 0 to GetArrayLength(Lines) - 1 do Log('  save list: ' + Lines[I]);
    ImportSave := True;
  end;
end;

procedure SaveImportRun;
begin
  SaveNote := '';
  if not ImportSave then Exit;
  if RunHelper('save-copy --rpcs3 ' + ArgQ(RpcsFoundDir) + ' --hdd0 ' + ArgQ(HddDir) + ' --plan ' + SavePlan) = 0 then
    SaveNote := 'Your RPCS3 save was copied into the game''s save folder.'
  else
    SaveNote := 'Your RPCS3 save was NOT copied: it changed after the list was shown, or the copy failed. Nothing was overwritten by a failed copy; run setup again to see a new list.';
end;

{ ---- opt-in music ---- }
const
  SND_ASYNC = $0001;
  SND_NODEFAULT = $0002;
  SND_LOOP = $0008;
  SND_FILENAME = $00020000;

var
  MusicBox: TNewCheckBox;

function PlaySound(Sound: String; Module: Integer; Flags: Cardinal): Boolean;
  external 'PlaySoundW@winmm stdcall';
function StopSound(Sound: Integer; Module: Integer; Flags: Cardinal): Boolean;
  external 'PlaySoundW@winmm stdcall';
function GetTickCount: Cardinal;
  external 'GetTickCount@kernel32 stdcall';

{ Each tick of the box starts a track picked at random, looped until the box is cleared or setup ends. }
procedure MusicBoxClick(Sender: TObject);
var
  Name: String;
begin
  StopSound(0, 0, 0);
  if not MusicBox.Checked then Exit;
  Name := 'music-' + IntToStr(GetTickCount mod {#MusicCount} + 1) + '.wav';
  ExtractTemporaryFile(Name);
  if not PlaySound(ExpandConstant('{tmp}\') + Name, 0, SND_FILENAME or SND_ASYNC or SND_LOOP or SND_NODEFAULT) then
    Log('music: PlaySound failed for ' + Name)
  else
    Log('music: playing ' + Name);
end;

procedure DeinitializeSetup;
begin
  StopSound(0, 0, 0);
end;

{ Silent installs (/VERYSILENT /DISC=<folder> /EBOOT=<file>, optional /DEVFLASH=<folder>) skip the pages: the same checks run here. A value that is
  not given is looked for in the player's RPCS3 install (/RPCS3=<folder>, or the usual places), as the RPCS3 page does; a value that is
  given always wins. /IMPORTSAVE=1 imports the RPCS3 save as the page's box does (the list is computed first and written to the log).
  /SKIPBUILD=1 (test switch, e.g. wizard layout tests) skips the build step only.
  The RPCS3 search needs the bundled Python, which Setup can unpack only once the wizard exists: so a silent install that needs the
  search is checked in InitializeWizard (Abort there ends setup with a non-zero exit code); one that is fully given is checked here. }
function SilentCheck(ArgDisc, ArgEboot, ArgFlash: String): Boolean;
var
  Msg: String;
begin
  Msg := DiscProblem(ArgDisc, DiscId, DiscPath);
  if Msg = '' then Msg := EbootProblem(ArgEboot);
  if (Msg = '') and (ArgFlash <> '') then Msg := FlashProblem(ArgFlash);
  Result := Msg = '';
  if not Result then
    Log('silent install refused: ' + Msg)
  else begin
    EbootPath := ArgEboot;
    FlashPath := RemoveBackslash(ArgFlash);
  end;
end;

function SilentNeedsRpcs3: Boolean;
begin
  Result := (ExpandConstant('{param:DISC|}') = '') or (ExpandConstant('{param:EBOOT|}') = '') or
    (ExpandConstant('{param:RPCS3|}') <> '') or (ExpandConstant('{param:IMPORTSAVE|0}') = '1');
end;

procedure SilentRpcs3;
var
  Msg, ArgDisc, ArgEboot, ArgFlash: String;
begin
  ArgDisc := ExpandConstant('{param:DISC|}');
  ArgEboot := ExpandConstant('{param:EBOOT|}');
  ArgFlash := ExpandConstant('{param:DEVFLASH|}');
  Msg := RemoveBackslash(ExpandConstant('{param:RPCS3|}'));
  if (Msg <> '') and (PathProblem(Msg, 'RPCS3 folder', True) <> '') then
    Log('rpcs3: /RPCS3 folder refused (characters the build cannot handle)')
  else begin
    RpcsSearch(Msg);
    if ArgDisc = '' then ArgDisc := PreDisc;
    if ArgEboot = '' then ArgEboot := PreEboot;
  end;
  if not SilentCheck(ArgDisc, ArgEboot, ArgFlash) then Abort;
  if (ExpandConstant('{param:IMPORTSAVE|0}') = '1') and (RpcsFoundDir <> '') and (SaveFound > 0) then SavePlanSilent;
end;

{ The settings window: launcher\JStarsLauncher.cs, compiled by the C# compiler that ships with Windows 10/11 (.NET Framework 4).
  It finds the install by the paths.txt / jsp-install.ini beside it, writes play_settings.txt where jsp-launcher.exe reads it and
  starts the game through jsp-launcher.exe. Used by the shortcuts when it was built (SettingsBuilt). }
function CscPath: String;
begin
  Result := ExpandConstant('{win}\Microsoft.NET\Framework64\v4.0.30319\csc.exe');
  if not FileExists(Result) then Result := ExpandConstant('{win}\Microsoft.NET\Framework\v4.0.30319\csc.exe');
  if not FileExists(Result) then Result := '';
end;

procedure BuildSettings;
var
  App, Csc: String;
  Rc: Integer;
begin
  App := ExpandConstant('{app}');
  Csc := CscPath;
  if Csc = '' then begin
    Log('settings window: no C# compiler of Windows (.NET Framework 4) found; the shortcuts start the game directly');
    Exit;
  end;
  if Exec(Csc, '/nologo /target:winexe /optimize+ /r:System.Windows.Forms.dll /r:System.Drawing.dll /win32icon:"' + App +
      '\jsp-launcher.ico" /out:"' + App + '\jsp-settings.exe" "' + App + '\tools\jsp-settings.cs"', App, SW_HIDE,
      ewWaitUntilTerminated, Rc) then
    Log('settings window: csc exit ' + IntToStr(Rc) + ', built ' + IntToStr(Ord(FileExists(App + '\jsp-settings.exe'))))
  else
    Log('settings window: csc did not start');
end;

function SettingsBuilt: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\jsp-settings.exe'));
end;

{ ---- wizard ---- }
procedure InitializeWizard;
begin
  { Bottom left, beside the buttons, on every page; off until the player ticks it. }
  MusicBox := TNewCheckBox.Create(WizardForm);
  MusicBox.Parent := WizardForm;
  MusicBox.Caption := 'Enable Music?';
  MusicBox.Checked := False;
  MusicBox.Left := WizardForm.ClientWidth - WizardForm.CancelButton.Left - WizardForm.CancelButton.Width;
  MusicBox.Top := WizardForm.CancelButton.Top + (WizardForm.CancelButton.Height - ScaleY(17)) div 2;
  MusicBox.Width := WizardForm.BackButton.Left - MusicBox.Left - ScaleX(8);
  MusicBox.Height := ScaleY(17);
  MusicBox.OnClick := @MusicBoxClick;
  { /MUSIC=1 ticks the box (for tests); silent installs never play. }
  if (ExpandConstant('{param:MUSIC|0}') = '1') and not WizardSilent then MusicBox.Checked := True;
  { Welcome: the ASCII star and J are in the wizard image on the left; no second copy on the white page (unreadable
    yellow on white). }
  { A custom page, not an input-folder page: Setup insists on a full path in those, and this one may stay empty (= RPCS3 is not used). }
  RpcsPage := CreateCustomPage(wpWelcome, 'Use your RPCS3 install?',
    'Optional: let setup look for your game and EBOOT.elf in RPCS3, and use its FFmpeg files for the opening movie');
  RpcsText := TNewStaticText.Create(RpcsPage);
  RpcsText.Parent := RpcsPage.Surface;
  RpcsText.AutoSize := False;
  RpcsText.WordWrap := True;
  RpcsText.Left := 0;
  RpcsText.Top := 0;
  RpcsText.Width := RpcsPage.SurfaceWidth;
  RpcsText.Height := ScaleY(48);
  RpcsText.Caption := 'If you have RPCS3, enter its folder (the one that holds rpcs3.exe) and click Find. Setup only reads it, and fills in the next ' +
    'pages for you; you can still change every field. Leave the field empty to skip this and fill in the pages yourself.';
  RpcsEdit := TEdit.Create(RpcsPage);
  RpcsEdit.Parent := RpcsPage.Surface;
  RpcsEdit.Left := 0;
  RpcsEdit.Top := RpcsText.Top + RpcsText.Height + ScaleY(8);
  RpcsEdit.Width := RpcsPage.SurfaceWidth - ScaleX(96);
  RpcsBrowse := TNewButton.Create(RpcsPage);
  RpcsBrowse.Parent := RpcsPage.Surface;
  RpcsBrowse.Caption := 'Browse...';
  RpcsBrowse.Left := RpcsEdit.Width + ScaleX(6);
  RpcsBrowse.Top := RpcsEdit.Top - ScaleY(1);
  RpcsBrowse.Width := ScaleX(90);
  RpcsBrowse.Height := ScaleY(23);
  RpcsBrowse.OnClick := @RpcsBrowseClick;
  RpcsButton := TNewButton.Create(RpcsPage);
  RpcsButton.Parent := RpcsPage.Surface;
  RpcsButton.Caption := 'Find';
  RpcsButton.Left := 0;
  RpcsButton.Top := RpcsEdit.Top + RpcsEdit.Height + ScaleY(8);
  RpcsButton.Width := ScaleX(90);
  RpcsButton.Height := ScaleY(26);
  RpcsButton.OnClick := @RpcsButtonClick;
  RpcsLabel := TNewStaticText.Create(RpcsPage);
  RpcsLabel.Parent := RpcsPage.Surface;
  RpcsLabel.AutoSize := False;
  RpcsLabel.WordWrap := True;
  RpcsLabel.Left := 0;
  RpcsLabel.Top := RpcsButton.Top + RpcsButton.Height + ScaleY(10);
  RpcsLabel.Width := RpcsPage.SurfaceWidth;
  RpcsLabel.Height := RpcsPage.SurfaceHeight - RpcsLabel.Top;

  DiscPage := CreateInputDirPage(RpcsPage.ID, 'Your game disc',
    'Where is your copy of J-Stars Victory VS+?',
    'Select the folder of your own game disc that holds PS3_GAME (US BLUS31519 or EU BLES02136). Setup reads it in place and copies nothing of it.',
    False, '');
  DiscPage.Add('Disc folder:');

  EbootFilePage := CreateInputFilePage(DiscPage.ID, 'Your decrypted EBOOT.elf',
    'The program file of your game, decrypted',
    'Select the decrypted EBOOT.elf of your own copy: see RPCS3 and its documentation. It stays where it is; setup only reads it.');
  EbootFilePage.Add('EBOOT.elf:', 'ELF files (*.elf)|*.elf|All files|*.*', '.elf');

  { The save import page (shown only when RPCS3 has a save of the game): the list is a dry run; nothing is copied unless the box is ticked. }
  SavePage := CreateCustomPage(EbootFilePage.ID, 'Import your save from RPCS3?',
    'A save of the game was found in your RPCS3 install');
  SaveMemo := TNewMemo.Create(SavePage);
  SaveMemo.Parent := SavePage.Surface;
  SaveMemo.Left := 0;
  SaveMemo.Top := 0;
  SaveMemo.Width := SavePage.SurfaceWidth;
  SaveMemo.Height := SavePage.SurfaceHeight - ScaleY(30);
  SaveMemo.ReadOnly := True;
  SaveMemo.ScrollBars := ssVertical;
  SaveBox := TNewCheckBox.Create(SavePage);
  SaveBox.Parent := SavePage.Surface;
  SaveBox.Caption := 'Import this save';
  SaveBox.Left := 0;
  SaveBox.Top := SavePage.SurfaceHeight - ScaleY(20);
  SaveBox.Width := SavePage.SurfaceWidth;
  SaveBox.Height := ScaleY(17);
  SaveBox.Checked := False;

  { Silent installs: Inno still runs the pages' Next checks in silent mode, so the pages take /DISC= and /EBOOT=. }
  if WizardSilent then begin
    if SilentNeedsRpcs3 then SilentRpcs3;
    DiscPage.Values[0] := DiscPath;
    EbootFilePage.Values[0] := EbootPath;
  end;
end;

{ A page refuses its value. SuppressibleMsgBox, not MsgBox: /SUPPRESSMSGBOXES (with /SILENT or /VERYSILENT) must not leave a
  silent install waiting for an OK nobody can see. Returning False from NextButtonClick then ends a silent install. }
procedure Refuse(Msg: String);
begin
  Log('refused: ' + Msg);
  SuppressibleMsgBox(Msg, mbError, MB_OK, IDOK);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Msg: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    Msg := PathProblem(WizardDirValue, 'install folder', True);
    if Msg <> '' then begin
      Refuse(Msg);
      Result := False;
    end;
  end else if (CurPageID = RpcsPage.ID) and not WizardSilent then begin   { a silent install does its own search (SilentRpcs3) }
    Msg := RemoveBackslash(RpcsEdit.Text);
    if Msg = '' then begin
      RpcsFoundDir := '';   { empty field: RPCS3 is not used, and no save is offered }
      SaveFound := 0;
      ImportSave := False;
    end else if Msg <> RpcsLastDir then
      RpcsRun;
  end else if CurPageID = DiscPage.ID then begin
    Msg := DiscProblem(DiscPage.Values[0], DiscId, DiscPath);
    if Msg <> '' then begin
      Refuse(Msg);
      Result := False;
    end;
  end else if CurPageID = EbootFilePage.ID then begin
    Msg := EbootProblem(EbootFilePage.Values[0]);
    if Msg <> '' then begin
      Refuse(Msg);
      Result := False;
    end else
      EbootPath := EbootFilePage.Values[0];
  end else if (CurPageID = SavePage.ID) and not WizardSilent then
    ImportSave := SaveBox.Checked;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = SavePage.ID then Result := (RpcsFoundDir = '') or (SaveFound = 0);
end;

function SaveMemoText(Space, NewLine: String): String;
begin
  Result := '';
  if ImportSave then
    Result := 'RPCS3 save:' + NewLine + Space + 'import it (' + IntToStr(SaveCopies) + ' files, ' + IntToStr(SaveReplaces) + ' replace files already there)' +
      NewLine + NewLine;
end;

{ The opening movie is decoded with the FFmpeg DLLs of the player's RPCS3 (firstrun.py copies them beside the game). }
function MovieMemoText(Space, NewLine: String): String;
begin
  if RpcsFoundDir <> '' then
    Result := 'Opening movie:' + NewLine + Space + 'decoded with the FFmpeg files of your RPCS3' + NewLine + NewLine
  else
    Result := 'Opening movie:' + NewLine + Space + 'stays black (no RPCS3 folder on the RPCS3 page; its FFmpeg' + NewLine +
      Space + 'files decode the movie)' + NewLine + NewLine;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := 'Disc folder:' + NewLine + Space + DiscPath + ' (' + DiscId + ')' + NewLine + NewLine +
    'EBOOT.elf:' + NewLine + Space + EbootPath + NewLine + NewLine +
    MemoDirInfo + NewLine + NewLine + MemoGroupInfo + NewLine + MemoTasksInfo + NewLine + NewLine +
    SaveMemoText(Space, NewLine) + MovieMemoText(Space, NewLine) +
    'After the files are copied, setup builds the game on this PC' + NewLine + 'from your disc and your EBOOT.elf. This takes a long' + NewLine +
    'time (the compile is most of it) and several GB of disk space' + NewLine + 'in the install folder. The game data install (about' + NewLine +
    '2.5 GB more, in your user data folder) runs the first time' + NewLine + 'you start the game.';
end;


{ Where saves (hdd0) and play_settings.txt live: the install folder for the Portable task, else the per-user data folder.
  firstrun.py and the launcher read the same choice from jsp-install.ini [install] portable. }
function SaveDataDir: String;
begin
  if WizardIsTaskSelected('portable') then Result := ExpandConstant('{app}')
  else Result := ExpandConstant('{userappdata}\{#AppDirName}');
end;

{ True when firstrun's place step finished: paths.txt is there and the exe its EXE= line names is in game\. }
function GamePlaced(App: String): Boolean;
var
  Lines: TArrayOfString;
  I: Integer;
begin
  Result := False;
  if not LoadStringsFromFile(App + '\paths.txt', Lines) then Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
    if Pos('EXE=', Lines[I]) = 1 then
      Result := FileExists(App + '\game\' + Trim(Copy(Lines[I], 5, Length(Lines[I]))));
end;

{ The build step: firstrun.py with the bundled Python builds this repository's port from the player's disc folder and EBOOT.elf
  (lift, SPU jobs, compile with the bundled llvm-mingw, CMake and Ninja). Resumable: finished steps are skipped when it runs
  again, which the "Build again" shortcut does (build-again.cmd holds the exact command line). Log: a new logs\firstrun-<time>.log in the install folder (the newest five are kept).
  The wizard stays responsive while Exec waits but shows only a fixed message (no per-step progress yet). }
procedure RunBuildStep;
var
  App, Py, Params, Hdd0: String;
  Again: TArrayOfString;
begin
  App := ExpandConstant('{app}');
  Hdd0 := SaveDataDir + '\hdd0';
  SetIniString('install', 'disc', DiscPath, App + '\jsp-install.ini');
  SetIniString('install', 'title_id', DiscId, App + '\jsp-install.ini');
  SetIniString('install', 'eboot', EbootPath, App + '\jsp-install.ini');
  SetIniString('install', 'devflash', FlashPath, App + '\jsp-install.ini');
  SetIniString('install', 'rpcs3', RpcsFoundDir, App + '\jsp-install.ini');   { firstrun copies RPCS3's FFmpeg DLLs for the opening movie }
  ForceDirectories(App + '\build');
  ForceDirectories(App + '\logs');
  Py := App + '\toolchain\python\python.exe';
  Params := '-X utf8 ' + ArgQ(App + '\firstrun.py') + ' --root ' + ArgQ(App) + ' --disc ' + ArgQ(DiscPath) + ' --eboot ' + ArgQ(EbootPath) +
    ' --devflash ' + ArgQ(FlashPath) + ' --rpcs3 ' + ArgQ(RpcsFoundDir) + ' --hdd0 ' + ArgQ(Hdd0) + ' --logdir ' + ArgQ(App + '\logs');
  { UTF-8 without a BOM and chcp 65001 first, like build-port.cmd: the ANSI code page broke folder names with accents
    (cmd reads a batch file in the console code page). }
  SetArrayLength(Again, 4);
  Again[0] := '@echo off';
  Again[1] := 'chcp 65001 >nul';
  Again[2] := '"' + Py + '" ' + Params;
  Again[3] := 'pause';
  SaveStringsToUTF8FileWithoutBOM(App + '\build-again.cmd', Again, False);
  if SkipBuild then begin
    Log('build step skipped (/SKIPBUILD=1)');
    BuildRc := 0;
    Exit;
  end;
  WizardForm.StatusLabel.Caption := 'Building the game on this PC. This takes a long time; please leave this window open.';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  if not Exec(Py, Params, App, SW_HIDE, ewWaitUntilTerminated, BuildRc) then begin
    Log('firstrun: could not start Python: ' + Py);
    BuildRc := -1;
  end;
  Log('firstrun exit code ' + IntToStr(BuildRc));
  if (BuildRc = 0) and not GamePlaced(App) then begin
    Log('firstrun returned 0, but the game is not in place (paths.txt or the game\ exe it names is missing)');
    BuildRc := -2;
  end;
end;

{ Setup's own exit code, for silent installs and scripts: 0 only when the game was built and placed (or the build was skipped
  on purpose). 10 = the files are installed but the build did not finish; "Build again" can finish it. 1-8 are Inno Setup's own. }
function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if not SkipBuild and (BuildRc <> 0) then Result := 10;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    SaveImportRun;
    RunBuildStep;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = RpcsPage.ID) and not RpcsAuto and not WizardSilent then begin
    RpcsAuto := True;   { first time here: look in the usual places once }
    if RemoveBackslash(RpcsEdit.Text) = '' then RpcsRun;
  end;
  if (CurPageID = SavePage.ID) and not WizardSilent then SaveShowPlan;
  if CurPageID = wpFinished then begin
    if SkipBuild then
      WizardForm.FinishedLabel.Caption := 'The files are in place; the build step was skipped (test switch), so the game does not start yet.'
    else if BuildRc = 0 then
      WizardForm.FinishedLabel.Caption := 'The game was built on this PC. Start it from the shortcut. The first start installs the game data from your disc, which takes a few minutes.'
    else
      WizardForm.FinishedLabel.Caption := 'The build did not finish (code ' + IntToStr(BuildRc) + '). Read the newest file in ' + ExpandConstant('{app}') +
        '\logs for the step that stopped. Fix it, then run "Build again" from the Start menu: finished steps are kept.';
    if SaveNote <> '' then WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + SaveNote;
  end;
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  SkipBuild := ExpandConstant('{param:SKIPBUILD|0}') = '1';
  if WizardSilent and not SilentNeedsRpcs3 then
    Result := SilentCheck(ExpandConstant('{param:DISC|}'), ExpandConstant('{param:EBOOT|}'), ExpandConstant('{param:DEVFLASH|}'));
end;

{ Uninstall keeps saves and installed game data unless asked (/REMOVEDATA=1, or the box below). A portable install keeps
  them in the install folder (hdd0, play_settings.txt): the ini that says so is read before it is deleted.
  Layout adapted from the maintainer's installer for another PS3 port. }
var
  WasPortable: Boolean;

function WantRemove(Where: String): Boolean;
begin
  if UninstallSilent then
    Result := ExpandConstant('{param:REMOVEDATA|0}') = '1'
  else
    Result := MsgBox('Also delete your saves and the game data setup made in' + #13#10 + Where + '?' + #13#10#13#10 +
      'No keeps them for a later install.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure AskDeletePortableSaves(App: String);
begin
  if not DirExists(App + '\hdd0') and not FileExists(App + '\play_settings.txt') then Exit;
  if WantRemove(App + '\hdd0') then begin
    DelTree(App + '\hdd0', True, True, True);
    DeleteFile(App + '\play_settings.txt');
    RemoveDir(App);
    Log('uninstall: portable saves removed: ' + App + '\hdd0');
  end else
    Log('uninstall: portable saves kept: ' + App + '\hdd0');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data: String;
begin
  if CurUninstallStep = usUninstall then begin
    WasPortable := GetIniInt('install', 'portable', 0, 0, 1, ExpandConstant('{app}\jsp-install.ini')) = 1;
    Exit;
  end;
  if CurUninstallStep <> usPostUninstall then Exit;
  Log('uninstall: cmdtail=[' + GetCmdTail + '] REMOVEDATA=[' + ExpandConstant('{param:REMOVEDATA|0}') + '] silent=' + IntToStr(Ord(UninstallSilent)) +
      ' portable=' + IntToStr(Ord(WasPortable)));
  if WasPortable then begin
    AskDeletePortableSaves(ExpandConstant('{app}'));
    Exit;
  end;
  Data := ExpandConstant('{userappdata}\{#AppDirName}');
  if not DirExists(Data) then Exit;
  if WantRemove(Data) then
  begin
    { a folder link (junction or symlink) goes first as a link: DelTree must never reach into its target }
    if not RemoveDir(Data) then
      DelTree(Data, True, True, True);
    if DirExists(Data) then Log('uninstall: data folder STILL THERE: ' + Data)
    else Log('uninstall: data folder removed: ' + Data);
  end else
    Log('uninstall: data folder kept: ' + Data);
end;
