"""Read-only Windows process identities, filtered to requested roots/descendants.

No command lines, environment, memory reads, signals, or process creation.
Requested PIDs and their descendants are candidates, never proof of ownership.
The caller must compare full identities. An unreadable identity is not absence.
"""
import ctypes as c
from ctypes import wintypes as w
import json, sys

k = c.WinDLL('kernel32', use_last_error=True)
class PROCESSENTRY32W(c.Structure):
    _fields_ = [('dwSize',w.DWORD),('cntUsage',w.DWORD),('th32ProcessID',w.DWORD),
                ('th32DefaultHeapID',c.c_size_t),('th32ModuleID',w.DWORD),
                ('cntThreads',w.DWORD),('th32ParentProcessID',w.DWORD),
                ('pcPriClassBase',w.LONG),('dwFlags',w.DWORD),('szExeFile',w.WCHAR*260)]
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD]; k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.Process32FirstW.argtypes=[w.HANDLE,c.POINTER(PROCESSENTRY32W)]; k.Process32FirstW.restype=w.BOOL
k.Process32NextW.argtypes=k.Process32FirstW.argtypes; k.Process32NextW.restype=w.BOOL
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD]; k.OpenProcess.restype=w.HANDLE
k.GetProcessTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
k.GetProcessTimes.restype=w.BOOL
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]; k.QueryFullProcessImageNameW.restype=w.BOOL
k.CloseHandle.argtypes=[w.HANDLE]; k.CloseHandle.restype=w.BOOL
k.GetSystemTimeAsFileTime.argtypes=[c.POINTER(w.FILETIME)]; k.GetSystemTimeAsFileTime.restype=None
req=json.load(sys.stdin); roots=set(req.get('pids',[]))
cutoff=w.FILETIME(); k.GetSystemTimeAsFileTime(c.byref(cutoff))
cutoff_ticks=(cutoff.dwHighDateTime<<32)|cutoff.dwLowDateTime
snap=k.CreateToolhelp32Snapshot(2,0)
if snap==c.c_void_p(-1).value: raise OSError(c.get_last_error(),'Process snapshot failed')
rows=[]; entry=PROCESSENTRY32W();entry.dwSize=c.sizeof(entry)
try:
    ok=k.Process32FirstW(snap,c.byref(entry))
    if not ok and c.get_last_error()!=18: raise OSError(c.get_last_error(),'Process snapshot unreadable')
    while ok:
        rows.append(dict(pid=entry.th32ProcessID,parentPid=entry.th32ParentProcessID,name=entry.szExeFile))
        ok=k.Process32NextW(snap,c.byref(entry))
        if not ok and c.get_last_error()!=18:
            raise OSError(c.get_last_error(),'Process snapshot iteration incomplete')
finally: k.CloseHandle(snap)
chosen=set(roots)
while True:
    children={r['pid'] for r in rows if r['parentPid'] in chosen}
    if children.issubset(chosen):break
    chosen.update(children)
result=[]
for r in rows:
    if r['pid'] not in chosen:continue
    handle=k.OpenProcess(0x1000,False,r['pid'])
    if not handle:r['identityError']=c.get_last_error();result.append(r);continue
    try:
        times=[w.FILETIME() for _ in range(4)]
        if k.GetProcessTimes(handle,*[c.byref(t) for t in times]):
            r['createdFiletime']=str((times[0].dwHighDateTime<<32)|times[0].dwLowDateTime)
            # Reject a new process acquired after the snapshot's start, including
            # PID reuse between enumeration and opening the process handle.
            if int(r['createdFiletime'])>cutoff_ticks:r['identityError']='created_after_snapshot_start'
        else:r['identityError']=c.get_last_error()
        size=w.DWORD(32768);name=c.create_unicode_buffer(size.value)
        if k.QueryFullProcessImageNameW(handle,0,name,c.byref(size)):r['image']=name.value
        else:r['imageError']=c.get_last_error()
    finally:k.CloseHandle(handle)
    result.append(r)
print(json.dumps({'complete':True,'snapshotBeforeFiletime':str(cutoff_ticks),'processes':result}))
