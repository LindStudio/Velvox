"""run every preset candidate on its material: python runp.py <exe> <tag> [name filter]"""
import sys, subprocess, os
from presets import P, args, key
exe, tag = sys.argv[1], sys.argv[2]
flt = sys.argv[3] if len(sys.argv) > 3 else ''
groups = {}
for p in P:
    if flt and flt.lower() not in p[0].lower() and flt != p[1]: continue
    groups.setdefault(p[2], []).append(p)
for mat, ps in groups.items():
    cfgs = ['%s:%s' % (key(p), (args(p) if os.environ.get('USEARGS') else 'preset=' + p[0].replace(' ', '_'))) for p in ps]
    if mat == 'mix':
        out = subprocess.run([sys.executable, 'mix.py', exe] + cfgs, capture_output=True, text=True).stdout
    else:
        out = subprocess.run([sys.executable, 'pb.py', exe, tag + '_' + str(abs(hash(mat)) % 10000), mat] + cfgs, capture_output=True, text=True).stdout
    print('##', mat); print(out[:-1])
