import json
from pathlib import Path

def solve(a,b):
    for k in range(len(b)):
        i=max(range(k,len(b)),key=lambda i:abs(a[i][k])); a[k],a[i]=a[i],a[k]; b[k],b[i]=b[i],b[k]
        q=a[k][k]; assert q
        a[k]=[c/q for c in a[k]]; b[k]/=q
        for i in range(len(b)):
            if i==k:continue
            q=a[i][k]; a[i]=[x-q*y for x,y in zip(a[i],a[k])]; b[i]-=q*b[k]
    return b

def circuit(kg):
    delta=kg/5; r=1000
    # Four ideal strain-sensitive arms. A+ on right; sign chosen for illustration.
    rs=[r-delta,r+delta,r+delta,r-delta] # R1,R2,R3,R4
    mat=[[0.]*3 for _ in range(3)]; vec=[0.,0.,5.]; mat[0][2]=mat[2][0]=1
    # Modified nodal analysis, nodes E+, A-, A+ and voltage-source current.
    for row in mat:row.append(0.)
    mat.append([1.,0.,0.,0.]); mat[2][0]=0; mat[0][2]=0; mat[0][3]=1; vec=[0.,0.,0.,5.]
    for a,b,R in [(1,None,rs[0]),(0,1,rs[1]),(2,None,rs[2]),(0,2,rs[3])]:
        g=1/R; mat[a][a]+=g
        if b is not None:mat[b][b]+=g;mat[a][b]-=g;mat[b][a]-=g
    original=[row[:] for row in mat]; v=solve(mat,vec[:]); assert max(abs(sum(x*y for x,y in zip(row,v))-t) for row,t in zip(original,vec))<1e-10
    return {'kg':kg,'a_minus':v[1],'a_plus':v[2],'mv':1000*(v[2]-v[1])}

rows=[circuit(x/4) for x in range(21)]; assert all(abs(r['kg']-r['mv'])<1e-8 for r in rows)
assert circuit(-1)['mv']<0
Path(__file__).with_name('bridge.json').write_text(json.dumps(rows,indent=2));print(json.dumps([rows[i] for i in (0,4,8,12,16,20)]))
