"""Candidate 7: is the de-essing level-independent?  python level.py <exe> [extra args comma separated]
Each take is processed at its own level (ref), at -20 dB and -30 dB (output made up again) and with a slow
level ride (+-8 dB, 5 s period, divided out after processing). Per S event: reduction 4-12 kHz vs the ref."""
import sys, os, glob, subprocess
import numpy as np, soundfile as sf
from concurrent.futures import ProcessPoolExecutor
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
import ana
from run import classes, TAKES
OUT = S + '/out/r3/level'
os.makedirs(OUT, exist_ok=True)


def ride(n, fs):
    t = np.arange(n) / fs
    return 10 ** (8 * np.sin(2 * np.pi * t / 5.0) / 20)


def job(a):
    exe, take, extra, tag = a
    name = os.path.basename(take)[:-4]
    C = classes(take)
    x, fs = sf.read(take, always_2d=True)
    res = {}
    for v, args in (('ref', []), ('m20', ['gain=-20', 'out=20']), ('m30', ['gain=-30', 'out=30']), ('ride', None)):
        o = f'{OUT}/{name}_{tag}_{v}.wav'
        if v == 'ride':
            g = ride(len(x), fs)
            src = f'{OUT}/{name}_{tag}_ridein.wav'
            sf.write(src, x * g[:, None], fs, subtype='FLOAT')
            subprocess.run([exe, '--process', src, o] + extra, capture_output=True)
            w, _ = sf.read(o, always_2d=True)
            sf.write(o, w / g[:len(w), None], fs, subtype='FLOAT')
        else:
            subprocess.run([exe, '--process', take, o] + args + extra, capture_output=True)
        w, _ = ana.load(o)
        r = ana.metrics(C, w)
        res[v] = (r['_ev'], r['S_core'])
    out = {}
    ref = res['ref'][0]
    for v in ('m20', 'm30', 'ride'):
        ev = res[v][0]
        n = min(len(ev), len(ref))
        out[v] = (float(np.mean(np.abs(ev[:n] - ref[:n]))), float(np.mean(ev[:n] - ref[:n])), res[v][1] - res['ref'][1],
                  float(np.corrcoef(ev[:n], ref[:n])[0, 1]))
    return name, out


if __name__ == '__main__':
    exe = sys.argv[1]
    extra = [x for x in (sys.argv[2] if len(sys.argv) > 2 else '').split(',') if x]
    tag = sys.argv[3] if len(sys.argv) > 3 else 'x'
    with ProcessPoolExecutor(max_workers=4) as ex:
        rs = list(ex.map(job, [(exe, t, extra, tag) for t in TAKES]))
    print('take           ' + ''.join('%-34s' % v for v in ('-20 dB |d| d core r', '-30 dB', 'ride +-8 dB')))
    agg = {v: [] for v in ('m20', 'm30', 'ride')}
    for name, o in rs:
        print('%-14s' % name[:14] + ''.join('  %5.2f %6.2f %6.2f %5.3f       ' % o[v] for v in ('m20', 'm30', 'ride')))
        for v in agg:
            agg[v].append(o[v])
    print('%-14s' % 'MEAN' + ''.join('  %5.2f %6.2f %6.2f %5.3f       ' % tuple(np.mean(agg[v], axis=0)) for v in ('m20', 'm30', 'ride')))
