"""Pool explicit, valid fixed-reset runs without mixing warm-ups or protocols."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "diagnostics/2026-11-09-emulator-fix"
spec = importlib.util.spec_from_file_location("route", ROOT / "tools/Measure-LatestLichiiRoute.py")
route = importlib.util.module_from_spec(spec)
spec.loader.exec_module(route)

parser = argparse.ArgumentParser()
parser.add_argument("output")
parser.add_argument("labels", nargs="+")
args = parser.parse_args()
if any(not re.fullmatch(r"[A-Za-z0-9_-]+", label) for label in [args.output, *args.labels]):
    parser.error("Use plain diagnostic labels without path separators")
if len(set(args.labels)) != len(args.labels):
    parser.error("Duplicate capture labels do not establish independent repeats")
if args.output in args.labels:
    parser.error("The summary must not overwrite an input capture")
output_path = OUT / f"{args.output}.json"
if output_path.exists():
    parser.error("The output already exists; preserve it and use a new summary label")
paths = [OUT / f"{label}.json" for label in args.labels]
file_ids = [(path.stat().st_dev, path.stat().st_ino) for path in paths]
if len(set(file_ids)) != len(file_ids):
    parser.error("Filename aliases refer to the same physical capture")
excluded = {key.casefold(): value for key, value in
            json.loads((OUT / "excluded-runs.json").read_text()).items()}
documents = []
capture_ids = set()
for label, path in zip(args.labels, paths):
    document = json.loads(path.read_text())
    identity_label = document["label"].casefold()
    if identity_label in excluded or "warm" in identity_label:
        raise ValueError(f"Excluded or warm-up run: {label}")
    if document.get("protocol") not in ("fixed-reset-v2", "fixed-reset-v3") or document.get("valid") is not True:
        raise ValueError(f"Run has no valid fixed-reset audit: {label}")
    capture_id = (document["validation"]["sha256"].casefold(), tuple(document["timestamps_ns"]))
    if capture_id in capture_ids:
        raise ValueError(f"Copied data duplicates an existing capture: {label}")
    capture_ids.add(capture_id)
    documents.append(document)
for key in ("protocol", "slot_index", "prefix_frames", "steps"):
    if any(item[key] != documents[0][key] for item in documents):
        raise ValueError(f"Runs have different {key}")
hashes = {item["validation"]["sha256"] for item in documents}
if len(hashes) != 1:
    raise ValueError("Pool each executable separately")
intervals = [value for item in documents for value in item["intervals_ms"]]
result = dict(protocol=documents[0]["protocol"], labels=args.labels,
              executable_sha256=next(iter(hashes)), slot_index=documents[0]["slot_index"],
              individual=[dict(label=item["label"], **item["summary"]) for item in documents],
              pooled=route.summary(intervals))
with output_path.open("x") as output:
    output.write(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
