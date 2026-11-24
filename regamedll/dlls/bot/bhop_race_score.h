// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <algorithm>
namespace BhopRace {
enum class Retry { None, Practice, SpeedAttempt };
inline Retry RetryAfterResult(bool speed,bool completed,float heatElapsed) {
    if(!std::isfinite(heatElapsed) || heatElapsed<0)return Retry::None;
    if(speed)return !completed && heatElapsed<30?Retry::SpeedAttempt:Retry::None;
    return heatElapsed<90?Retry::Practice:Retry::None;
}
// Best one-second speed is eligible only when the footprint spans192 units.
// Pairwise diameter permits full circles; endpoint displacement would reject them.
struct SpeedWindow {
    struct Sample {float x=0,y=0,time=0,speed=0;} samples[12];
    int count=0;
    float Update(float x,float y,float now,float speed) {
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(now)||!std::isfinite(speed)||speed<0){count=0;return 0;}
        if(count && (now<samples[count-1].time || now-samples[count-1].time>.25f))count=0;
        if(count && now-samples[count-1].time<.095f)return 0;
        while(count && now-samples[0].time>1.15f){for(int i=1;i<count;++i)samples[i-1]=samples[i];--count;}
        if(count==12){for(int i=1;i<count;++i)samples[i-1]=samples[i];--count;}
        samples[count++]={x,y,now,speed};
        const float duration=now-samples[0].time;
        if(duration<.95f)return 0;
        float diameter2=0,integral=0;
        for(int i=0;i<count;++i){
            if(i)integral+=(samples[i].time-samples[i-1].time)*(samples[i].speed+samples[i-1].speed)*.5f;
            for(int j=0;j<i;++j){const float dx=samples[i].x-samples[j].x,dy=samples[i].y-samples[j].y;diameter2=std::max(diameter2,dx*dx+dy*dy);}
        }
        return diameter2>=192*192?integral/duration:0;
    }
};
struct TravelCoverage {
    struct Point {float x=0,y=0;} places[64];
    int count=0;
    float Novelty(float x,float y) const {
        float nearest=1000000;
        for(int i=0;i<count;++i){const float dx=x-places[i].x,dy=y-places[i].y;nearest=std::min(nearest,dx*dx+dy*dy);}
        return nearest;
    }
    void Visit(float x,float y) {
        if(std::isfinite(x)&&std::isfinite(y)&&count<64 && (!count || Novelty(x,y)>=256*256))places[count++]={x,y};
    }
    float Score(float speed) const {
        if(!std::isfinite(speed)||speed<=0)return 0;
        return speed*(1+.05f*std::min(20,std::max(0,count-1)));
    }
};
struct ProgressWatch {
    float x=0,y=0,z=0,since=0;
    void Reset(float px,float py,float now,float pz=0){x=px;y=py;z=pz;since=now;}
    bool Stuck(float px,float py,float now,float pz=0,bool climbing=false) {
        const float dx=px-x,dy=py-y;
        if(now<since || dx*dx+dy*dy>=24*24 || (climbing && std::fabs(pz-z)>=24)){Reset(px,py,now,pz);return false;}
        return now-since>=6;
    }
};
inline float ObjectiveScore(bool speed,float seconds,float peak,bool completed) {
    if(!completed || !std::isfinite(seconds) || seconds<=0 || !std::isfinite(peak) || peak<0)return 0;
    return speed?peak:1000.0f/seconds;
}
inline float Score(float plannedDistance,float seconds,bool completed) {
    return completed && std::isfinite(plannedDistance) && std::isfinite(seconds)
        && plannedDistance>0 && seconds>0 ? plannedDistance/seconds : 0;
}
}
