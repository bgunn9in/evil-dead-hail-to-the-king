# Builds an altered SDL 3.4.18 SDK; upstream LICENSE.txt is retained in install/licenses/SDL3.
param([string]$Directory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build/dependencies/SDL3-3.4.18-vulkan'))
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$Directory = [IO.Path]::GetFullPath($Directory)
$version = '3.4.18'
$archiveHash = '9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3'
$patch = Join-Path $PSScriptRoot "SDL3-$version-vulkan.patch"
$patchHash = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
$install = Join-Path $Directory 'install'
$stampPath = Join-Path $Directory 'vulkan-sdk-stamp.json'
$options = @('-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TEST_LIBRARY=OFF', '-DSDL_TESTS=OFF', '-DSDL_EXAMPLES=OFF', '-DSDL_GPU_VULKAN_ONLY=ON', '-DSDL_RENDER_D3D=OFF', '-DSDL_RENDER_D3D11=OFF', '-DSDL_RENDER_D3D12=OFF', '-DSDL_OPENGL=OFF', '-DSDL_OPENGLES=OFF', '-DSDL_METAL=OFF')
$fingerprint = "$version|x64|$archiveHash|$patchHash|$($options -join '|')"
if (Test-Path -LiteralPath $stampPath) {
    $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
    $valid = $stamp.fingerprint -eq $fingerprint -and $stamp.files.Count -gt 0
    foreach ($file in $stamp.files) {
        $path = Join-Path $install $file.path
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { $valid = $false; break }
    }
    if ($valid) { Write-Host 'Using verified cached SDL Vulkan SDK'; Write-Output (Join-Path $install 'cmake'); return }
}
New-Item -ItemType Directory -Path $Directory -Force | Out-Null
$archive = Join-Path $Directory "SDL3-$version.tar.gz"
if (!(Test-Path -LiteralPath $archive)) {
    $download = "$archive.download"
    Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL3-$version.tar.gz" -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveHash) { throw 'Official SDL archive hash mismatch' }
    Move-Item -LiteralPath $download -Destination $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveHash) { throw 'Cached SDL archive hash mismatch' }
$source = Join-Path $Directory "SDL3-$version"
if (!(Test-Path -LiteralPath $source)) {
    & tar.exe -xzf $archive -C $Directory
    if ($LASTEXITCODE -ne 0) { throw 'SDL source extraction failed' }
}
& git.exe -c core.autocrlf=false -C $source apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    & git.exe -c core.autocrlf=false -C $source apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'SDL source does not match the pinned vendor patch' }
    & git.exe -c core.autocrlf=false -C $source apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'SDL vendor patch failed' }
}
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (!$vs) { throw 'Visual Studio x64 C++ build tools are required' }
$devcmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$environment = & $env:ComSpec /d /s /c ('call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && set' -f $devcmd)
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio x64 environment failed' }
foreach ($line in $environment) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$build = Join-Path $Directory 'build'
& $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_INSTALL_PREFIX=$install" @options | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'SDL configuration failed' }
& $cmake --build $build --parallel 12 | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'SDL build failed' }
& $cmake --install $build | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'SDL installation failed' }
$config = Get-Content -LiteralPath (Join-Path $build 'include-config-release/build_config/SDL_build_config.h') -Raw
if ($config -notmatch '(?m)^#define SDL_GPU_VULKAN 1\r?$' -or $config -match '(?m)^#define SDL_GPU_(D3D12|METAL) 1\r?$') { throw 'Unexpected SDL GPU configuration' }
$commands = Get-Content -LiteralPath (Join-Path $build 'compile_commands.json') -Raw
if ($commands -match '/src/(gpu/d3d12|render/direct3d12)/') { throw 'Excluded SDL backend source is in the compilation' }
$files = @(Get-ChildItem -LiteralPath $install -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($install.Length + 1).Replace('\','/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
@{ fingerprint = $fingerprint; files = $files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $stampPath -Encoding utf8
Write-Output (Join-Path $install 'cmake')
