#!/usr/bin/env python3
"""Read-only BSP/NAV audit. Findings are collision-backed geometric candidates,
not a simulation of bot steering or a proof that an entire route is unusable."""
import argparse, hashlib, importlib.util, json, math, re, struct
from pathlib import Path
spec=importlib.util.spec_from_file_location("nav_audit",Path(__file__).with_name("audit-navigation.py"))
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,n):return tuple(x*n for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def bbox(v):return tuple(min(p[k] for p in v) for k in range(3))+tuple(max(p[k] for p in v) for k in range(3))
def height(area,x,y):
 e,_,ne,sw=area;t=max(0,min(1,(x-e[0])/max(.01,e[3]-e[0])));u=max(0,min(1,(y-e[1])/max(.01,e[4]-e[1])))
 return (e[2]*(1-t)+ne*t)*(1-u)+(sw*(1-t)+e[5]*t)*u

def on_face(face,p):
 if any(p[k]<face['bounds'][k]-.1 or p[k]>face['bounds'][k+3]+.1 for k in range(3)):return False
 signs=[dot(cross(sub(b,a),sub(p,a)),face['normal']) for a,b in zip(face['vertices'],face['vertices'][1:]+face['vertices'][:1])]
 return min(signs)>=-.1 or max(signs)<=.1

class BSP:
 def __init__(self,path):
  self.data=path.read_bytes();b=self.data
  if struct.unpack_from('<i',b)[0]!=30:raise ValueError('BSP is not GoldSrc version 30')
  lumps=[struct.unpack_from('<ii',b,4+i*8) for i in range(15)]
  def records(i,fmt):
   off,size=lumps[i];assert off>=0 and off+size<=len(b);return list(struct.iter_unpack('<'+fmt,b[off:off+size]))
  self.planes=records(1,'4fi');self.clips=records(9,'ihh');self.models=records(14,'9f7i')
  eo,en=lumps[0];self.entities={0:{'classname':'worldspawn'}}
  for e in re.findall(r'\{([^}]+)\}',b[eo:eo+en].decode(errors='replace')):
   d=dict(re.findall(r'"([^\"]*)"\s*"([^\"]*)"',e));m=d.get('model','')
   if m.startswith('*'):self.entities[int(m[1:])]=d
  to,tn=lumps[2];textures=[]
  for i in range(struct.unpack_from('<i',b,to)[0]):
   offset=struct.unpack_from('<i',b,to+4+4*i)[0]
   textures.append(b[to+offset:to+offset+16].split(b'\0')[0].decode(errors='replace') if offset>=0 else '')
  faces=records(7,'Hhihh4Bi');texinfo=records(6,'8fii');vertices=records(3,'3f');edges=records(12,'HH');surfedges=records(13,'i')
  self.barriers=[];self.solids=[]
  for model,m in enumerate(self.models):
   entity=self.entities.get(model,{});cls=entity.get('classname','unknown')
   origin=tuple(map(float,entity.get('origin','0 0 0').split()))
   rotated=any(float(v)!=0 for v in entity.get('angles','0 0 0').split()) or float(entity.get('angle','0'))!=0
   if model==0 or (cls=='func_wall' and not rotated):self.solids.append((model,origin))
   if cls in ('func_door','func_door_rotating','func_breakable','func_pushable'):continue
   transparent=cls=='func_wall' and int(entity.get('rendermode','0'))!=0
   for fi in range(m[14],m[14]+m[15]):
    f=faces[fi];name=textures[texinfo[f[4]][8]];normal=self.planes[f[0]][:3];dist=self.planes[f[0]][3]
    if abs(normal[2])>.35 or not (name.startswith('{') or transparent or re.search(r'fence|grate|razor|wire|net',name,re.I)):continue
    verts=[]
    for j in range(f[3]):
     edge=surfedges[f[2]+j][0];verts.append(add(vertices[edges[abs(edge)][0 if edge>=0 else 1]],origin))
    box=bbox(verts)
    if box[5]-box[2]<24 or max(box[3]-box[0],box[4]-box[1])<24:continue
    self.barriers.append(dict(face=fi,model=model,classname=cls,texture=name,normal=normal,dist=dist+dot(origin,normal),vertices=verts,bounds=box,rotated=rotated))
 def solid_segment(self,node,a,b,depth=0):
  if node<0:return node==-2
  if depth>256:raise ValueError('clip tree depth limit')
  plane,front,back=self.clips[node];normal=self.planes[plane][:3];distance=self.planes[plane][3]
  da=dot(a,normal)-distance;db=dot(b,normal)-distance
  if da>=0 and db>=0:return self.solid_segment(front,a,b,depth+1)
  if da<0 and db<0:return self.solid_segment(back,a,b,depth+1)
  t=da/(da-db);middle=add(a,mul(sub(b,a),t))
  first,second=(front,back) if da>=0 else (back,front)
  return self.solid_segment(first,a,middle,depth+1) or self.solid_segment(second,middle,b,depth+1)
 def blocked(self,start,end):
  for model,origin in self.solids:
   if self.solid_segment(self.models[model][12],sub(start,origin),sub(end,origin)):return model
  return None

def intersection(face,start,end):
 da=dot(start,face['normal'])-face['dist'];db=dot(end,face['normal'])-face['dist']
 if da*db>=0 or abs(da-db)<1e-6:return None
 point=add(start,mul(sub(end,start),da/(da-db)))
 return point if on_face(face,point) else None

def scan(bsp_path,nav_path):
 bsp=BSP(bsp_path);areas,size=audit.nav(nav_path,allow_trailing=True)
 if size!=len(bsp.data):raise ValueError('NAV BSP-size mismatch')
 findings={};visual_crossings=set();dynamic=set()
 def record(face,kind,ids,point,start,end):
  key=(face['model'],face['texture'],tuple(round(v/32) for v in point))
  if face['rotated'] or face['classname'] not in ('worldspawn','func_wall','func_illusionary'):
   dynamic.add((face['model'],face['classname']));return
  # Local crossing at player step clearance avoids unrelated ground collision on
  # a long nav edge. Crouched hull is more permissive than the standing player.
  horizontal=math.hypot(face['normal'][0],face['normal'][1]);n=mul(face['normal'],1/horizontal);n=(n[0],n[1],0)
  a=add(point,mul(n,32));b=sub(point,mul(n,32))
  block=bsp.blocked(a,b)
  if block is None:visual_crossings.add(key);return
  issue=findings.setdefault(key,{'model':face['model'],'classname':face['classname'],'texture':face['texture'],'position':[round(v,1) for v in point],'collision_model':block,'probe_start':a,'probe_end':b,'probe_start_solid':bsp.blocked(a,a) is not None,'probe_end_solid':bsp.blocked(b,b) is not None,'area_ids':set(),'edge_ids':set(),'kinds':set()})
  issue['kinds'].add(kind)
  (issue['area_ids'] if kind=='area_crossing' else issue['edge_ids']).add(ids)
 for id,area in areas.items():
  e,adj,ne,sw=area
  for face in bsp.barriers:
   box=face['bounds']
   if e[3]<box[0] or e[0]>box[3] or e[4]<box[1] or e[1]>box[4] or max(e[2],e[5],ne,sw)+54<box[2] or min(e[2],e[5],ne,sw)+36>box[5]:continue
   # Sample a crossing inside the area, using a line through its center parallel
   # to the material normal. Only both-inside crossings implicate the area.
   nx,ny,_=face['normal'];length=math.hypot(nx,ny);nx/=length;ny/=length
   center=((max(e[0],box[0])+min(e[3],box[3]))/2,(max(e[1],box[1])+min(e[4],box[4]))/2)
   z=height(area,*center)+36.1
   denominator=nx*face['normal'][0]+ny*face['normal'][1]
   t=(face['dist']-dot((center[0],center[1],z),face['normal']))/denominator
   point=(center[0]+nx*t,center[1]+ny*t,z)
   a=(point[0]-nx*20,point[1]-ny*20,z);b=(point[0]+nx*20,point[1]+ny*20,z)
   if all(e[0]<=v[0]<=e[3] and e[1]<=v[1]<=e[4] for v in (a,b)) and on_face(face,point):record(face,'area_crossing',id,point,a,b)
  center=((e[0]+e[3])/2,(e[1]+e[4])/2);start=(*center,height(area,*center)+36.1)
  for target in adj:
   if target not in areas:raise ValueError('missing adjacent area')
   f=areas[target][0];c=((f[0]+f[3])/2,(f[1]+f[4])/2);end=(*c,height(areas[target],*c)+36.1)
   # Adjacent rectangular areas traverse their shared portal, not necessarily
   # the straight line between centers (which can cut outside the nav mesh).
   portal=None
   if abs(e[3]-f[0])<.1 or abs(f[3]-e[0])<.1:
    lo,hi=max(e[1],f[1]),min(e[4],f[4])
    if lo<=hi:portal=((e[3] if abs(e[3]-f[0])<.1 else e[0]),(lo+hi)/2)
   elif abs(e[4]-f[1])<.1 or abs(f[4]-e[1])<.1:
    lo,hi=max(e[0],f[0]),min(e[3],f[3])
    if lo<=hi:portal=((lo+hi)/2,(e[4] if abs(e[4]-f[1])<.1 else e[1]))
   points=[start,end] if portal is None else [start,(*portal,max(height(area,*portal),height(areas[target],*portal))+36.1),end]
   for a,b in zip(points,points[1:]):
    for face in bsp.barriers:
     point=intersection(face,a,b)
     if point:record(face,'edge_crossing',(id,target),point,a,b)
 for item in findings.values():
  item['classification']='solid_endpoint_needs_review' if item['probe_start_solid'] or item['probe_end_solid'] else 'clear_endpoints_blocked_crossing'
  for field in ['area_ids','edge_ids','kinds']:item[field]=sorted(item[field])
 return {'areas':len(areas),'candidate_faces':len(bsp.barriers),'collision_backed_candidates':list(findings.values()),'nonblocking_visual_crossings':len(visual_crossings),'dynamic_or_rotated_models_needing_engine_check':sorted(dynamic)}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('snapshot',type=Path);ap.add_argument('output',type=Path);args=ap.parse_args();report=[]
 for row in json.loads((args.snapshot/'catalog.json').read_text()):
  for variant in row['variants']:
   item={'map':row['id'],'enabled':row['enabled'],'nav_source':variant['source'],'nav_sha256':variant['sha256'],'bsp_sha256':row.get('bspSha256')}
   try:item.update(scan(args.snapshot/row['id']/(row['id']+'.bsp'),args.snapshot/variant['file']))
   except Exception as error:item['error']=type(error).__name__+': '+str(error)
   report.append(item);print(row['id'],item.get('areas'),len(item.get('collision_backed_candidates',[])),item.get('error',''),flush=True)
 args.output.write_text(json.dumps({'method':'Static BSP crouched-hull collision correlated with sampled NAV area and portal crossings; candidates require gameplay review. Doors and breakables excluded; dynamic and rotated geometry flagged separately.','results':report},indent=2))
if __name__=='__main__':main()
