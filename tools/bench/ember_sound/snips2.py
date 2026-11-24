import sys, os, numpy as np, soundfile as sf
sys.path.insert(0,'.'); import ana
from snips import best_window
D=sys.argv[1]; os.makedirs(D,exist_ok=True)
cases=[('J3_lonely','01_J3_Lonely'),('J1_chemicals','02_J1_Chemicals'),('J2_kissed','03_J2_Kissed'),('M36','04_M36_tiefe_Maennerstimme_SONOVOX_intern')]
for name,prefix in cases:
    t0,fs=best_window(name)
    o,_=sf.read('data/takes/%s.wav'%name,always_2d=True)
    files=[('0_Original','data/takes/%s.wav'%name),('1_EMBER_vorher_AUTO','hb/%s_alt.wav'%name),('2_EMBER_neu_AUTO','hb/%s_neu.wav'%name)]
    if os.path.exists('data/prods/%s.wav'%name): files.append(('3_FabFilter_ProDS','data/prods/%s.wav'%name))
    a=int(t0*fs); L=int(14*fs); n=int(0.01*fs); ramp=np.linspace(0,1,n)[:,None]
    for tag,f in files:
        x,_=sf.read(f,always_2d=True); y=x[a:a+L].copy(); y[:n]*=ramp; y[-n:]*=ramp[::-1]
        sf.write(D+'/%s_%s.wav'%(prefix,tag), y, fs, subtype='PCM_24')
        if tag!='0_Original':
            m=min(len(o),len(x)); d=(o[:m]-x[:m])[a:a+L]
            sf.write(D+'/%s_%s_DELTA.wav'%(prefix,tag), d, fs, subtype='PCM_24')
    print(prefix,'%.1f s'%t0)
