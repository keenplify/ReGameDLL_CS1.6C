// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include "../regamedll/game_shared/bot/nav_generate.h"

struct Point {
 float x,y,z;
 float operator[](int axis) const {return axis==0?x:axis==1?y:z;}
};

int main() {
 using namespace NavGeneration;
 Point from{0,0,0}, flat{100,0,0}, landing{100,0,-100};
 // A 72-unit solid rail blocks a crouched player, even with normal step lift.
 // This simulates the BSP hull expanded by 16 horizontally and 18 vertically.
 auto rail=[](Point a,Point b) {
  Point lo{34,-116,-18},hi{66,116,90};
  // Reuse the segment slab test without an additional player expansion.
  lo.x+=16;lo.y+=16;lo.z+=72;
  hi.x-=16;hi.y-=16;hi.z+=0.1f;
  return !TouchesVolume(a,b,lo,hi);
 };
 assert(!WalkRoute(from,flat,18,58,rail));
 assert(!DropRoute(from,landing,18,rail));
 // The original eye-height drop ray (feet + 72) misses a shorter solid rail.
 // Testing the hull, rather than visibility, is the required distinction.
 assert(!DropRoute(Point{0,0,50},Point{100,0,-100},18,rail));
 auto clear=[](Point,Point){return true;};
 assert(WalkRoute(from,flat,18,58,clear));
 assert(WalkRoute(from,Point{25,0,18},18,58,clear));
 assert(WalkRoute(from,Point{25,0,50},18,58,clear));
 assert(DropRoute(from,landing,18,clear));
 // GoldSrc liquid damage uses the feet + 1 sample. Preserve the dry walkway
 // less than a unit above the liquid while rejecting the lower basin floor.
 Point waterLo{-3528,1472,-456},waterHi{-2480,2898,-453};
 assert(!TouchesVolume(Point{-2950,2400,-453.95f},Point{-3150,2400,-453.95f},waterLo,waterHi,true));
 assert(TouchesVolume(Point{-2950,2400,-455.93f},Point{-3150,2400,-455.93f},waterLo,waterHi,true));
 // Floor-level damage and full hull width count, even with clear endpoints.
 Point lo{40,-10,-5},hi{60,10,5};
 assert(TouchesVolume(from,flat,lo,hi));
 assert(TouchesVolume(Point{20,0,0},Point{30,0,0},lo,hi));
 assert(!TouchesVolume(Point{0,0,10},Point{100,0,10},lo,hi));
 assert(!TouchesVolume(Point{0,40,0},Point{100,40,0},lo,hi));
 auto hazard=[=](Point a,Point b) {
  a.z-=18.1f;b.z-=18.1f;return !TouchesVolume(a,b,lo,hi);
 };
 auto safe=[=](Point a,Point b){return !TouchesVolume(a,b,lo,hi);};
 assert(!PlayerRoute(from,flat,18,58,false,clear,safe));
 assert(!PlayerRoute(Point{0,0,100},Point{50,0,0},18,58,true,clear,safe));
}
