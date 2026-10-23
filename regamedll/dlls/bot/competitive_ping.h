// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
namespace CompetitivePing {
// GoldSrc svc_pings: repeated 1 + slot(5) + ping(12) + loss(7), then 0.
class Packet {
public:
 uint8_t bytes[101] = {};
 size_t bits = 0;
 unsigned count = 0;
 bool finished = false;
 void Write(unsigned value, unsigned width) {
  for (unsigned i=0; i<width; ++i, ++bits)
   bytes[bits/8] |= ((value>>i)&1u) << (bits%8);
 }
 bool Add(unsigned slot, unsigned ping) {
  if (finished || count>=32 || slot>=32 || ping>4095) return false;
  Write(1,1); Write(slot,5); Write(ping,12); Write(0,7); ++count; return true;
 }
 size_t Finish() {
  if (!finished) { Write(0,1); finished=true; }
  return (bits+7)/8;
 }
};
inline int Bound(float value, int fallback) {
 // NaN also fails the range test; never convert it or infinity to int.
 if (!(value>=1)) return fallback;
 return value>999 ? 999 : int(value);
}
}
