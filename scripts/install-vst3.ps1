<#
  Copies the built Headliner.vst3 bundle into the system VST3 folder
  (C:\Program Files\Common Files\VST3). Writing there needs administrator
  rights, so the script re-launches itself elevated (UAC prompt) if necessary.

  Usage:  .\scripts\install-vst3.ps1 [-Config Release] [-BuildDir <path>]
  Remove: delete "C:\Program Files\Common Files\VST3\Headliner.vst3"
  The old "Velvox.vst3" bundle (same plugin ID) is removed so the host never sees two copies.
#>
param(
    [string]$Config   = "Release",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) { $BuildDir = Join-Path (Split-Path -Parent $PSScriptRoot) "build" }
$sourceBundle = Join-Path $BuildDir "Velvox_artefacts\$Config\VST3\Headliner.vst3"
$targetDir    = Join-Path $env:CommonProgramFiles "VST3"
$targetBundle = Join-Path $targetDir "Headliner.vst3"
$legacyBundle = Join-Path $targetDir "Velvox.vst3"

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
if (Test-Path $legacyBundle) { Remove-Item -Recurse -Force $legacyBundle; Write-Host "Removed old bundle: $legacyBundle" }
Copy-Item -Recurse $sourceBundle $targetBundle
Write-Host "Installed: $targetBundle"
