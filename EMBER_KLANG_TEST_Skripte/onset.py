import sys, subprocess, numpy as np, soundfile as sf
from scipy.signal import butter, sosfilt
S=sys.argv[1]; E='C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
fs=48000; rng=np.random.default_rng(3)
t=np.arange(int(0.3*fs))/fs
vowel=sum(0.3/(k)*np.sin(2*np.pi*200*k*t) for k in range(1,25))*0.5
noise=sosfilt(butter(4,[5000,11000],'bp',fs=fs,output='sos'),rng.standard_normal(int(0.15*fs)))
noise=noise/np.abs(noise).max()*0.3
x=np.concatenate([np.zeros(fs//10),vowel,noise,vowel,np.zeros(fs//10)]); s0=fs//10+len(vowel); s1=s0+len(noise)
sf.write(S+'/synth/on.wav',x.astype(np.float32),fs,subtype='FLOAT')
for la in (0,2,5):
  for att in (0.5,):
    subprocess.run([E,'--process',S+'/synth/on.wav',S+'/synth/on_o.wav','mode=1','det='+sys.argv[2],'thr=-50','range=10','la=%g'%la,'attack=%g'%att,'trace='+S+'/synth/on_t.wav'],capture_output=True)
    y=sf.read(S+'/synth/on_o.wav')[0]
    def red(a,b): return 10*np.log10((x[a:b]**2).sum()/((y[a:b]**2).sum()+1e-30))
    ms=lambda m:int(m*fs/1000)
    print('lookahead %g ms: S first 1 ms %.1f dB, 1-3 ms %.1f, 3-10 ms %.1f, steady %.1f | vowel after S: first 5 ms %.2f, 5-20 ms %.2f | vowel before S last 5 ms %.2f'%(la,red(s0,s0+ms(1)),red(s0+ms(1),s0+ms(3)),red(s0+ms(3),s0+ms(10)),red(s0+ms(30),s1-ms(30)),red(s1,s1+ms(5)),red(s1+ms(5),s1+ms(20)),red(s0-ms(5),s0)))
