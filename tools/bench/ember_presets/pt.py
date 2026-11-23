"""per-take S core / untouched / cent for configs: python pt.py json [json...]"""
import json, sys, numpy as np
r = {}
for f in sys.argv[1:]: r.update(json.load(open(f)))
names = sorted(next(iter(r.values())).keys())
print('%-14s' % 'take' + ''.join('%16s' % c[:16] for c in r))
for n in names:
    print('%-14s' % n[:14] + ''.join(('%6.2f %3.0f%% %4.0f' % (-d[n]['S_core'], d[n]['ev_untouched'], d[n]['S_cent_wet'] - d[n]['S_cent_dry'])) if n in d else '%16s' % '-' for d in r.values()))
