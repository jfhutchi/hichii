# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 7.2
# Private two-stage experiment; default Plan performs no writes or builds.
[CmdletBinding()]
param(
    [ValidateSet('Plan','Generate','Merge','Use')][string]$Stage = 'Plan',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$RunName = 'oot-pgo-7ab5ff666-20261109',
    [ValidatePattern('^[a-f0-9]{40}$')][string]$Revision = '7ab5ff66688cc6603bedb23d0fda2cc2e6e2699a',
    [int[]]$TrainingProcessIds = @(),
    [ValidateRange(1,6)][int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspace = 'C:\Users\JHutc\Desktop\lichii-windows-x86_64'
$reference = Join-Path $workspace 'updates\latest-comparison\lichii-source'
$experiment = [IO.Path]::GetFullPath((Join-Path $workspace "updates\$RunName"))
$source = Join-Path $experiment 'source'
$generateBuild = Join-Path $experiment 'build-generate'
$useBuild = Join-Path $experiment 'build-use'
$raw = Join-Path $experiment 'profiles\gameplay'
$probeRaw = Join-Path $experiment 'profiles\configure'
$profile = Join-Path $experiment 'profiles\lichii-fresh.profdata'
$manifest = Join-Path $experiment 'profiles\merge-manifest.json'
$toolsRoot = Join-Path $workspace 'updates\toolchain'
$llvm = Join-Path $toolsRoot 'llvm-22.1.8\bin'
$compiler = Join-Path $llvm 'clang-cl.exe'
$profdata = Join-Path $llvm 'llvm-profdata.exe'
$runtime = Join-Path $toolsRoot 'llvm-22.1.8\lib\clang\22\lib\windows'
$qt = Join-Path $toolsRoot 'qt\6.8.3\msvc2022_64'
$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
$python = 'C:\Users\JHutc\AppData\Local\Programs\Python\Python312\python.exe'
$glslang = Join-Path $toolsRoot 'glslang-16.6.0\bin\glslang.exe'

if ($Stage -eq 'Plan') {
    [pscustomobject]@{
        Revision=$revision; Source=$source; GenerateBuild=$generateBuild; UseBuild=$useBuild
        GameplayProfilePattern=(Join-Path $raw 'lichii-%m-%p.profraw'); MergedProfile=$profile
        GenerateFlags='-flto=thin -fprofile-generate=<configure directory> -fprofile-update=atomic'
        UseFlags='-flto=thin -fprofile-use=<merged profile>'; Jobs=$Jobs
    } | Format-List
    return
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Assert-Source {
    $head = & git -C $source rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $head -ne $revision) { throw 'Experiment source revision mismatch.' }
    $dirty = & git -C $source status --porcelain=v1
    if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Experiment source has changes; inspect before proceeding.' }
}
function Save-Json([string]$Path, $Value) {
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8
}

if (Get-Process lichii,hookshot -ErrorAction SilentlyContinue) {
    throw 'Close game processes before building or merging their final training profiles.'
}

foreach ($file in @($compiler,$profdata,$cmake,$ninja,$vcvars,$python,$glslang,
                   (Join-Path $runtime 'clang_rt.profile-x86_64.lib'),
                   (Join-Path $qt 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing dependency: $file" }
}
if (-not (Test-Path -LiteralPath $source)) {
    if ($Stage -ne 'Generate') { throw 'Run Generate first to create the isolated checkout.' }
    if (Test-Path -LiteralPath $experiment) {
        throw 'Experiment directory exists without a checkout. Inspect it or choose a new RunName.'
    }
    New-Item -ItemType Directory -Path $experiment | Out-Null
    # No alternates or hard links: the source and objects remain independent of the benchmark tree.
    Invoke-Checked git @('clone','--local','--no-hardlinks','--no-checkout',$reference,$source)
    Invoke-Checked git @('-C',$source,'-c','core.longpaths=true','checkout','--detach',$revision)
    # Reuse a private copy of downloaded dependency SOURCES, never compiled objects or profiles.
    $referenceCache = Join-Path $reference '.cache\cpm'
    if (Test-Path -LiteralPath $referenceCache -PathType Container) {
        New-Item -ItemType Directory -Path (Join-Path $source '.cache') -Force | Out-Null
        Copy-Item -LiteralPath $referenceCache -Destination (Join-Path $source '.cache\cpm') -Recurse
    }
}
Assert-Source
New-Item -ItemType Directory -Path $raw,$probeRaw -Force | Out-Null
if ($Stage -eq 'Generate' -and
    ((Test-Path -LiteralPath $profile) -or
     @(Get-ChildItem -LiteralPath $raw -Filter 'lichii-*.profraw' -File).Count -gt 0)) {
    throw 'This run already has gameplay profiles. Use a new RunName to rebuild the instrumented executable.'
}

if ($Stage -eq 'Merge') {
    if ((Test-Path -LiteralPath $profile) -or (Test-Path -LiteralPath $manifest)) {
        throw 'Merged profiles are immutable per RunName. Choose a new run for another training set.'
    }
    $trainingRecordPath = Join-Path $experiment 'Generate-build.json'
    if (-not (Test-Path -LiteralPath $trainingRecordPath)) { throw 'No completed instrumented build manifest.' }
    $training = Get-Content -LiteralPath $trainingRecordPath -Raw | ConvertFrom-Json
    if ($training.Revision -ne $revision -or
        $training.CompilerSHA256 -ne (Get-FileHash $compiler).Hash -or
        $training.SHA256 -ne (Get-FileHash -LiteralPath $training.Binary).Hash) {
        throw 'The instrumented binary, compiler or source differs from its build manifest.'
    }
    if ($TrainingProcessIds.Count -eq 0 -or @($TrainingProcessIds | Where-Object {$_ -le 0}).Count) {
        throw 'Specify the actual main gameplay process IDs with -TrainingProcessIds.'
    }
    $allInputs = @(Get-ChildItem -LiteralPath $raw -Filter 'lichii-*.profraw' -File | Sort-Object Name)
    $inputs = @()
    $excludedInputs = @()
    foreach ($inputFile in $allInputs) {
        if ($inputFile.Name -notmatch '-(\d+)\.profraw$') {throw 'Unrecognized profile producer filename'}
        $producer = [int]$Matches[1]
        if ($producer -in $TrainingProcessIds) {
            $inputs += $inputFile
        } else {
            $excludedInputs += [pscustomobject]@{Path=$inputFile.FullName;ProcessId=$producer;SHA256=(Get-FileHash -LiteralPath $inputFile.FullName).Hash;Reason='Not one of the selected main gameplay processes'}
        }
    }
    if ($inputs.Count -eq 0) { throw 'No gameplay profiles. Quit the instrumented emulator normally first.' }
    if (@($inputs | Where-Object Length -eq 0).Count) { throw 'Empty gameplay profile found.' }
    $before = @($inputs | ForEach-Object {
        [pscustomobject]@{Path=$_.FullName; Bytes=$_.Length; SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    })
    $inputList = Join-Path $experiment 'profiles\merge-inputs.txt'
    [IO.File]::WriteAllLines($inputList, [string[]]$inputs.FullName, [Text.UTF8Encoding]::new($false))
    # Preserve zero-count functions for optimization; do not use --sparse or blend the old profile.
    Invoke-Checked $profdata @('merge','--num-threads=1','--failure-mode=any',"--input-files=$inputList",'-o',$profile)
    foreach ($entry in $before) {
        if ((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.SHA256) {
            throw 'A raw profile changed during merge. Stop training and use a new RunName.'
        }
    }
    $summary = & $profdata show --all-functions $profile
    if ($LASTEXITCODE -ne 0) { throw 'Merged profile could not be read by LLVM 22.' }
    if (-not ($summary -match '^\s*\?')) { throw 'No MSVC-ABI C++ symbols in merged profile.' }
    $summary | Set-Content -LiteralPath (Join-Path $experiment 'profiles\profile-summary.txt') -Encoding utf8
    Save-Json $manifest ([ordered]@{
        Revision=$revision; Compiler=$compiler; CompilerSHA256=(Get-FileHash $compiler).Hash
        ProfileTool=$profdata; ProfileToolSHA256=(Get-FileHash $profdata).Hash
        TrainingBinarySHA256=$training.SHA256; Inputs=$before; Profile=$profile
        TrainingProcessIds=$TrainingProcessIds; ExcludedInputs=$excludedInputs
        ProfileSHA256=(Get-FileHash $profile).Hash; MergedUtc=[DateTime]::UtcNow.ToString('o')
    })
    Write-Output "Merged $($inputs.Count) private gameplay profiles into $profile"
    return
}

if ($Stage -eq 'Use') {
    if (-not (Test-Path -LiteralPath $manifest)) { throw 'Run Merge before Use.' }
    $record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($record.Revision -ne $revision -or
        $record.CompilerSHA256 -ne (Get-FileHash $compiler).Hash -or
        $record.ProfileToolSHA256 -ne (Get-FileHash $profdata).Hash -or
        $record.ProfileSHA256 -ne (Get-FileHash $profile).Hash) {
        throw 'Source, compiler or merged-profile identity does not match the training manifest.'
    }
}

$buildEnvironment = & $env:ComSpec /d /c "`"$vcvars`" x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not load the VS2022 build environment.' }
foreach ($line in $buildEnvironment) {
    if ($line -match '^(.*?)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
if ($env:VCToolsVersion.TrimEnd('\') -ne '14.44.35207') { throw "Unexpected VS headers: $env:VCToolsVersion" }
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
$env:PATH = $llvm + ';' + (Split-Path $python) + ';' + $env:PATH
$compilerVersion = & $compiler --version
if ($LASTEXITCODE -ne 0 -or -not ($compilerVersion -match 'clang version 22\.1\.8')) {
    throw 'This experiment requires clang-cl 22.1.8.'
}
# Configure probes must never enter the gameplay training set.
$env:LLVM_PROFILE_FILE = (Join-Path $probeRaw 'probe-%m-%p.profraw').Replace('\','/')
$commonFlags = '-DWIN32 -D_WINDOWS -DNOMINMAX -flto=thin'
$linkFlags = '/MAP'
if ($Stage -eq 'Generate') {
    $build = $generateBuild
    $flags = "$commonFlags -fprofile-generate=$($probeRaw.Replace('\','/')) -fprofile-update=atomic"
    # clang-cl objects auto-link this runtime; CMake invokes lld-link directly, so supply its directory.
    $linkFlags += " /LIBPATH:$($runtime.Replace('\','/'))"
} else {
    $build = $useBuild
    $flags = "$commonFlags -fprofile-use=$($profile.Replace('\','/')) -Wno-error=profile-instr-out-of-date -Wno-error=profile-instr-unprofiled -Wno-error=backend-plugin"
}
$arguments = @(
    '-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$ninja","-DCMAKE_C_COMPILER=$compiler","-DCMAKE_CXX_COMPILER=$compiler",
    "-DCMAKE_LINKER=$(Join-Path $llvm 'lld-link.exe')","-DCMAKE_AR=$(Join-Path $llvm 'llvm-lib.exe')",
    "-DCMAKE_C_FLAGS=$flags","-DCMAKE_CXX_FLAGS=-EHsc $flags",
    "-DCMAKE_EXE_LINKER_FLAGS=$linkFlags","-DCMAKE_SHARED_LINKER_FLAGS=$linkFlags","-DCMAKE_MODULE_LINKER_FLAGS=$linkFlags",
    "-DCMAKE_PREFIX_PATH=$qt","-DPython_EXECUTABLE=$python","-DPYTHON_EXECUTABLE=$python",
    "-DGLSLANGVALIDATOR=$glslang","-DCPM_SOURCE_CACHE=$(Join-Path $source '.cache\cpm')",
    '-DBUILD_TESTING=ON','-DYUZU_TESTS=ON','-DENABLE_QT=ON','-DYUZU_CMD=ON',
    '-DYUZU_ROOM=OFF','-DYUZU_ROOM_STANDALONE=OFF','-DYUZU_USE_BUNDLED_QT=OFF',
    '-DYUZU_USE_BUNDLED_SDL3=ON','-DVulkanHeaders_FORCE_BUNDLED=ON',
    '-DVulkanUtilityLibraries_FORCE_BUNDLED=ON','-DENABLE_UPDATE_CHECKER=OFF',
    '-DUSE_DISCORD_PRESENCE=OFF','-DUSE_CCACHE=OFF','-DENABLE_WERROR=ON','-DENABLE_LTO=OFF'
)
# ENABLE_LTO remains OFF as in the reference build: explicit -flto=thin controls LTO.
Save-Json (Join-Path $experiment "$Stage-configure.json") ([ordered]@{
    Revision=$revision; Stage=$Stage; Arguments=$arguments; Jobs=$Jobs
    CompilerSHA256=(Get-FileHash $compiler).Hash; VCToolsVersion=$env:VCToolsVersion
    WindowsSDKVersion=$env:WindowsSDKVersion
    StartedUtc=[DateTime]::UtcNow.ToString('o')
})
$log = Join-Path $experiment "$Stage-build.log"
& $cmake @arguments 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "Configuration failed: $log" }
[string[]]$targets = if ($Stage -eq 'Generate') { @('yuzu') } else { @('yuzu','tests') }
& $cmake --build $build --target @targets --parallel $Jobs 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Build failed: $log" }
Assert-Source
$binary = Join-Path $build 'bin\lichii.exe'
Save-Json (Join-Path $experiment "$Stage-build.json") ([ordered]@{
    Revision=$revision; Stage=$Stage; Binary=$binary; SHA256=(Get-FileHash $binary).Hash
    CompilerSHA256=(Get-FileHash $compiler).Hash
    FinishedUtc=[DateTime]::UtcNow.ToString('o')
})
Write-Output "Built $binary; tests and gameplay have NOT been run."
if ($Stage -eq 'Generate') {
    Write-Output "Stage a separate portable runtime with a copied user directory, then train with LLVM_PROFILE_FILE=$raw\lichii-%m-%p.profraw. Quit normally."
} else {
    Write-Output "Next: run $build\bin\tests.exe, then matched fresh-launch comparisons against the old-profile build."
}
