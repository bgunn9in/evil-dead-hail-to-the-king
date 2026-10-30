# Official dav1d 1.5.2, static 8-bit AV1 decoder; dependencies stay in this workspace.
param(
    [string]$Directory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build/dependencies/dav1d-1.5.2'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$Directory = [IO.Path]::GetFullPath($Directory)
$install = Join-Path $Directory 'install'
$stampPath = Join-Path $Directory 'sdk-stamp.json'
$options = @('--buildtype=release', '-Ddefault_library=static', '-Db_vscrt=md', '-Dbitdepths=8', '-Denable_tools=true', '-Denable_tests=false', '-Denable_docs=false', '-Dxxhash_muxer=disabled')
$fingerprint = "dav1d1.5.2|x64|MSVC|$($options -join '|')"
if (Test-Path -LiteralPath $stampPath) {
    $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
    $valid = $stamp.fingerprint -eq $fingerprint -and $stamp.archive_sha256 -eq '2fc0810b4cdf72784b3c107827ff10b1d83ec709a1ec1fbdbc6a932daf65ead6' -and $stamp.files.Count -gt 0
    foreach ($file in $stamp.files) {
        $path = Join-Path $install $file.path
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { $valid = $false; break }
    }
    if ($valid) { Write-Host 'Using verified cached dav1d SDK'; Write-Output $install; return }
}
New-Item -ItemType Directory -Path $Directory -Force | Out-Null
function Fetch([string]$Name, [string]$Url, [string]$Sha) {
    $path = Join-Path $Directory $Name
    if (!(Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -Uri $Url -OutFile "$path.download"
        if ((Get-FileHash -LiteralPath "$path.download" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha) { throw "Official $Name hash mismatch" }
        Move-Item -LiteralPath "$path.download" -Destination $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha) { throw "Cached $Name hash mismatch" }
    return $path
}
$archive = Fetch 'dav1d-1.5.2.tar.gz' 'https://github.com/videolan/dav1d/archive/refs/tags/1.5.2.tar.gz' '2fc0810b4cdf72784b3c107827ff10b1d83ec709a1ec1fbdbc6a932daf65ead6'
$nasm = Fetch 'nasm-2.16.03-win64.zip' 'https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip' '3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6'
$wheel = Fetch 'meson-1.9.1-py3-none-any.whl' 'https://files.pythonhosted.org/packages/9c/07/b48592d325cb86682829f05216e4efb2dc881762b8f1bafb48b57442307a/meson-1.9.1-py3-none-any.whl' 'f824ab770c041a202f532f69e114c971918ed2daff7ea56583d80642564598d0'
$source = Join-Path $Directory 'dav1d-1.5.2'
if (!(Test-Path -LiteralPath $source)) {
    & tar.exe -xzf $archive -C $Directory
    if ($LASTEXITCODE -ne 0) { throw 'dav1d extraction failed' }
}
if (!(Test-Path -LiteralPath (Join-Path $Directory 'nasm-2.16.03/nasm.exe'))) { Expand-Archive -LiteralPath $nasm -DestinationPath $Directory }
$meson = Join-Path $Directory 'meson-py'
if (!(Test-Path -LiteralPath (Join-Path $meson 'mesonbuild/mesonmain.py'))) {
    & $Python -m pip install --no-deps --target $meson $wheel
    if ($LASTEXITCODE -ne 0) { throw 'Workspace Meson installation failed' }
}
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (!$vs) { throw 'Visual Studio x64 C++ tools are required' }
$devcmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$environment = & $env:ComSpec /d /s /c ('call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && set' -f $devcmd)
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio x64 environment failed' }
foreach ($line in $environment) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }
$env:PYTHONPATH = $meson
$env:PATH = "$(Join-Path $Directory 'nasm-2.16.03');$env:PATH"
$build = Join-Path $Directory 'build'
& $Python -m mesonbuild.mesonmain setup $build $source "--prefix=$install" @options
if ($LASTEXITCODE -ne 0) { throw 'dav1d configuration failed' }
& $Python -m mesonbuild.mesonmain compile -C $build -j 8
if ($LASTEXITCODE -ne 0) { throw 'dav1d build failed' }
& $Python -m mesonbuild.mesonmain install -C $build
if ($LASTEXITCODE -ne 0) { throw 'dav1d installation failed' }
$licenses = Join-Path $install 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'COPYING') -Destination (Join-Path $licenses 'dav1d-COPYING.txt')
$files = @(Get-ChildItem -LiteralPath $install -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($install.Length + 1).Replace('\','/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
@{ fingerprint = $fingerprint; files = $files; source = 'https://github.com/videolan/dav1d/tree/1.5.2'; license = 'BSD-2-Clause'; archive_sha256 = '2fc0810b4cdf72784b3c107827ff10b1d83ec709a1ec1fbdbc6a932daf65ead6' } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $stampPath -Encoding utf8
Write-Output $install
