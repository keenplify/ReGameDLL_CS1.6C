// SPDX-License-Identifier: MIT
#pragma once

#include "competitive_team_order.h"
#include <cstdio>
#include <cstddef>

struct TeamAllocation {
   int a {};
   int b {};
   int mid {};
};

inline bool parseTeamAllocation (const char *raw, TeamAllocation &allocation) {
   char text[97] {};
   bool question = false;
   if (!normalizeTeamPhrase (raw, text, question) || question) return false;
   TeamAllocation parsed {};
   const char *at = text;
   int groups = 0;
   while (*at) {
      if (*at < '1' || *at > '9') return false;
      const int count = *at++ - '0';
      while (*at == ' ') ++at;
      char place[12] {};
      int length = 0;
      while (*at >= 'a' && *at <= 'z') {
         if (length >= 11) return false;
         place[length++] = *at++;
      }
      if (length == 0 || (*at && *at != ' ')) return false;
      if (std::strcmp (place, "a") == 0 && parsed.a == 0) parsed.a = count;
      else if (std::strcmp (place, "b") == 0 && parsed.b == 0) parsed.b = count;
      else if ((std::strcmp (place, "mid") == 0 || std::strcmp (place, "middle") == 0) && parsed.mid == 0) parsed.mid = count;
      else return false;
      ++groups;
      while (*at == ' ') ++at;
   }
   if (groups < 2 || parsed.a + parsed.b + parsed.mid > 16) return false;
   allocation = parsed;
   return true;
}

// Announce only groups that actually contain teammates.
inline bool formatTeamAllocation(const TeamAllocation &allocation, char *out, std::size_t size) {
 if(!out || !size) return false;
 out[0]=0;
 const int counts[]={allocation.a,allocation.b,allocation.mid};
 const char *places[]={"a","b","mid"};
 std::size_t used=0;
 for(int i=0;i<3;++i) {
  if(counts[i]<=0) continue;
  int n=std::snprintf(out+used,size-used,"%s%d%s",used?" ":"",counts[i],places[i]);
  if(n<0 || static_cast<std::size_t>(n)>=size-used) {out[0]=0;return false;}
  used+=static_cast<std::size_t>(n);
 }
 return used!=0;
}

// Full-team attacks dominate ordinary rounds; adaptation makes splits situational.
enum class AttackPlan { AllA, AllB, MidToA, MidToB, Split };
inline AttackPlan chooseAttackPlan(bool eco, int consecutiveLosses, bool playingTime, int roll, int route) {
 int splitChance=playingTime?60:consecutiveLosses>=2?45:eco?35:5;
 if(roll<splitChance)return AttackPlan::Split;
 switch(route%4) {
  case 0:return AttackPlan::AllA;
  case 1:return AttackPlan::AllB;
  case 2:return AttackPlan::MidToA;
  default:return AttackPlan::MidToB;
 }
}
inline bool midStageReady(int members, int nearby, bool bombNearby, float elapsed) {
 return elapsed>=20 || (elapsed>=4 && members>0 && nearby*3>=members*2 && bombNearby);
}
