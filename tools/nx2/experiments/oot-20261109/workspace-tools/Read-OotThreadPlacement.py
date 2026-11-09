"""Read thread affinity and preferred CPUs without changing target scheduling."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import psutil


class GroupAffinity(C.Structure):
    _fields_ = [('mask', C.c_size_t), ('group', W.WORD), ('reserved', W.WORD * 3)]


class ProcessorNumber(C.Structure):
    _fields_ = [('group', W.WORD), ('number', W.BYTE), ('reserved', W.BYTE)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.OpenThread.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenThread.restype = W.HANDLE
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.GetThreadGroupAffinity.argtypes = [W.HANDLE, C.POINTER(GroupAffinity)]
    kernel.GetThreadIdealProcessorEx.argtypes = [W.HANDLE, C.POINTER(ProcessorNumber)]
    kernel.GetThreadDescription.argtypes = [W.HANDLE, C.POINTER(C.c_void_p)]
    kernel.GetThreadDescription.restype = W.LONG
    kernel.LocalFree.argtypes = [C.c_void_p]
    rows = []
    process = psutil.Process(args.pid)
    for thread in process.threads():
        handle = kernel.OpenThread(0x800, False, thread.id)
        if not handle:
            raise C.WinError(C.get_last_error())
        try:
            affinity, ideal, name = GroupAffinity(), ProcessorNumber(), C.c_void_p()
            if not kernel.GetThreadGroupAffinity(handle, C.byref(affinity)):
                raise C.WinError(C.get_last_error())
            if not kernel.GetThreadIdealProcessorEx(handle, C.byref(ideal)):
                raise C.WinError(C.get_last_error())
            result = kernel.GetThreadDescription(handle, C.byref(name))
            if result < 0:
                raise OSError(f'GetThreadDescription HRESULT {result:#x}')
            if not name.value:
                raise OSError('GetThreadDescription returned no description buffer')
            try:
                description = C.wstring_at(name)
            finally:
                kernel.LocalFree(name)
            rows.append(dict(id=thread.id, name=description, group=affinity.group,
                             allowed_mask=hex(affinity.mask), preferred_group=ideal.group,
                             preferred_cpu=ideal.number, cpu_seconds=thread.user_time + thread.system_time))
        finally:
            kernel.CloseHandle(handle)
    result = dict(pid=args.pid, executable=process.exe(), utc=datetime.now(timezone.utc).isoformat(),
                  qpc_ns=time.perf_counter_ns(), threads=rows,
                  limitation='Affinity is allowed placement; preferred CPU is not observed execution placement.')
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f'Read {len(rows)} threads; no scheduling changes made.')


if __name__ == '__main__':
    main()
