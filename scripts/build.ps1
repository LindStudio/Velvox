<#
  Configures and builds Velvox (VST3 + Standalone + test runner) and runs the tests.

  Usage (PowerShell):
    .\scripts\build.ps1                       # Release build + tests
    .\scripts\build.ps1 -Config Debug
    .\scripts\build.ps1 -JuceDir D:\libs\JUCE -BuildDir D:\builds\velvox

  The build directory defaults to a local folder OUTSIDE this (Google-Drive-synced)
  project folder, so the cloud sync never touches thousands of build files.
#>
param(
    [string]$Config   = "Release",
    [string]$JuceDir  = "$env:USERPROFILE\dev\JUCE-8.0.9",
    [string]$BuildDir = "$env:USERPROFILE\dev\velvox-build",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$source = Split-Path -Parent $PSScriptRoot

# Locate CMake: PATH first, then the copy shipped with Visual Studio
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $candidate = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) { $cmake = $candidate }
    }
}
if (-not $cmake) { throw "CMake not found. Install Visual Studio 2022 with 'Desktop development with C++' (includes CMake)." }

if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
    throw "JUCE not found at '$JuceDir'. Clone it with: git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git `"$JuceDir`""
}

Write-Host "CMake : $cmake"
Write-Host "Source: $source"
Write-Host "Build : $BuildDir"
Write-Host "JUCE  : $JuceDir"

& $cmake -S $source -B $BuildDir -G "Visual Studio 17 2022" -A x64 "-DJUCE_DIR=$($JuceDir -replace '\\','/')"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& $cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$vst3 = Join-Path $BuildDir "Velvox_artefacts\$Config\VST3\Velvox.vst3"
Write-Host "`nVST3 bundle: $vst3"

if (-not $SkipTests) {
    & (Join-Path $BuildDir "VelvoxTests_artefacts\$Config\VelvoxTests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
}
