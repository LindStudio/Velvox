"""r3: why does an S get little reduction? Renders with levels= (detector dB + smart weight), per event:
detector max over the event, weight there, threshold/range from AUTO, applied GR peak, measured red.
usage: python levdiag.py <exe> <tag> [args...]"""
import sys, os, pickle, subprocess, re, numpy as np
from concurrent.futures import ThreadPoolExecutor
S = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, S)
import ana
from run import DATA, CLS, OUT, TAKES
from diag import per_event

exe, tag, args = sys.argv[1], sys.argv[2], sys.argv[3:]


def job(t):
    name = os.path.basename(t)[:-4]
    o, tr, lv = [f'{OUT}/{name}__{tag}{s}.wav' for s in ('', '_tr', '_lv')]
    p = subprocess.run([exe, '--process', t, o, 'trace=' + tr, 'levels=' + lv, 'auto=1'] + args, capture_output=True, text=True)
    m = re.search(r'SETTINGS freq (\S+) thr (\S+) range (\S+)', p.stdout)
    C = pickle.load(open(CLS + '/' + name + '.cls', 'rb'))
    w, _ = ana.load(o); trc, _ = ana.load(tr)
    import soundfile as sf
    L, _ = sf.read(lv, always_2d=True)
    rows = per_event(C, w, trc)
    hop = C.hop; n = int(ana.N_MS * C.fs / 1000)
    for r in rows:
        a, b = r['a'], r['a'] + r['dur']
        seg = slice(a * hop, (b - 1) * hop + n)
        lev = L[seg, 0]; wt = L[seg, 1]
        i = int(np.argmax(lev))
        r['det'] = float(lev[i]); r['wAtMax'] = float(wt[i]); r['wmax'] = float(wt.max())
        # effective: max over the event of level where weight>0.5
        ok = wt > 0.5
        r['detW'] = float(lev[ok].max()) if ok.any() else -99.0
        tr_seg = trc[seg]
        r['grfrac'] = float((tr_seg > 0.5 * max(tr_seg.max(), 1e-3)).mean())
    return name, dict(freq=float(m.group(1)), thr=float(m.group(2)), range=float(m.group(3))), rows


with ThreadPoolExecutor(6) as ex:
    res = {n: (s, r) for n, s, r in ex.map(job, TAKES)}
pickle.dump(res, open(f'{OUT}/levdiag_{tag}.pkl', 'wb'))
print('done', tag)
