param(
    [Parameter(Mandatory=$true)][string]$OutputFile,
    [ValidateRange(1,900)][int]$Samples=35
)
$ErrorActionPreference='Stop'
$rows=[Collections.Generic.List[object]]::new()
$paths=@('\Processor Information(0,*)\Processor Frequency','\Processor Information(0,*)\% Processor Performance')
Get-Counter -Counter $paths -SampleInterval 1 -MaxSamples $Samples | ForEach-Object {
    $limits=& nvidia-smi --query-gpu=clocks_event_reasons.sw_power_cap,clocks_event_reasons.hw_thermal_slowdown,clocks_event_reasons.sw_thermal_slowdown --format=csv,noheader,nounits
    if($LASTEXITCODE -ne 0){throw 'GPU clock-limit query failed'}
    $rows.Add([pscustomobject]@{
        UTC=$_.Timestamp.ToUniversalTime().ToString('o')
        CPUCounters=@($_.CounterSamples | Select-Object Path,CookedValue)
        GPUClockLimits=$limits
    })
}
[pscustomobject]@{
    Method='Raw Windows Processor Information counters; no CPU temperature or hardware power telemetry'
    GPUClockLimitOrder='software power cap, hardware thermal slowdown, software thermal slowdown'
    Rows=$rows
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputFile -Encoding utf8
Write-Output "Recorded $($rows.Count) clock/limit samples."
