// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
namespace BhopRace {
inline float TravelPitch(float distance,float rise,float seconds,unsigned slot,unsigned heat) {
    if(!std::isfinite(distance)||!std::isfinite(rise)||!std::isfinite(seconds))return 0;
    const unsigned style=(slot*17u+heat*7u)%31u;
    const float amplitude=1.5f+float(style%5)*.5f;
    const float period=4.0f+float(style%7)*.5f;
    float terrain=distance>=32 && distance<=512 ? -std::atan2(rise,distance)*57.29578f : 0;
    terrain=std::fmax(-12.0f,std::fmin(12.0f,terrain));
    return terrain+1.5f+amplitude*std::sin(seconds*6.2831853f/period+float(style));
}
inline bool WorthShortcut(float pathDistance,float directDistance) {
    return std::isfinite(pathDistance) && std::isfinite(directDistance)
        && directDistance>=64 && directDistance<=384
        && pathDistance-directDistance>=64 && directDistance<=pathDistance*.8f;
}
struct RouteTrials {
    static constexpr int Count=8;
    unsigned assigned[Count]{},samples[Count]{},total=0;
    float reward[Count]{};
    int Select(bool explore=false) {
        int selected=0;
        for(int i=1;i<Count;++i)if(assigned[i]<assigned[selected])selected=i;
        if(!explore && assigned[selected]>0 && total%4!=0) {
            float best=-1;
            for(int i=0;i<Count;++i)if(samples[i] && reward[i]/samples[i]>best) {
                best=reward[i]/samples[i];selected=i;
            }
        }
        ++assigned[selected];++total;return selected;
    }
    void Record(int variant,float score) {
        if(variant<0 || variant>=Count || !std::isfinite(score) || score<0)return;
        ++samples[variant];reward[variant]+=score;
    }
};
}
