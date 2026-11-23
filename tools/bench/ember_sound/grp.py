import json,sys,numpy as np,re
r=json.load(open(sys.argv[1] if len(sys.argv)>1 else 'out/last.json'))
G={'tief':['F_wings','M36','M40','M42','M_emerald'],'mittel':['F08','F22','J1_chemicals','M_allittakes'],'hoch':['F01','F_badboy','F_runwild','J2_kissed','J3_lonely','M_notme']}
print('%-10s'%'cfg'+''.join('%26s'%g for g in G))
for c in r:
    if c=='prods': continue
    s='%-10s'%c
    for g,ns in G.items():
        x=[r[c][n] for n in ns]
        s+='   K%5.2f unb%5.1f%% C%5.0f'%(np.mean([v['S_core'] for v in x]),np.mean([v['ev_untouched'] for v in x]),np.mean([v['S_cent_wet']-v['S_cent_dry'] for v in x]))
    print(s)
# AUTO stability across segments
for n in sorted(r['def']):
    fr=[]
    for c in ('auto','auto25','auto50'):
        if c in r: m=re.search(r'freq (\d+) thr (-?[\d.]+) range ([\d.]+)',r[c][n]['log']); fr.append(m.groups())
    if fr: print('%-13s'%n,'  '.join('%s Hz %s dB R%s'%f for f in fr))
