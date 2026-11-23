import numpy as np, soundfile as sf, subprocess, sys, os
fs=44100; f0=6200*1.25; t=np.arange(int(fs*2))/fs
env=0.02+0.3*(0.5-0.5*np.cos(2*np.pi*3*t)); x=env*np.sin(2*np.pi*f0*t)
sf.write('out/zip_in.wav',x,fs,subtype='FLOAT')
def tp(y,hz,a,b):
    n=b-a; w=np.hanning(n); k=np.arange(n); return abs((w*y[a:b]*np.exp(-2j*np.pi*hz*k/fs)).sum())**2
for name in sys.argv[1:]:
    exe=os.path.abspath(name+'/EmberTests.exe')
    subprocess.run([exe,'--process','out/zip_in.wav','out/zip_'+name+'.wav','det=2','thr=-45','range=12','attack=0.3','release=20'],capture_output=True)
    y,_=sf.read('out/zip_'+name+'.wav'); a,b=int(fs*0.5),int(fs*1.5)
    tone=tp(y,f0,a,b); side=max(tp(y,f0+d,a,b) for d in (-fs/16,fs/16,-fs/8,fs/8))
    # broadband spurs: energy outside +-400 Hz of the tone (and its first harmonics/aliases of the detector ripple excluded)
    Y=np.abs(np.fft.rfft(y[a:b]*np.hanning(b-a)))**2; f=np.fft.rfftfreq(b-a,1/fs)
    m=np.abs(f-f0)>400
    print(f'{name}: sidebands at fs/16, fs/8: {10*np.log10(side/tone):6.1f} dB under the tone; all energy away from the tone: {10*np.log10(Y[m].sum()/Y[~m].sum()):6.1f} dB')
