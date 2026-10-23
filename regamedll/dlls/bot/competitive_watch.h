// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>

namespace CompetitiveWatch {
inline bool Approach(float horizontal, float vertical, float fraction, bool solid) {
 return std::isfinite(horizontal) && std::isfinite(vertical) && std::isfinite(fraction)
  && horizontal >= 128 && std::fabs(vertical) <= horizontal * .7f && !solid && fraction >= .9999f;
}
inline float OpenScore(float distance, float alignment, bool solid) {
 return !solid && std::isfinite(distance) && distance >= 192 ? distance + alignment * 32 : -1;
}
}
