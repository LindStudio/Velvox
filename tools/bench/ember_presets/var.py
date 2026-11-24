"""variants of a factory preset on its material: python var.py <exe> "<Preset name>" tag k=v,k=v tag2 k=v ...
keys: freq thr range amount(%) attack release la width; 'ref' = the preset as it is"""
import sys, subprocess
from presets import P, args
exe, name = sys.argv[1], sys.argv[2]
p = [q for q in P if q[0] == name][0]
idx = dict(freq=3, thr=4, range=5, amount=6, attack=7, release=8, la=9, width=10)
cfgs = ['ref:' + args(p)]
rest = sys.argv[3:]
for tag, ov in zip(rest[::2], rest[1::2]):
    q = list(p)
    for kv in ov.split(','):
        k, v = kv.split('=')
        q[idx[k]] = float(v)
    cfgs.append(tag + ':' + args(tuple(q)))
out = subprocess.run([sys.executable, 'pb.py', exe, 'var', p[2]] + cfgs, capture_output=True, text=True)
print('##', name, p[2]); print(out.stdout[:-1] or out.stderr[-2000:])
