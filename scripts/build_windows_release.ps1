param(
    [string]$QtPrefix = $env:Qt6_DIR,
    [string]$GdalPrefix = $env:GDAL_DIR,
    [string]$BuildDir = "build-windows-release",
    [string]$OutDir = "dist\GISQCWorkbench-latest"
)

$ErrorActionPreference = "Stop"

function Require-Command($Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$Name.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$Name.exe"
    )
    foreach ($path in $fallbacks) {
        if (Test-Path $path) {
            return $path
        }
    }

    throw "Command not found: $Name. Install it and add it to PATH first."
}

function Invoke-Native($File, $Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $File $($Arguments -join ' ')"
    }
}

function Copy-DirectoryFresh($Source, $Destination) {
    $resolvedDestination = [System.IO.Path]::GetFullPath($Destination)
    $workspaceRoot = [System.IO.Path]::GetFullPath((Get-Location).Path)
    if (-not $resolvedDestination.StartsWith($workspaceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace directory outside workspace: $resolvedDestination"
    }
    if (Test-Path $resolvedDestination) {
        Remove-Item $resolvedDestination -Recurse -Force
    }
    Copy-Item $Source -Destination $resolvedDestination -Recurse -Force
}

$cmake = Require-Command "cmake"

if (-not $QtPrefix) {
    $qtCandidates = @(
        "D:\Qt\6.8.3\msvc2022_64",
        "C:\Qt\6.8.3\msvc2022_64"
    )
    foreach ($candidate in $qtCandidates) {
        if (Test-Path (Join-Path $candidate "lib\cmake\Qt6\Qt6Config.cmake")) {
            $QtPrefix = $candidate
            break
        }
    }
}

$gdalPrefixRoot = $null
if ($GdalPrefix) {
    if (Test-Path (Join-Path $GdalPrefix "GDALConfig.cmake")) {
        $gdalPrefixRoot = (Resolve-Path (Join-Path $GdalPrefix "..\..\..")).Path
    } elseif (Test-Path (Join-Path $GdalPrefix "lib\cmake\gdal\GDALConfig.cmake")) {
        $gdalPrefixRoot = $GdalPrefix
    }
}
if (-not $GdalPrefix) {
    $gdalCandidates = @(
        "D:\jiedan\136\tools\OSGeo4W",
        "C:\OSGeo4W"
    )
    foreach ($candidate in $gdalCandidates) {
        if (Test-Path (Join-Path $candidate "lib\cmake\gdal\GDALConfig.cmake")) {
            $GdalPrefix = $candidate
            $gdalPrefixRoot = $candidate
            break
        }
    }
}

$prefixPaths = @()
if ($QtPrefix) {
    $prefixPaths += $QtPrefix
}
if ($GdalPrefix) {
    $prefixPaths += $GdalPrefix
}

$cmakeArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DGISQC_BUILD_TESTS=OFF",
    "-DGISQC_BUILD_QT_APP=ON",
    "-DGISQC_WITH_GDAL=ON"
)
if ($prefixPaths.Count -gt 0) {
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$($prefixPaths -join ';')"
}

Invoke-Native $cmake $cmakeArgs
Invoke-Native $cmake @("--build", $BuildDir, "--config", "Release", "--target", "GISQCWorkbench")
Invoke-Native $cmake @("--build", $BuildDir, "--config", "Release", "--target", "GISQCWorkbenchCLI")
Invoke-Native $cmake @("--build", $BuildDir, "--config", "Release", "--target", "GISQCWorkbenchWinPreview")

$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "GISQCWorkbench.exe" | Select-Object -First 1
if (-not $exe) {
    throw "Build finished but GISQCWorkbench.exe was not found."
}

$windeployqt = Get-Command "windeployqt" -ErrorAction SilentlyContinue
$windeployqtPath = if ($windeployqt) { $windeployqt.Source } else { $null }
if (-not $windeployqtPath -and $QtPrefix) {
    $candidate = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (Test-Path $candidate) {
        $windeployqtPath = $candidate
    }
}

$runtimeDlls = @(
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "concrt140.dll"
)

function Deploy-App($DeployDir) {
    New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

    $mainDestination = [System.IO.Path]::GetFullPath((Join-Path $DeployDir "GISQCWorkbench.exe"))
    if ([System.IO.Path]::GetFullPath($exe.FullName) -ne $mainDestination) {
        Copy-Item $exe.FullName -Destination $mainDestination -Force
    }
    foreach ($name in @("GISQCWorkbenchCLI.exe", "GISQCWorkbenchWinPreview.exe")) {
        $toolExe = Get-ChildItem -Path $BuildDir -Recurse -Filter $name | Select-Object -First 1
        if ($toolExe) {
            $toolDestination = [System.IO.Path]::GetFullPath((Join-Path $DeployDir $name))
            if ([System.IO.Path]::GetFullPath($toolExe.FullName) -ne $toolDestination) {
                Copy-Item $toolExe.FullName -Destination $toolDestination -Force
            }
        }
    }

    if ($windeployqtPath) {
        & $windeployqtPath (Join-Path $DeployDir "GISQCWorkbench.exe")
    } else {
        Write-Warning "windeployqt was not found. The exe was copied, but Qt runtime libraries may be missing."
    }

    foreach ($dll in $runtimeDlls) {
        $source = Join-Path $env:WINDIR "System32\$dll"
        if (Test-Path $source) {
            Copy-Item $source -Destination (Join-Path $DeployDir $dll) -Force
        }
    }

    if ($gdalPrefixRoot) {
        $gdalBin = Join-Path $gdalPrefixRoot "bin"
        if (Test-Path $gdalBin) {
            Get-ChildItem $gdalBin -Filter "*.dll" | ForEach-Object {
                Copy-Item $_.FullName -Destination (Join-Path $DeployDir $_.Name) -Force
            }
        }

        $gdalApps = Join-Path $gdalPrefixRoot "apps\gdal"
        if (Test-Path $gdalApps) {
            Copy-DirectoryFresh $gdalApps (Join-Path $DeployDir "gdal")
        }

        $projShare = Join-Path $gdalPrefixRoot "share\proj"
        if (Test-Path $projShare) {
            Copy-DirectoryFresh $projShare (Join-Path $DeployDir "proj")
        }
    }

    Copy-DirectoryFresh "data" (Join-Path $DeployDir "data")
    if (Test-Path "docs") {
        Copy-DirectoryFresh "docs" (Join-Path $DeployDir "docs")
    }
    if (Test-Path (Join-Path $DeployDir "license.dat")) {
        Remove-Item (Join-Path $DeployDir "license.dat") -Force
    }
    Write-Host "Generated: $DeployDir\GISQCWorkbench.exe"
}

$releaseDir = Join-Path $BuildDir "Release"
Deploy-App $OutDir
Deploy-App $releaseDir
