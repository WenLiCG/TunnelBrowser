param([switch]$Package, [switch]$Test)
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
            if ($taskVs) { $taskSetup = Join-Path $taskVs 'VC\Auxiliary\Build\vcvars64.bat' }
        }
    }
    if (-not (Test-Path -LiteralPath $taskSetup)) { throw 'Install Visual Studio 2022 C++ Build Tools and the Windows SDK, or run from an x64 Native Tools prompt.' }
    # Pass a simple filename to cmd, avoiding PS 5.1 native argument quoting.
    $taskCmd = Join-Path ([IO.Path]::GetTempPath()) ('tb-vcvars-' + [guid]::NewGuid() + '.cmd')
    $taskEnvFile = [IO.Path]::ChangeExtension($taskCmd, '.env')
    $taskPreviousSetup = $env:TB_VCVARS_SETUP_PATH
    $taskPreviousResult = $env:TB_VCVARS_OUTPUT_PATH
    try {
        $env:TB_VCVARS_SETUP_PATH = $taskSetup
        $env:TB_VCVARS_OUTPUT_PATH = $taskEnvFile
        [IO.File]::WriteAllLines($taskCmd, @(
            '@echo off',
            'call "%TB_VCVARS_SETUP_PATH%" >nul',
            'if errorlevel 1 exit /b %errorlevel%',
            'set >"%TB_VCVARS_OUTPUT_PATH%"'
        ), [Text.Encoding]::ASCII)
        Push-Location -LiteralPath (Split-Path -Parent $taskCmd)
        try {
            & $env:ComSpec /d /u /c (Split-Path -Leaf $taskCmd)
            $taskImportExitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if ($taskImportExitCode -ne 0) { throw "Toolchain environment setup failed (exit $taskImportExitCode)" }
        Get-Content -LiteralPath $taskEnvFile -Encoding Unicode | ForEach-Object {
            if ($_ -match '^([^=]+)=(.*)$' -and $matches[1] -notin @('TB_VCVARS_SETUP_PATH', 'TB_VCVARS_OUTPUT_PATH')) {
                [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
            }
        }
    } finally {
        $env:TB_VCVARS_SETUP_PATH = $taskPreviousSetup
        $env:TB_VCVARS_OUTPUT_PATH = $taskPreviousResult
        Remove-Item -LiteralPath $taskCmd, $taskEnvFile -Force -ErrorAction SilentlyContinue
    }
}
$taskCmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
$taskNinja = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
if (-not $taskCmake) { $taskCmake = Join-Path $taskRoot 'downloads\buildtools\cmake\data\bin\cmake.exe' }
if (-not $taskNinja) { $taskNinja = Join-Path $taskRoot 'downloads\buildtools\bin\ninja.exe' }
if (-not (Test-Path -LiteralPath $taskCmake) -or -not (Test-Path -LiteralPath $taskNinja)) { throw 'Install CMake and Ninja (or: python -m pip install --target downloads/buildtools cmake ninja)' }
& $taskCmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DTB_BUILD_TESTS=$($Test.IsPresent)" "-DCMAKE_MAKE_PROGRAM=$taskNinja"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $taskCmake --build build --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Test) {
    & .\build\test\bootstrapc.exe --module=TunnelBrowser --self-test
    if ($LASTEXITCODE -ne 0) { throw 'Self-check failed' }
}
if ($Package) { python "$PSScriptRoot\package.py"; if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' } }
