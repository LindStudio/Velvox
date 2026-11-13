<#
  Copies a built VST3 bundle into the system VST3 folder
  (C:\Program Files\Common Files\VST3). Writing there needs administrator
  rights, so the script re-launches itself elevated (UAC prompt) if necessary.

  Usage:  .\scripts\install-vst3.ps1 [-Product Vocalypse|Ember] [-Config Release] [-BuildDir <path>]
          (default: Vocalypse; "Ember" installs "EMBER De-Esser.vst3")
  Remove: delete the bundle in "C:\Program Files\Common Files\VST3"
  For VOCALYPSE the bundles of the earlier names "Headliner.vst3" and "Velvox.vst3" (same plugin ID) are
  removed so the host never sees two copies. Close FL Studio first: a loaded plugin DLL cannot be replaced.
#>
param(
    [ValidateSet("Vocalypse", "Ember")]
    [string]$Product  = "Vocalypse",
    [string]$Config   = "Release",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) { $BuildDir = Join-Path (Split-Path -Parent $PSScriptRoot) "build" }
# absolute, because the elevated (UAC) copy of this script starts in System32
if (Test-Path $BuildDir) { $BuildDir = (Resolve-Path $BuildDir).Path }
$bundleName = if ($Product -eq "Ember") { "EMBER De-Esser.vst3" } else { "VOCALYPSE.vst3" }
$sourceBundle = Join-Path $BuildDir "${Product}_artefacts\$Config\VST3\$bundleName"
$targetDir    = Join-Path $env:CommonProgramFiles "VST3"
$targetBundle = Join-Path $targetDir $bundleName
# earlier product names of VOCALYPSE (same plugin ID); never the new name - Windows paths ignore case
$legacyBundles = @()
if ($Product -eq "Vocalypse") { $legacyBundles = @("Headliner.vst3", "Velvox.vst3") | ForEach-Object { Join-Path $targetDir $_ } }

if (-not (Test-Path $sourceBundle)) { throw "No build found at $sourceBundle - run scripts\build.ps1 first." }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
               [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Administrator rights are required - confirm the UAC prompt."
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Product $Product -Config $Config -BuildDir `"$BuildDir`""
    $p = Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $p.ExitCode
}

New-Item -ItemType Directory -Force $targetDir | Out-Null
if (Test-Path $targetBundle) { Remove-Item -Recurse -Force $targetBundle }
foreach ($legacy in $legacyBundles) {
    if (Test-Path $legacy) { Remove-Item -Recurse -Force $legacy; Write-Host "Removed old bundle: $legacy" }
}
Copy-Item -Recurse $sourceBundle $targetBundle
Write-Host "Installed: $targetBundle"
