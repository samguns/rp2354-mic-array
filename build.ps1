param([switch]$Pristine)
$ErrorActionPreference = 'Stop'
$workspace = Join-Path $PSScriptRoot 'zephyrproject'
$sdk = Join-Path $workspace 'zephyr-sdk-1.0.1'
$udc = Join-Path $workspace 'zephyr\drivers\usb\udc\udc_rpi_pico.c'
if (-not (Test-Path (Join-Path $workspace '.west\config')) -or -not (Test-Path $sdk)) {
    throw "In-tree workspace is missing. Run .\setup.ps1"
}
if (-not (Select-String -Path $udc -Pattern 'sof_rd' -Quiet)) {
    throw "Zephyr USB patch is not applied. Run .\setup.ps1"
}
$env:ZEPHYR_BASE = Join-Path $workspace 'zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = $sdk
$buildDir = Join-Path $PSScriptRoot 'build'
$pristineMode = if ($Pristine) { 'always' } else { 'auto' }
Push-Location $workspace
try {
    # Windows PowerShell can turn native stderr (including CMake status
    # messages) into errors when callers redirect output. Check the exit code.
    $ErrorActionPreference = 'Continue'
    west build -b rpi_pico2/rp2350a/m33 -d $buildDir -p $pristineMode $PSScriptRoot -- "-DUSER_CACHE_DIR=$buildDir/.cache"
    $buildExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($buildExitCode -ne 0) { throw "west build failed ($buildExitCode)" }
} finally { Pop-Location }
