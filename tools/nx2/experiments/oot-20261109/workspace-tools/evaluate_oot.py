"""Read-only guest-present timing and deterministic input for local Lichii evaluation."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'diagnostics/2026-11-08-native-evaluation'

def request(method, params=None):
    with socket.create_connection(('127.0.0.1', 6545), timeout=120) as s:
        s.sendall((json.dumps({'id': 1, 'method': method, 'params': params or {}})+'\n').encode())
        answer = json.loads(s.makefile('rb').readline())
    if 'error' in answer:
        raise RuntimeError(answer['error'])
    return answer['result']

K = C.WinDLL('kernel32', use_last_error=True)
P = C.WinDLL('psapi', use_last_error=True)
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
K.CloseHandle.argtypes = [W.HANDLE]
P.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(C.c_void_p), W.DWORD, C.POINTER(W.DWORD), W.DWORD]

class Reader:
    def __init__(self, pid, exe):
        self.handle = K.OpenProcess(0x410, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        modules = (C.c_void_p * 1024)()
        needed = W.DWORD()
        if not P.EnumProcessModulesEx(self.handle, modules, C.sizeof(modules), C.byref(needed), 3):
            raise C.WinError(C.get_last_error())
        self.base = modules[0]
        pe = Path(exe).read_bytes()
        off = struct.unpack_from('<I', pe, 0x3c)[0]
        nsec, optsize = struct.unpack_from('<H', pe, off+6)[0], struct.unpack_from('<H', pe, off+20)[0]
        self.sections = []
        for i in range(nsec):
            at = off+24+optsize+i*40
            name = pe[at:at+8].rstrip(b'\0').decode()
            size, rva = struct.unpack_from('<II', pe, at+8)
            self.sections.append((name, rva, size))

    def read(self, addr, size):
        buf = C.create_string_buffer(size)
        n = C.c_size_t()
        if not K.ReadProcessMemory(self.handle, addr, buf, size, C.byref(n)):
            raise C.WinError(C.get_last_error())
        return buf.raw[:n.value]

    def find_ring(self):
        # Find the existing timestamp ring by its clock and cyclic chronology, then
        # validate it against the public server's independent present counter.
        status = request('status', {'fps_window_ms': 0})
        count = status['presents']
        now = time.perf_counter_ns()
        candidates = []
        for name, rva, size in self.sections:
            if name != '.data':
                continue
            data = self.read(self.base+rva, size)
            for at in range(0, len(data)-32768, 8):
                v = struct.unpack_from('<Q', data, at)[0]
                if not now-86_400_000_000_000 < v < now:
                    continue
                a = struct.unpack_from('<8Q', data, at)
                if all(1_000_000 < a[i+1]-a[i] < 1_000_000_000 for i in range(7)):
                    candidates.append(self.base+rva+at)
                    break
        if len(candidates) != 1:
            raise RuntimeError(f'Expected one timestamp region; found {candidates}')
        # Any interior address works: find the discontinuity and its newest timestamp.
        self.region = candidates[0]
        block = self.read(self.region-32768, 65536)
        vals = list(struct.unpack('<8192Q', block))
        valid = [i for i,v in enumerate(vals) if now-86_400_000_000_000 < v < now]
        # The ring is 4096 entries, with its end inferred from the contiguous valid run.
        groups=[]
        for i in valid:
            if not groups or i != groups[-1][-1]+1: groups.append([i])
            else: groups[-1].append(i)
        group = max(groups, key=len)
        if count < 4096 and abs(len(group)-count) < 120:
            self.ring = self.region-32768+group[0]*8-8
        elif len(group) == 4096:
            self.ring = self.region-32768+group[0]*8
        else:
            raise RuntimeError(f'Timestamp region length {len(group)}, expected 4096')
        latest = max(struct.unpack('<4096Q', self.read(self.ring,32768)))
        check = request('status', {'fps_window_ms': 0})
        arr = struct.unpack('<4096Q', self.read(self.ring,32768))
        if abs(time.perf_counter_ns()-max(arr)) > 1_000_000_000:
            raise RuntimeError('Timestamp clock validation failed')
        return {'base':hex(self.base),'ring':hex(self.ring),'status_before':status,
                'status_after':check,'newest_age_ms':(time.perf_counter_ns()-latest)/1e6}

    def times(self):
        return struct.unpack('<4096Q', self.read(self.ring,32768))

def summary(ms):
    a=sorted(ms)
    def p(q):
        x=(len(a)-1)*q; lo=int(x); hi=min(lo+1,len(a)-1)
        return a[lo]+(a[hi]-a[lo])*(x-lo)
    return {'intervals':len(a),'fps':1000/(sum(a)/len(a)),'median_ms':p(.5),'p95_ms':p(.95),
            'p99_ms':p(.99),'max_ms':max(a),'budget_16_6667_share':sum(x<=1000/60 for x in a)/len(a),
            'over_33_3':sum(x>33.3 for x in a),'over_50':sum(x>50 for x in a),'over_100':sum(x>100 for x in a)}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('label'); ap.add_argument('--pid',type=int,default=8480)
    ap.add_argument('--route',choices=['spin','traverse','idle'],default='spin'); ap.add_argument('--frames',type=int,default=900)
    ap.add_argument('--exe',type=Path,default=ROOT/'local-upstream-ad477ef1-pgo/lichii.exe')
    ap.add_argument('--observe-seconds',type=float,default=0,help='Observe without sending controller input')
    ap.add_argument('--ring-rva',type=lambda x:int(x,0),help='Previously validated timestamp ring RVA for this exact executable')
    a=ap.parse_args(); OUT.mkdir(exist_ok=True)
    if not 60 <= a.frames <= 3600 or a.frames % 60:
        ap.error('--frames must be a multiple of 60 between 60 and 3600')
    r=Reader(a.pid,a.exe)
    if a.ring_rva is not None:
        r.ring=r.base+a.ring_rva
        status=request('status',{'fps_window_ms':0})
        ring=r.times()
        if abs(time.perf_counter_ns()-ring[status['presents']%4096])>2_000_000_000:
            raise RuntimeError('Supplied ring RVA failed present-counter/clock validation')
        validation={'ring':hex(r.ring),'status':status,'provided_rva':hex(a.ring_rva)}
    else:
        validation=r.find_ring()
    (OUT/(a.label+'-history.json')).write_text(json.dumps({'now_ns':time.perf_counter_ns(),'timestamps_ns':sorted(r.times()),'validation':validation}))
    (OUT/'timestamp-ring-validation.json').write_text(json.dumps(validation,indent=2))
    steps=[]
    for i in range(a.frames//60):
        move=([0,0] if a.route!='traverse' else [[0,1],[1,0],[0,-1],[-1,0]][i%4])
        steps.extend([{'frames':6,'buttons':[['X'],['Y']][i%2] if a.route!='idle' else [],'left_stick':move,'right_stick':[1,0] if a.route!='idle' else [0,0]},
                      {'frames':54,'buttons':[],'left_stick':move,'right_stick':[1,0] if a.route!='idle' else [0,0]}])
    start=time.perf_counter_ns()
    if a.observe_seconds:
        time.sleep(a.observe_seconds)
        result=None
    else:
        result=request('input_sequence',{'steps':steps,'timeout_s':120})
    end=time.perf_counter_ns(); ring=r.times()
    if result:
        first=result['first_present']; frames=result['frames']
        times=[ring[i%4096] for i in range(first-1,first+frames)]
    else:
        times=sorted(t for t in ring if start<=t<=end)
    if len(times)>4096 or any(y<=x for x,y in zip(times,times[1:])):
        raise RuntimeError('Timestamp ring chronology or capacity validation failed')
    if times[0] < start-100_000_000 or times[-1] > end:
        raise RuntimeError('Timestamp ring does not match the requested input sequence')
    ms=[(y-x)/1e6 for x,y in zip(times,times[1:])]
    doc={'label':a.label,'route':a.route,'method':'ReadProcessMemory of existing guest-present timestamp ring; read only; no injection',
         'clock':'QueryPerformanceCounter normalized nanoseconds','start_ns':start,'end_ns':end,'input':result,'steps':steps,
         'summary':summary(ms),'timestamps_ns':times,'intervals_ms':ms}
    (OUT/(a.label+'.json')).write_text(json.dumps(doc,indent=2))
    print(json.dumps(doc['summary'],indent=2))

if __name__=='__main__': main()
