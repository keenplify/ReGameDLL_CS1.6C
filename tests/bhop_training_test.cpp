// SPDX-License-Identifier: MIT
#include "../regamedll/dlls/bot/bhop_training_policy.h"
#include <cassert>
#include "../regamedll/dlls/bot/bhop_door_policy.h"
#include "../regamedll/dlls/bot/bhop_match_policy.h"
#include "../regamedll/dlls/bot/bhop_race_score.h"
#include <limits>
#include "../regamedll/dlls/bot/bhop_route_policy.h"
#include <random>
using namespace BhopTraining;
int main() {
    {
        using namespace BhopRace;
        DoorUseGate gate;
        assert(!gate.Acquire(1,10,false,true)); // open/moving door
        assert(!gate.Acquire(1,10,true,false)); // distant or not facing
        assert(gate.Acquire(2,10,true,true));
        for(int slot=1;slot<=32;++slot) if(slot!=2) assert(!gate.Acquire(slot,10,true,true));
        assert(gate.Acquire(2,10,true,true)); // owner can retry a blocked opening
        assert(!gate.Acquire(3,10.999f,true,true));
        assert(gate.Acquire(3,11,true,true)); // bounded retry / owner leaves
        assert(gate.owner==3);
        assert(DoorClosed(true,false,false));
        assert(!DoorClosed(false,true,false));
        assert(DoorClosed(false,true,true)); // reversed start-open endpoints
        assert(!DoorClosed(true,false,true));
        assert(!DoorClosed(false,false,false)); // moving
        assert(!DoorClosed(false,false,true));
    }
    {
        Observation o{0,0,300,0,.004f,true};PostHopSGS s;
        for(int i=0;i<100;++i)assert(!s.Step(o)); // speed without bhop is insufficient
        s.Arm();o.grounded=false;assert(!s.Step(o));o.grounded=true;
        assert(s.Step(o));
        for(int i=1;i<38;++i)assert(s.Step(o)==(i>=3 && i<23));
        for(int i=0;i<500;++i)assert(!s.Step(o)); // never repeat without another hop
        s.Arm();o.speed=250;assert(!s.Step(o));o.speed=300;assert(!s.Step(o));
        s.Arm();o.floorAhead=false;assert(!s.Step(o));assert(!s.active);
    }

    {
        Observation o{0,0,250,0,.004f,true};SGSController s;
        bool started=false;for(int i=0;i<100;++i)if(s.Step(o)){started=true;break;}
        assert(started);
        for(int frame=1;frame<38;++frame)assert(s.Step(o)==(frame>=3 && frame<23));
        o.floorAhead=false;assert(!s.Step(o));
        o.floorAhead=true;o.speed=240;s={};for(int i=0;i<500;++i)assert(!s.Step(o));
    }

    {
        GroundStrafeParameters p;
        assert(p.Valid());p.hold=p.period;assert(!p.Valid());p={};p.period=NAN;assert(!p.Valid());
        p={};Observation o{0,0,250,0,.01f,true};GroundStrafeController c;
        int presses=0,releases=0;bool previous=false;
        for(int i=0;i<200;++i){bool duck=c.Step(o,p);if(i<24)assert(!duck);presses+=duck&&!previous;releases+=!duck&&previous;previous=duck;}
        assert(presses>=2 && presses<=6 && releases>=2);
        o.floorAhead=false;assert(!c.Step(o,p));o.floorAhead=true;o.grounded=false;assert(!c.Step(o,p));
        o.grounded=true;o.speed=244;c={};for(int i=0;i<100;++i)assert(!c.Step(o,p));
        o.speed=250;o.dt=.1f;p.period=.06f;p.hold=.005f;c={};
        assert(!c.Step(o,p));assert(!c.Step(o,p));assert(c.Step(o,p));assert(!c.Step(o,p));
        o.speed=230;assert(!c.Step(o,p));o.speed=250;assert(!c.Step(o,p)); // momentum must rebuild
        o.dt=NAN;assert(!c.Step(o,p));
        o.dt=.1f;o.grounded=false;c={};for(int i=0;i<4;++i)assert(!c.Step(o,p));
        o.grounded=true;assert(c.Step(o,p)); // sustained flight can establish momentum

    }

    for(unsigned slot=1;slot<=31;++slot)for(int n=0;n<1000;++n) {
        const float t=n*.01f;
        const float pitch=BhopRace::TravelPitch(128,0,t,slot,4);
        assert(pitch>=-2.01f && pitch<=5.01f);
        assert(std::fabs(BhopRace::TravelPitch(128,0,t+.01f,slot,4)-pitch)<.06f);
        assert(BhopRace::TravelPitch(128,64,t,slot,4)<pitch);
        assert(BhopRace::TravelPitch(128,-64,t,slot,4)>pitch);
    }
    assert(BhopRace::TravelPitch(128,0,1,1,4)!=BhopRace::TravelPitch(128,0,1,2,4));
    assert(BhopRace::TravelPitch(128,NAN,1,1,4)==0);

    assert(BhopRace::RetryAfterResult(false,true,10)==BhopRace::Retry::Practice);
    assert(BhopRace::RetryAfterResult(false,false,10)==BhopRace::Retry::Practice);
    assert(BhopRace::RetryAfterResult(false,false,90)==BhopRace::Retry::None);
    assert(BhopRace::RetryAfterResult(true,false,29.9f)==BhopRace::Retry::SpeedAttempt);
    assert(BhopRace::RetryAfterResult(true,false,30)==BhopRace::Retry::None);
    assert(BhopRace::RetryAfterResult(true,true,30)==BhopRace::Retry::None);
    assert(BhopRace::RetryAfterResult(true,false,NAN)==BhopRace::Retry::None);
    assert(BhopRace::ObjectiveScore(false,10,250,true)>BhopRace::ObjectiveScore(false,11,400,true));
    assert(BhopRace::ObjectiveScore(true,10,250,true)<BhopRace::ObjectiveScore(true,11,400,true));
    assert(BhopRace::ObjectiveScore(true,10,400,false)==0);
    assert(BhopRace::ObjectiveScore(true,10,NAN,true)==0);
    assert(BhopRace::ObjectiveScore(false,0,250,true)==0);
    assert(BhopRace::Score(2000,10,true)==200);
    assert(BhopRace::Score(4000,20,true)==200);
    assert(BhopRace::Score(2000,5,true)>BhopRace::Score(2000,10,true));
    assert(BhopRace::Score(2000,0,true)==0);
    assert(BhopRace::Score(2000,10,false)==0);
    assert(BhopRace::Score(NAN,10,true)==0);
    BhopRace::ProgressWatch progress;
    progress.Reset(100,200,10);
    assert(!progress.Stuck(100,200,15.99f));
    assert(progress.Stuck(100,200,16)); // jumping changes Z, not horizontal progress
    assert(!progress.Stuck(124,200,16)); // real movement renews the deadline
    assert(!progress.Stuck(124,200,21.99f));
    assert(progress.Stuck(125,201,22)); // tiny jitter cannot postpone disqualification
    progress.Reset(0,0,30);assert(!progress.Stuck(0,0,1)); // map clock reset
    assert(BhopRace::WorthShortcut(400,250));
    assert(!BhopRace::WorthShortcut(300,250)); // insufficient saving
    assert(!BhopRace::WorthShortcut(1000,500)); // outside bounded sensing range
    assert(!BhopRace::WorthShortcut(100,20)); // too close to skip
    assert(!BhopRace::WorthShortcut(NAN,250));
    BhopRace::RouteTrials routes;
    for(int i=0;i<8;++i){assert(routes.Select()==i);routes.Record(i,i==3?200:100);}
    routes.Select(); // periodic exploration
    assert(routes.Select()==3); // measured winner is reused
    routes.Record(3,0); // failed runs lower the mean rather than disappearing
    assert(routes.samples[3]==2 && routes.reward[3]==200);
    BhopRace::ProgressWatch ladderWatch;
    ladderWatch.Reset(0,0,0,0);
    assert(!ladderWatch.Stuck(0,0,5,30,true));
    assert(!ladderWatch.Stuck(0,0,10,60,true));
    assert(ladderWatch.Stuck(0,0,16,60,true));
    ladderWatch.Reset(0,0,0,0);
    assert(ladderWatch.Stuck(0,0,6,60,false)); // jumping in place is still stuck
    BhopMatch::Context match{true,true,false,false,false,false,false,false,false,true,false,4,300,0,0,250};
    assert(BhopMatch::Eligible(match));
    // Looking sideways while strafing down the route must not suppress takeoff.
    match.headingError=BhopMatch::MovementHeadingError(0,90,0,250);
    assert(BhopMatch::Eligible(match));
    assert(BhopMatch::MovementHeadingError(0,-90,0,-250)<.001f);
    assert(BhopMatch::MovementHeadingError(0,180,-250,0)<.001f);
    assert(BhopMatch::MovementHeadingError(0,45,250,250)<.001f);
    assert(BhopMatch::MovementHeadingError(-179,179,250,0)<2.01f);
    match.headingError=BhopMatch::MovementHeadingError(0,0,0,250);
    assert(!BhopMatch::Eligible(match)); // facing route while moving sideways is unsafe
    match.headingError=BhopMatch::MovementHeadingError(0,0,-250,0);
    assert(!BhopMatch::Eligible(match));
    assert(BhopMatch::MovementHeadingError(0,0,0,0)==180);
    assert(BhopMatch::MovementHeadingError(0,NAN,250,0)==180);
    match.headingError=0;
    match.validPath=false;match.goalDistance=80;
    assert(!BhopMatch::Eligible(match));
    assert(BhopMatch::Eligible(match,true));
    match.specialArea=true;assert(!BhopMatch::Eligible(match,true));
    match.specialArea=false;match.validPath=true;match.goalDistance=300;
    auto original=match;
    match.combat=true;assert(!BhopMatch::Eligible(match));match=original;
    match.busy=true;assert(!BhopMatch::Eligible(match));match=original;
    match.ladder=true;assert(!BhopMatch::Eligible(match));match=original;
    match.waiting=true;assert(!BhopMatch::Eligible(match));match=original;
    match.specialArea=true;assert(!BhopMatch::Eligible(match));match=original;
    match.running=false;assert(!BhopMatch::Eligible(match));match=original;
    match.goalDistance=100;match.sinceEnemy=1;
    assert(!BhopMatch::Eligible(match));
    assert(BhopMatch::Eligible(match,false,true));
    match.combat=true;assert(!BhopMatch::Eligible(match,false,true));match=original;
    match.goalHeight=32;assert(!BhopMatch::Eligible(match));match=original;
    match.headingError=90;assert(!BhopMatch::Eligible(match));

    assert(!BhopMatch::AirDuck(0,250));
    assert(BhopMatch::AirDuck(.1f,170));
    assert(!BhopMatch::AirDuck(.3f,100));
    assert(!BhopMatch::AirDuck(.1f,-10));
    assert(!BhopMatch::AirDuck(NAN,170));
    Parameters p;
    assert(p.Valid());
    p.airAngle=std::numeric_limits<float>::quiet_NaN();assert(!p.Valid());p={};
    p.jumpDelay=.16f;assert(!p.Valid());p={};
    Controller c;
    Observation o{-179,179,250,179,.03f,true};
    auto a=c.Step(o,p,false);
    assert(std::fabs(Wrap(a.yaw-179))<=10.8f && a.side==0 && !a.jump);
    assert(c.Step(o,p,true).jump);
    assert(!c.Step(o,p,true).jump); // a held key must be released before jumping again
    o.grounded=false;
    a=c.Step(o,p,true);assert(!a.jump && a.forward==0 && std::fabs(a.side)==400);
    o.grounded=true;assert(c.Step(o,p,true).jump);
    c={};o={0,0,250,0,.03f,true};
    o.clearance[2]=32;
    a=c.Step(o,p,true);assert(!a.jump && a.yaw!=0); // steer away from the forward wall
    for(auto &distance:o.clearance)distance=16;
    a=c.Step(o,p,true);assert(!a.jump && a.forward==0 && a.side==0);
    o={0,0,250,0,.03f,true};o.ceiling=32;
    assert(!c.Step(o,p,true).jump);
    o.ceiling=64;o.floorAhead=false;
    a=c.Step(o,p,true);assert(!a.jump && a.forward==0);
    c={};o={0,0,250,0,.03f,false};o.ceiling=16;o.clearance[1]=60;o.clearance[3]=60;
    a=c.Step(o,p,true);assert(!a.jump && std::fabs(a.side)==100);
    o.grounded=true;a=c.Step(o,p,true);assert(!a.jump); // no new hop under low ceilings
    o={0,0,250,0,.03f,false};
    auto wide=WideSwing(p,o,7);
    assert(wide.Valid() && wide.weave==20 && wide.airAngle==72);
    assert(WideSwing(p,o,0).weave==p.weave);
    o.clearance[0]=100;assert(WideSwing(p,o,7).weave==p.weave);
    o.clearance[0]=256;o.floorAhead=false;assert(WideSwing(p,o,7).weave==p.weave);
    o.floorAhead=true;o.ceiling=32;assert(WideSwing(p,o,7).weave==p.weave);
    o.ceiling=64;o.grounded=true;assert(WideSwing(p,o,7).weave==p.weave);
    // Tight circles cannot score; wide circles remain valid even on returning.
    BhopRace::SpeedWindow tight,wideWindow,straight,spike;
    float tightBest=0,wideBest=0,straightBest=0,spikeBest=0;
    for(int i=0;i<=30;++i){
        const float t=i*.1f,angle=t*2*3.14159265f;
        tightBest=std::max(tightBest,tight.Update(32*std::cos(angle),32*std::sin(angle),t,500));
        wideBest=std::max(wideBest,wideWindow.Update(128*std::cos(angle),128*std::sin(angle),t,500));
        straightBest=std::max(straightBest,straight.Update(t*300,0,t,300));
        spikeBest=std::max(spikeBest,spike.Update(t*300,0,t,i==15?1000:200));
    }
    assert(tightBest==0);assert(std::fabs(wideBest-500)<.1f);
    assert(std::fabs(straightBest-300)<.1f);assert(spikeBest<300);
    assert(wideWindow.Update(0,0,10,500)==0); // a gap cannot reuse an old footprint
    assert(wideWindow.Update(NAN,0,10.1f,500)==0);
    BhopRace::TravelCoverage coverage;
    coverage.Visit(0,0);coverage.Visit(20,20);assert(coverage.count==1);
    coverage.Visit(256,0);assert(coverage.count==2);
    coverage.Visit(0,0);assert(coverage.count==2);
    assert(std::fabs(coverage.Score(300)-315)<.01f);
    assert(coverage.Score(0)==0);assert(coverage.Score(NAN)==0);
    for(int i=1;i<100;++i)coverage.Visit(float(i*256),0);
    assert(coverage.count==64);assert(coverage.Score(300)==600);
    // A larger race must explore genuinely distinct, stable policies.
    for(unsigned heat=1;heat<=100;++heat) {
        Parameters base;
        int changed=0;
        for(unsigned profile=1;profile<=31;++profile) {
            const auto a=Explore(base,profile,heat),b=Explore(base,profile,heat);
            assert(a.Valid());assert(a.airAngle==b.airAngle && a.strafeStrength==b.strafeStrength);
            if(profile%5==0)assert(a.airAngle==base.airAngle && a.jumpDelay==base.jumpDelay && a.turnRate==base.turnRate);
            else {assert(a.airAngle!=base.airAngle || a.strafeStrength!=base.strafeStrength);++changed;}
        }
        assert(changed==25);
    }
    assert(Explore(Parameters{},1,1).airAngle!=Explore(Parameters{},1,2).airAngle);
    BhopRace::RouteTrials diverse;
    for(int i=0;i<100;++i)diverse.Record(0,1000);
    int routeCounts[8]{};
    for(int i=0;i<31;++i)++routeCounts[diverse.Select(true)];
    for(int n:routeCounts)assert(n>=3 && n<=4);
    // Added controls change commands while preserving fixed safety gates.
    p={};p.lookAhead=.8f;c={};o={0,0,250,0,.03f,true};o.clearance[2]=160;
    assert(!c.Step(o,p,true).jump);
    p.lookAhead=.5f;c={};assert(c.Step(o,p,true).jump);
    p.turnRate=180;c={};o={90,0,250,0,.03f,true};
    assert(std::fabs(c.Step(o,p,false).yaw-5.4f)<.01f);
    p.strafeStrength=220;c={};o={0,0,250,0,.03f,false};
    assert(std::fabs(c.Step(o,p,true).side)==220);
    assert(!BhopMatch::AirDuck(.06f,100,.1f,.1f));
    assert(BhopMatch::AirDuck(.15f,100,.1f,.1f));
    assert(!BhopMatch::AirDuck(.21f,100,.1f,.1f));
    assert(!BhopMatch::AirDuck(.31f,100,.12f,.25f));
    assert(!BhopMatch::AirDuck(.15f,-1,.1f,.1f));
    p={};p.lookAhead=.49f;assert(!p.Valid());p={};
    // Stress across rates, angle boundaries, air/ground transitions and all legal parameters.
    std::mt19937 rng(1601);
    std::uniform_real_distribution<float> u(0,1);
    for(int i=0;i<200000;++i) {
        p.airAngle=60+u(rng)*29;p.weave=u(rng)*25;p.period=.1f+u(rng)*.9f;
        p.takeoffSpeed=100+u(rng)*150;p.jumpDelay=u(rng)*.15f;
        o={u(rng)*360-180,u(rng)*360-180,u(rng)*1000,u(rng)*360-180,u(rng)*.5f,u(rng)>.5f};
        a=c.Step(o,p,i%2==0);
        assert(p.Valid() && std::isfinite(a.yaw) && std::fabs(a.yaw)<=180);
        assert(std::fabs(Wrap(a.yaw-o.yaw))<=360*std::min(.1f,o.dt)+.001f);
        assert(a.forward>=0 && a.forward<=400 && std::fabs(a.side)<=400);
        assert(!a.jump || (o.grounded && i%2==0 && o.speed>=p.takeoffSpeed));
    }
}
