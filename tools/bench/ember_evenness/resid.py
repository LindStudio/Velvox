"""residual analysis: per-S reduction vs S energy, then residual vs context / peak freq / duration"""
import pickle,numpy as np,sys
def feats(t,rows):
    C=pickle.load(open('C:/Users/Shadow/dev/velvox-wt/ember-fixes/build/bench/out/'+t+'.cls','rb'))
    X=[]
    for r in rows:
        a,b=r['a'],r['a']+r['dur']
        prev=C.lvl[max(0,a-25):a+1].max() if a>0 else -200
        own=C.lvl[a:b].max()
        X.append([r['hd']-C.L, max(prev-own,0), r['dur'], np.log2(r['pk'])])
    return np.array(X)
def resid(red,X):
    sel=red>0.3
    A=np.c_[X[sel][:,0],np.ones(sel.sum())]
    c=np.linalg.lstsq(A,red[sel],rcond=None)[0]
    r=red[sel]-A@c
    return [np.corrcoef(X[sel][:,j],r)[0,1] for j in (1,2,3)], r.std()
