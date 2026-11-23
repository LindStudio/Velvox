"""Allround check: the takes inside a synthetic 'mix' (a loud band-limited bed under the voice).
usage: python mix.py <exe> name:args ...   (classes from the dry vocal, levels measured on the mix)"""
import sys, os, glob, subprocess, numpy as np, soundfile as sf, scipy.signal as sg
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run, ana
MIX = run.S + '/data/mix'; os.makedirs(MIX, exist_ok=True)
def mixfile(t):
    n = os.path.basename(t)[:-4]; p = f'{MIX}/{n}.wav'
    if not os.path.exists(p):
        x, fs = ana.load(t); rng = np.random.default_rng(1)
        b, a = sg.butter(2, [60 / (fs / 2), 2500 / (fs / 2)], 'band'); bed = sg.lfilter(b, a, rng.standard_normal(len(x)))
        # slow 'chord' movement so it is not stationary
        bed *= 0.7 + 0.3 * np.sin(2 * np.pi * 0.5 * np.arange(len(x)) / fs)
        voc = np.sqrt(np.mean(x[np.abs(x) > 1e-3] ** 2)); bed *= voc / np.sqrt(np.mean(bed ** 2))
        sf.write(p, x + bed, fs, subtype='FLOAT')
    return p
def one(a):
    exe, t, cfg, args = a
    n = os.path.basename(t)[:-4]; C = run.classes(t); m = mixfile(t)
    o = f'{run.OUT}/{n}__mix_{cfg}.wav'
    subprocess.run([exe, '--process', m, o] + args, capture_output=True)
    d, fs = ana.load(m); w, _ = ana.load(o)
    Pd, f, _ = ana.stft(d, fs); Pw, _, _ = ana.stft(w, fs); k = min(len(Pd), len(Pw), len(C.P))
    M = {q: v[:k] for q, v in C.m.items()}
    return cfg, n, dict(S=ana.red(Pd[:k], Pw[:k], f, M['S'], 4000, 12000), core=ana.red(Pd[:k], Pw[:k], f, M['S'], C.peak / 2 ** .5, C.peak * 2 ** .5),
                        vair=ana.red(Pd[:k], Pw[:k], f, M['vowel'], 10000, 16000), vpres=ana.red(Pd[:k], Pw[:k], f, M['vowel'], 2000, 5000))
exe = sys.argv[1]; cfgs = [(s.partition(':')[0], [x for x in s.partition(':')[2].split(',') if x]) for s in sys.argv[2:]]
res = {}
with ThreadPoolExecutor(os.cpu_count()) as ex:
    for cfg, n, r in ex.map(one, [(exe, t, c, a) for c, a in cfgs for t in run.TAKES]):
        res.setdefault(cfg, []).append(r)
print('%-14s %8s %8s %8s %8s' % ('mix config', 'S4-12', 'Kern', 'VokLuft', 'VokPres'))
for c, rs in res.items():
    print('%-14s' % c + ''.join('%9.2f' % -np.nanmean([r[k] for r in rs]) for k in ('S', 'core', 'vair', 'vpres')))
