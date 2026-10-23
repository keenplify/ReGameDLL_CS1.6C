#include "../regamedll/dlls/bot/competitive_ping.h"
#include <cstdlib>
#include <limits>
void check(bool ok) { if(!ok) std::abort(); }
unsigned read(const CompetitivePing::Packet &p, size_t &offset, unsigned width) {
 unsigned result=0;
 for(unsigned i=0;i<width;++i,++offset) result|=((p.bytes[offset/8]>>(offset%8))&1u)<<i;
 return result;
}
int main() {
 CompetitivePing::Packet p;
 check(!p.Add(32,20)); check(!p.Add(0,4096)); check(p.bits==0);
 for(unsigned i=0;i<32;++i) check(p.Add(i,i==31?4095:20+i));
 check(!p.Add(0,1)); check(p.Finish()==101); check(p.Finish()==101); check(!p.Add(0,1));
 size_t offset=0;
 for(unsigned i=0;i<32;++i) {
  check(read(p,offset,1)==1); check(read(p,offset,5)==i);
  check(read(p,offset,12)==(i==31?4095:20+i)); check(read(p,offset,7)==0);
 }
 check(read(p,offset,1)==0);
 CompetitivePing::Packet one; check(one.Add(0,40)); check(one.Finish()==4);
 CompetitivePing::Packet empty; check(empty.Finish()==1); check(empty.bytes[0]==0);
 check(CompetitivePing::Bound(std::numeric_limits<float>::quiet_NaN(),20)==20);
 check(CompetitivePing::Bound(-1,20)==20); check(CompetitivePing::Bound(5000,20)==999);
}
