<#
  Builds the Windows installer of a single plugin (Release build, its tests, then Inno Setup).

  Usage:  .\scripts\make-installer.ps1 [-Product Ember] [-SkipTests]
  Output: build\installer\EMBER_De-Esser_<version>_Windows_Setup.exe

  Needs Inno Setup 6 (winget install JRSoftware.InnoSetup --scope user). Not code-signed yet: Windows
  SmartScreen warns about unsigned installers - buy a code-signing certificate before selling.
#>
param(
    [ValidateSet("Ember")]
    [string]$Product = "Ember",
    [string]$JuceDir = "$env:USERPROFILE\dev\JUCE-8.0.9",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$source = Split-Path -Parent $PSScriptRoot
$build  = Join-Path $source "build"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found: winget install JRSoftware.InnoSetup --scope user" }

# Version from Products\<Product>\<Product>.cmake (set(EMBER_VERSION x.y.z))
$cmakeFile = Join-Path $source "Products\$Product\$Product.cmake"
$version = (Select-String -Path $cmakeFile -Pattern 'set\(\w+_VERSION ([0-9.]+)\)').Matches[0].Groups[1].Value
Write-Host "$Product $version"

& $cmake -S $source -B $build -G "Visual Studio 17 2022" -A x64 "-DJUCE_DIR=$($JuceDir -replace '\\','/')"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $build --config Release --target "${Product}_VST3" "${Product}_Standalone" "${Product}Tests" --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $SkipTests) {
    & (Join-Path $build "${Product}Tests_artefacts\Release\${Product}Tests.exe")
    if ($LASTEXITCODE -ne 0) { throw "$Product tests failed - no installer" }
}

$iss = Join-Path $source "Products\$Product\Installer\$Product.iss"
$out = Join-Path $build "installer"
& $iscc "/DAppVersion=$version" "/DBuildDir=$(Join-Path $build "${Product}_artefacts\Release")" "/DOutDir=$out" $iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
Get-ChildItem $out -Filter "*$version*Setup.exe" | ForEach-Object { Write-Host "Installer: $($_.FullName)" }
