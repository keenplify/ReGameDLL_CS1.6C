// SPDX-License-Identifier: MIT
#pragma once
namespace CompetitiveBotPolicy {
// Per-bot, directional and expiring: never mutate the map's shared nav mesh.
struct NavBlockMemory {
 struct Edge { unsigned from=0,to=0; float until=0; } edges[8];
 unsigned next=0;
 void Add(unsigned from,unsigned to,float now) {
  if(!from || !to || from==to)return;
  for(auto &edge:edges) if(edge.from==from && edge.to==to) {edge.until=now+20;return;}
  edges[next++%8]={from,to,now+20};
 }
 bool Contains(unsigned from,unsigned to,float now) const {
  for(const auto &edge:edges)
   if(edge.from==from && edge.to==to && now<edge.until && edge.until-now<=20)return true;
  return false;
 }
};
}
