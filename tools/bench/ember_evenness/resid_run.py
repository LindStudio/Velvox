import pickle,numpy as np,sys,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import ana
from diag import per_event
from resid import feats,resid
from run import TAKES,DATA,CLS,OUT
from concurrent.futures import ProcessPoolExecutor
cfgs=sys.argv[1].split(',')
takes=[os.path.basename(t)[:-4] for t in TAKES] if len(sys.argv)<3 else sys.argv[2].split(',')
def job(a):
    cfg,t=a
    p=DATA+'/prods/'+t+'.wav' if cfg=='prods' else f'{OUT}/{t}__{cfg}.wav'
    if not os.path.exists(p): return cfg,t,None
    C=pickle.load(open(CLS+'/'+t+'.cls','rb'))
    rows=per_event(C,ana.load(p)[0]); red=np.array([r['red'] for r in rows]); X=feats(t,rows)
    c,sd=resid(red,X)
    sl=np.polyfit(X[:,0],red,1)[0]
    return cfg,t,(c[0],c[1],c[2],sd,sl,np.subtract(*np.percentile(red,[75,25])))
if __name__=='__main__':
    with ProcessPoolExecutor(7) as ex:
        R=list(ex.map(job,[(c,t) for c in cfgs for t in takes]))
    for c in cfgs:
        v=[r for cc,t,r in R if cc==c and r]
        vj=[r for (cc,t,r) in R if cc==c and r and t[0]=='J']
        f=lambda v:' '.join('%6.2f'%x for x in np.mean(v,axis=0))
        print('%-9s all: ctx/dur/pk/residsd/slope/iqr %s | J: %s'%(c,f(v) if v else '',f(vj) if vj else ''))
