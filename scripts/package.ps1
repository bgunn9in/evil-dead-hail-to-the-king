[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\episode'),
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$SdlLicensePath = (Join-Path $PSScriptRoot '..\build\dependencies\SDL3-3.4.18-vulkan\install\licenses\SDL3\LICENSE.txt'),
    [string]$CrtDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This package targets Windows x64.' }
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\')
$destination = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($destination.StartsWith('\\') -or [IO.DriveInfo]::new([IO.Path]::GetPathRoot($destination)).DriveType -eq [IO.DriveType]::Network) { throw 'Package output must be a local filesystem directory.' }
$original = Join-Path $workspace 'game'
function Within([string]$Path, [string]$Root) {
    return $Path.Equals($Root, [StringComparison]::OrdinalIgnoreCase) -or $Path.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}
function Assert-NoLinks([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked/reparse path is not accepted: $current" }
        }
        $parent = [IO.Directory]::GetParent($current)
        $current = if ($parent) { $parent.FullName } else { $null }
    }
}
if (Within $destination $original) { throw 'Package output cannot be inside the original game directory.' }
if ((Within $destination $buildRoot) -or (Within $buildRoot $destination)) { throw 'Package output must be separate from the build tree.' }
Assert-NoLinks $destination
if (Test-Path -LiteralPath $destination) { throw "Output already exists; choose a NEW directory: $destination" }
$binary = Join-Path $buildRoot "$Configuration\evil-dead-scene.exe"
$sdl = Join-Path $buildRoot "$Configuration\SDL3.dll"
$sdkStamp = Join-Path $workspace 'build\dependencies\SDL3-3.4.18-vulkan\vulkan-sdk-stamp.json'
$dav1dRoot = Join-Path $workspace 'build\dependencies\dav1d-1.5.2'
$dav1dStamp = Join-Path $dav1dRoot 'sdk-stamp.json'
$dav1dLibrary = Join-Path $dav1dRoot 'install\lib\libdav1d.a'
$dav1dLicense = Join-Path $dav1dRoot 'install\licenses\dav1d-COPYING.txt'
$assets = Join-Path $buildRoot 'assets'
$nativeSeal = Join-Path $assets 'dreamcast-runtime.json'
$nativeInputs = Join-Path $assets 'provenance\import-inputs.json'
$assetVerifier = Join-Path $workspace 'tools\import_dreamcast_runtime.py'
$native = $null
foreach ($required in @($nativeSeal, $nativeInputs, $assetVerifier)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing sealed Dreamcast import input: $required" }
    Assert-NoLinks $required
}
$nativeSealHash = (Get-FileHash -LiteralPath $nativeSeal -Algorithm SHA256).Hash.ToLowerInvariant()
$native = Get-Content -LiteralPath $nativeSeal -Raw -Encoding UTF8 | ConvertFrom-Json
if ($native.format -cne 'ED-DREAMCAST-RUNTIME1' -or $native.status -cne 'verified-import') { throw 'Package requires a sealed verified-import Dreamcast resource profile.' }
if ($native.source_manifest_sha256 -cne 'cb0c24fee804eb7cc7bc65f4e2311f7498ab1c075f12f7c8cc7f5ff1d4cbc9c9' -or
        $native.original_script_sha256.A -cnotmatch '^[0-9a-f]{64}$' -or $native.original_script_sha256.B -cnotmatch '^[0-9a-f]{64}$') { throw 'Unknown original Dreamcast resource provenance.' }
