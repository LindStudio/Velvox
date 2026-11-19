import sys, os, json, subprocess, glob
S=sys.argv[1]; sys.path.insert(0,S)
import ana, numpy as np
E='C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
CONFIGS = {
 'def_split': [], 'def_wide': ['mode=1'],
 'tief': ['freq=5000'], 'hoch': ['freq=8000'],
 'det_rel': ['det=1'], 'det_abs': ['det=2'],
 'auto_split': ['auto=1'], 'auto_wide': ['auto=1','mode=1'],
}
only = sys.argv[2].split(',') if len(sys.argv)>2 and sys.argv[2] else None
cfgsel = sys.argv[3].split(',') if len(sys.argv)>3 else list(CONFIGS)
os.makedirs(S+'/out', exist_ok=True)
res = json.load(open(S+'/results.json')) if os.path.exists(S+'/results.json') else {}
for take in sorted(glob.glob(S+'/data/takes/*.wav')):
    name=os.path.basename(take)[:-4]
    if only and name not in only: continue
    d,fs=ana.load(take); C=ana.Classes(d,fs)
    res.setdefault(name,{})
    res[name]['_classes']={k:int(v.sum()) for k,v in C.m.items()} | {'events':len(C.events),'pause':C.ctx.count('pause')}
    for cfg in cfgsel:
        o=f'{S}/out/{name}__{cfg}.wav'; t=f'{S}/out/{name}__{cfg}_tr.wav'
        p=subprocess.run([E,'--process',take,o,'trace='+t]+CONFIGS[cfg],capture_output=True,text=True)
        w,_=ana.load(o); tr,_=ana.load(t)
        r=ana.metrics(C,w,tr); r.pop('_ev')
        r['log']=p.stdout.strip()
        res[name][cfg]={k:(float(v) if isinstance(v,(float,np.floating,int)) else v) for k,v in r.items()}
        print(name,cfg,'S %.2f vowelAir %.2f weak %.2f burst %.2f'%(r['S_4_12'],r['vowel_air'],r['weak_4_12'],r['burst_4_12']), flush=True)
    pf=S+'/data/prods/'+name+'.wav'
    if os.path.exists(pf):
        w,_=ana.load(pf); r=ana.metrics(C,w); r.pop('_ev'); res[name]['prods']={k:float(v) for k,v in r.items()}
    json.dump(res,open(S+'/results.json','w'),indent=1)
