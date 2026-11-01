// SPDX-License-Identifier: MIT
#pragma once

namespace CompetitiveBotPolicy {
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
inline bool RememberThreat(float age) { return age>=0 && age<6.0f; }
inline bool HoldLostAngle(float age) { return age>=0 && age<1.25f; }
inline bool RifleTapReady(float speed, float punch, int shots, bool grounded) {
 return grounded && speed<10 && punch<0.6f && shots==0;
}
inline bool PeekParticipation(bool lockedIn, int teammates) {
 return lockedIn && teammates<2;
}
// One draw per buy phase, rather than rerolling on every failed purchase.
inline int CompetitiveBuyRole(int roll) { return roll < 85 ? 0 : roll < 95 ? 1 : 2; }
}
