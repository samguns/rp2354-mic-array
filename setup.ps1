$ErrorActionPreference = 'Stop'
# git writes diagnostics to stderr. Keep those from aborting the script.
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}
$root = $PSScriptRoot
$workspace = Join-Path $root 'zephyrproject'
$zephyrRev = '27150c9dd348690a6b41d3992dd5d3d1458fdc66'
$patch = Join-Path $root 'patches\zephyr-udc-rpi-pico.patch'
$sdkRoot = Join-Path $workspace 'zephyr-sdk-1.0.1'
$sdkArchive = 'zephyr-sdk-1.0.1_windows-x86_64_minimal.7z'
$sdkUrl = "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/$sdkArchive"
$gcc = Join-Path $sdkRoot 'gnu\arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe'

if (-not (Get-Command west -ErrorAction SilentlyContinue)) {
    throw "west is not on PATH. Install it with: python -m pip install west"
}

# west init --mr accepts a branch or tag, not an arbitrary commit.
if (-not (Test-Path (Join-Path $workspace '.west\config'))) {
    west init -o=--filter=blob:none --mr v4.4.0 $workspace
    if ($LASTEXITCODE -ne 0) { throw "west init failed ($LASTEXITCODE)" }
}

$zephyr = Join-Path $workspace 'zephyr'
Push-Location $zephyr
try {
    cmd /c "git apply --check --reverse `"$patch`" >nul 2>&1"
    if ($LASTEXITCODE -eq 0) {
        git apply --reverse $patch
        if ($LASTEXITCODE -ne 0) { throw "Failed to lift Zephyr USB patch before checkout" }
    }
    git fetch origin $zephyrRev
    if ($LASTEXITCODE -ne 0) { throw "git fetch of Zephyr $zephyrRev failed" }
    git checkout --detach $zephyrRev
    if ($LASTEXITCODE -ne 0) { throw "git checkout of Zephyr $zephyrRev failed" }
} finally { Pop-Location }

Push-Location $workspace
try {
    west update
    if ($LASTEXITCODE -ne 0) { throw "west update failed ($LASTEXITCODE)" }
} finally { Pop-Location }

Push-Location $zephyr
try {
    cmd /c "git apply --check --reverse `"$patch`" >nul 2>&1"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Zephyr USB patch already applied."
    } else {
        git apply --check $patch
        if ($LASTEXITCODE -ne 0) { throw "Zephyr USB patch does not apply to $zephyrRev" }
        git apply $patch
        if ($LASTEXITCODE -ne 0) { throw "Failed to apply Zephyr USB patch" }
        Write-Host "Applied Zephyr USB patch."
    }
} finally { Pop-Location }

if (-not (Test-Path $gcc)) {
    $sevenZip = Join-Path ${env:ProgramFiles} '7-Zip\7z.exe'
    if (-not (Test-Path $sevenZip)) { throw "7-Zip is required at $sevenZip" }
    $env:Path = "$(Split-Path $sevenZip);$env:Path"
    if (-not (Test-Path (Join-Path $sdkRoot 'setup.cmd'))) {
        $archive = Join-Path $workspace $sdkArchive
        curl.exe -L --fail --retry 3 -o $archive $sdkUrl
        if ($LASTEXITCODE -ne 0) { throw "Zephyr SDK download failed ($LASTEXITCODE)" }
        & $sevenZip x -y "-o$workspace" $archive
        if ($LASTEXITCODE -ne 0) { throw "Zephyr SDK extract failed ($LASTEXITCODE)" }
        Remove-Item $archive
    }
    Push-Location $sdkRoot
    try {
        cmd /c "setup.cmd /t arm-zephyr-eabi /h /c"
        if ($LASTEXITCODE -ne 0) { throw "Zephyr SDK setup failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
}
if (-not (Test-Path $gcc)) { throw "Arm Zephyr toolchain was not installed at $gcc" }
Write-Host "Workspace ready: $workspace"
