param(
    [string]$Runtime = 'updates/oot-stutter-20261109/baseline',
    [switch]$Trace
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (Get-Process lichii,hookshot -ErrorAction SilentlyContinue) { throw 'Another game is running' }
$folder = [IO.Path]::GetFullPath((Join-Path $root $Runtime))
$out = Join-Path $root 'diagnostics/2026-11-09-emulator-fix'
$prefix = Join-Path $out ('run-' + (Get-Date -Format 'HHmmssfff'))
$game = @(Get-ChildItem -LiteralPath (Join-Path $root 'The Legend of Zelda Ocarina of Time [0400486024dec000][v131072]') -Filter '*.nup' -File)
if ($game.Count -ne 1) { throw 'Expected one local Zelda container' }
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = Join-Path $folder 'lichii.exe'
if (-not (Test-Path -LiteralPath $info.FileName -PathType Leaf)) { throw 'Runtime executable is missing' }
$info.WorkingDirectory = $folder
$info.UseShellExecute = $false
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.Arguments = '-f -g "' + $game[0].FullName + '"'
foreach ($name in @($info.Environment.Keys | Where-Object { $_ -match '^(HOS_|RX_|EDEN_NX2_|EDEN_DKB_|HOOKSHOT_)' })) { $info.Environment.Remove($name) | Out-Null }
$info.Environment['EDEN_NX2_DEBUG_SERVER'] = '127.0.0.1:6545'
$info.Environment['EDEN_NX2_DEBUG_DIR'] = $out
$info.Environment['EDEN_NX2_SPLIT'] = '2'
$info.Environment['EDEN_NX2_FENCE_BATCH'] = '1'
$info.Environment['EDEN_NX2_PRELOAD_ALL_PIPELINES'] = '1'
$info.Environment['EDEN_NX2_DLSS_REPLACEMENT'] = 'native'
if ($Trace) {
    $info.Environment['EDEN_NX2_STUTTER_TRACE'] = '1'
    $info.Environment['EDEN_NX2_GPU_PROFILE'] = '1'
    $info.Environment['EDEN_NX2_PIPELINE_BUILD_TRACE'] = $prefix + '-pipelines.log'
    $info.Environment['EDEN_NX2_FS_READ_TRACE'] = '1'
    $info.Environment['EDEN_NX2_HOSTTIMING_STATS'] = '1'
}
$stdout = [IO.FileStream]::new($prefix + '.stdout.log', [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
$stderr = [IO.FileStream]::new($prefix + '.stderr.log', [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
try {
    $process = [Diagnostics.Process]::Start($info)
    $output = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
    $errors = $process.StandardError.BaseStream.CopyToAsync($stderr)
    $environment = @{}
    foreach ($name in $info.Environment.Keys | Where-Object { $_ -match '^EDEN_' }) { $environment[$name] = $info.Environment[$name] }
    [pscustomobject]@{PID=$process.Id;Prefix=$prefix;Executable=$info.FileName;SHA256=(Get-FileHash -LiteralPath $info.FileName).Hash;Trace=[bool]$Trace;Environment=$environment;StartedUTC=[DateTime]::UtcNow.ToString('o');Purpose='Isolated controlled diagnosis; trace-on timing is not final performance validation'} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'active-game.json')
    Write-Output "Started isolated Lichii PID $($process.Id), trace=$Trace"
    $process.WaitForExit()
    $output.GetAwaiter().GetResult()
    $errors.GetAwaiter().GetResult()
    [pscustomobject]@{PID=$process.Id;ExitCode=$process.ExitCode;ExitedUTC=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath ($prefix + '-exit.json')
    if ($process.ExitCode -ne 0) { throw "Game exited $($process.ExitCode); inspect $prefix.stderr.log" }
} finally {
    $stdout.Dispose()
    $stderr.Dispose()
}
