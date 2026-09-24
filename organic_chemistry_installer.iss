#define MyAppName "Organic Chemistry"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "iisaacbeats.cn"
#define MyPluginBundle "Organic Chemistry.vst3"

; VST3 顶层目录（即包含 "Organic Chemistry.vst3" bundle 的父目录）。
; 默认指向 Ninja 构建目录；build_installer.bat 会用 -DVST3_DIR 覆盖为实际路径。
#ifndef VST3_DIR
  #define VST3_DIR "cmake-build-release-visual-studio\OrganicChemistry_artefacts\Release\VST3"
#endif

[Setup]
AppId={{C4B9E7D2-5A8F-4C3E-9B1A-6D7F2E8A4B3C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={commoncf}\VST3\iisaacbeats.cn
OutputDir=dist
OutputBaseFilename={#MyAppName}_Setup_{#MyAppVersion}_x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
SetupLogging=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DisableDirPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#VST3_DIR}\{#MyPluginBundle}\*"; DestDir: "{app}\{#MyPluginBundle}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
var
  InstallDirWarningShown: Boolean;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  DefaultVst3Path: string;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    DefaultVst3Path := ExpandConstant('{commoncf}\VST3\iisaacbeats.cn');

    if (CompareText(RemoveBackslashUnlessRoot(WizardDirValue), RemoveBackslashUnlessRoot(DefaultVst3Path)) <> 0) and (not InstallDirWarningShown) then
    begin
      MsgBox('你选择了非默认VST3目录。安装完成后，你可能需要在宿主软件(DAW)中手动添加该目录并重新扫描插件，才能正常识别并使用本插件。', mbInformation, MB_OK);
      InstallDirWarningShown := True;
    end;
  end;
end;
