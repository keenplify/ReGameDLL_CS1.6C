// SPDX-License-Identifier: MIT
#pragma once

struct CasualBanterSchedule {
   float next {}, lastHuman {-1000.0f}, checked {};
   unsigned sequence {};
   bool due (float now) {
      if (now < checked) { next = 0; lastHuman = -1000.0f; }
      if (now < checked + 1.0f) return false;
      checked = now;
      if (next == 0) { next = now + 35.0f; return false; }
      return now >= next && now >= lastHuman + 20.0f;
   }
   void sent (float now) { next = now + 100.0f + (sequence++ % 3) * 15.0f; }
};
inline const char *casualBanterLine (unsigned index, bool winter) {
   static const char *const any[] = {
      "having fun so far?",
      "worth another map after this?",
      "slow start or straight into it?",
      "did you warm up today?",
      "what keeps cs in your rotation?",
      "what got you into 1.6?",
      "still have a favorite old map?",
      "any map you miss playing?",
      "what makes you stay for one more?",
      "what's your comfort map?",
      "is this a warmup or the main event?",
      "what's your go-to warmup?",
      "anyone here for a lazy game?",
      "mouse sens finally feel right?",
      "ever actually stop at one game?",
      "do you warm up your movement first?",
      "favorite sound in old cs?",
      "any map you know by sound alone?",
      "what would you name a new map?",
      "any old demos worth keeping?",
      "what makes a lobby worth staying in?",
      "do you play with music on?",
      "any map you learned the hard way?",
      "what makes a good last map?",
      "what's the first map you remember?",
      "any jump you still practice?",
      "small maps are my comfort zone.",
      "i like a lobby with a little chat.",
      "getting comfy yet?",
      "got a favorite bit of map trivia?",
      "what made you queue today?",
      "feel like a quick game today?",
      "i like the quiet between plays.",
      "ever miss a map just for its music?",
      "what's one cs habit you kept?",
      "got a map you could draw by heart?",
      "i vote for keeping the old footsteps.",
      "what makes a good demo clip?",
      "anyone else warm up on ladders?",
      "i like a good long jump map.",
      "what's your favorite map detail?",
      "do you ever watch your old demos?",
      "anyone playing with default binds?",
      "i could talk maps all night.",
      "still chasing that perfect bhop?",
      "how do you pick your crosshair?",
      "what's your ideal session length?",
      "early bird cs or late night cs?",
      "ever played this with a trackball?",
      "got a favorite movement trick?",
   };
   static const char *const snow[] = {
      "snowball rematch sometime?",
      "lob it high or keep it low?",
      "what's your best snow map idea?",
      "i like the little windup.",
      "full charge feels so dramatic.",
      "tiny tosses have their charm.",
      "snow fort or ice castle?",
      "snowball duels are a funny warmup.",
      "does anyone practice the long lob?",
      "i vote for a sled hill next.",
      "what would you call a snow arena?",
      "snow day rules are my speed.",
      "ever tried a jump toss?",
      "i could play catch on this mode.",
      "powder or packed snow?",
      "who else likes a quick release?",
      "got a favorite throw style?",
      "i like a snow map with some color.",
      "anyone up for another snow day?",
      "i'd put a cocoa stand at spawn.",
   };
   static const char *const classic[] = {
      "which pistol feels best?",
      "what's your favorite reload sound?",
      "silencer on or off for the sound?",
      "which rifle has the best old model?",
      "i've got a soft spot for the scout.",
      "ever practice with just the usp?",
      "which pistol would you put on a poster?",
      "the old mp5 sound still holds up.",
      "what's your comfort buy?",
      "any love for the famas?",
   };
   const unsigned common = sizeof(any) / sizeof(any[0]);
   if (winter) {
      index %= common + sizeof(snow) / sizeof(snow[0]);
      return index < common ? any[index] : snow[index - common];
   }
   index %= common + sizeof(classic) / sizeof(classic[0]);
   return index < common ? any[index] : classic[index - common];
}
