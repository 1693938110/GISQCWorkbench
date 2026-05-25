$ErrorActionPreference = 'Stop'
Copy-Item 'dist\packages\GISQC-Keygen.exe' 'dist\packages\GISQC-Keygen-with-LicenseGenerator.exe' -Force
Get-Item 'dist\packages\GISQCWorkbench-Setup.exe','dist\packages\GISQC-Keygen-with-LicenseGenerator.exe' | Select-Object FullName,Length,LastWriteTime | Format-Table -AutoSize
