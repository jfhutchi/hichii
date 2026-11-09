"""Exercise the instrumented game; never use these acknowledgements as FPS results."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

from evaluate_oot import request
from oot_benchmark_inputs import balanced_steps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "diagnostics/2026-11-09-emulator-fix"
parser = argparse.ArgumentParser()
parser.add_argument("area", choices=["field", "town"])
parser.add_argument("--blocks", type=int, choices=range(1, 13), default=8)
args = parser.parse_args()
output = OUT / f"training-{args.area}.jsonl"
if output.exists():
    raise FileExistsError("Preserve completed training records; do not overwrite them")
active = json.loads((OUT / "active-game.json").read_text(encoding="utf-8-sig"))
launch = json.loads((OUT / "training-launch.json").read_text(encoding="utf-8-sig"))
if active["SHA256"] != launch["SHA256"]:
    raise RuntimeError("The running executable is not the staged instrumented build")
status = request("status", {"fps_window_ms": 0})
if status["scm_rev"] != launch["SourceCommit"] or status["program_id"] != "0400486024DEC000":
    raise RuntimeError("Unexpected source or game")
steps = balanced_steps(480)
with output.open("x") as journal:
    for block in range(args.blocks):
        started = time.perf_counter_ns()
        ack = request("input_sequence", {"steps": steps, "timeout_s": 115})
        if ack["frames"] != 480:
            raise RuntimeError("Incomplete training input acknowledgement")
        screenshot = None
        if (block + 1) % 4 == 0:
            shot = request("screenshot", {"height": 720, "save": True, "timeout_s": 10})
            screenshot = {key: shot[key] for key in ["path", "width", "height"]}
        record = dict(purpose="Instrumented training, excluded from FPS acceptance",
                      area=args.area, block=block + 1, start_ns=started,
                      end_ns=time.perf_counter_ns(), UTC=datetime.now(timezone.utc).isoformat(),
                      executable_sha256=launch["SHA256"], input=ack, steps=steps,
                      screenshot=screenshot,
                      input_builder_sha256=hashlib.sha256((ROOT / "tools/oot_benchmark_inputs.py").read_bytes()).hexdigest())
        journal.write(json.dumps(record) + "\n")
        journal.flush()
        print(json.dumps(dict(area=args.area, block=block + 1, frames=ack["frames"],
                              elapsed_s=ack["elapsed_s"], screenshot=screenshot,
                              purpose="training only")), flush=True)
