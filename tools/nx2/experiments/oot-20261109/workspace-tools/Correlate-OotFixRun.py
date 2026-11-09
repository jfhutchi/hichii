"""Adapt a fixed-reset result to the existing VI-counter/log correlation tool."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "diagnostics/2026-11-09-emulator-fix"
parser = argparse.ArgumentParser()
parser.add_argument("label")
parser.add_argument("--log", type=Path, required=True)
args = parser.parse_args()
run = json.loads((OUT / f"{args.label}.json").read_text())
if run.get("protocol") not in ("fixed-reset-v2", "fixed-reset-v3") or run.get("valid") is not True:
    raise ValueError("A valid fixed-reset run is required")
folder = OUT / f"correlation-{args.label}"
folder.mkdir()
first = run["timed_first_present"]
with (folder / "frames.jsonl").open("w") as output:
    for index, interval in enumerate(run["intervals_ms"]):
        output.write(json.dumps(dict(frame=first + index,
                                     qpc_ns=run["timestamps_ns"][index + 1],
                                     interval_ms=interval)) + "\n")
shutil.copyfile(args.log, folder / "log-before-monitor.txt")
(folder / "runtime-follow.log").write_text("")
completed = subprocess.run([sys.executable, str(ROOT / "tools/Correlate-LichiiLiveStutters.py"),
                            str(folder)], capture_output=True, text=True)
(folder / "tool-output.txt").write_text(completed.stdout + completed.stderr)
completed.check_returncode()
result = json.loads((folder / "correlation.json").read_text())
print(json.dumps(dict(label=args.label, anchors=len(result["anchors"]),
                      anchor_spread_ms=result["anchor_spread_ms"],
                      events=result["events"]), indent=2))
