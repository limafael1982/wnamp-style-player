# Configure + build wnamp, logging everything to build.log.
#   .\build.ps1 -GStreamerRoot C:\path\to\gstreamer   (folder containing include\, lib\, bin\)
param([string]$GStreamerRoot)
Set-Location $PSScriptRoot
$cfgFile = Join-Path $PSScriptRoot 'local.config.ps1'
if (Test-Path $cfgFile) { . $cfgFile }
if (-not $GStreamerRoot) { $GStreamerRoot = if ($DefaultGStreamerRoot) { $DefaultGStreamerRoot } else { $env:GSTREAMER_1_0_ROOT_MSVC_X86_64 } }
$vsw = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path $vsw) { $vs = & $vsw -latest -property installationPath }
if (-not $vs) { Write-Host 'Visual Studio (C++ workload) not found'; exit 1 }

$cfg = @('-S', '.', '-B', 'build', '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($GStreamerRoot -and (Test-Path "$GStreamerRoot\include\gstreamer-1.0")) {
    $cfg += "-DGSTREAMER_ROOT=$($GStreamerRoot -replace '\\','/')"
} else {
    Write-Host 'GStreamer SDK not found - building with the stub engine (no sound)'
    $cfg += '-DWNAMP_GSTREAMER=OFF'
}

& cmake @cfg *>&1 | Tee-Object -FilePath build.log
& cmake --build build --config Release *>&1 | Tee-Object -FilePath build.log -Append
Write-Host ""
Write-Host "Done. Log: $PSScriptRoot\build.log"
