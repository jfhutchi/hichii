param(
    [ValidateSet('BaselineTests','Candidate','Committed')][string]$Phase,
    [string]$ExpectedCommit = '7ab5ff66688cc6603bedb23d0fda2cc2e6e2699a',
    [string]$FocusFilter = '[maxwell_state_write_regression]',
    [ValidatePattern('^[a-z0-9-]+$')][string]$LogPrefix = 'state-dispatch',
    [switch]$SkipMatrix
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $root 'updates/latest-comparison/lichii-source'
$build = Join-Path $source 'build-latest'
$out = Join-Path $root 'diagnostics/2026-11-09-emulator-fix'
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$llvm = Join-Path $root 'updates/toolchain/llvm-22.1.8/bin'
if (Get-Process lichii,hookshot -ErrorAction SilentlyContinue) {
    throw 'Stop game benchmarks before building this isolated candidate.'
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $ExpectedCommit) { throw 'Unexpected source revision.' }
$changed = @(& git -C $source diff --name-only HEAD)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect source changes.' }
$allowed = @('src/tests/video_core/maxwell_fast_method.cpp')
if ($Phase -eq 'Candidate') {
    $allowed += @('src/video_core/engines/maxwell_3d.cpp','src/video_core/engines/maxwell_3d.h')
}
if ($Phase -eq 'Committed') { $allowed = @() }
if (@($changed | Where-Object { $_ -notin $allowed }).Count -ne 0) {
    throw 'Unrelated source changes must be reviewed before building.'
}
$profile = 'E:/Documents/GitHub/hichii/lichii-windows.profdata'
if ((Get-FileHash -LiteralPath $profile).Hash -ne 'A6BB81A2E65F35236749388647DE834A42AB05A0BFBE077ECBF70B632A9E7A77') {
    throw 'The matched old PGO input changed.'
}
$cache = Get-Content -Raw -LiteralPath (Join-Path $build 'CMakeCache.txt')
if ($cache -notmatch 'CMAKE_CXX_FLAGS:STRING=.*-flto=thin.*-fprofile-use=E:/Documents/GitHub/hichii/lichii-windows.profdata') {
    throw 'Persistent compiler flags no longer match the comparison.'
}
$vcvars = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvarsall.bat'
$buildEnvironment = & $env:ComSpec /d /c "`"$vcvars`" x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'VS2022 build environment failed.' }
foreach ($line in $buildEnvironment) {
    if ($line -match '^(.*?)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
if ($env:VCToolsVersion.TrimEnd('\') -ne '14.44.35207') { throw 'Unexpected VS headers.' }
$env:PATH = $llvm + ';' + $env:PATH
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$log = Join-Path $out ($LogPrefix + '-' + $Phase + '.log')
$started = [DateTime]::UtcNow.ToString('o')
& $cmake -S $source -B $build 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Configure failed: $log" }
$targets = @('tests')
if ($Phase -ne 'BaselineTests') { $targets += 'yuzu' }
& $cmake --build $build --target @targets --parallel 6 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Build failed: $log" }
$tests = Join-Path $build 'bin/tests.exe'
& $tests $FocusFilter 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Preservation tests failed.' }
if ($Phase -ne 'BaselineTests') {
    if (-not $SkipMatrix) {
    $priorSkip = [Environment]::GetEnvironmentVariable('EDEN_NX2_SKIP_SAME','Process')
    $priorVerify = [Environment]::GetEnvironmentVariable('EDEN_NX2_PERF_VERIFY','Process')
    try {
        foreach ($skip in @('0','1')) {
            foreach ($verify in @('0','1')) {
                $env:EDEN_NX2_SKIP_SAME = $skip
                $env:EDEN_NX2_PERF_VERIFY = $verify
                "Matrix SKIP_SAME=$skip PERF_VERIFY=$verify" | Tee-Object -FilePath $log -Append
                & $tests '[maxwell_fast_method],[gpu_split_dispatch],[gpu_split_dependencies]' 2>&1 | Tee-Object -FilePath $log -Append
                if ($LASTEXITCODE -ne 0) { throw 'State/dispatch test matrix failed.' }
            }
        }
    } finally {
        [Environment]::SetEnvironmentVariable('EDEN_NX2_SKIP_SAME',$priorSkip,'Process')
        [Environment]::SetEnvironmentVariable('EDEN_NX2_PERF_VERIFY',$priorVerify,'Process')
    }
    }
    & $tests 2>&1 | Tee-Object -FilePath $log -Append
    if ($LASTEXITCODE -ne 0) { throw 'Full unit suite failed.' }
}
[pscustomobject]@{Phase=$Phase;BaseCommit=$head;ChangedFiles=$changed;FocusFilter=$FocusFilter;MatrixSkipped=[bool]$SkipMatrix;StartedUTC=$started;CompletedUTC=[DateTime]::UtcNow.ToString('o');Jobs=6;OldProfileSHA256=(Get-FileHash -LiteralPath $profile).Hash;TestsSHA256=(Get-FileHash -LiteralPath $tests).Hash} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out ($LogPrefix + '-' + $Phase + '-validation.json'))
