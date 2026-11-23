"""group summary of a pb json: python grp2.py out/pb_tag.json"""
import json, sys, numpy as np
sys.path.insert(0, '.')
from pb import GROUPS
r = json.load(open(sys.argv[1]))
K = [('S_core', 'Kern', -1), ('S_above', 'Luft', -1), ('ev_untouched', 'unb%', 1), ('ev_over8', '>8%', 1), ('cent', 'Cent', 1),
     ('vowel_air', 'VLuft', -1), ('vowel_pres', 'VPres', -1), ('near_air', 'nebS', -1), ('purity', 'Rein', 1), ('weak_4_12', 'f/l', -1), ('burst_4_12', 't/k', -1)]
def val(x, k):
    return x['S_cent_wet'] - x['S_cent_dry'] if k == 'cent' else x[k]
gs = [g for g in ('deep', 'mid', 'high', 'all')]
print('%-16s %-5s' % ('cfg', 'grp') + ''.join('%8s' % h for _, h, _ in K))
for c, d in r.items():
    for g in gs:
        xs = [d[n] for n in GROUPS[g] if n in d]
        if not xs: continue
        print('%-16s %-5s' % (c[:16], g) + ''.join('%8.2f' % (s * np.nanmean([val(x, k) for x in xs])) for k, _, s in K))