$shaders = Join-Path $buildRoot 'shaders'
if (!(Test-Path -LiteralPath $shaders -PathType Container)) { $shaders = Join-Path $buildRoot "$Configuration\shaders" }
foreach ($required in @($binary, $sdl, $SdlLicensePath, (Join-Path $assets 'world.txt'), (Join-Path $assets 'scripts\levelA.jpc'),
        (Join-Path $assets 'actors\ash\actor-parts.bin'), (Join-Path $assets 'audio\manifest.txt'), (Join-Path $assets 'movies\manifest.json'),
        (Join-Path $assets 'objects\source.json'), (Join-Path $assets 'vfx\source.json'), (Join-Path $shaders 'spirv-manifest.json'))) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing package input: $required" }
    Assert-NoLinks $required
}
$runtimeRoot = Join-Path $workspace 'runtime'
$binaryTime = (Get-Item -LiteralPath $binary).LastWriteTimeUtc
$runtimeSources = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$pendingHeaders = [Collections.Generic.Queue[string]]::new()
$pendingHeaders.Enqueue((Join-Path $runtimeRoot 'main.cpp'))
$pendingHeaders.Enqueue((Join-Path $runtimeRoot 'flac_decode.cpp'))
while ($pendingHeaders.Count) {
    $source = $pendingHeaders.Dequeue()
    if (!$runtimeSources.Add($source)) { continue }
    if ((Get-Item -LiteralPath $source).LastWriteTimeUtc -gt $binaryTime) { throw "Runtime sources are newer than the executable; rebuild $Configuration before packaging: $source" }
    foreach ($match in [regex]::Matches([IO.File]::ReadAllText($source), '(?m)^\s*#\s*include\s+"([^"]+)"')) {
        $included = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($source)) $match.Groups[1].Value))
        if (Within $included $runtimeRoot) { $pendingHeaders.Enqueue($included) }
    }
}
foreach ($profile in @($assets, (Join-Path $assets 'levelB'))) {
    foreach ($name in @('world.txt', 'episode.json', 'actors\ash\actor-parts.bin', 'audio\manifest.txt', 'movies\manifest.json', 'objects\source.json', 'vfx\source.json',
            'gui\text.bin', 'gui\layout.bin', 'gui\models\game-over.mesh', 'gui\textures\0b942034.rgba', 'gui\source.json')) {
        $required = Join-Path $profile $name
        if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing package profile input: $required" }
        Assert-NoLinks $required
    }
    $menu = if ($profile -eq $assets) { 'aa01' } else { 'bb01' }
    $script = if ($profile -eq $assets) { 'levelA.jpc' } else { 'levelB.jpc' }
    foreach ($name in @("scripts\$script", "$menu\menu.txt", "$menu\scene.native", "$menu\menu-resources.bin", "$menu\camera-frames.bin", "$menu\background_0.bmp")) {
        $required = Join-Path $profile $name
        if (!(Test-Path -LiteralPath $required -PathType Leaf) -and $required.EndsWith('.bmp')) {
            $required = [IO.Path]::ChangeExtension($required, '.png')
        }
        if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing package menu/script input: $required" }
        Assert-NoLinks $required
    }
    $world = @(Get-Content -LiteralPath (Join-Path $profile 'world.txt'))
    $sceneCount = 0
    if ($world.Count -lt 3 -or $world[0] -ne 'EDWORLD1' -or ![int]::TryParse($world[2], [ref]$sceneCount) -or
            $sceneCount -lt 8 -or $sceneCount -gt 256 -or $world.Count -ne $sceneCount + 3) { throw 'Invalid generated episode world manifest.' }
    $scenes = @($world | Select-Object -Skip 3)
    $episode = Get-Content -LiteralPath (Join-Path $profile 'episode.json') -Raw | ConvertFrom-Json
    if (($scenes | Select-Object -Unique).Count -ne $sceneCount -or $world[1] -notin $scenes -or
            ($scenes -join ',') -cne (@($episode.scenes) -join ',')) { throw 'Episode metadata does not match the world manifest.' }
    foreach ($scene in $scenes) {
        if ($scene -cnotmatch '^[a-z]{2}[0-9]{2}$') { throw 'Invalid episode scene identity.' }
        foreach ($name in @('scene.txt', 'scene.native', 'portals.bin')) {
            $required = Join-Path $profile "$scene\$name"
            if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing scene input: $required" }
            Assert-NoLinks $required
        }
    }
}
$shaderNames = @('scene.vert.spv', 'scene.frag.spv', 'scene.bgfrag.spv', 'scene.hudfrag.spv', 'scene.particlefrag.spv', 'scene.opaquefrag.spv', 'scene.maskfrag.spv', 'scene.postfrag.spv')
$sourceManifest = Join-Path $runtimeRoot 'shaders\spirv-manifest.json'
$builtManifest = Join-Path $shaders 'spirv-manifest.json'
$shaderSource = Join-Path $runtimeRoot 'scene.hlsl'
foreach ($path in @($sourceManifest, $builtManifest, $shaderSource)) { Assert-NoLinks $path }
if ((Get-FileHash -LiteralPath $sourceManifest -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath $builtManifest -Algorithm SHA256).Hash) {
    throw 'SPIR-V manifest differs from the current source; reconfigure/rebuild before packaging.'
}
$gpu = Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json
if ($gpu.format -cne 'EDSPIRV1' -or @($gpu.shaders).Count -ne 8 -or
        $gpu.source_sha256 -cne (Get-FileHash -LiteralPath $shaderSource -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'SPIR-V source manifest is stale or invalid; regenerate the Vulkan shaders.'
}
$entries = @('VSMain', 'PSMain', 'PSBackground', 'PSHud', 'PSParticle', 'PSOpaque', 'PSDCMask', 'PSPost')
$samplerCounts = @(0, 1, 2, 1, 1, 1, 2, 1)
for ($index = 0; $index -lt $shaderNames.Count; ++$index) {
    $shader = $gpu.shaders[$index]; $required = Join-Path $shaders $shaderNames[$index]
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing Vulkan shader: $required" }
    Assert-NoLinks $required
    if ($shader.file -cne $shaderNames[$index] -or $shader.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            (Get-Item -LiteralPath $required).Length -ne $shader.bytes -or
            $shader.sha256 -cne (Get-FileHash -LiteralPath $required -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw "SPIR-V shader checksum differs from its source manifest: $required"
    }
    $stage = if ($index -eq 0) { 0 } else { 4 }
    $uniformCount = 1
    $resources = @($shader.resources)
    $samplers = @($resources | Where-Object { $_.kind -ceq 'combined-image-sampler' } | Sort-Object binding)
    $uniforms = @($resources | Where-Object { $_.kind -ceq 'uniform' })
    if (@($shader.entries).Count -ne 1 -or $shader.entries[0].stage -ne $stage -or $shader.entries[0].entry -cne $entries[$index] -or
            $samplers.Count -ne $samplerCounts[$index] -or $uniforms.Count -ne $uniformCount -or $resources.Count -ne $samplers.Count + $uniforms.Count) {
        throw "SPIR-V SDL resource ABI differs: $required"
    }
    for ($binding = 0; $binding -lt $samplers.Count; ++$binding) {
        if ($samplers[$binding].set -ne 2 -or $samplers[$binding].binding -ne $binding) { throw "SPIR-V sampler ABI differs: $required" }
    }
    if ($index -eq 0 -and ($shader.inputs -join ',') -cne '0,1,2') { throw "SPIR-V vertex ABI differs: $required" }
    if ($uniformCount) {
        $set = if ($index -eq 0) { 1 } else { 3 }
        $offsets = if ($index -eq 0) { '0;64' } elseif ($index -eq 7) { '0;16;32' } elseif ($index -eq 4) { '0;16' } else { '0' }
        $strides = if ($index -eq 0) { '16' } else { '' }
        if ($uniforms[0].set -ne $set -or $uniforms[0].binding -ne 0 -or
                (($uniforms[0].offsets | ForEach-Object { $_ -join ',' }) -join ';') -cne $offsets -or
                ($uniforms[0].matrix_strides -join ',') -cne $strides) { throw "SPIR-V uniform ABI differs: $required" }
    }
}
if (!(Test-Path -LiteralPath $sdkStamp -PathType Leaf)) { throw 'Missing verified Windows Vulkan SDK stamp; run scripts/build-sdl.ps1.' }
Assert-NoLinks $sdkStamp
$sdk = Get-Content -LiteralPath $sdkStamp -Raw | ConvertFrom-Json
$sdkDll = @($sdk.files | Where-Object { $_.path -ceq 'bin/SDL3.dll' })
if ($sdkDll.Count -ne 1 -or $sdkDll[0].sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $sdkDll[0].sha256 -cne (Get-FileHash -LiteralPath $sdl -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'SDL library differs from the verified Windows Vulkan SDK; rebuild before packaging.'
}
$sdkLicense = @($sdk.files | Where-Object { $_.path -ceq 'licenses/SDL3/LICENSE.txt' })
if ($sdkLicense.Count -ne 1 -or $sdkLicense[0].sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $sdkLicense[0].sha256 -cne (Get-FileHash -LiteralPath $SdlLicensePath -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'SDL license differs from the verified Windows Vulkan SDK.'
}
foreach ($path in @($dav1dStamp, $dav1dLibrary, $dav1dLicense)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw 'Missing pinned static dav1d SDK; run scripts/build-dav1d.ps1.' }
    Assert-NoLinks $path
}
$dav1d = Get-Content -LiteralPath $dav1dStamp -Raw | ConvertFrom-Json
if ($dav1d.fingerprint -cnotlike 'dav1d1.5.2|x64|MSVC|*' -or $dav1d.license -cne 'BSD-2-Clause' -or
        $dav1d.archive_sha256 -cne '2fc0810b4cdf72784b3c107827ff10b1d83ec709a1ec1fbdbc6a932daf65ead6') { throw 'Unexpected dav1d source/build pin.' }
foreach ($entry in @(@('lib/libdav1d.a', $dav1dLibrary), @('licenses/dav1d-COPYING.txt', $dav1dLicense))) {
    $record = @($dav1d.files | Where-Object { $_.path -ceq $entry[0] })
    if ($record.Count -ne 1 -or $record[0].sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $record[0].sha256 -cne (Get-FileHash -LiteralPath $entry[1] -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'dav1d static library/license differs from its verified SDK.' }
}
if ((Get-Item -LiteralPath $dav1dLibrary).LastWriteTimeUtc -gt $binaryTime) { throw 'Static dav1d is newer than the executable; rebuild before packaging.' }
if (!$CrtDirectory) {
    $instance = (Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') | Where-Object { $_ -like 'CMAKE_GENERATOR_INSTANCE:INTERNAL=*' } | Select-Object -First 1) -replace '^.*?=', ''
    if (!$instance) { throw 'Cannot locate the compiler redistributable folder; pass -CrtDirectory.' }
    $redist = Join-Path $instance 'VC\Redist\MSVC'
    $versions = @(Get-ChildItem -LiteralPath $redist -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending)
    foreach ($version in $versions) {
        if (!(Test-Path -LiteralPath (Join-Path $version.FullName 'x64'))) { continue }
        $candidate = @(Get-ChildItem -LiteralPath (Join-Path $version.FullName 'x64') -Directory | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' })
        if ($candidate.Count) { $CrtDirectory = $candidate[0].FullName; break }
    }
}
if (!$CrtDirectory -or $CrtDirectory -notmatch '[\\/]VC[\\/]Redist[\\/]MSVC[\\/]') { throw 'Pass the x64 Microsoft.VC*.CRT folder from the installed Visual Studio Redist tree.' }
$crtFiles = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') | ForEach-Object { Join-Path $CrtDirectory $_ }
function Assert-X64([string]$Path) {
    Assert-NoLinks $Path
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE image: $Path" }
        $stream.Position = 0x3c; $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset -gt $stream.Length - 6) { throw "Invalid PE offset: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Expected x64 executable/library: $Path" }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
foreach ($file in @($binary, $sdl) + $crtFiles) { Assert-X64 $file }
$files = [Collections.Generic.List[object]]::new()
function Plan-File([string]$Source, [string]$Relative) {
    Assert-NoLinks $Source
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing source file: $Source" }
    $files.Add([pscustomobject]@{ Source = [IO.Path]::GetFullPath($Source); Relative = $Relative })
}
Plan-File $binary 'bin\evil-dead-scene.exe'
Plan-File $sdl 'bin\SDL3.dll'
foreach ($file in $crtFiles) { Plan-File $file ('bin\' + [IO.Path]::GetFileName($file)) }
Plan-File $SdlLicensePath 'licenses\SDL3-LICENSE.txt'
Plan-File $dav1dLicense 'licenses\dav1d-COPYING.txt'
Plan-File $dav1dStamp 'licenses\dav1d-sdk.json'
Plan-File (Join-Path $runtimeRoot 'third_party\dr_flac-LICENSE.txt') 'licenses\dr_flac-LICENSE.txt'
Plan-File (Join-Path $runtimeRoot 'third_party\dr_flac-pin.json') 'licenses\dr_flac-pin.json'
Plan-File (Join-Path $workspace 'README-package.md') 'README-package.md'
foreach ($name in $shaderNames + @('spirv-manifest.json')) { Plan-File (Join-Path $shaders $name) ('bin\shaders\' + $name) }
$root = $assets; $relativeRoot = 'assets'
Assert-NoLinks $root
# Enumerate one level at a time so junctions are rejected before recursion.
$pending = [Collections.Generic.Queue[string]]::new(); $pending.Enqueue($root)
while ($pending.Count) {
    $folder = $pending.Dequeue()
    foreach ($entry in Get-ChildItem -LiteralPath $folder -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked package input: $($entry.FullName)" }
        if ($entry.Name.StartsWith('.')) { throw "Incomplete/hidden generated input: $($entry.FullName)" }
        if ($entry.PSIsContainer) { $pending.Enqueue($entry.FullName); continue }
        if ($relativeRoot -eq 'assets' -and $entry.Extension -in '.pc', '.bik', '.exe', '.dll', '.pdb', '.obj') { throw "Original binary/build output is not a runtime asset: $($entry.FullName)" }
        Plan-File $entry.FullName ($relativeRoot + '\' + $entry.FullName.Substring($root.TrimEnd('\').Length + 1))
    }
}
# All native provenance is already included in the sealed assets. Keep unrelated research out of the package.
# Reuse the complete native file-set/hash validator before creating any output.
& python -B $assetVerifier --inputs $nativeInputs --output $buildRoot --verify
if ($LASTEXITCODE -ne 0) { throw 'Sealed Dreamcast asset verification failed; no package output was created.' }
if ($nativeSealHash -cne (Get-FileHash -LiteralPath $nativeSeal -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'Dreamcast import seal changed during verification.' }
$nativeHashes = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
foreach ($record in $native.files) { $nativeHashes.Add([string]$record.path, [string]$record.sha256) }
# CreateDirectoryW is exclusive: even concurrent invocations cannot reuse an output.
if (!('EvilDeadPackageDirectory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EvilDeadPackageDirectory {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool CreateDirectory(string path, IntPtr attributes);
}
'@
}
$parent = [IO.Directory]::GetParent($destination).FullName
Assert-NoLinks $parent
[IO.Directory]::CreateDirectory($parent) | Out-Null
if (![EvilDeadPackageDirectory]::CreateDirectory($destination, [IntPtr]::Zero)) { throw "Cannot exclusively create output: $destination (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
function Write-Text([string]$Relative, [string]$Text) {
    $path = Join-Path $destination $Relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    try { $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text); $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
}
Write-Text 'INCOMPLETE.txt' 'Packaging has not completed. Do not run this folder until package-manifest.json exists.'
$manifest = [Collections.Generic.List[object]]::new()
foreach ($file in $files) {
    $target = Join-Path $destination $file.Relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::Copy($file.Source, $target, $false)
    $expected = if ($file.Relative.StartsWith('assets\', [StringComparison]::Ordinal)) {
        $name = $file.Relative.Substring(7).Replace('\', '/')
        if ($name -ceq 'dreamcast-runtime.json') { $nativeSealHash } else { $nativeHashes[$name] }
    } else { (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash.ToLowerInvariant() }
    $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) { throw "Copied bytes differ: $($file.Relative)" }
    $manifest.Add([pscustomobject]@{ file = $file.Relative.Replace('\', '/'); bytes = (Get-Item -LiteralPath $target).Length; sha256 = $actual })
}
Write-Text 'run.cmd' "@echo off`r`n`"%~dp0bin\evil-dead-scene.exe`" --assets `"%~dp0assets`" %*`r`n"
Write-Text 'NOTICE.txt' @"
Evil Dead: Hail to the King - local modern-engine prototype
Original resource platform: Sega Dreamcast, from this user's original disc.
Original extraction manifest SHA256: $($native.source_manifest_sha256)
Original LEVELA.JDC SHA256: $($native.original_script_sha256.A)
Original LEVELB.JDC SHA256: $($native.original_script_sha256.B)

Original backgrounds, models, fonts, scripts, sounds and movie content retain
their original copyright. This folder contains locally generated runtime assets
from this user's copy, with derived Russian text; no ownership of the original content is claimed.
Original LEVELA.JDC bytes are preserved as assets/scripts/levelA.jpc.
Original LEVELB.JDC bytes are preserved as assets/levelB/scripts/levelB.jpc.
The complete native import seal is assets/dreamcast-runtime.json; verified-import
describes asset conversion and integrity, not gameplay completion or console raster parity.
SDL3's exact bundled license is in licenses/SDL3-LICENSE.txt.
dr_flac is the statically linked lossless FLAC decoder. Its license and exact upstream commit are in licenses/dr_flac-LICENSE.txt and licenses/dr_flac-pin.json.
dav1d1.5.2 is the statically linked AV1 decoder, licensed under BSD-2-Clause.
Its exact notice is licenses/dav1d-COPYING.txt; the pinned SDK hashes are licenses/dav1d-sdk.json.
Microsoft CRT libraries are copied from the installed Visual Studio Redist tree.
FFmpeg and NihAV were external offline import tools; neither is included or linked into this runtime.
Native resource hashes, original/derived distinctions and conversion evidence are in
the sealed assets source metadata and assets/provenance/import-inputs.json.
"@
$crtNotice = @('Microsoft Visual C++ runtime - app-local x64 libraries', 'Source: installed Visual Studio VC/Redist/MSVC tree',
    'https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp', 'https://visualstudio.microsoft.com/license-terms/')
foreach ($file in $crtFiles) { $info = (Get-Item -LiteralPath $file).VersionInfo; $crtNotice += "$([IO.Path]::GetFileName($file)) $($info.FileVersion) $($info.LegalCopyright)" }
Write-Text 'licenses\MSVC-runtime.txt' ($crtNotice -join "`r`n")
foreach ($relative in @('run.cmd', 'NOTICE.txt', 'licenses\MSVC-runtime.txt')) {
    $target = Join-Path $destination $relative
    $manifest.Add([pscustomobject]@{ file = $relative.Replace('\', '/'); bytes = (Get-Item -LiteralPath $target).Length;
        sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() })
}
Write-Text 'package-manifest.json' (([ordered]@{ format = 'EDPACKAGE1'; created_utc = [DateTime]::UtcNow.ToString('o'); architecture = 'x64';
    entrypoint = 'run.cmd'; sdl_version = (Get-Item -LiteralPath $sdl).VersionInfo.FileVersion; files = $manifest.ToArray() } | ConvertTo-Json -Depth 6) + "`n")
# This is our one explicitly owned marker, never a recursive cleanup.
[IO.File]::Delete((Join-Path $destination 'INCOMPLETE.txt'))
Write-Output $destination
