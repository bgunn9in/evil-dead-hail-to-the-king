$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$package = Join-Path $PSScriptRoot 'package.ps1'
$errors = $null
[Management.Automation.Language.Parser]::ParseFile($package, [ref]$null, [ref]$errors) | Out-Null
if ($errors.Count) { throw ($errors.Message -join '; ') }
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('evil-dead-package-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($scratch) | Out-Null
$sentinel = Join-Path $scratch 'sentinel.txt'
[IO.File]::WriteAllText($sentinel, 'existing output must remain intact')
function Reject([string]$Output, [string]$Reason) {
    $failed = $false
    try { & $package -OutputDirectory $Output | Out-Null }
    catch { $failed = $_.Exception.Message.Contains($Reason) }
    if (!$failed) { throw "Expected package rejection: $Reason" }
}
try {
    Reject $scratch 'already exists'
    if ([IO.File]::ReadAllText($sentinel) -ne 'existing output must remain intact') { throw 'Existing file was modified.' }
    $originalTarget = Join-Path $workspace ('game\package-guard-' + [guid]::NewGuid().ToString('N'))
    Reject $originalTarget 'original game directory'
    if (Test-Path -LiteralPath $originalTarget) { throw 'Package touched original game directory.' }
    Reject (Join-Path $workspace 'build\episode\package-guard') 'separate from the build tree'
    Write-Output 'Package syntax and non-destructive existing/original/build-directory guards: passed'
} finally {
    [IO.File]::Delete($sentinel)
    [IO.Directory]::Delete($scratch)
}
