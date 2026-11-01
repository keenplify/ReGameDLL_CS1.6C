// SPDX-License-Identifier: MIT
#pragma once

// Time without positional progress, only while the caller permits roaming.
struct CompetitiveIdleRecovery {
 float since = -1;
 bool Update(float now, bool eligible, bool progressed) {
  if(!eligible || progressed || since<0 || now<since) { since=eligible?now:-1; return false; }
  if(now-since<8) return false;
  since=now;
  return true;
 }
};
