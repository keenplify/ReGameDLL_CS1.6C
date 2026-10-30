// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
namespace CompetitiveUtility {
struct Vec {
 float x=0,y=0,z=0;
 Vec() = default;
 Vec(float a,float b,float c):x(a),y(b),z(c) {}
 Vec operator+(const Vec &v) const {return {x+v.x,y+v.y,z+v.z};}
 Vec operator-(const Vec &v) const {return {x-v.x,y-v.y,z-v.z};}
 Vec operator*(float f) const {return {x*f,y*f,z*f};}
 float Dot(const Vec &v) const {return x*v.x+y*v.y+z*v.z;}
 float LengthSq() const {return Dot(*this);}
};
inline Vec Direction(float pitch,float yaw) {
 const float rad=0.0174532925199433f;
 float p=pitch*rad,y=yaw*rad;
 return {std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),-std::sin(p)};
}
struct Launch { Vec source,velocity; };
inline Launch NativeLaunch(const Vec &eye,const Vec &movement,float viewPitch,float yaw) {
 float pitch=-10+viewPitch*(viewPitch<0?80.0f/90.0f:100.0f/90.0f);
 float speed=(90-pitch)*6;
 if(speed>750) speed=750;
 auto forward=Direction(pitch,yaw);
 return {eye+forward*16,forward*speed+movement};
}
struct Hit { float fraction=1; bool solid=false; Vec normal {0,0,1}; };
// Conservative preview: allow floor settling/bounces, reject walls/ceilings/solid starts.
// It models ordinary grenade launch physics; actual projectiles remain engine-owned.
template<class Trace> bool Predict(Launch launch,float gravity,float friction,float duration,Trace trace,Vec &end) {
 if(!(gravity>0 && gravity<4000 && duration>0 && duration<=3)) return false;
 Vec pos=launch.source, velocity=launch.velocity;
 constexpr float step=.05f;
 for(float t=0;t<duration-.001f;t+=step) {
  float dt=duration-t<step?duration-t:step;
  Vec next=pos+velocity*dt+Vec(0,0,-.5f*gravity*dt*dt);
  Hit hit=trace(pos,next);
  if(hit.solid || !(hit.fraction>=0 && hit.fraction<=1)) return false;
  velocity.z-=gravity*dt;
  if(hit.fraction<1) {
   if(hit.normal.z<.7f) return false;
   pos=pos+(next-pos)*hit.fraction+hit.normal*.1f;
   float into=velocity.Dot(hit.normal);
   if(into<0) velocity=velocity-hit.normal*(into*(2-friction));
   if(velocity.z<60) {
    // A grounded grenade slows on each contact and ceases travelling at low speed.
    velocity.z=0; velocity.x*=.8f; velocity.y*=.8f;
    if(velocity.x*velocity.x+velocity.y*velocity.y<400) {end=pos;return true;}
   }
  } else pos=next;
 }
 end=pos;
 return true;
}
struct Context {
 bool enabled=false,alive=false,completed=false,freeze=false,roundOver=false;
 bool busy=false,blind=false,reloading=false,underPressure=false,visibleThreat=false;
 float now=0,seen=0,range=0,height=0;
};
inline bool CanConsider(const Context &c) {
 return c.enabled && c.alive && !c.completed && !c.freeze && !c.roundOver
  && !c.busy && !c.blind && !c.reloading && !c.underPressure && !c.visibleThreat
  && c.now>=c.seen+.35f && c.now<=c.seen+5
  && c.range>=300 && c.range<=1100 && c.height<=300 && c.height>=-300;
}
enum class Kind { HE, Flash, Smoke };
// Try preferred utility first, then every alternative once. A blocked HE must
// not prevent using a safe flash/smoke that is already in the inventory.
template<class Usable> int ChooseCarried(int preferred,const int (&choices)[3],Usable usable) {
 if(preferred && usable(preferred))return preferred;
 for(int choice:choices)if(choice!=preferred && usable(choice))return choice;
 return 0;
}
inline bool SafeDistance(const Vec &point,const Vec &ally,Kind kind) {
 float radius=kind==Kind::Smoke?170.0f:kind==Kind::Flash?550.0f:300.0f;
 return (point-ally).LengthSq()>=radius*radius;
}
}
