$ErrorActionPreference = 'Stop'
$installer = (Resolve-Path 'dist\packages\GISQCWorkbench-Setup.exe').Path
$installDir = Join-Path $env:TEMP ('GISQCInstallerSmoke_' + [Guid]::NewGuid().ToString('N'))
Write-Host "InstallDir=$installDir"
& $installer /silent $installDir
if (($null -ne $LASTEXITCODE) -and ($LASTEXITCODE -ne 0)) { throw "installer failed: $LASTEXITCODE" }
$exe = Join-Path $installDir 'GISQCWorkbench.exe'
$uninstaller = Join-Path $installDir 'Uninstall-GISQCWorkbench.exe'
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline -and -not (Test-Path $exe)) { Start-Sleep -Milliseconds 500 }
if (-not (Test-Path $exe)) { throw "main exe missing: $exe" }
if (-not (Test-Path $uninstaller)) { throw "uninstaller missing: $uninstaller" }
$reg = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\GISQCWorkbench'
if (-not (Test-Path $reg)) { throw "uninstall registry entry missing" }
$item = Get-ItemProperty $reg
if ($item.DisplayName -ne 'GIS 数据质量检查工作台') { throw "bad DisplayName: $($item.DisplayName)" }
if ($item.UninstallString -notmatch 'Uninstall-GISQCWorkbench.exe') { throw "bad UninstallString: $($item.UninstallString)" }
$shortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'GIS数据质量检查工作台.lnk'
if (-not (Test-Path $shortcut)) { throw "desktop lnk missing: $shortcut" }
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
if ($link.TargetPath -ne $exe) { throw "shortcut target mismatch: $($link.TargetPath)" }
if ($link.TargetPath -match '%|file:///') { throw "shortcut target appears encoded/garbled: $($link.TargetPath)" }
Write-Host "ShortcutTarget=$($link.TargetPath)"
Write-Host "RegistryDisplayName=$($item.DisplayName)"
Write-Host "RegistryUninstallString=$($item.UninstallString)"
& $uninstaller /uninstall /silent /dir $installDir
Start-Sleep -Seconds 4
if (Test-Path $reg) { throw "uninstall registry entry still exists" }
if (Test-Path $shortcut) { throw "desktop shortcut still exists" }
if (Test-Path $installDir) { throw "install dir still exists: $installDir" }
Write-Host "SMOKE_OK"
