// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>

namespace CompetitiveLockIn {
struct Window { float start, end; int phase; };

inline float Duration(int difficulty, float roll) {
 const int level=std::max(0,std::min(4,difficulty));
 const float chance=std::max(0.0f,std::min(1.0f,roll));
 return .08f+level*.025f+chance*(.06f+level*.01f);
}

// One window per bot and match. Late arrivals select from phases still ahead.
inline Window Plan(int difficulty,float progress,float phaseRoll,float offsetRoll,float durationRoll) {
 const float current=std::max(0.0f,std::min(1.0f,progress));
 const float starts[]={.02f,.35f,.68f};
 const float latest[]={.23f,.56f,.82f};
 int first=0;
 while(first<2 && current>latest[first])++first;
 const float phaseChoice=std::max(0.0f,std::min(.999999f,phaseRoll));
 const int phase=first+int(phaseChoice*float(3-first));
 const float offset=std::max(0.0f,std::min(1.0f,offsetRoll));
 const float start=std::max(current,starts[phase]+offset*(latest[phase]-starts[phase]));
 return {start,std::min(1.0f,start+Duration(difficulty,durationRoll)),phase};
}

inline bool Active(const Window &window,float progress) {
 return progress>=window.start && progress<window.end;
}
}
