param([string]$Rom = '')
$ErrorActionPreference = 'Stop'
$cmake = Join-Path $PSScriptRoot 'tools\python\cmake\data\bin\cmake.exe'
$ninja = Join-Path $PSScriptRoot 'tools\python\bin\ninja.exe'
$build = Join-Path $PSScriptRoot 'build-game'
$python = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python -ErrorAction Stop).Source }
$compilerRoot = $PSScriptRoot.Replace('\', '/')
$configure = @('-S', $PSScriptRoot, '-B', $build, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_C_COMPILER=$compilerRoot/tools/zig-cc.cmd", "-DCMAKE_CXX_COMPILER=$compilerRoot/tools/zig-cxx.cmd",
    "-DCMAKE_AR=$compilerRoot/tools/zig-ar.cmd", "-DCMAKE_RANLIB=$compilerRoot/tools/zig-ranlib.cmd",
    "-DCMAKE_RC_COMPILER=$compilerRoot/tools/zig-rc.cmd",
    '-DCMAKE_RC_COMPILE_OBJECT=<CMAKE_RC_COMPILER> <DEFINES> <INCLUDES> /fo <OBJECT> <SOURCE>',
    "-DPython3_EXECUTABLE=$python", '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-DNTDDI_VERSION=0x0A000000',
    '-DCMAKE_CXX_FLAGS=-DNTDDI_VERSION=0x0A000000', '-DGALAXY_BUILD_GAME=ON')
# Reuse the pinned dependency sources already downloaded by the platform build.
if ($Rom) { $configure += "-DGALAXY_ROM_PATH=$Rom" }
$dependencyCache = Join-Path $PSScriptRoot 'build-platform\_deps'
if (Test-Path -LiteralPath $dependencyCache) { Get-ChildItem $dependencyCache -Directory -Filter '*-src' | ForEach-Object {
    $dependency = $_.Name.Substring(0, $_.Name.Length - 4).ToUpperInvariant()
    $configure += "-DFETCHCONTENT_SOURCE_DIR_${dependency}=$($_.FullName)"
}
}
& $cmake @configure
if ($LASTEXITCODE) { throw 'Game build configuration failed' }
& $cmake --build $build --target galaxy galaxy_portable --parallel 4 -- -k 0
if ($LASTEXITCODE) { throw 'Native game build failed; see the compiler output above' }
