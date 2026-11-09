param([ValidateRange(1,12)][int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$source = 'C:\Users\JHutc\Desktop\lichii-windows-x86_64\updates\latest-comparison\lichii-source'
$build = Join-Path $source 'build-latest'
$workspace = 'C:\Users\JHutc\Desktop\lichii-windows-x86_64'
$toolsRoot = Join-Path $workspace 'updates\toolchain'
$llvm = Join-Path $toolsRoot 'llvm-22.1.8\bin'
$qt = Join-Path $toolsRoot 'qt\6.8.3\msvc2022_64'
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$python = 'C:\Users\JHutc\AppData\Local\Programs\Python\Python312\python.exe'
$compiler = Join-Path $llvm 'clang-cl.exe'
$glslang = Join-Path $toolsRoot 'glslang-16.6.0\bin\glslang.exe'
$profile = 'E:\Documents\GitHub\hichii\lichii-windows.profdata'
$log = Join-Path $workspace 'diagnostics\2026-11-09-emulator-fix\upstream-build.log'

foreach ($file in @($cmake,$ninja,$python,$compiler,$glslang,$profile,(Join-Path $qt 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    if (-not(Test-Path -LiteralPath $file)) { throw "Missing build dependency: $file" }
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne '7afad5494e650edfc169624be54e606f54b079ec') {
    throw 'Source revision differs from the pinned latest upstream build.'
}
$dirty = & git -C $source status --porcelain=v1
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Source checkout has changes; inspect them before building.' }

$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
if (-not(Test-Path -LiteralPath $vcvars)) { throw 'Visual Studio 2022 C++ build environment is missing.' }
$buildEnvironment = & $env:ComSpec /d /c "`"$vcvars`" x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not load the Visual Studio 2022 C++ environment.' }
foreach ($line in $buildEnvironment) {
    if ($line -match '^(.*?)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
if ($env:VCToolsVersion.TrimEnd('\') -ne '14.44.35207') {
    throw "Unexpected MSVC headers: $env:VCToolsVersion"
}
Write-Output "Using Visual Studio 2022 headers $env:VCToolsVersion with clang-cl 22.1.8."
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$env:PATH = $llvm + ';' + (Split-Path $python) + ';' + $env:PATH
$flags = '-DWIN32 -D_WINDOWS -DNOMINMAX -flto=thin -fprofile-use=' + $profile.Replace('\','/') + ' -Wno-error=profile-instr-out-of-date -Wno-error=profile-instr-unprofiled -Wno-error=backend-plugin'
$arguments = @(
    '-S',$source,'-B',$build,'-G','Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_C_COMPILER=$compiler", "-DCMAKE_CXX_COMPILER=$compiler",
    "-DCMAKE_LINKER=$(Join-Path $llvm 'lld-link.exe')",
    "-DCMAKE_AR=$(Join-Path $llvm 'llvm-lib.exe')",
    "-DCMAKE_C_FLAGS=$flags", "-DCMAKE_CXX_FLAGS=-EHsc $flags",
    "-DCMAKE_PREFIX_PATH=$qt", "-DPython_EXECUTABLE=$python", "-DPYTHON_EXECUTABLE=$python",
    "-DGLSLANGVALIDATOR=$glslang",
    '-DCPM_SOURCE_CACHE=E:/Documents/GitHub/hichii/.cache/cpm','-DBUILD_TESTING=ON','-DYUZU_TESTS=ON','-DENABLE_QT=ON','-DYUZU_CMD=ON',
    '-DYUZU_ROOM=OFF','-DYUZU_ROOM_STANDALONE=OFF','-DYUZU_USE_BUNDLED_QT=OFF',
    '-DYUZU_USE_BUNDLED_SDL3=ON','-DVulkanHeaders_FORCE_BUNDLED=ON',
    '-DVulkanUtilityLibraries_FORCE_BUNDLED=ON','-DENABLE_UPDATE_CHECKER=OFF',
    '-DUSE_DISCORD_PRESENCE=OFF','-DUSE_CCACHE=OFF','-DENABLE_WERROR=ON'
)
$ErrorActionPreference = 'Continue'
& $cmake @arguments 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed. See $log" }
& $cmake --build $build --target yuzu tests --parallel $Jobs 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Build failed. See $log" }
& (Join-Path $build 'bin\tests.exe') 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Unit tests failed. See $log" }
Write-Output "Build and tests completed at $head. Runtime comparison is still required."
