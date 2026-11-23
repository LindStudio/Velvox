import sys, os, glob, subprocess, numpy as np, soundfile as sf
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run, ana
exe=sys.argv[1]; args=sys.argv[2:]
def one(t):
    n=os.path.basename(t)[:-4]; o=f'{run.OUT}/{n}__lev.wav'; l=f'{run.OUT}/{n}__lev_l.wav'
    subprocess.run([exe,'--process',t,o,'levels='+l]+args,capture_output=True)
    L,fs=sf.read(l,always_2d=True); C=run.classes(t); hop=C.hop; nn=int(ana.N_MS*fs/1000)
    lev=L[:,0]; w=L[:,1]
    ev=[]
    for a,b in C.events:
        seg=lev[a*hop:(b-1)*hop+nn]; ws=w[a*hop:(b-1)*hop+nn]
        if len(seg): ev.append(seg[ws>0.5].max() if (ws>0.5).any() else np.nan)
    ev=np.array(ev); 
    return n, np.nanpercentile(ev,[10,50,90,99]), np.nanmax(lev[w>0.5]) if (w>0.5).any() else np.nan, np.nanmax(lev)
with ThreadPoolExecutor(8) as ex:
    for n,p,mx,mxall in ex.map(one, run.TAKES):
        print(f'{n:14s} S-event peak level p10 {p[0]:6.1f} p50 {p[1]:6.1f} p90 {p[2]:6.1f} p99 {p[3]:6.1f}  max(gate open) {mx:6.1f} max all {mxall:6.1f}')
