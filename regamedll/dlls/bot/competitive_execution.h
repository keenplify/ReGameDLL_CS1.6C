// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
namespace CompetitiveExecution {
struct Profile {int early;float reaction,tracking,settle,error,skill;};
inline Profile Get(int level) {
 static const Profile profiles[]={{85,.45f,.18f,.85f,.45f,.15f},{70,.35f,.15f,.65f,.32f,.35f},
  {40,.27f,.10f,.42f,.20f,.55f},{20,.20f,.07f,.28f,.12f,.80f},{10,.15f,.045f,.18f,.06f,.95f}};
 return profiles[std::max(0,std::min(4,level))];
}
inline float Reaction(int level,bool early) {return std::max(.12f,Get(level).reaction*(early?.55f:1))*.9f;}
inline float AimOffset(int level,float nativeError,bool settling) {
 static const float caps[]={18,14,10,7,4};
 auto p=Get(level);float error=std::max(0.0f,nativeError)*p.error+(settling?5*p.error:0);
 return std::max(.25f,std::min(caps[std::max(0,std::min(4,level))],error));
}
inline float FireTolerance(float range,bool sniper,bool early) {
 // A body-sized cone that tightens with distance; early shots add four units,
 // rather than allowing an almost twenty-degree miss at every range.
 float radius=(sniper?8.0f:12.0f)+(early?4.0f:0.0f);
 return std::cos(std::atan(radius/std::max(1.0f,range)));
}
// Opening accuracy is per roster bot, independent of the lobby difficulty.
inline bool HasRatedAim(int mmr) { return mmr>=0; }
inline float OpeningDuration(int mmr) {
 if(!HasRatedAim(mmr))return 0.0f;
 // 1000 MMR baseline; each additional 1000 halves correction time.
 // Continuous scaling avoids artificial skill jumps at rating boundaries.
 return std::max(.35f,std::min(3.2f,2.6f*std::pow(.5f,(float(mmr)-1000.0f)/1000.0f)));
}
inline float OpeningOffset(int mmr,float elapsed,float range) {
 const float duration=OpeningDuration(mmr);
 if(duration<=0)return 0;
 const float remaining=std::max(0.0f,std::min(1.0f,1-elapsed/duration));
 // Stable lateral error, gradually corrected rather than frame-random jitter.
 // Keep an appreciable miss at close range without huge distant aim offsets.
 return std::max(32.0f,std::min(96.0f,range*.10f))*remaining;
}
inline float OpeningFireTolerance(float range,bool sniper,bool early,int mmr) {
 const float normal=FireTolerance(range,sniper,early);
 return HasRatedAim(mmr) && early && !sniper?std::min(normal,std::cos(12.0f*float(3.141592653589793/180.0))):normal;
}
inline bool ShouldSave(int friends,int enemies,int money,bool primary,bool bombCarrier,bool defuser,float remaining) {
 return friends>0 && enemies>=friends+2 && money<2500 && primary && !bombCarrier && !defuser && remaining<35;
}
inline bool CanSupport(bool visible,bool alive,bool busy,bool combat,float range) {
 return visible && alive && !busy && !combat && range>=150 && range<=900;
}
}
