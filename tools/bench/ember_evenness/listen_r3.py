"""A/B listening files for the r3 evenness work (30 s per take, 20-50 s): original / before / new / Pro-DS, each + DELTA."""
import os, sys, subprocess, numpy as np, soundfile as sf
from concurrent.futures import ThreadPoolExecutor
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
from run import DATA
D = r'C:\Users\Shadow\dev\velvox-bench\2026-11-24\evenness'
os.makedirs(D, exist_ok=True)
OLD, NEW = S + r'\exe\base\EmberTests.exe', S + r'\exe\new\EmberTests.exe'
takes = ['J1_chemicals', 'J2_kissed', 'J3_lonely', 'F01', 'M40', 'F_wings', 'F_badboy']
A, B = 20.0, 50.0
jobs = []
for t in takes:
    src = DATA + '/takes/' + t + '.wav'
    jobs += [(OLD, src, f'{t}__02_VORHER_AUTO.wav', ['auto=1']), (OLD, src, f'{t}__03_VORHER_AUTO_DELTA.wav', ['auto=1', 'listen=2']),
             (NEW, src, f'{t}__04_NEU_AUTO.wav', ['auto=1']), (NEW, src, f'{t}__05_NEU_AUTO_DELTA.wav', ['auto=1', 'listen=2'])]


def crop(path):
    x, fs = sf.read(path, always_2d=True)
    sf.write(path, x[int(A * fs):int(B * fs)], fs, subtype='FLOAT')


def run(j):
    exe, src, name, args = j
    o = os.path.join(D, name)
    subprocess.run([exe, '--process', src, o] + args, capture_output=True)
    crop(o)
    return name


with ThreadPoolExecutor(4) as ex:
    for n in ex.map(run, jobs):
        print(n)
for t in takes:
    src = DATA + '/takes/' + t + '.wav'
    a, fs = sf.read(src, always_2d=True)
    sf.write(os.path.join(D, f'{t}__01_Original.wav'), a[int(A * fs):int(B * fs)], fs, subtype='FLOAT')
    pf = DATA + '/prods/' + t + '.wav'
    if os.path.exists(pf):
        b, _ = sf.read(pf, always_2d=True)
        n = min(len(a), len(b))
        aa = np.repeat(a[:, :1], b.shape[1], axis=1) if a.shape[1] != b.shape[1] else a
        sf.write(os.path.join(D, f'{t}__06_ProDS_Standard.wav'), b[int(A * fs):int(B * fs)], fs, subtype='FLOAT')
        sf.write(os.path.join(D, f'{t}__07_ProDS_Standard_DELTA.wav'), (aa[:n] - b[:n])[int(A * fs):int(B * fs)], fs, subtype='FLOAT')
print(len(os.listdir(D)), 'files')
