"""Align local QPC presents with log timestamps using identical present counters."""
import argparse
import json
from pathlib import Path
import re
import statistics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture', type=Path)
    args = ap.parse_args()
    frames = {}
    for line in (args.capture / 'frames.jsonl').read_text().splitlines():
        item = json.loads(line)
        frames[item['frame']] = item
    text = (args.capture / 'log-before-monitor.txt').read_text(errors='replace')
    text += '\n' + (args.capture / 'runtime-follow.log').read_text(errors='replace')
    records = []
    anchors = []
    for line in text.splitlines():
        match = re.match(r'\[\s*([\d.]+)\]', line)
        if not match:
            continue
        seconds = float(match[1])
        records.append((seconds, line))
        present = re.search(r'229 present #(\d+) token=', line)
        if present and int(present[1]) in frames:
            frame = int(present[1])
            offset = frames[frame]['qpc_ns'] / 1e9 - seconds
            anchors.append(dict(frame=frame, log_s=seconds, qpc_minus_log_s=offset))
    if len(anchors) < 2:
        raise RuntimeError('At least two identical-counter log/QPC anchors are required')
    offsets = [a['qpc_minus_log_s'] for a in anchors]
    spread_ms = (max(offsets)-min(offsets))*1000
    if spread_ms > 5:
        raise RuntimeError(f'Counter clock alignment is not stable: {spread_ms:.3f} ms spread')
    offset = statistics.median(offsets)
    events = []
    for frame in frames.values():
        if frame['interval_ms'] <= 50:
            continue
        end = frame['qpc_ns']/1e9-offset
        start = end-frame['interval_ms']/1000
        related = []
        for seconds, line in records:
            if start-.01 <= seconds <= end+.025 and re.search(r'pipeline|Nx2Perf|Error>|Warning>|download|wait|alloc', line, re.I):
                related.append(line)
        events.append(dict(**frame, start_log_s=start, end_log_s=end, nearby_log_lines=related))
    result = dict(method='Same VI present counter anchors QPC to existing log clock; no debug-uptime assumption',
                  anchors=anchors, anchor_spread_ms=spread_ms, qpc_minus_log_s=offset,
                  events=events, limitations='Nearby events show correlation, not universal causality. Worker pipeline durations include concurrent driver work. Mixed live play may include loading and menus.')
    (args.capture / 'correlation.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
