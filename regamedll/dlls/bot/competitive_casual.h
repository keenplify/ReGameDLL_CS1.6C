// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
namespace CompetitiveCasual {
inline bool Enabled(float mode) { return mode == 1.0f || mode == 2.0f; }
inline int SkillLevel(int mmr, int fallback) {
 return mmr < 0 ? fallback : std::max(0, std::min(4, (mmr - 800) / 240));
}
// Roll only once per eligible decision, with a cooldown to prevent frame-by-frame retries.
inline float CampDuration(float now, float &nextDecision, int roll) {
 if (now < nextDecision) return 0;
 nextDecision = now + 20.0f;
 return roll >= 0 && roll < 10 ? 5.0f : 0.0f;
}
}
