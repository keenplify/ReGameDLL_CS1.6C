// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace BotQuery {
struct Bot { char name[64]; float seconds; };
// Validate the entire packet before changing it. Split/compressed replies pass through.
inline bool Rewrite(uint8_t *p, size_t n, const Bot *bots, size_t count) {
 if (n < 6 || std::memcmp(p, "\xff\xff\xff\xff", 4)) return false;
 size_t pos=5, botOffset=0, times[256]={}, matches[256]={}, used=0;
 auto skip=[&](size_t bytes) { if(bytes>n-pos) return false; pos+=bytes; return true; };
 auto str=[&]() { while(pos<n && p[pos]) ++pos; return pos<n && skip(1); };
 if(p[4]=='I') {
  if(!skip(1)) return false;
  for(int i=0;i<4;++i) if(!str()) return false;
  if(!skip(4)) return false;
  botOffset=pos; if(!skip(3) || !skip(2) || !str()) return false;
 } else if(p[4]=='m') {
  for(int i=0;i<5;++i) if(!str()) return false;
  if(!skip(7)) return false;
  // legacy mod flag follows password
  uint8_t mod=p[pos-1];
  if(mod) { if(!str() || !str() || !skip(11)) return false; }
  if(!skip(1)) return false;
  botOffset=pos; if(!skip(1) || pos!=n) return false;
 } else if(p[4]=='D') {
  unsigned players=p[pos++];
  for(unsigned i=0;i<players;++i) {
   if(!skip(1)) return false;
   size_t name=pos; if(!str() || !skip(4)) return false;
   size_t t=pos; if(!skip(4)) return false;
   for(size_t b=0;b<count;++b) if(!std::strcmp(reinterpret_cast<char*>(p+name),bots[b].name)) {
    times[used]=t; matches[used++]=b; break;
   }
  }
  if(pos!=n) return false;
  for(size_t i=0;i<used;++i) std::memcpy(p+times[i],&bots[matches[i]].seconds,4);
  return used!=0;
 } else return false;
 p[botOffset]=0; return true;
}
}
