import sys, os, numpy as np, soundfile as sf
S=sys.argv[1]; D=sys.argv[2]; sys.path.insert(0,S)
import ana
def best_window(name, secs=14):
    d,fs=ana.load(S+'/data/takes/%s.wav'%name); C=ana.Classes(d,fs)
    hop=C.hop; s=C.m['S'].astype(float); w=int(secs*fs/hop)
    c=np.convolve(s,np.ones(w),'valid'); i=int(np.argmax(c)); return i*hop/fs, fs
def cut(src, dst, t0, secs=14, gain_db=0.0):
    x,fs=sf.read(src,always_2d=True); a=int(t0*fs); y=x[a:a+int(secs*fs)]*10**(gain_db/20)
    n=int(0.01*fs); ramp=np.linspace(0,1,n)[:,None]; y[:n]*=ramp; y[-n:]*=ramp[::-1]
    sf.write(D+'/'+dst, y, fs, subtype='PCM_24'); 
cases=[
 ('J3_lonely','01_J3_Lonely', [('00_Original','data/takes/J3_lonely.wav'),('01_EMBER_AUTO_Split_(heute)','out/J3_lonely__auto_split.wav'),
   ('02_FabFilter_ProDS_Standard','data/prods/J3_lonely.wav'),('03_EMBER_AUTO_Glocke_PROTOTYP','out/J3_lonely__x_bell_q1.wav'),('04_EMBER_AUTO_Breitband','out/J3_lonely__auto_wide.wav')]),
 ('J1_chemicals','02_J1_Chemicals', [('00_Original','data/takes/J1_chemicals.wav'),('01_EMBER_AUTO_Split_(heute)','out/J1_chemicals__auto_split.wav'),
   ('02_FabFilter_ProDS_Standard','data/prods/J1_chemicals.wav'),('03_EMBER_AUTO_Glocke_PROTOTYP','out/J1_chemicals__x_bell_q1.wav')]),
 ('F22','03_F22_Lispeln', [('00_Original','data/takes/F22.wav'),('01_Kraeftig_Split','out/F22__h_kraeftig_split.wav'),('02_Amount100_Split_LISPELT','out/F22__h_amount100_split.wav'),('03_Amount100_Breitband','out/F22__h_amount100_wide.wav')]),
 ('M36','04_M36_tiefe_Maennerstimme', [('00_Original','data/takes/M36.wav'),('01_Standard_6500Hz','out/M36__def_split.wav'),('02_TIEF_5000Hz','out/M36__tief.wav'),('03_AUTO','out/M36__auto_split.wav'),('04_AUTO_Glocke_PROTOTYP','out/M36__x_bell_q1.wav')]),
 ('M40','05_M40_Standard_greift_kaum', [('00_Original','data/takes/M40.wav'),('01_Standard_ohne_AUTO','out/M40__def_split.wav'),('02_AUTO','out/M40__auto_split.wav')]),
]
for name, prefix, files in cases:
    t0,fs=best_window(name)
    for tag,f in files:
        cut(S+'/'+f, '%s_%s.wav'%(prefix,tag), t0)
    # delta of EMBER AUTO split and Pro-DS where present
    o,_=sf.read(S+'/data/takes/%s.wav'%name,always_2d=True)
    for tag,f in files[1:]:
        w,_=sf.read(S+'/'+f,always_2d=True); n=min(len(o),len(w))
        a=int(t0*fs); dlt=(o[:n]-w[:n])[a:a+int(14*fs)]
        sf.write(D+'/%s_%s_DELTA.wav'%(prefix,tag), dlt, fs, subtype='PCM_24')
    print(prefix, 'start %.1f s'%t0)
