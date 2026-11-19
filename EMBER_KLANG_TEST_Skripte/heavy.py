import sys, os, json, subprocess
S=sys.argv[1]; sys.path.insert(0,S)
import ana, numpy as np
E='C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
V={'kraeftig_split':['thr=-40','range=9','release=45'],'kraeftig_wide':['thr=-40','range=9','release=45','mode=1'],
   'amount100_split':['thr=-40','range=9','release=45','amount=1'],'amount100_wide':['thr=-40','range=9','release=45','amount=1','mode=1']}
out={}
for name in ['F01','F22','J1_chemicals','J3_lonely','M40','M_notme','M36','F_runwild']:
    d,fs=ana.load(S+'/data/takes/%s.wav'%name); C=ana.Classes(d,fs)
    for v,kv in V.items():
        o=f'{S}/out/{name}__h_{v}.wav'
        subprocess.run([E,'--process',S+'/data/takes/%s.wav'%name,o,'trace='+o[:-4]+'_tr.wav']+kv,capture_output=True)
        w,_=ana.load(o); tr,_=ana.load(o[:-4]+'_tr.wav')
        r=ana.metrics(C,w,tr); r.pop('_ev'); out.setdefault(name,{})[v]={k:float(x) for k,x in r.items()}
        print(name,v,'S4-12 %.2f core %.2f above %.2f cent %.0f->%.0f evmax %.1f over8 %.0f%% weak %.2f burst %.2f grmax %.1f'%(r['S_4_12'],r['S_core'],r['S_above'],r['S_cent_dry'],r['S_cent_wet'],r['ev_max'],r['ev_over8'],r['weak_4_12'],r['burst_4_12'],r['gr_peak_max']),flush=True)
json.dump(out,open(S+'/heavy.json','w'),indent=1)
