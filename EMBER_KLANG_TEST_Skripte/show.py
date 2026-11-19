import sys, json, numpy as np
S=sys.argv[1]; res=json.load(open(S+'/results.json'))
keys=sys.argv[2].split(',')
cfgs=sys.argv[3].split(',')
for cfg in cfgs:
    print('==',cfg); print('%-14s'%'take'+''.join('%13s'%k[:12] for k in keys))
    rows=[]
    for t,v in res.items():
        if cfg in v:
            row=[v[cfg].get(k,np.nan) for k in keys]; rows.append(row)
            print('%-14s'%t+''.join('%13.2f'%x if isinstance(x,(int,float)) else '%13s'%x for x in row))
    if rows: print('%-14s'%'MEAN'+''.join('%13.2f'%np.nanmean([r[i] for r in rows]) for i in range(len(keys))))
