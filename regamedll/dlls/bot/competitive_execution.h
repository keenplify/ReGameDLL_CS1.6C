// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
namespace CompetitiveExecution {
struct Profile {int early;float reaction,tracking,settle,error,skill;};
inline Profile Get(int level) {
 static const Profile profiles[]={{85,.45f,.28f,.85f,1,.15f},{70,.35f,.22f,.65f,.75f,.35f},
  {40,.27f,.14f,.42f,.45f,.55f},{20,.20f,.09f,.28f,.22f,.80f},{10,.15f,.05f,.18f,.10f,.95f}};
 return profiles[std::max(0,std::min(4,level))];
}
inline float Reaction(int level,bool early) {return std::max(.12f,Get(level).reaction*(early?.55f:1));}
inline bool ShouldSave(int friends,int enemies,int money,bool primary,bool bombCarrier,bool defuser,float remaining) {
 return friends>0 && enemies>=friends+2 && money<2500 && primary && !bombCarrier && !defuser && remaining<35;
}
inline bool CanSupport(bool visible,bool alive,bool busy,bool combat,float range) {
 return visible && alive && !busy && !combat && range>=150 && range<=900;
}
}
