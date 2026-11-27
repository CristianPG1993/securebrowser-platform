[Setup]
AppId={{1AF41AD5-0FA5-403D-AC09-8377988A22C7}
AppName=SecureBrowser Demo
AppVersion=1.0
DefaultDirName={localappdata}\Programs\SecureBrowser Demo
DisableDirPage=yes
UsePreviousAppDir=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
WizardStyle=modern
OutputDir={#InstallerDir}
OutputBaseFilename=SecureBrowser-Setup
SetupIconFile=..\..\assets\securebrowser-desktop.ico
UninstallDisplayIcon={app}\SecureBrowser.exe
Compression=lzma2/fast
SolidCompression=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\SecureBrowser Demo"; Filename: "{app}\Iniciar-demo.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\SecureBrowser.exe"
Name: "{userprograms}\SecureBrowser Demo - Guía"; Filename: "{app}\LEEME-demo.txt"; WorkingDir: "{app}"
