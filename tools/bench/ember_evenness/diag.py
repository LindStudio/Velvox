"""r3 evenness diagnosis: per-S reduction (4-12 kHz, as ana.ev_iqr) for EMBER renders vs Pro-DS, with S features.
usage: python diag.py <cfg> [cfg2 ...]   (renders out/r3/<take>__<cfg>.wav from run.py; Pro-DS from data/prods)
Prints per take iqr and correlations; writes out/r3/diag_<cfg>.csv"""
import sys, os, pickle, numpy as np
S = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, S)
import ana
from run import DATA, CLS, OUT, TAKES


def per_event(C, wet, trace=None):
    Pw, f, _ = ana.stft(wet, C.fs)
    k = min(len(Pw), len(C.P)); Pd = C.P[:k]; Pw = Pw[:k]
    mm = (f >= 4000) & (f < 12000)
    sel = (f >= 3500) & (f <= 12000)
    rows = []
    hop = C.hop; n = int(ana.N_MS * C.fs / 1000)
    for i, (a, b) in enumerate(C.events):
        if b > k: continue
        sd = Pd[a:b].sum(axis=0); sw = Pw[a:b].sum(axis=0)
        hd = 10 * np.log10(sd[mm].sum() / (b - a) + 1e-20); hw = 10 * np.log10(sw[mm].sum() / (b - a) + 1e-20)
        sm = np.convolve(sd, np.ones(5) / 5, mode='same'); pk = f[sel][np.argmax(sm[sel])]
        # frame-wise reduction inside the event
        fr = 10 * np.log10(Pd[a:b][:, mm].sum(1) / (Pw[a:b][:, mm].sum(1) + 1e-20) + 1e-20)
        lvl = C.lvl[a:b].max() - C.L
        ratio = C.ratio[a:b].max()
        tr = trace[a * hop:(b - 1) * hop + n].max() if trace is not None else np.nan
        rows.append(dict(ev=i, a=a, dur=b - a, hd=hd, red=hd - hw, pk=pk, lvl=lvl, ratio=ratio, ctx=C.ctx[i],
                         frmax=fr.max(), frmin=fr.min(), trmax=tr, slv=C.Slevel[i]))
    return rows


def iqr(v): return float(np.percentile(v, 75) - np.percentile(v, 25))


if __name__ == '__main__':
    cfgs = sys.argv[1:]
    allr = {}
    for t in TAKES:
        name = os.path.basename(t)[:-4]
        C = pickle.load(open(CLS + '/' + name + '.cls', 'rb'))
        line = '%-13s n=%3d' % (name, len(C.events))
        for cfg in cfgs + (['prods'] if os.path.exists(DATA + '/prods/' + name + '.wav') else []):
            p = DATA + '/prods/' + name + '.wav' if cfg == 'prods' else f'{OUT}/{name}__{cfg}.wav'
            if not os.path.exists(p): continue
            w, _ = ana.load(p)
            trp = f'{OUT}/{name}__{cfg}_tr.wav'
            tr = ana.load(trp)[0] if os.path.exists(trp) else None
            rows = per_event(C, w, tr)
            allr.setdefault(cfg, {})[name] = rows
            red = np.array([r['red'] for r in rows])
            line += '  %s iqr %.2f med %.2f p10 %.2f p90 %.2f' % (cfg[:8], iqr(red), np.median(red), np.percentile(red, 10), np.percentile(red, 90))
        print(line)
    pickle.dump(allr, open(OUT + '/diag_' + '_'.join(cfgs) + '.pkl', 'wb'))
