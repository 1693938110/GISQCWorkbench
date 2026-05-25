$ErrorActionPreference = 'Stop'
$payload = 'dist\installer_build\payload.zip'
if (Test-Path $payload) { Remove-Item $payload -Force }
if (-not (Test-Path 'dist\packages')) { New-Item -ItemType Directory -Force -Path 'dist\packages' | Out-Null }
Compress-Archive -Path 'dist\GISQCWorkbench-latest\*' -DestinationPath $payload -Force
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /codepage:65001 /target:winexe /out:dist\packages\GISQCWorkbench-Setup.exe /win32icon:assets\user-logo.ico /resource:dist\installer_build\payload.zip,payload.zip /resource:assets\user-logo.ico,app.ico /reference:System.IO.Compression.FileSystem.dll /reference:System.Windows.Forms.dll /reference:System.Drawing.dll dist\installer_build\Installer.cs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
