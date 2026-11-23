"""Run EmberTests --process over all takes for named configs, print mean metrics.
usage: python run.py <exe> name:arg,arg name2:arg ...   (args are key=value for --process, comma separated)
"""
import sys, os, glob, pickle, subprocess, json
from concurrent.futures import ProcessPoolExecutor
import numpy as np
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
sys.path.insert(1, os.path.dirname(S))   # deess_metrics.py (tools/bench)
import ana, deess_metrics as dm

TAKES = sorted(glob.glob(S + '/data/takes/*.wav'))
OUT = S + '/out'
BASE = os.path.join(S, 'base', 'EmberTests.exe')
os.makedirs(OUT, exist_ok=True)


def classes(take):
    p = OUT + '/' + os.path.basename(take)[:-4] + '.cls'
    if os.path.exists(p):
        return pickle.load(open(p, 'rb'))
    d, fs = ana.load(take)
    C = ana.Classes(d, fs)
    pickle.dump(C, open(p, 'wb'))
    return C


def job(a):
    exe, take, cfg, args = a
    name = os.path.basename(take)[:-4]
    C = classes(take)
    o = f'{OUT}/{name}__{cfg}.wav'
    t = f'{OUT}/{name}__{cfg}_tr.wav'
    p = subprocess.run([exe, '--process', take, o, 'trace=' + t] + args, capture_output=True, text=True)
    w, _ = ana.load(o)
    tr, _ = ana.load(t)
    r = ana.metrics(C, w, tr)
    r.pop('_ev')
    d, fs = ana.load(take)
    r['purity'] = dm.metrics(d, w, fs)['purity']
    r['log'] = p.stdout.strip()
    return name, cfg, r


def prods(take):
    name = os.path.basename(take)[:-4]
    pf = S + '/data/prods/' + name + '.wav'
    if not os.path.exists(pf):
        return None
    w, _ = ana.load(pf)
    r = ana.metrics(classes(take), w)
    r.pop('_ev')
    d, fs = ana.load(take)
    r['purity'] = dm.metrics(d, w, fs)['purity']
    return name, 'prods', r


KEYS = [('S_4_12', 'S4-12'), ('S_core', 'Kern'), ('S_above', 'Luft'), ('ratio', 'L/K'), ('cent', 'Schwerp'),
        ('ev_untouched', 'unber%'), ('ev_over8', '>8dB%'), ('weak_4_12', 'f/leise'), ('burst_4_12', 't/k'),
        ('vowel_air', 'VokLuft'), ('vowel_pres', 'VokPres'), ('near_air', 'nebenS'), ('vowel_gr_frames_pct', 'VokGR%'), ('purity', 'Rein%'), ('retrig_per_S', 'retrig')]


def row(rs):
    out = {}
    for k, _ in KEYS:
        if k == 'ratio':
            v = [r['S_above'] / r['S_core'] for r in rs if r['S_core'] > 0.2]
        elif k == 'cent':
            v = [r['S_cent_wet'] - r['S_cent_dry'] for r in rs]
        else:
            v = [r[k] for r in rs if k in r and r[k] == r[k]]
        out[k] = float(np.mean(v)) if v else float('nan')
    return out


def show(title, res, names=None):
    print(title)
    print('%-14s' % 'config' + ''.join('%9s' % h for _, h in KEYS))
    for cfg, d in res.items():
        rs = [r for n, r in d.items() if names is None or n in names]
        if not rs:
            continue
        o = row(rs)
        print('%-14s' % cfg + ''.join('%9.2f' % (-o[k] if k not in ('ratio', 'cent', 'ev_untouched', 'ev_over8', 'vowel_gr_frames_pct', 'purity', 'retrig_per_S') else o[k]) for k, _ in KEYS))


if __name__ == '__main__':
    exe = sys.argv[1]
    cfgs = []
    for spec in sys.argv[2:]:
        name, _, a = spec.partition(':')
        cfgs.append((name, [x for x in a.split(',') if x]))
    res = {}
    with ProcessPoolExecutor(max_workers=os.cpu_count()) as ex:
        jobs = [((BASE if c.startswith('B_') else exe), t, c, a) for c, a in cfgs for t in TAKES]
        pj = [ex.submit(prods, t) for t in TAKES]
        for name, cfg, r in ex.map(job, jobs):
            res.setdefault(cfg, {})[name] = r
        for f in pj:
            x = f.result()
            if x:
                res.setdefault('prods', {})[x[0]] = x[2]
    json.dump(res, open(OUT + '/last.json', 'w'), indent=1, default=float)
    for cfg in list(res):
        if cfg != 'prods' and len(res[cfg]) == 0: res.pop(cfg)
    show('== alle 15 Takes (Mittel) ==', {k: v for k, v in res.items() if k != 'prods'})
    show('== nur J1-J3 (mit Pro-DS) ==', res, {'J1_chemicals', 'J2_kissed', 'J3_lonely'})
    for cfg, d in res.items():
        for n, r in sorted(d.items()):
            if 'AUTO' in r.get('log', ''):
                print(cfg, n, r['log'].splitlines()[0])
