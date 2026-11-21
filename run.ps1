# Runs wnamp with GStreamer's DLLs and plugins on the path.
#   .\run.ps1                      uses defaults from local.config.ps1
#   .\run.ps1 -Skin other.wsz      override the skin
param([string]$GStreamerRoot, [string]$Skin)
$cfgFile = Join-Path $PSScriptRoot 'local.config.ps1'
if (Test-Path $cfgFile) { . $cfgFile }
if (-not $GStreamerRoot) { $GStreamerRoot = if ($DefaultGStreamerRoot) { $DefaultGStreamerRoot } else { $env:GSTREAMER_1_0_ROOT_MSVC_X86_64 } }
if (-not $Skin) { $Skin = $DefaultSkin }
if ($GStreamerRoot) {
    $env:PATH = "$GStreamerRoot\bin;$env:PATH"
    $plugins = Join-Path $GStreamerRoot 'lib\gstreamer-1.0'
    if (Test-Path $plugins) {
        $env:GST_PLUGIN_PATH = $plugins
        $env:GST_PLUGIN_SYSTEM_PATH = $plugins
    }
}
$exe = Join-Path $PSScriptRoot 'build\Release\wnamp.exe'
if ($Skin -and (Test-Path $Skin)) { & $exe $Skin } else { & $exe }
