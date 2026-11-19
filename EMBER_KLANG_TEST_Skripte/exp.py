import sys, os, json, re, subprocess
S=sys.argv[1]; sys.path.insert(0,S)
import ana, numpy as np
E='C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
res=json.load(open(S+'/results.json'))
exp=json.load(open(S+'/exp.json')) if os.path.exists(S+'/exp.json') else {}
for name in res:
    log=res[name]['auto_split']['log']
    m=re.search(r'AUTO status 0 freq (\d+) thr (\S+) range (\S+) peakHz (\d+)',log)
    if not m: continue
    fr,thr,rg,pk=m.groups(); pk=min(float(pk),12000)
    base=['freq='+fr,'thr='+thr,'range='+rg,'release=35']
    V={'bell_q1':base+['shape=1','bellhz=%g'%pk,'bellq=1.0'],
       'bell_q07':base+['shape=1','bellhz=%g'%pk,'bellq=0.7'],
       'gatefall5':base+['gatefall=5'],
       'shelf_ref':base}
    d,fs=ana.load(S+'/data/takes/%s.wav'%name); C=ana.Classes(d,fs)
    exp.setdefault(name,{})
    for v,kv in V.items():
        o=f'{S}/out/{name}__x_{v}.wav'
        subprocess.run([E,'--process',S+'/data/takes/%s.wav'%name,o,'trace='+o[:-4]+'_tr.wav']+kv,capture_output=True)
        w,_=ana.load(o); tr,_=ana.load(o[:-4]+'_tr.wav')
        r=ana.metrics(C,w,tr); r.pop('_ev')
        exp[name][v]={k:float(x) for k,x in r.items()}
        print(name,v,'core %.2f above %.2f cent %.0f->%.0f near_air %.3f near_pres %.3f ev_untouched %.1f retrig %.2f'%(r['S_core'],r['S_above'],r['S_cent_dry'],r['S_cent_wet'],r['near_air'],r['near_pres'],r['ev_untouched'],r['retrig_per_S']),flush=True)
    json.dump(exp,open(S+'/exp.json','w'),indent=1)
