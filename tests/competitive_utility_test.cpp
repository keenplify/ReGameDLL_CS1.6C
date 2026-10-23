#include "../regamedll/dlls/bot/competitive_utility.h"
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <initializer_list>
using namespace CompetitiveUtility;
void check(bool v) { if(!v) std::abort(); }
int main() {
 Context c;c.enabled=c.alive=true;c.now=10;c.seen=9;c.range=600;
 check(CanConsider(c));
 for(auto member:{&Context::completed,&Context::freeze,&Context::roundOver,&Context::busy,
                  &Context::blind,&Context::reloading,&Context::underPressure,&Context::visibleThreat}) {
  auto deny=c;deny.*member=true;check(!CanConsider(deny));
 }
 auto stale=c;stale.seen=4;check(!CanConsider(stale));
 stale=c;stale.seen=10;check(!CanConsider(stale));
 stale=c;stale.range=100;check(!CanConsider(stale));
 stale=c;stale.range=1200;check(!CanConsider(stale));
 stale=c;stale.height=400;check(!CanConsider(stale));
 check(!SafeDistance({0,0,0},{299,0,0},Kind::HE));
 check(SafeDistance({0,0,0},{300,0,0},Kind::HE));
 check(!SafeDistance({0,0,0},{500,0,0},Kind::Flash));
 check(SafeDistance({0,0,0},{550,0,0},Kind::Flash));
 auto launch=NativeLaunch({0,0,64},{0,0,0},-30,0);
 check(launch.velocity.x>600 && launch.velocity.z>400);
 auto moved=NativeLaunch({0,0,64},{50,20,0},-30,0);
 check(std::fabs(moved.velocity.x-launch.velocity.x-50)<.001f);
 check(std::fabs(moved.velocity.y-launch.velocity.y-20)<.001f);
 Vec end;
 auto clear=[](const Vec&,const Vec&){return Hit {};};
 check(Predict(launch,440,.7f,1.5f,clear,end));
 check(std::fabs(end.x-launch.source.x-launch.velocity.x*1.5f)<.1f);
 check(std::fabs(end.z-(launch.source.z+launch.velocity.z*1.5f-.5f*440*2.25f))<.1f);
 auto wall=[](const Vec&,const Vec&){return Hit {.5f,false,{-1,0,0}};};
 check(!Predict(launch,440,.7f,1.5f,wall,end));
 auto solid=[](const Vec&,const Vec&){return Hit {0,true,{0,0,1}};};
 check(!Predict(launch,440,.7f,1.5f,solid,end));
 check(!Predict(launch,0,.7f,1.5f,clear,end));
 check(!Predict(launch,std::numeric_limits<float>::quiet_NaN(),.7f,1.5f,clear,end));
 auto floor=[](const Vec&a,const Vec&b) {
  if(b.z<0) return Hit {a.z/(a.z-b.z),false,{0,0,1}};
  return Hit {};
 };
 check(Predict(NativeLaunch({0,0,64},{},0,0),440,.7f,1.5f,floor,end));
 check(end.z>=0 && end.x>100);
 std::puts("utility memory, launch physics, floor/wall collision and teammate safety passed");
}
