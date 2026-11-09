"""Run the existing guest-present benchmark into this investigation's directory."""
import importlib.util
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import re
import sys
import time
import threading
from present_ring_locator import IncompleteWindowError, select_input_window
from oot_benchmark_inputs import balanced_steps

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "oot_route", root / "tools/Measure-LatestLichiiRoute.py"
)
route = importlib.util.module_from_spec(spec)
spec.loader.exec_module(route)
route.OUT = root / "diagnostics/2026-11-09-emulator-fix"
if "--reload" not in sys.argv:
    route.main()
    raise SystemExit

parser = argparse.ArgumentParser(description="Reset and time one fixed guest-input sequence")
parser.add_argument("label")
parser.add_argument("--reload", action="store_true")
parser.add_argument("--pid", type=int, required=True)
parser.add_argument("--exe", type=Path, required=True)
parser.add_argument("--ring-rva", type=lambda value: int(value, 0), required=True)
parser.add_argument("--frames", type=int, choices=[1200, 1800, 1920], default=1800)
parser.add_argument("--route", choices=["traverse", "balanced"], default="traverse")
parser.add_argument("--slot-index", type=int, choices=range(6), default=0)
args = parser.parse_args()
if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
    parser.error("Use a plain diagnostic label without path separators")
output_path = route.OUT / (args.label + ".json")
if output_path.exists():
    raise FileExistsError(f"Preserve the existing capture; use a new label: {output_path}")
if args.route == "balanced" and args.frames % 240:
    parser.error("The balanced route requires complete 240-frame cycles; use --frames 1920")
reader = route.Reader(args.pid, args.exe)
route.P.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.LPWSTR, wintypes.DWORD]
route.P.GetModuleFileNameExW.restype = wintypes.DWORD
module_path = ctypes.create_unicode_buffer(32768)
length = route.P.GetModuleFileNameExW(reader.handle, reader.base, module_path, len(module_path))
if not length:
    raise ctypes.WinError(ctypes.get_last_error())
if length >= len(module_path) - 1 or not Path(module_path.value).samefile(args.exe):
    raise RuntimeError("The loaded executable does not match --exe")
exe_hash = hashlib.sha256(args.exe.read_bytes()).hexdigest()
status = route.request("status", {"fps_window_ms": 0})
if status["program_id"].upper() != "0400486024DEC000" or not status["powered_on"] or status["paused"]:
    raise RuntimeError("The expected Zelda game is not running unpaused")
if status["presents"] < 4096:
    raise RuntimeError("A complete timestamp ring is required; warm the game past 4096 presents first")
ring_proof = reader.find_ring()
if int(ring_proof["ring_rva"], 16) != args.ring_rva:
    raise RuntimeError("Supplied ring RVA differs from exact cyclic/stable-counter proof")
prefix = []
navigation = [("Plus",48),("R",48),("Down",48),("A",108)]
navigation += [("Down",48)] * args.slot_index
navigation += [("A",108),("Up",48),("A",600)]
for button, wait in navigation:
    prefix.extend([dict(frames=12, buttons=[button]), dict(frames=wait, buttons=[])])
prefix_frames = sum(step["frames"] for step in prefix)
timed_steps = []
if args.route == "balanced":
    # Keep translation separate from yaw: continuous turning made slower v2 runs drift.
    timed_steps = balanced_steps(args.frames)
else:
    for index in range(args.frames // 60):
        move = [[0,1],[1,0],[0,-1],[-1,0]][index % 4]
        timed_steps.extend([
            dict(frames=6, buttons=[["X"],["Y"]][index % 2], left_stick=move, right_stick=[1,0]),
            dict(frames=54, buttons=[], left_stick=move, right_stick=[1,0]),
        ])
user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.GetForegroundWindow.restype = wintypes.HWND
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]


def foreground_pid():
    value = wintypes.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(value))
    return value.value


if foreground_pid() != args.pid:
    raise RuntimeError("Target game is not in the foreground; activate it before measuring")
focus_samples = []
finished = threading.Event()


def observe_focus():
    while not finished.is_set():
        focus_samples.append(dict(timestamp_ns=time.perf_counter_ns(), pid=foreground_pid()))
        finished.wait(0.1)


observer = threading.Thread(target=observe_focus)
observer.start()
request_start = time.perf_counter_ns()
snapshot_errors = []
try:
    result = route.request("input_sequence", {"steps": prefix + timed_steps, "timeout_s": 115})
    request_end = time.perf_counter_ns()
    if result["frames"] != prefix_frames + args.frames:
        raise RuntimeError("Input acknowledgement length mismatch")
    first_input_counter = result["first_present"] + prefix_frames
    first = first_input_counter + 1
    for attempt in range(20):
        before = route.request("status", {"fps_window_ms": 0})["presents"]
        ring = reader.times()
        after = route.request("status", {"fps_window_ms": 0})["presents"]
        capture_end = time.perf_counter_ns()
        try:
            times = select_input_window(ring, first_input_counter, args.frames, before, after,
                                        request_start, capture_end)
            break
        except IncompleteWindowError as error:
            snapshot_errors.append(dict(before=before, after=after, reason=str(error)))
            time.sleep(0.01)
    else:
        raise RuntimeError("Could not read a fully published timestamp window")
finally:
    finished.set()
    observer.join()
focus_samples.append(dict(timestamp_ns=time.perf_counter_ns(), pid=foreground_pid()))
if hashlib.sha256(args.exe.read_bytes()).hexdigest() != exe_hash:
    raise RuntimeError("The executable file changed during capture")
intervals = [(b-a)/1e6 for a,b in zip(times,times[1:])]
focus_intrusions = [row for index, row in enumerate(focus_samples)
                    if row["pid"] != args.pid
                    and focus_samples[max(0,index-1)]["timestamp_ns"] < times[-1]
                    and focus_samples[min(len(focus_samples)-1,index+1)]["timestamp_ns"] > times[0]]
protocol = "fixed-reset-v3"
document = dict(label=args.label, route=args.route, protocol=protocol,
                method="ReadProcessMemory of existing guest VI present ring; separate guest input sequence",
                request_start_ns=request_start, request_end_ns=request_end,
                capture_end_ns=capture_end,
                start_ns=times[0], end_ns=times[-1], input=result,
                prefix_frames=prefix_frames, timed_first_present=first,
                timed_input_first_counter=first_input_counter,
                prefix_steps=prefix, steps=timed_steps,
                summary=route.summary(intervals), timestamps_ns=times, intervals_ms=intervals,
                valid=not focus_intrusions, focus_sample_period_ms=100,
                focus_samples=focus_samples, focus_intrusions=focus_intrusions,
                slot_index=args.slot_index,
                validation=dict(executable=str(args.exe), sha256=exe_hash,
                                loaded_executable=module_path.value,
                                ring_rva=hex(args.ring_rva), ring_proof=ring_proof, status=status,
                                snapshot_before_counter=before, snapshot_after_counter=after,
                                snapshot_retries=snapshot_errors,
                                tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                supporting_tool_sha256={name: hashlib.sha256((root / "tools" / name).read_bytes()).hexdigest()
                                                       for name in ["present_ring_locator.py", "oot_benchmark_inputs.py", "Measure-LatestLichiiRoute.py"]}))
output_path.write_text(json.dumps(document, indent=2))
print(json.dumps(document["summary"], indent=2))
if focus_intrusions:
    raise RuntimeError("Saved but excluded: foreground focus changed during the timed route")
