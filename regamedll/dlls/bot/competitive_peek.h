// SPDX-License-Identifier: MIT
#pragma once

namespace CompetitiveBotPolicy {
inline int RunGunChance(bool smg,bool shotgun,bool rifle,bool pistol,float range) {
 if(range<80)return 0;
 return smg && range<=900?85:shotgun && range<=450?45:rifle && range<=600?20:pistol && range<=550?30:0;
}
struct RunGunWindow {
 bool decided=false;
 float until=0;
 int side=1;
 void Decide(float now,int chance,int roll,int direction) {
  if(decided)return;
  decided=true;side=direction<0?-1:1;
  until=roll<chance?now+1.8f:now;
 }
 bool Active(float now) const {return decided && now>=until-1.8f && now<until;}
};
enum class PeekPhase { Idle, Retreat, Hidden, Expose, Fire };
struct PeekCycle {
 PeekPhase phase = PeekPhase::Idle;
 float deadline = 0;
 void Start(float now) { phase = PeekPhase::Retreat; deadline = now + 0.9f; }
 // Only confirmed arrival behind opaque cover starts the hidden pause.
 bool Step(float now, bool arrived, bool covered, float pause) {
  switch (phase) {
  case PeekPhase::Retreat:
   if (arrived && covered) { phase=PeekPhase::Hidden; deadline=now+pause; }
   else if (now>=deadline) phase=PeekPhase::Idle;
   break;
  case PeekPhase::Hidden:
   if (!covered) phase=PeekPhase::Idle;
   else if (now>=deadline) { phase=PeekPhase::Expose; deadline=now+0.9f; }
   break;
  case PeekPhase::Expose:
   if (arrived) { phase=PeekPhase::Fire; deadline=now+1.5f; }
   else if (now>=deadline) phase=PeekPhase::Idle;
   break;
  case PeekPhase::Fire:
   if (now>=deadline) Start(now);
   break;
  default: break;
  }
  return phase!=PeekPhase::Idle;
 }
};
// Track progress toward the current leg, not animation or requested movement.
struct PeekProgress {
 float bestDistance=0, since=0;
 bool active=false;
 void Reset() { active=false; }
 bool Stalled(float now,float distance) {
  if(!active || distance<bestDistance-3 || now<since) {
   active=true; bestDistance=distance; since=now;
  }
  return now-since>=0.35f;
 }
};
inline bool RememberThreat(float age) { return age>=0 && age<6.0f; }
inline bool HoldLostAngle(float age) { return age>=0 && age<1.25f; }
// Longer ranges favor taps; close rifle duels mix taps and two/three-shot bursts.
inline int RifleVolleySize(float range, int roll) {
 return range>1000 || roll<30 ? 1 : range>600 || roll<75 ? 2 : 3;
}
inline bool RifleVolleyComplete(int initialClip, int clip, int budget) {
 return initialClip>=0 && (clip<=0 || initialClip-clip>=budget);
}
inline bool RifleTapReady(float speed, float punch, int shots, bool grounded) {
 return grounded && speed<10 && punch<0.6f && shots==0;
}
inline bool PeekParticipation(int teammates) {
 return teammates<2;
}
// One draw per buy phase, rather than rerolling on every failed purchase.
inline int CompetitiveBuyRole(int roll) { return roll < 85 ? 0 : roll < 95 ? 1 : 2; }
}
