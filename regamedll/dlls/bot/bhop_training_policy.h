// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>

// Dependency-free, bounded policy. Only normal user commands enter game physics.
namespace BhopTraining {
constexpr float Pi = 3.14159265358979323846f;
inline float Clamp(float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); }
inline float Wrap(float x) { return std::remainder(x, 360.0f); }
struct Parameters {
    float airAngle = 83, weave = 6, period = .3f, takeoffSpeed = 200, jumpDelay = 0;
    float lookAhead = .5f, turnRate = 360, strafeStrength = 400, crouchStart = .05f, crouchDuration = .25f;
    void Load(const float *v, int count) {
        airAngle=v[0];weave=v[1];period=v[2];takeoffSpeed=v[3];jumpDelay=v[4];
        if(count==10){lookAhead=v[5];turnRate=v[6];strafeStrength=v[7];crouchStart=v[8];crouchDuration=v[9];}
    }
    bool Valid() const {
        return std::isfinite(airAngle) && airAngle >= 60 && airAngle <= 89
            && std::isfinite(weave) && weave >= 0 && weave <= 25
            && std::isfinite(period) && period >= .1f && period <= 1
            && std::isfinite(takeoffSpeed) && takeoffSpeed >= 100 && takeoffSpeed <= 250
            && std::isfinite(jumpDelay) && jumpDelay >= 0 && jumpDelay <= .15f
            && std::isfinite(lookAhead) && lookAhead>=.5f && lookAhead<=.8f
            && std::isfinite(turnRate) && turnRate>=180 && turnRate<=540
            && std::isfinite(strafeStrength) && strafeStrength>=100 && strafeStrength<=400
            && std::isfinite(crouchStart) && crouchStart>=.02f && crouchStart<=.12f
            && std::isfinite(crouchDuration) && crouchDuration>=.05f && crouchDuration<=.25f;
    }
};
// Reproducible per-racer exploration; never randomize controls each frame.
inline Parameters Explore(const Parameters &base, unsigned profile, unsigned heat) {
    if(profile%5==0)return base;
    unsigned seed=(profile+1)*747796405u ^ (heat+1)*2891336453u;
    const float amount=profile%5==4?1.0f:.35f;
    auto sample=[&](float value,float low,float high) {
        seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
        const float target=low+(high-low)*float(seed&65535u)/65535.0f;
        return Clamp(value+(target-value)*amount,low,high);
    };
    Parameters p=base;
    p.airAngle=sample(p.airAngle,60,89);p.weave=sample(p.weave,0,25);
    p.period=sample(p.period,.1f,1);p.takeoffSpeed=sample(p.takeoffSpeed,100,250);
    p.jumpDelay=sample(p.jumpDelay,0,.15f);p.lookAhead=sample(p.lookAhead,.5f,.8f);
    p.turnRate=sample(p.turnRate,180,540);p.strafeStrength=sample(p.strafeStrength,100,400);
    p.crouchStart=sample(p.crouchStart,.02f,.12f);p.crouchDuration=sample(p.crouchDuration,.05f,.25f);
    return p;
}
struct Observation {
    float goalYaw, velocityYaw, speed, yaw, dt;
    bool grounded;
    // Player-hull sweeps at -60,-30,0,+30,+60 degrees relative to goalYaw.
    float clearance[5] = {256,256,256,256,256};
    float ceiling = 64;
    bool floorAhead = true;
};
// Exploration styles are allowed only with room on both sides of the route.
inline Parameters WideSwing(const Parameters &base, const Observation &o, int variant) {
    Parameters p=base;
    if(variant<6 || variant>7 || o.grounded || !o.floorAhead || o.ceiling<48)return p;
    for(float distance:o.clearance)if(distance<192)return p;
    p.airAngle=variant==6?78:72;
    p.weave=variant==6?12:20;
    p.period=variant==6?.65f:.9f;
    return p;
}
// Ground-strafe is an independent lab policy, never inferred from old bhop profiles.
struct GroundStrafeParameters {
    float period=.06f, hold=.005f, minSpeed=245;
    bool Valid() const {
        return std::isfinite(period) && period>=.04f && period<=.8f
            && std::isfinite(hold) && hold>=.005f && hold<=.5f && period-hold>=.03f
            && std::isfinite(minSpeed) && minSpeed>=100 && minSpeed<=250;
    }
};
struct GroundStrafeController {
    float clock=0,nextPress=0,releaseAt=0,readyTime=0,nextBurst=0,entrySpeed=0;
    int burstPulses=0;
    bool pressing=false;
    bool Step(const Observation &o,const GroundStrafeParameters &p) {
        if(!p.Valid() || !std::isfinite(o.dt) || o.dt<=0){pressing=false;readyTime=0;return false;}
        const float dt=Clamp(o.dt,0,.1f);clock+=dt;
        const bool safe=o.floorAhead && o.ceiling>=48 && o.clearance[2]>=96
            && std::fabs(Wrap(o.goalYaw-o.yaw))<=35 && std::isfinite(o.speed);
        // Preserve momentum instead of periodically braking during acceleration.
        if(!safe || o.speed<p.minSpeed){pressing=false;readyTime=0;burstPulses=0;releaseAt=0;nextPress=std::max(nextPress,clock+.03f);return false;}
        if(clock<nextBurst)return false;
        if(burstPulses==0){readyTime+=dt;if(readyTime<.25f)return false;}
        if(!pressing && burstPulses>=2){burstPulses=0;readyTime=0;nextBurst=clock+.5f;return false;}
        // Airborne speed can qualify, but only ground contact permits duck taps.
        if(!o.grounded){pressing=false;return false;}
        if(pressing && clock<releaseAt)return true;
        if(pressing){
            pressing=false;
            if(burstPulses>=2){burstPulses=0;readyTime=0;nextBurst=clock+.5f;}
            return false; // A release command is mandatory even on coarse ticks.
        }
        if(clock>=nextPress) {
            if(burstPulses==0)entrySpeed=o.speed;
            ++burstPulses;releaseAt=clock+p.hold;nextPress=clock+p.period;pressing=true;return true;
        }
        return false;
    }
};
// User-provided SGS sequence: +duck 1 frame, -duck 2, +duck 20, -duck 15 at 250 FPS.
// Lab-only reproduction; normal engine movement handles every state transition.
struct SGSController {
    float readyTime=0,phaseTime=0,cooldown=0;
    bool active=false;
    int cycles=0;
    bool Step(const Observation &o) {
        const float dt=Clamp(o.dt,0,.1f);
        if(!std::isfinite(o.dt) || o.dt<=0 || !o.floorAhead || o.ceiling<48 || o.clearance[2]<96){active=false;readyTime=0;return false;}
        cooldown=std::max(0.0f,cooldown-dt);
        if(!active){
            readyTime=o.speed>=245?readyTime+dt:0;
            if(readyTime<.25f || !o.grounded || cooldown>0)return false;
            active=true;phaseTime=0;cycles=0;
        }
        if(phaseTime>=.152f-0.000001f){
            phaseTime-=.152f;++cycles;
            if(o.speed<245 || cycles>=2){active=false;readyTime=0;cooldown=.5f;return false;}
        }
        const bool duck=phaseTime<.004f-0.000001f || (phaseTime>=.012f-0.000001f && phaseTime<.092f-0.000001f);
        phaseTime+=dt;
        return duck;
    }
};
// Experimental one-shot landing SGS. Timing is quantized to normal bot commands.
struct PostHopSGS {
    bool armed=false, active=false, sawAir=false, released=false;
    float elapsed=0;
    void Arm(){armed=true;sawAir=false;}
    bool Step(const Observation &o) {
        if(!std::isfinite(o.dt) || o.dt<=0 || !std::isfinite(o.speed) || !o.floorAhead || o.ceiling<48 || o.clearance[2]<96){*this={};return false;}
        if(armed && !o.grounded)sawAir=true;
        if(!active && armed && sawAir && o.grounded){
            armed=false;
            if(o.speed<270)return false; // Running speed alone must never qualify.
            active=true;elapsed=0;released=false;
        }
        if(!active)return false;
        if(elapsed>=.152f){active=false;return false;}
        const bool forceRelease=elapsed>=.004f && !released;
        if(forceRelease)released=true;
        const bool duck=!forceRelease && (elapsed<.004f || (elapsed>=.012f && elapsed<.092f));
        elapsed+=Clamp(o.dt,0,.1f);
        return duck;
    }
};
struct Action { float yaw = 0, forward = 0, side = 0; bool jump = false; };
struct Controller {
    float elapsed = 0, groundTime = 0;
    bool previousJump = false;
    Action Step(const Observation &o, const Parameters &p, bool hop) {
        const float dt = Clamp(o.dt, 0, .1f);
        elapsed += dt;
        groundTime = o.grounded ? groundTime + dt : 0;
        Action a;
        float target = o.goalYaw;
        a.forward = 400;
        const bool safeJump = o.clearance[2] >= std::max(96.0f, o.speed * p.lookAhead)
            && o.ceiling >= 48 && o.floorAhead;
        if (!o.floorAhead) { a.forward=0;a.yaw=Wrap(o.yaw); previousJump=false; return a; }
        if (o.clearance[2] < 96) {
            int best=2;
            // Prefer the smallest viable detour, with a stable tie-break.
            for (int index : {1,3,0,4})
                if (o.clearance[index] > o.clearance[best]+16) best=index;
            if (o.clearance[best]<48) { a.yaw=Wrap(o.yaw);a.forward=0;previousJump=false;return a; }
            target = Wrap(o.goalYaw+(best-2)*30);
        }
        const bool safeAir = o.clearance[2]>=48 && o.ceiling>=8 && o.floorAhead;
        if (hop && safeAir && !o.grounded && o.speed > 50) {
            // Wish direction is offset from velocity; mouse and side key cooperate.
            const bool narrow=std::min(o.clearance[1],o.clearance[3])<96;
            const float wave = narrow?0:std::sin(elapsed * 2 * Pi / p.period) * p.weave;
            const float sign = Wrap(o.goalYaw + wave - o.velocityYaw) >= 0 ? 1 : -1;
            target = o.velocityYaw + sign * (p.airAngle - 90);
            a.forward = 0;
            a.side = -sign * (narrow?std::min(100.0f,p.strafeStrength):p.strafeStrength);
        }
        a.yaw = Wrap(o.yaw + Clamp(Wrap(target - o.yaw), -p.turnRate * dt, p.turnRate * dt));
        a.jump = hop && safeJump && o.grounded && o.speed >= p.takeoffSpeed
            && groundTime >= p.jumpDelay && !previousJump;
        previousJump = a.jump; // release between presses, including a rejected jump
        return a;
    }
};
}
