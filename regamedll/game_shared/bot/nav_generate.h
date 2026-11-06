// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace NavGeneration {
// Floor positions against a brush expanded by the player hull. In particular,
// a damage volume below the eyes is still hazardous. Do not mark dry platforms
// above the volume, or treat the entire future sweep of moving water as liquid.
template<class Point>
bool TouchesVolume(const Point &from, const Point &to, const Point &lo, const Point &hi, bool liquid=false) {
 // Liquids use GoldSrc's water-level sample at feet + 1, not a player-box
 // overlap. A walkway just above that sample must remain traversable.
 float first=0, last=1;
 for (int axis=0;axis<3;++axis) {
  float low=lo[axis]-(axis==2?(liquid?1.0f:72.0f):(liquid?0.0f:16.0f));
  float high=hi[axis]+(axis==2?(liquid?-1.0f:-0.1f):(liquid?0.0f:16.0f));
  float delta=to[axis]-from[axis];
  if (delta==0) { if(from[axis]<low || from[axis]>high)return false; }
  else {
   float a=(low-from[axis])/delta, b=(high-from[axis])/delta;
   if(a>b) {float swap=a;a=b;b=swap;}
   if(a>first)first=a;
   if(b<last)last=b;
   if(first>last)return false;
  }
 }
 return true;
}

template<class Point, class Trace>
bool WalkRoute(const Point &from, const Point &to, float step, float jump, Trace clear) {
 Point start=from, end=to;start.z+=18.1f;end.z+=18.1f;
 if(!clear(start,start))start.z+=step;
 if(!clear(end,end))end.z+=step;
 if(!clear(start,start) || !clear(end,end))return false;
 if(clear(start,end))return true;
 // Ordinary step clearance is allowed. A full jump is allowed only when the
 // destination floor actually rises: a flat link cannot encode a fence vault.
 float rise=to.z-from.z;
 float lift=rise>step && rise<=jump ? jump : step;
 float height=(start.z>end.z?start.z:end.z)+lift;
 Point up=start, across=end;up.z=across.z=height;
 return clear(start,up) && clear(up,across) && clear(across,end);
}

template<class Point, class Trace>
bool DropRoute(const Point &from, const Point &to, float step, Trace clear) {
 Point start=from, end=to;start.z+=18.1f;end.z+=18.1f;
 if(!clear(start,start) || !clear(end,end))return false;
 // At most a normal step before leaving the edge. The old eye-height ray
 // could see over a railing without proving the player could cross it.
 Point up=start, across=end;up.z+=step;across.z=up.z;
 return clear(start,up) && clear(up,across) && clear(across,end);
}

template<class Point, class Trace, class Safe>
bool PlayerRoute(const Point &from, const Point &to, float step, float jump,
 bool drop, Trace clear, Safe safe) {
 // Never turn a floor-level damage crossing into a nominally clear step arc.
 if(!safe(from,to))return false;
 return drop ? DropRoute(from,to,step,clear) : WalkRoute(from,to,step,jump,clear);
}
}

