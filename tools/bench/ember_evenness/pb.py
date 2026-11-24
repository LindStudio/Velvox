"""Preset bench (r2/ember-presets): run configs over a chosen set of takes, keep per-take results.
usage: python pb.py <exe> <tag> <takes: group name or comma list of take stems or 'all'> name:arg,arg ...
groups: all deep mid high male female genre (genre = data/genre/*.wav, made by mkgenre.py)
results: out/pb_<tag>.json ; prints mean per config (+ per take with -v in env PBV=1)
"""
import sys, os, glob, json
from concurrent.futures import ProcessPoolExecutor
import numpy as np
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
import run

T = run.DATA + '/takes/'
G = run.DATA + '/genre/'
GROUPS = {
    'deep': ['F_wings', 'M36', 'M40', 'M42', 'M_emerald'],
    'mid': ['F08', 'F22', 'J1_chemicals', 'M_allittakes'],
    'high': ['F01', 'F_badboy', 'F_runwild', 'J2_kissed', 'J3_lonely', 'M_notme'],
    'male': ['M36', 'M40', 'M42', 'M_emerald', 'M_allittakes', 'M_notme'],
    'female': ['F01', 'F08', 'F22', 'F_badboy', 'F_runwild', 'F_wings'],
}
GROUPS['all'] = sorted(sum((GROUPS[g] for g in ('deep', 'mid', 'high')), []))


def files(spec):
    out = []
    for part in spec.split('+'):
        if part == 'genre':
            out += sorted(glob.glob(G + '*.wav'))
        elif part in GROUPS:
            out += [T + n + '.wav' for n in GROUPS[part]]
        else:
            for n in part.split(','):
                out.append(T + n + '.wav' if os.path.exists(T + n + '.wav') else G + n + '.wav')
    return out


def fmt(o):
    return ''.join('%9.2f' % (-o[k] if k not in ('ratio', 'cent', 'ev_untouched', 'ev_over8', 'vowel_gr_frames_pct', 'purity', 'retrig_per_S', 'harsh_slope', 'spread', 'ev_iqr', 'res_sd', 'res_ctx') else o[k]) for k, _ in run.KEYS)


if __name__ == '__main__':
    exe, tag, spec = sys.argv[1], sys.argv[2], sys.argv[3]
    takes = files(spec)
    cfgs = []
    for sp in sys.argv[4:]:
        name, _, a = sp.partition(':')
        cfgs.append((name, [x for x in a.split(',') if x]))
    res = {}
    with ProcessPoolExecutor(max_workers=7) as ex:
        jobs = [(exe, t, c, a) for c, a in cfgs for t in takes]
        for name, cfg, r in ex.map(run.job, jobs):
            res.setdefault(cfg, {})[name] = r
    json.dump(res, open(run.OUT + '/pb_' + tag + '.json', 'w'), indent=1, default=float)
    print('%-18s' % 'config' + ''.join('%9s' % h for _, h in run.KEYS))
    for cfg, d in res.items():
        print('%-18s' % cfg + fmt(run.row(list(d.values()))))
        if os.environ.get('PBV'):
            for n, r in sorted(d.items()):
                print('   %-15s' % n[:15] + fmt(run.row([r])))
