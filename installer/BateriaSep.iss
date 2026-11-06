; Instalador do BateriaSep (Inno Setup 6)
; Os caminhos chegam por /D na linha de comando (ver .github/workflows/build.yml)
#ifndef Versao
  #define Versao "1.0.0"
#endif
#ifndef VstDir
  #define VstDir "..\build\BateriaSep_artefacts\Release\VST3\BateriaSep.vst3"
#endif
#ifndef StandaloneDir
  #define StandaloneDir "..\build\BateriaSep_artefacts\Release\Standalone"
#endif
#ifndef ModeloOnnx
  #define ModeloOnnx "..\drumsep_core.onnx"
#endif
#ifndef VcRedist
  #define VcRedist "..\vc_redist.x64.exe"
#endif

[Setup]
AppId={{B7A3C1D2-5E4F-4A6B-9C8D-1F2E3A4B5C6D}
AppName=BateriaSep
AppVersion={#Versao}
AppPublisher=Gabriel Figueiredo
DefaultDirName={autopf}\BateriaSep
DisableDirPage=yes
DefaultGroupName=BateriaSep
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=BateriaSep-Setup
Compression=lzma2/fast
SolidCompression=no
WizardStyle=modern
UninstallDisplayName=BateriaSep

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Files]
Source: "{#VstDir}\*"; DestDir: "{commoncf64}\VST3\BateriaSep.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#StandaloneDir}\BateriaSep.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StandaloneDir}\bateriasep_onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ModeloOnnx}"; DestDir: "{app}"; DestName: "drumsep_core.onnx"; Flags: ignoreversion nocompression
Source: "{#VcRedist}"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
Name: "{autoprograms}\BateriaSep"; Filename: "{app}\BateriaSep.exe"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Instalando componentes do Windows..."; Check: VCRedistNeeded; Flags: waituntilterminated
Filename: "{app}\BateriaSep.exe"; Description: "Abrir o BateriaSep agora"; Flags: nowait postinstall skipifsilent

[Code]
function VCRedistNeeded: Boolean;
var
  Installed: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then
    Result := (Installed <> 1);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption :=
      'O BateriaSep foi instalado.' + #13#10#13#10 +
      'Na sua DAW, atualize/reescaneie a lista de plugins VST3 e procure por "BateriaSep".' + #13#10#13#10 +
      'Tambem ha um programa independente no Menu Iniciar.';
end;
