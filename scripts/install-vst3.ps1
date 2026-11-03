<#
  Copies the built VOCALYPSE.vst3 bundle into the system VST3 folder
  (C:\Program Files\Common Files\VST3). Writing there needs administrator
  rights, so the script re-launches itself elevated (UAC prompt) if necessary.

  Usage:  .\scripts\install-vst3.ps1 [-Config Release] [-BuildDir <path>]
  Remove: delete "C:\Program Files\Common Files\VST3\VOCALYPSE.vst3"
  The bundles of the earlier names "Headliner.vst3" and "Velvox.vst3" (same plugin ID) are removed
  so the host never sees two copies.
#>
param(
    [string]$Config   = "Release",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) { $BuildDir = Join-Path (Split-Path -Parent $PSScriptRoot) "build" }
$sourceBundle = Join-Path $BuildDir "Vocalypse_artefacts\$Config\VST3\VOCALYPSE.vst3"
$targetDir    = Join-Path $env:CommonProgramFiles "VST3"
$targetBundle = Join-Path $targetDir "VOCALYPSE.vst3"
# earlier product names (same plugin ID); never the new name - Windows paths ignore case
$legacyBundles = @("Headliner.vst3", "Velvox.vst3") | ForEach-Object { Join-Path $targetDir $_ }

if (-not (Test-Path $sourceBundle)) { throw "No build found at $sourceBundle - run scripts\build.ps1 first." }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
               [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Administrator rights are required - confirm the UAC prompt."
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Config $Config -BuildDir `"$BuildDir`""
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
