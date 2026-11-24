import numpy as np, soundfile as sf, glob, os
def voiced(x, fs, start, secs=12):
    # like AutoCapture: skip 512-sample blocks under -50 dBFS, take `secs` of voice from `start`
    x=x[int(start*fs):]; out=[]; n=0
    for i in range(0,len(x)-512,512):
        b=x[i:i+512]
        if np.abs(b).max()>=0.00316: out.append(b); n+=512
        if n>=secs*fs: break
    return np.concatenate(out)
def spec(x,fs):
    n=1024 if fs<=50000 else 2048; hop=n//2
    w=np.hanning(n); F=np.fft.rfftfreq(n,1/fs)
    fr=1+(len(x)-n)//hop; idx=np.arange(n)[None,:]+hop*np.arange(fr)[:,None]
    rms=10*np.log10(np.maximum(1e-12,(x[idx]**2).mean(1))); loud=np.percentile(rms,95)
    P=np.abs(np.fft.rfft(x[idx]*w,axis=1))**2
    m=(F>=80)&(F<=min(16000,fs*.45)); tot=P[:,m].sum(1)
    hi=P[:,(F>=4000)&(F<=min(16000,fs*.45))].sum(1); vo=P[:,(F>=200)&(F<=2500)].sum(1)
    ok=rms>=max(-60,loud-40)
    S=ok&(hi>=.5*tot)&(hi>vo)
    sp=(P[S]/tot[S,None]).sum(0)
    return F,sp,S.sum()
def smooth(F,sp,oct=1/6):
    return np.array([sp[(F>=f/2**(oct/2))&(F<=f*2**(oct/2))].mean() if f>0 else 0 for f in F])
def est(F,sp):
    sm=smooth(F,sp); sel=(F>=4000)&(F<=12000)
    peak=F[sel][np.argmax(sm[sel])]
    # log centroid 4-12k
    p=sp[sel]; f=F[sel]
    cen=2**((p*np.log2(f)).sum()/p.sum())
    # excess over floor (min of 1/3-oct smoothed spectrum in 4-12k)
    sm3=smooth(F,sp,1/3); q=np.maximum(0,sm3[sel]-np.percentile(sm3[sel],20))
    exc=2**((q*np.log2(f)).sum()/q.sum())
    # mass median (half the S energy below)
    c=np.cumsum(p); med=f[np.searchsorted(c,c[-1]/2)]
    return peak,cen,exc,med
if __name__=="__main__":
 for t in sorted(glob.glob('data/takes/*.wav')):
     x,fs=sf.read(t,always_2d=True); x=x.mean(1)
     rows=[]
     for st in (0,25,50):
         F,sp,ns=spec(voiced(x,fs,st),fs); rows.append(est(F,sp))
     F,sp,ns=spec(x,fs); full=est(F,sp)
     r=np.array(rows)
     print('%-13s'%os.path.basename(t)[:-4], ' '.join('%s[%s] full %5.0f spread %4.0f%%'%(nm,'/'.join('%5.0f'%v for v in r[:,k]),full[k],100*(r[:,k].max()/r[:,k].min()-1)) for k,nm in enumerate(['pk','cen','exc','med'])))
 