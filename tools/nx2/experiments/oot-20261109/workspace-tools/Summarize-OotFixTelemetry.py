"""Summarize one-second CPU/GPU samples overlapping explicit gameplay windows."""
import argparse
from collections import defaultdict
from datetime import datetime
import json
from pathlib import Path
import re
from statistics import mean

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "diagnostics/2026-11-09-emulator-fix"
parser = argparse.ArgumentParser()
parser.add_argument("output")
parser.add_argument("--runs", nargs="+", required=True)
parser.add_argument("--journals", nargs="+", required=True)
parser.add_argument("--clock-anchor", type=Path, default=OUT / "clock-anchor.json")
args = parser.parse_args()
if not re.fullmatch(r"[A-Za-z0-9_-]+", args.output):
    parser.error("Use a plain output label without path separators")
output_path = OUT / f"{args.output}.json"
if output_path.exists():
    raise FileExistsError(f"Preserve existing evidence; use a new output label: {output_path}")
anchor = json.loads(args.clock_anchor.read_text())
offset = anchor["utc_minus_qpc_ns"]
windows = []
for label in args.runs:
    run = json.loads((OUT / f"{label}.json").read_text())
    if run.get("valid") is not True:
        raise ValueError(f"Invalid run: {label}")
    windows.append(((run["start_ns"] + offset) / 1e9, (run["end_ns"] + offset) / 1e9))
samples = []
seen = set()
for journal in args.journals:
    for line in (OUT / journal).read_text(encoding="utf-8-sig").splitlines():
        row = json.loads(line)
        stamp = datetime.fromisoformat(row["UTC"].replace("Z", "+00:00")).timestamp()
        if row["UTC"] not in seen and any(start <= stamp <= end for start, end in windows):
            samples.append(row)
            seen.add(row["UTC"])
if not samples:
    raise ValueError("No telemetry overlaps these gameplay windows")
collector_hashes = {row.get("CollectorSHA256") for row in samples}
if len(collector_hashes) != 1:
    raise ValueError("Do not pool different telemetry collector versions")

gpu = [[float(value.strip()) for value in row["GPU"].split(",")] for row in samples]
threads = defaultdict(list)
for row in samples:
    by_name = defaultdict(float)
    for thread in row["Threads"]:
        if thread["Name"] and thread["OneCorePercent"] is not None:
            by_name[thread["Name"]] += thread["OneCorePercent"]
    for name, value in by_name.items():
        threads[name].append(value)
result = dict(
    runs=args.runs, journals=args.journals, samples=len(samples),
    collector_sha256=next(iter(collector_hashes)),
    clock_anchor=str(args.clock_anchor),
    clock_alignment=anchor["note"],
    limits="One-second device-wide GPU samples and CPU activity including spins; no GPU execution-time or input-latency measurement. CPU intervals can straddle route boundaries.",
    working_set_gib_range=[min(row["WorkingSetBytes"] for row in samples) / 2**30,
                           max(row["WorkingSetBytes"] for row in samples) / 2**30],
    private_bytes_gib_range=[min(row["PrivateBytes"] for row in samples) / 2**30,
                             max(row["PrivateBytes"] for row in samples) / 2**30],
    gpu_util_percent_mean=mean(row[0] for row in gpu),
    gpu_util_percent_range=[min(row[0] for row in gpu), max(row[0] for row in gpu)],
    gpu_vram_mib_max=max(row[1] for row in gpu),
    gpu_graphics_clock_mhz_range=[min(row[2] for row in gpu), max(row[2] for row in gpu)],
    gpu_temperature_c_max=max(row[4] for row in gpu),
    gpu_power_w_mean=mean(row[5] for row in gpu),
    named_threads=sorted([dict(name=name, samples=len(values),
                              one_core_percent_mean=mean(values),
                              one_core_percent_max=max(values))
                          for name, values in threads.items()],
                         key=lambda row: row["one_core_percent_mean"], reverse=True),
)
memory_fields = {"SystemCommittedBytes", "SystemCommitLimitBytes",
                 "SystemPhysicalTotalBytes", "SystemPhysicalAvailableBytes"}
if all(memory_fields <= row.keys() for row in samples):
    result["system_memory"] = dict(
        method="GetPerformanceInfo, native page counts multiplied by page size",
        peak_committed_gib=max(row["SystemCommittedBytes"] for row in samples) / 2**30,
        commit_limit_gib_range=[min(row["SystemCommitLimitBytes"] for row in samples) / 2**30,
                                max(row["SystemCommitLimitBytes"] for row in samples) / 2**30],
        minimum_available_physical_gib=min(row["SystemPhysicalAvailableBytes"] for row in samples) / 2**30,
    )
elif any(memory_fields & row.keys() for row in samples):
    raise ValueError("Incomplete system-memory sampling")
else:
    result["system_memory"] = None
with output_path.open("x") as stream:
    stream.write(json.dumps(result, indent=2))
print(json.dumps({**result, "named_threads": result["named_threads"][:8]}, indent=2))
