# Run with Windows PowerShell 5.1. Execute only the real toolchain block;
# never invoke dependency downloads, CMake, or the full build.
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'build.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
$block = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -like '*Get-Command cl.exe*'
}, $true)
if (-not $block) { throw 'Toolchain import guard not found' }
$import = [scriptblock]::Create($block.Extent.Text)
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('TB vcvars ' + [guid]::NewGuid())
$originalPath = $env:PATH
$originalTemp = $env:TEMP
$originalTmp = $env:TMP
$originalMarker = $env:TB_VCVARS_TEST
try {
    $taskRoot = $fixture
    $setupDir = Join-Path $fixture 'downloads\toolchain\msvc'
    New-Item -ItemType Directory -Path $setupDir -Force | Out-Null
    $tempDir = Join-Path $fixture 'Temporary Files'
    New-Item -ItemType Directory -Path $tempDir | Out-Null
    $env:TEMP = $tempDir
    $env:TMP = $tempDir
    $env:PATH = "$env:SystemRoot\System32"
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { throw 'Fixture must start without cl.exe' }
    $setup = Join-Path $setupDir 'setup_x64.bat'
    Set-Content -LiteralPath $setup -Encoding ASCII -Value "@echo off`r`nset TB_VCVARS_TEST=imported=value"
    $env:TB_VCVARS_TEST = $null
    & $import
    if ($env:TB_VCVARS_TEST -ne 'imported=value') { throw 'Spaced vcvars path did not import environment' }
    if (@(Get-ChildItem -LiteralPath $tempDir -Force).Count) { throw 'Temporary wrapper leaked after success' }
    Write-Output 'PASS: spaced path imports into current process; temporary files removed'

    Set-Content -LiteralPath $setup -Encoding ASCII -Value "@echo off`r`nexit /b 23"
    $failed = $false
    try { & $import } catch { $failed = $true }
    if (-not $failed) { throw 'Failed vcvars must throw' }
    if (@(Get-ChildItem -LiteralPath $tempDir -Force).Count) { throw 'Temporary wrapper leaked after failure' }
    Write-Output 'PASS: failed vcvars throws and cleans temporary files'

    # A discoverable cl.exe must bypass even a broken setup script.
    $compilerDir = Join-Path $fixture 'Compiler Tools'
    New-Item -ItemType Directory -Path $compilerDir | Out-Null
    Copy-Item -LiteralPath "$env:SystemRoot\System32\where.exe" -Destination (Join-Path $compilerDir 'cl.exe')
    $env:PATH = "$compilerDir;$env:PATH"
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Fake cl.exe is not discoverable' }
    Set-Content -LiteralPath $setup -Encoding ASCII -Value "@echo off`r`necho invoked>`"$fixture\called.txt`"`r`nset TB_VCVARS_TEST=overwritten`r`nexit /b 23"
    $env:TB_VCVARS_TEST = 'preserved'
    & $import
    if ($env:TB_VCVARS_TEST -ne 'preserved') { throw 'Existing environment was overwritten' }
    if (Test-Path -LiteralPath (Join-Path $fixture 'called.txt')) { throw 'vcvars was invoked despite cl.exe in PATH' }
    if (@(Get-ChildItem -LiteralPath $tempDir -Force).Count) { throw 'Skip path leaked temporary files' }
    Write-Output 'PASS: cl.exe in PATH skips vcvars and preserves environment'
} finally {
    $env:PATH = $originalPath
    $env:TEMP = $originalTemp
    $env:TMP = $originalTmp
    $env:TB_VCVARS_TEST = $originalMarker
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}
