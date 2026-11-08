param([switch]$Package)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
python "$PSScriptRoot\fetch-deps.py"
if ($LASTEXITCODE -ne 0) { throw 'Dependency download failed' }

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $taskSetup = Join-Path $taskRoot 'downloads\toolchain\msvc\setup_x64.bat'
    if (-not (Test-Path -LiteralPath $taskSetup)) {
        $taskVswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $taskVswhere) {
            $taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            $taskSetup = Join-Path $taskVs 'VC\Auxiliary\Build\vcvars64.bat'
        }
    }
    if (-not (Test-Path -LiteralPath $taskSetup)) { throw 'Install Visual Studio 2022 C++ Build Tools and the Windows SDK, or run from an x64 Native Tools prompt.' }
    & $env:ComSpec /d /s /c ('""{0}" >nul && set"' -f $taskSetup) | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$taskCmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
$taskNinja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
if (-not $taskCmake) { $taskCmake = Join-Path $taskRoot 'downloads\buildtools\cmake\data\bin\cmake.exe' }
if (-not $taskNinja) { $taskNinja = Join-Path $taskRoot 'downloads\buildtools\bin\ninja.exe' }
if (-not (Test-Path -LiteralPath $taskCmake) -or -not (Test-Path -LiteralPath $taskNinja)) { throw 'Install CMake and Ninja (or: python -m pip install --target downloads/buildtools cmake ninja)' }
& $taskCmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$taskNinja"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $taskCmake --build build --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& .\build\app\bootstrapc.exe --module=TunnelBrowser --self-test
if ($LASTEXITCODE -ne 0) { throw 'Self-check failed' }
if ($Package) { python "$PSScriptRoot\package.py"; if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' } }
