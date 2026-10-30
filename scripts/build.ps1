$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build/episode'
$nativeManifest = Join-Path $buildDirectory 'assets/dreamcast-runtime.json'
if (Test-Path -LiteralPath $nativeManifest -PathType Leaf) {
    $native = Get-Content -LiteralPath $nativeManifest -Raw | ConvertFrom-Json
    if ($native.format -cne 'ED-DREAMCAST-RUNTIME1' -or $native.status -cne 'verified-import') { throw 'Dreamcast resource profile has not passed import verification.' }
}
$configureArguments = @('-S', $projectRoot, '-B', $buildDirectory, '-A', 'x64')
& "$PSScriptRoot/build-sdl.ps1"
& "$PSScriptRoot/build-dav1d.ps1"
cmake @configureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $buildDirectory --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (!(Test-Path -LiteralPath $nativeManifest -PathType Leaf)) {
    Write-Host 'Source-only build complete. Supply your own Dreamcast assets before gameplay or the full test suite. See README.md.'
    exit 0
}
ctest --test-dir $buildDirectory -C Release --output-on-failure
exit $LASTEXITCODE
