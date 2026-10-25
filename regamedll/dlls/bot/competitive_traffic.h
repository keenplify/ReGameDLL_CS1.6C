// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
namespace CompetitiveTraffic {
inline bool Yields(unsigned mine,unsigned theirs,int mySlot,int theirSlot) {
 return mine>theirs || (mine==theirs && mySlot>theirSlot);
}
inline bool HeadOn(float along,float lateral,float height,float headingDot) {
 return along>0 && along<=112 && std::fabs(lateral)<36 && std::fabs(height)<32 && headingDot<-.25f;
}
}
