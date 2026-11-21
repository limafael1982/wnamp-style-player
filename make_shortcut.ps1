# Creates a Desktop shortcut to Wnamp. Run this once from the project folder, after building.
$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "build\Release\wnamp.exe"
if (-not (Test-Path $exe)) {
    Write-Error "wnamp.exe not found at $exe. Build the project first (.\build.ps1)."
    exit 1
}
$desktop = [Environment]::GetFolderPath("Desktop")
$lnkPath = Join-Path $desktop "Wnamp.lnk"

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($lnkPath)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = Split-Path $exe
$shortcut.IconLocation = "$exe,0"
$shortcut.Description = "Wnamp Style Player"
$shortcut.Save()

Write-Host "Shortcut created: $lnkPath"
