import json,sys
r=json.load(open('out/last.json'))
cfgs=sys.argv[1].split(',')
print('%-13s'%'take'+''.join('%22s'%c for c in cfgs))
for n in sorted(r[cfgs[0]]):
    s='%-13s'%n
    for c in cfgs:
        x=r[c][n]; s+='   K%5.1f L%5.1f C%5.0f'%(x['S_core'],x['S_above'],x['S_cent_wet']-x['S_cent_dry'])
    print(s)
