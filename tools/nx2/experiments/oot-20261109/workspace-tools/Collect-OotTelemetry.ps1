param(
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$OutputFile,
    [ValidateRange(1,1800)][int]$Seconds=60
)
$ErrorActionPreference='Stop'
$journal=$OutputFile+'.jsonl'
$journalStream=[IO.FileStream]::new($journal,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
$journalWriter=[IO.StreamWriter]::new($journalStream,[Text.UTF8Encoding]::new($false))
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OotThreadNames {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr text);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr ptr);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static string Name(uint id) {
        IntPtr h=OpenThread(0x800,false,id);
        if(h==IntPtr.Zero) return "unavailable";
        try {
            IntPtr p;
            if(GetThreadDescription(h,out p)<0) return "unavailable";
            try {return Marshal.PtrToStringUni(p);} finally {LocalFree(p);}
        } finally {CloseHandle(h);}
    }
}
public static class OotSystemMemory {
    [StructLayout(LayoutKind.Sequential)]
    struct PerformanceInformation {
        public uint Size;
        public UIntPtr CommitTotal, CommitLimit, CommitPeak;
        public UIntPtr PhysicalTotal, PhysicalAvailable, SystemCache;
        public UIntPtr KernelTotal, KernelPaged, KernelNonpaged, PageSize;
        public uint HandleCount, ProcessCount, ThreadCount;
    }
    public struct Sample {
        public ulong CommittedBytes, CommitLimitBytes, PhysicalTotalBytes, PhysicalAvailableBytes;
    }
    [DllImport("psapi.dll", SetLastError=true)]
    static extern bool GetPerformanceInfo(out PerformanceInformation info, uint size);
    public static Sample Read() {
        PerformanceInformation info;
        if (!GetPerformanceInfo(out info, (uint)Marshal.SizeOf(typeof(PerformanceInformation)))) {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        ulong page = info.PageSize.ToUInt64();
        return new Sample {
            CommittedBytes = info.CommitTotal.ToUInt64() * page,
            CommitLimitBytes = info.CommitLimit.ToUInt64() * page,
            PhysicalTotalBytes = info.PhysicalTotal.ToUInt64() * page,
            PhysicalAvailableBytes = info.PhysicalAvailable.ToUInt64() * page
        };
    }
}
'@
$rows=[Collections.Generic.List[object]]::new()
$prior=@{}
$collectorHash=(Get-FileHash -LiteralPath $PSCommandPath).Hash
$initialProcess=Get-Process -Id $ProcessId -ErrorAction Stop
$processStart=$initialProcess.StartTime.ToUniversalTime()
$endReason='Requested samples completed'
$clock=[Diagnostics.Stopwatch]::StartNew()
$previousTime=0.0
try {
for($i=0;$i -le $Seconds;$i++) {
    $process=Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if(-not $process) {
        $endReason='Target process exited; retained partial sampling period'
        Write-Output $endReason
        break
    }
    if($process.StartTime.ToUniversalTime() -ne $processStart) {throw 'Target PID was reused'}
    $now=$clock.Elapsed.TotalSeconds
    $threads=@($process.Threads | ForEach-Object {
        $threadId=$_.Id
        $cpu=$_.TotalProcessorTime.TotalMilliseconds
        $percent=if($prior.ContainsKey($threadId) -and $now -gt $previousTime){100*($cpu-$prior[$threadId])/(1000*($now-$previousTime))}else{$null}
        $prior[$threadId]=$cpu
        [pscustomobject]@{Id=$threadId;Name=[OotThreadNames]::Name($threadId);CPUms=$cpu;OneCorePercent=$percent;State=$_.ThreadState.ToString()}
    })
    $gpu=& nvidia-smi --query-gpu=utilization.gpu,memory.used,clocks.current.graphics,clocks.current.memory,temperature.gpu,power.draw --format=csv,noheader,nounits
    if($LASTEXITCODE -ne 0){throw 'nvidia-smi failed'}
    $systemMemory=[OotSystemMemory]::Read()
    $rows.Add([pscustomobject]@{UTC=[DateTime]::UtcNow.ToString('o');ElapsedSeconds=$now;WorkingSetBytes=$process.WorkingSet64;PrivateBytes=$process.PrivateMemorySize64;CPUms=$process.TotalProcessorTime.TotalMilliseconds;GPU=$gpu;Threads=$threads;SystemCommittedBytes=$systemMemory.CommittedBytes;SystemCommitLimitBytes=$systemMemory.CommitLimitBytes;SystemPhysicalTotalBytes=$systemMemory.PhysicalTotalBytes;SystemPhysicalAvailableBytes=$systemMemory.PhysicalAvailableBytes;CollectorSHA256=$collectorHash})
    # Keep completed samples even if the target exits before the final JSON write.
    $journalWriter.WriteLine(($rows[$rows.Count-1] | ConvertTo-Json -Depth 6 -Compress))
    $journalWriter.Flush()
    $previousTime=$now
    if($i -lt $Seconds){Start-Sleep -Milliseconds 1000}
}
} finally {
    $journalWriter.Dispose()
}
$rows | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputFile -Encoding utf8
[pscustomobject]@{ProcessId=$ProcessId;ProcessStartedUTC=$processStart.ToString('o');CollectorSHA256=$collectorHash;RequestedSeconds=$Seconds;Samples=$rows.Count;EndReason=$endReason;CompletedUTC=[DateTime]::UtcNow.ToString('o');SystemMemoryMethod='GetPerformanceInfo, page counts multiplied by returned page size'} | ConvertTo-Json | Set-Content -LiteralPath ($OutputFile+'.collector.json')
Write-Output "Recorded $($rows.Count) telemetry samples. Thread CPU is a percentage of one logical processor."
