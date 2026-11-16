#include "product-pins.iss"

[Setup]
AppId=GEHD-{#GameId}-Local
AppName={#GameId} 高清貼圖模組
AppVersion=0.1.0
AppVerName={#GameId} 高清貼圖模組 {#ReleaseShort}
AppPublisher=GEHD Project
DefaultDirName={autopf}\Steam\steamapps\common\{#SteamFolder}
AppendDefaultDirName=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
UninstallFilesDir={app}\GEHD\uninstall
UninstallDisplayName={#GameId} 高清貼圖模組
UninstallLogging=yes
CloseApplications=no
RestartApplications=no
AllowNetworkDrive=no
AllowUNCPath=no
SetupMutex=GEHD_{#GameId}_Local_Setup
ArchiveExtraction=enhanced/nopassword
Compression=lzma2
SolidCompression=no
WizardStyle=modern
SetupLogging=yes
OutputDir=media
OutputBaseFilename=GEHD_Setup

[Languages]
Name: "traditionalchinese"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
SelectDirLabel3=請選擇含有 {#GameExeName} 的遊戲資料夾。資料預設放在 GEHD\data，安裝後可由管理器搬移。
FinishedHeadingLabel=高清模組已完成安裝
FinishedLabel=平常請從 Steam 啟動遊戲，不需讓管理器常駐。管理器可啟停、搬移、核對及維護本地資料。

[Files]
Source: "payload\GEHD\bin\pack-control.exe"; DestName: "setup-control.exe"; Flags: dontcopy
Source: "payload\GEHD\bundle.identity"; Flags: dontcopy
Source: "payload\GEHD\shell.files"; Flags: dontcopy
Source: "payload\GEHD\bin\pack-control.exe"; DestDir: "{app}\GEHD\uninstall"; DestName: "remove-control.exe"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\{#GameId} 高清模組管理器"; Filename: "{app}\GEHD\GEHD_Manager.exe"

[Run]
Filename: "{app}\GEHD\GEHD_Manager.exe"; Description: "開啟高清模組管理器"; Flags: postinstall nowait skipifsilent unchecked; Check: InstallationReady

[Code]
var StageDirectory, SetupController, RemovalController: String;
    ModePage: TInputOptionWizardPage;
    ReuseStage, InstallCompleted: Boolean;

function GetCustomSetupExitCode: Integer;
begin
  if InstallCompleted then Result := 0 else Result := 42;
end;

function InstallationReady: Boolean;
begin
  Result := InstallCompleted;
end;

function RollbackRequested: Boolean;
begin
  Result := ModePage.SelectedValueIndex = 1;
end;

procedure Require(OK: Boolean; Reason: String);
begin
  if not OK then RaiseException(Reason);
end;

function Quoted(Value: String): String;
begin
  Require(Pos('"', Value) = 0, 'INVALID_PATH');
  Result := '"' + Value + '"';
end;

procedure CheckFile(Path, Digest: String);
begin
  Require(FileExists(Path), 'MISSING_FILE: ' + Path);
  Require(CompareText(GetSHA256OfFile(Path), Digest) = 0, 'HASH_MISMATCH: ' + Path);
end;

function RunControl(Controller, Arguments: String): TArrayOfString;
var Code, I: Integer; Output: TExecOutput; MessageText: String;
begin
  if not ExecAndCaptureOutput(Controller, Arguments, '', SW_SHOWNORMAL,
    ewWaitUntilTerminated, Code, Output) then
    RaiseException('CANNOT_START_CONTROLLER: ' + IntToStr(Code) + ' ' + SysErrorMessage(Code));
  MessageText := '';
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do Log(Output.StdOut[I]);
  for I := 0 to GetArrayLength(Output.StdErr) - 1 do begin
    Log(Output.StdErr[I]); MessageText := MessageText + Output.StdErr[I] + #13#10;
  end;
  Require(not Output.Error, 'CONTROLLER_OUTPUT_ERROR');
  Require(Code = 0, '操作未完成。檔案與恢復紀錄會保留。' + #13#10 + MessageText);
  Result := Output.StdOut;
end;

function StageRoot(Param: String): String;
begin
  { Inno expands external paths during wizard setup before extraction.
    ExternalSize supplies sizing; the real stage is assigned before file copy. }
  if StageDirectory = '' then
    Result := AddBackslash(ExpandConstant('{app}')) + '.GEHD-stage-not-ready'
  else Result := StageDirectory;
end;

procedure InitializeWizard;
begin
  ExtractTemporaryFile('setup-control.exe');
  ExtractTemporaryFile('bundle.identity');
  ExtractTemporaryFile('shell.files');
  SetupController := ExpandConstant('{tmp}\setup-control.exe');
  CheckFile(SetupController, '{#ControllerHash}');
  ModePage := CreateInputOptionPage(wpSelectDir, '選擇操作', '安裝、更新或回復上次版本',
    '回復會保留新版本候選與備份。只適用於此管理器記錄的最近一次完整版本更新。', True, False);
  ModePage.Add('安裝／更新本地高清模組');
  ModePage.Add('回復上次完整版本更新');
  if ExpandConstant('{param:ROLLBACK|0}') = '1' then ModePage.SelectedValueIndex := 1
  else ModePage.SelectedValueIndex := 0;
end;

#include "product-media-checks.iss"

function PrepareToInstall(var NeedsRestart: Boolean): String;
var FreeBytes, TotalBytes: Int64; Output: TArrayOfString;
begin
  Result := '';
  try
    if RollbackRequested then begin
      Output := RunControl(SetupController, '--product status --game ' + Quoted(ExpandConstant('{app}')));
      Require((GetArrayLength(Output) > 0) and (Pos('gameRunning=0 managed=1', Output[0]) = 1), '請確認已選取原安裝位置，並先退出遊戲。');
      Exit;
    end;
    Output := RunControl(SetupController, '--product preflight-install --game ' +
      Quoted(ExpandConstant('{app}')) + ' --bundle ' + Quoted(ExpandConstant('{tmp}')));
    ReuseStage := (GetArrayLength(Output) > 0) and (Output[0] = 'SHELL_RESUME');
    if GetArrayLength(Output) > 0 then Log('GEHD_INSTALL_MODE=' + Output[0]);
    if not ReuseStage then begin
      CheckMedia(ExpandConstant('{src}'));
      Require(GetSpaceOnDisk64(ExtractFileDrive(ExpandConstant('{app}')), FreeBytes, TotalBytes), 'DISK_SPACE_UNAVAILABLE');
#ifdef GEHD_TEST_NO_SPACE
      FreeBytes := 0;
#endif
      Require(FreeBytes >= {#PayloadBytes} + 33554432, 'INSUFFICIENT_DISK_SPACE');
    end;
  except
    Result := GetExceptionMessage;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Output: TArrayOfString;
begin
  if CurStep = ssInstall then begin
    if RollbackRequested then begin
      RunControl(SetupController, '--product rollback-bundle --game ' + Quoted(ExpandConstant('{app}')));
      Exit;
    end;
    if not ReuseStage then CheckMedia(ExpandConstant('{src}'));
    Output := RunControl(SetupController, '--product prepare-stage --game ' +
      Quoted(ExpandConstant('{app}')) + ' --bundle ' + Quoted(ExpandConstant('{tmp}')));
    Require((GetArrayLength(Output) = 1) and (Pos('.GEHD-stage-', Output[0]) = 1), 'INVALID_STAGE_RESULT');
    StageDirectory := AddBackslash(ExpandConstant('{app}')) + Output[0];
    Log('GEHD_STAGE=' + StageDirectory);
    if not ReuseStage then begin
      WizardForm.StatusLabel.Caption := '正在解壓縮本地資料包…';
      ExtractMedia(ExpandConstant('{src}'), StageDirectory);
    end;
    WizardForm.StatusLabel.Caption := '正在核對貼圖與管理工具…';
    RunControl(SetupController, '--product verify-stage --game ' + Quoted(ExpandConstant('{app}')) +
      ' --data ' + Quoted(StageDirectory));
  end;
  if CurStep = ssPostInstall then begin
    try
    if RollbackRequested then begin
      InstallCompleted := True;
      WizardForm.FinishedHeadingLabel.Caption := '已回復上次版本';
      WizardForm.FinishedLabel.Caption := '已依更新紀錄回復原工具與資料。平常請從 Steam 啟動遊戲。';
      Exit;
    end;
    WizardForm.StatusLabel.Caption := '正在安裝及啟用高清資料…';
    RunControl(SetupController, '--product install --game ' + Quoted(ExpandConstant('{app}')) +
      ' --data ' + Quoted(StageDirectory));
    InstallCompleted := True;
    try
      RunControl(SetupController, '--product cleanup-stage --game ' + Quoted(ExpandConstant('{app}')) +
        ' --data ' + Quoted(StageDirectory));
    except
      Log('GEHD_INSTALLED_STAGE_CLEANUP_PENDING: ' + GetExceptionMessage);
      WizardForm.FinishedLabel.Caption := '模組已安裝。部分暫存檔尚未清理，位置：' + #13#10 +
        StageDirectory + #13#10 + '平常請從 Steam 啟動遊戲。';
    end;
    except
      Log('GEHD_INSTALL_INCOMPLETE: ' + GetExceptionMessage);
      WizardForm.FinishedHeadingLabel.Caption := '模組安裝尚未完成';
      WizardForm.FinishedLabel.Caption := '檔案與恢復紀錄已保留。請排除下列問題後，重新執行此安裝程式：' + #13#10 +
        GetExceptionMessage + #13#10 + StageDirectory;
    end;
  end;
end;

function InitializeUninstall: Boolean;
var Output: TArrayOfString;
begin
  Result := False;
  try
    CheckFile(ExpandConstant('{app}\GEHD\uninstall\remove-control.exe'), '{#ControllerHash}');
    RemovalController := ExpandConstant('{tmp}\gehd-remove-control.exe');
    Require(FileCopy(ExpandConstant('{app}\GEHD\uninstall\remove-control.exe'), RemovalController, True), 'CANNOT_PREPARE_REMOVAL');
    Output := RunControl(RemovalController, '--product status --game ' + Quoted(ExpandConstant('{app}')));
    Require((GetArrayLength(Output) > 0) and (Pos('gameRunning=0', Output[0]) = 1), '請先正常退出遊戲，再移除模組。');
    Result := True;
  except
    Log(GetExceptionMessage);
    if not UninstallSilent then MsgBox(GetExceptionMessage, mbError, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RunControl(RemovalController, '--product uninstall-bundle --game ' + Quoted(ExpandConstant('{app}')));
end;
