import subprocess, sys, soundfile as sf, numpy as np, os, ctypes
from ctypes import wintypes
S=os.path.dirname(os.path.abspath(__file__))
src=S+'/data/takes/J1_chemicals.wav'; st=S+'/out/cpu_stereo.wav'
if not os.path.exists(st):
    x,fs=sf.read(src,always_2d=True); x=x[:, :1]; y=np.hstack([x, np.roll(x,37,axis=0)*0.9]); y=np.vstack([y]*3)
    sf.write(st,y,fs,subtype='FLOAT')
dur=sf.info(st).duration
k32=ctypes.windll.kernel32
def cpu_ms(cmd):
    p=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); p.wait()
    c,e,k,u=(wintypes.FILETIME() for _ in range(4))
    k32.GetProcessTimes(wintypes.HANDLE(int(p._handle)),ctypes.byref(c),ctypes.byref(e),ctypes.byref(k),ctypes.byref(u))
    f=lambda t:(t.dwHighDateTime<<32|t.dwLowDateTime)/1e4
    return f(k)+f(u)
import collections
runs=collections.defaultdict(list)
specs=[e.partition('@') for e in sys.argv[1:]]
for r in range(12):
    for exe,_,args in specs:
        runs[(exe,args)].append(cpu_ms([exe,'--process',st,S+'/out/cpu_out.wav']+[a for a in args.split(',') if a]))
io=None
for exe,_,args in specs:
    v=min(runs[(exe,args)])
    if 'noengine' in args: io=v
for exe,_,args in specs:
    v=min(runs[(exe,args)])
    eng=v-(io or 0)
    print(f'{os.path.basename(os.path.dirname(exe)):5s} {args:22s} total {v:6.0f} ms  engine {eng:6.0f} ms = {100*eng/1000/dur/1:.3f} % of a core (stereo 44.1k)')
