#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\release\obs-multistream"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\release"
#endif

#define MinimumObsMajor 33

[Setup]
AppId={{8F5C2B1E-6C1D-4E7A-9C55-2B7D0C9A4E31}
AppName=Multistream para OBS
AppVersion={#AppVersion}
AppVerName=Multistream para OBS {#AppVersion}
AppPublisher=IO GT
AppPublisherURL=https://multistream.lexo.dev/
AppSupportURL=https://github.com/aalonzolu/obs-multistream/issues
DefaultDirName={commonappdata}\obs-studio\plugins\obs-multistream
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=obs-multistream-{#AppVersion}-windows-x64-instalador
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Multistream para OBS
CloseApplications=no

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.NeedsNewerObs=Esta versión de Multistream necesita OBS Studio {#MinimumObsMajor} o más reciente, y en este equipo hay OBS %1.%n%nActualizá OBS desde obsproject.com y volvé a abrir este instalador.
en.NeedsNewerObs=This version of Multistream needs OBS Studio {#MinimumObsMajor} or newer, and this computer has OBS %1.%n%nUpdate OBS from obsproject.com and run this installer again.
es.ObsNotFound=No encontramos OBS Studio en este equipo. ¿Instalar Multistream de todas formas?
en.ObsNotFound=OBS Studio was not found on this computer. Install Multistream anyway?
es.CloseObs=OBS Studio está abierto. Cerralo y tocá Aceptar para seguir.
en.CloseObs=OBS Studio is open. Close it and press OK to continue.
es.ObsStillOpen=OBS Studio sigue abierto.
en.ObsStillOpen=OBS Studio is still open.
es.FinishedNote=Abrí OBS: Multistream aparece en la barra de estado, abajo a la izquierda.
en.FinishedNote=Open OBS: Multistream shows up in the status bar, bottom left.

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: filesandordirs; Name: "{app}"
Type: filesandordirs; Name: "{userappdata}\obs-studio\plugins\obs-multistream"
Type: files; Name: "{code:ObsDir}\obs-plugins\64bit\obs-multistream.dll"
Type: filesandordirs; Name: "{code:ObsDir}\data\obs-plugins\obs-multistream"

[Code]
function ObsDir(Param: String): String;
begin
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) or (Result = '') then
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

function ObsVersion(): String;
begin
  if not GetVersionNumbersString(ObsDir('') + '\bin\64bit\obs64.exe', Result) then
    Result := '';
end;

function ObsRunning(): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Processes.Count > 0;
  except
    Result := False;
  end;
end;

function InitializeSetup(): Boolean;
var
  Version: String;
  Major: Integer;
begin
  Result := True;
  Version := ObsVersion();
  if Version = '' then
  begin
    Result := MsgBox(CustomMessage('ObsNotFound'), mbConfirmation, MB_YESNO) = IDYES;
    exit;
  end;
  Major := StrToIntDef(Copy(Version, 1, Pos('.', Version) - 1), 0);
  if Major < {#MinimumObsMajor} then
  begin
    MsgBox(FmtMessage(CustomMessage('NeedsNewerObs'), [Copy(Version, 1, Pos('.', Version) + 1)]), mbError, MB_OK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  while ObsRunning() do
  begin
    if MsgBox(CustomMessage('CloseObs'), mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := CustomMessage('ObsStillOpen');
      exit;
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 + CustomMessage('FinishedNote');
end;
