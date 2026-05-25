param(
    [string]$BuildDir = "build-windows-preview-vs2022",
    [string]$OutDir = "dist\GISQCWorkbench-latest"
)

$ErrorActionPreference = "Stop"

function Find-CMake() {
    $cmd = Get-Command "cmake" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $fallbacks = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($path in $fallbacks) {
        if (Test-Path $path) {
            return $path
        }
    }
    throw "Command not found: cmake."
}

function Invoke-Native($File, $Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $File $($Arguments -join ' ')"
    }
}

$cmake = Find-CMake

Invoke-Native $cmake @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DGISQC_BUILD_TESTS=OFF",
    "-DGISQC_BUILD_QT_APP=OFF",
    "-DGISQC_BUILD_CLI=ON",
    "-DGISQC_BUILD_WIN_PREVIEW=ON",
    "-DGISQC_WITH_GDAL=OFF"
)
Invoke-Native $cmake @("--build", $BuildDir, "--config", "Release", "--target", "GISQCWorkbenchWinPreview")
Invoke-Native $cmake @("--build", $BuildDir, "--config", "Release", "--target", "GISQCWorkbenchCLI")

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
foreach ($name in @("GISQCWorkbenchWinPreview.exe", "GISQCWorkbenchCLI.exe")) {
    $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter $name | Select-Object -First 1
    if (-not $exe) {
        throw "Build finished but $name was not found."
    }
    Copy-Item $exe.FullName -Destination (Join-Path $OutDir $name) -Force
}
Copy-Item "data" -Destination (Join-Path $OutDir "data") -Recurse -Force
Write-Host "Generated: $OutDir\GISQCWorkbenchWinPreview.exe"
Write-Host "Generated: $OutDir\GISQCWorkbenchCLI.exe"
