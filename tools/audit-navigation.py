#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from pathlib import Path
import struct,re,json,math,argparse

def nav(path):
 b=path.read_bytes();p=0
 def r(fmt):
  nonlocal p
  v=struct.unpack_from('<'+fmt,b,p);p+=struct.calcsize('<'+fmt);return v if len(v)>1 else v[0]
 def skip(n):
  nonlocal p
  p+=n
 magic,version,size=r('III');assert magic==0xFEEDFACE and version==5
 for _ in range(r('H')):skip(r('H'))
 n=r('I');areas={}
 for _ in range(n):
  id=r('I');attr=r('B');e=r('6f');ne,sw=r('2f');adj=[]
  for d in range(4):adj.extend(r('I') for i in range(r('I')))
  skip(r('B')*17);skip(r('B')*14)
  for z in range(r('I')):r('IBIB');skip(r('B')*5)
  r('H');areas[id]=(e,adj,ne,sw)
 assert p==len(b)
 return areas,size

def nearest(areas,pos):
 best=None
 for id,(e,adj,ne,sw) in areas.items():
  x=max(e[0],min(e[3],pos[0]));y=max(e[1],min(e[4],pos[1]));tx=(x-e[0])/max(1,e[3]-e[0]);ty=(y-e[1])/max(1,e[4]-e[1]);z=(e[2]*(1-tx)+ne*tx)*(1-ty)+(sw*(1-tx)+e[5]*tx)*ty
  dist=math.sqrt((x-pos[0])**2+(y-pos[1])**2+(z-pos[2])**2)
  if best is None or dist<best[0]:best=(dist,id)
 return best

def segment_intersects_box(start,end,box):
 low,high=0.0,1.0
 for axis in range(3):
  delta=end[axis]-start[axis]
  if abs(delta)<1e-6:
   if not box[axis]<=start[axis]<=box[axis+3]:return False
  else:
   first,last=sorted(((box[axis]-start[axis])/delta,(box[axis+3]-start[axis])/delta))
   low,high=max(low,first),min(high,last)
 return low<=high

def audit(path,bsp):
 name=bsp.stem
 a,size=nav(path);b=bsp.read_bytes();assert len(b)==size
 o,n=struct.unpack_from('<ii',b,4);mo,_=struct.unpack_from('<ii',b,116);spawns=[];goals=[]
 for e in re.findall(r'\{([^}]+)\}',b[o:o+n].decode(errors='replace')):
  d=dict(re.findall(r'"([^\"]*)"\s*"([^\"]*)"',e));cl=d.get('classname')
  if cl not in ['info_player_start','info_player_deathmatch','info_bomb_target','func_bomb_target']:continue
  if d.get('model','').startswith('*'):
   bounds=struct.unpack_from('<6f',b,mo+int(d['model'][1:])*64);pos=tuple((bounds[i]+bounds[i+3])/2 for i in range(3))
  else:pos=tuple(map(float,d.get('origin','0 0 0').split()))
  near=nearest(a,pos)
  (spawns if cl.startswith('info_player') else goals).append((pos,near))
 badpaths=[]
 for pos,(_,start) in spawns:
  seen={start};todo=[start]
  while todo:
   for target in a[todo.pop()][1]:
    assert target in a
    if target not in seen:seen.add(target);todo.append(target)
  for goal,(_,target) in goals:
   if target not in seen:badpaths.append((start,target))
 result={'map':name,'areas':len(a),'spawns':len(spawns),'objectives':len(goals),'max_spawn_distance':round(max(v[1][0] for v in spawns),1),'objective_distances':[round(v[1][0],1) for v in goals],'unreachable_spawn_objective_pairs':len(badpaths)}
 if name=='de_downtown_csneo':result['barrier_overlaps']=[id for id,(e,*_) in a.items() if e[0]<-1332 and e[3]>-1800 and e[1]<-1644 and e[4]>-1681 and e[2]<100]
 if name=='de_mirage':
  # Masked barbed-wire models *61/*62, on the route out of T spawn.
  # The visible meshes are illusionaries; the BSP player collision blocks them.
  boxes=[(1001,-236,24,1003,-56,120),(1001,-48,24,1003,132,120)]
  overlaps=set();crossings=set()
  for id,(e,adj,ne,sw) in a.items():
   for box in boxes:
    if e[0]<box[3] and e[3]>box[0] and e[1]<box[4] and e[4]>box[1] and min(e[2],e[5],ne,sw)<box[5] and max(e[2],e[5],ne,sw)+36>box[2]:overlaps.add(id)
   start=((e[0]+e[3])/2,(e[1]+e[4])/2,(e[2]+e[5]+ne+sw)/4+18)
   for target in adj:
    f,_,ne2,sw2=a[target];end=((f[0]+f[3])/2,(f[1]+f[4])/2,(f[2]+f[5]+ne2+sw2)/4+18)
    if any(segment_intersects_box(start,end,box) for box in boxes):crossings.add((id,target))
  result['spawn_fence_overlaps']=sorted(overlaps)
  result['spawn_fence_crossings']=sorted(crossings)
 return result
if __name__=='__main__':
 parser=argparse.ArgumentParser(description="Validate GoldSrc NAV v5 structure and spawn-to-bombsite reachability")
 parser.add_argument('nav',type=Path)
 parser.add_argument('bsp',type=Path)
 args=parser.parse_args()
 result=audit(args.nav,args.bsp)
 print(json.dumps(result,indent=2))
 raise SystemExit(1 if result['unreachable_spawn_objective_pairs'] or result.get('barrier_overlaps') or result.get('spawn_fence_overlaps') or result.get('spawn_fence_crossings') else 0)
