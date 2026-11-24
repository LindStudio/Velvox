import sys, os, numpy as np, soundfile as sf
S='.'
import ana
def best_window(name, secs=14):
    d,fs=ana.load(S+'/data/takes/%s.wav'%name); C=ana.Classes(d,fs)
    hop=C.hop; s=C.m['S'].astype(float); w=int(secs*fs/hop)
    c=np.convolve(s,np.ones(w),'valid'); i=int(np.argmax(c)); return i*hop/fs, fs
def cut(src, dst, t0, secs=14, gain_db=0.0):
    x,fs=sf.read(src,always_2d=True); a=int(t0*fs); y=x[a:a+int(secs*fs)]*10**(gain_db/20)
    n=int(0.01*fs); ramp=np.linspace(0,1,n)[:,None]; y[:n]*=ramp; y[-n:]*=ramp[::-1]
    sf.write(D+'/'+dst, y, fs, subtype='PCM_24'); 
