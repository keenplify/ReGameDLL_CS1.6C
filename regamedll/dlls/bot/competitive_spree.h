// SPDX-License-Identifier: MIT
#pragma once
namespace CompetitiveSpree {
inline bool Ace(bool roundPlay, int enemies, int alive, int credited) {
 return roundPlay && enemies>=3 && alive==0 && credited==enemies;
}
struct Pending {
 int kills=0, announced=0, count=0;
 float due=0, expires=0;
 void Kill(float now, bool ffa) {
  ++kills;
  if (kills < 3 || (ffa && kills > 5 && kills % 5)) return;
  count=kills;
  due=now+1.4f; // Combine rapid kills into the highest pending milestone.
  expires=now+10;
 }
 bool Ready(float now) const { return count>0 && now>=due && now<=expires; }
 void Retry(float now) { if(now>expires){count=0;due=0;}else due=now+.5f; }
 void Delivered() { announced=count;count=0;due=0; }
 void Died(bool ffa) { if(ffa){*this=Pending {};} }
};
}
