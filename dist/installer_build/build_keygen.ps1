$ErrorActionPreference = 'Stop'
if (-not (Test-Path 'dist\packages')) { New-Item -ItemType Directory -Force -Path 'dist\packages' | Out-Null }
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /out:dist\packages\GISQC-Keygen.exe /win32icon:assets\user-logo.ico /resource:assets\user-logo.ico,app.ico /reference:System.Windows.Forms.dll /reference:System.Drawing.dll dist\installer_build\GISQC-Keygen.cs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
