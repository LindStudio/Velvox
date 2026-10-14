<#
  Mirrors the local Velvox repository to Google Drive as a backup.

  - Writes a complete git bundle (all branches + tags, full history):
      <DriveDir>\_git-backup\velvox.bundle
    Restore with:  git clone velvox.bundle velvox
  - Copies the current working tree (tracked files only) over the Drive copy,
    so the Drive folder always shows the latest source. Nothing is deleted
    on the Drive side.

  Usage:  .\scripts\backup-to-drive.ps1 [-DriveDir "G:\Meine Ablage\MUSIC\Plugin"]
#>
param(
    [string]$DriveDir = "G:\Meine Ablage\MUSIC\Plugin"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path $DriveDir)) { throw "Drive folder not found: $DriveDir" }

Push-Location $repo
try {
    $status = git status --porcelain
    if ($status) { Write-Warning "Uncommitted changes are NOT in the bundle (working tree copy includes them)." }

    $bundleDir = Join-Path $DriveDir "_git-backup"
    New-Item -ItemType Directory -Force $bundleDir | Out-Null
    git bundle create (Join-Path $bundleDir "velvox.bundle") --all
    if ($LASTEXITCODE -ne 0) { throw "git bundle failed" }

    foreach ($file in (git ls-files)) {
        $target = Join-Path $DriveDir $file
        New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
        Copy-Item -Force (Join-Path $repo $file) $target
    }

    $head = git describe --tags --always --dirty
    Set-Content -Encoding utf8 (Join-Path $bundleDir "BACKUP_INFO.txt") "Backup of $head created $(Get-Date -Format s)"
    Write-Host "Backup of $head written to $DriveDir"
}
finally {
    Pop-Location
}
