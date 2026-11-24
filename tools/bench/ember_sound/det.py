import numpy as np, soundfile as sf, glob, os, pickle, sys
from scipy.signal import butter, sosfilt
sys.path.insert(0,'.'); import ana
fr=[4000,5000,6500,8000,10000]
allv=[]
for t in sorted(glob.glob('data/takes/*.wav')):
    C=pickle.load(open('out/'+os.path.basename(t)[:-4]+'.cls','rb'))
    x,fs=sf.read(t,always_2d=True); x=x.mean(1)
    hop=C.hop; n=int(20*fs/1000); S=np.where(C.m['S'])[0]
    row=[]
    for f in fr:
        y=sosfilt(butter(4,f,'hp',fs=fs,output='sos'),x)
        pk=np.array([np.abs(y[i*hop:i*hop+n]).max() for i in S])
        row.append(np.median(20*np.log10(pk+1e-9)))
    row=np.array(row)-row[2]; allv.append(row)
    print('%-13s peak %5.0f '%(os.path.basename(t)[:-4],C.peak)+' '.join('%5.1f'%v for v in row))
print('mean            ',' '.join('%5.1f'%v for v in np.mean(allv,0)))
