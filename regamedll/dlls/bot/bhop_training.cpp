// SPDX-License-Identifier: MIT
#include "precompiled.h"
#include "bhop_training.h"
#include "bhop_training_policy.h"
#include "bhop_match_policy.h"
#include <vector>
#include <sstream>
#include <cstdlib>

namespace BhopTraining {
namespace {
cvar_t enabled = { "bot_bhop_training", "0", FCVAR_SERVER, 0.0f, nullptr };
struct Route { Vector start, goal; float length; Vector via; bool corner=false; float yawOffset=0; int family=0; const char *scenario="straight"; };
std::vector<Route> routes;
struct Episode {
    bool active = false, reset = false, hop = false, gstrafe = false, previousDuck = false;
    int duckPresses=0, duckCommands=0, crouchedCommands=0;
    float minDuckPressSpeed=0,commandTime=0;
    bool sgs=false,substep=false,sgsDuck=false;
    SGSController sgsController;
    GroundStrafeParameters groundParameters;
    GroundStrafeController groundController;
    int id = 0, route = 0, slot = 0, userid = 0, jumps = 0, landings = 0, collisions = 0;
    bool grounded = true, blocked = false, passedCorner = false;
    float startTime = 0, nextSample = 0, lastProgress = 0, best = 0, peak = 0, previousSpeed = 0, flightStarted = -100;
    Parameters parameters;
    Controller controller;
} episode;

bool Lab() {
    if (enabled.value != 1 || !IS_DEDICATED_SERVER() || CVAR_GET_FLOAT("sv_lan") != 1
        || gpGlobals->maxClients != 2) return false;
    int bots = 0;
    for (int i = 1; i <= gpGlobals->maxClients; ++i) {
        auto *p = UTIL_PlayerByIndex(i);
        if (!p || FNullEnt(p->edict()) || p->IsDormant()) continue;
        if (!p->IsBot()) return false;
        ++bots;
    }
    return bots == 1;
}
void Error(const char *reason) { SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"error\",\"reason\":\"%s\"}\n", reason)); }
bool ClearHull(const Vector &a, const Vector &b) {
    TraceResult tr;
    UTIL_TraceHull(a, b, ignore_monsters, human_hull, nullptr, &tr);
    return !tr.fStartSolid && !tr.fAllSolid && tr.flFraction >= .9999f;
}
bool Course(const Vector &start, const Vector &goal) {
    if (!ClearHull(start, goal) || !ClearHull(start + Vector(0,0,64), goal + Vector(0,0,64))) return false;
    const int steps = int((goal-start).Length2D()/32) + 1;
    for (int i=0; i<=steps; ++i) {
        Vector p = start + (goal-start)*(float(i)/steps);
        TraceResult tr;
        UTIL_TraceLine(p, p-Vector(0,0,64), ignore_monsters, nullptr, &tr);
        if (tr.fStartSolid || tr.flFraction >= 1 || std::fabs(tr.vecEndPos.z-(start.z-36)) > 4) return false;
    }
    return true;
}
void ReportEnvironment() {
    SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"environment\",\"sv_gravity\":%.4f,\"sv_maxspeed\":%.4f,\"sv_accelerate\":%.4f,\"sv_airaccelerate\":%.4f,\"sv_friction\":%.4f,\"sv_stopspeed\":%.4f,\"sv_stepsize\":%.4f,\"sv_maxvelocity\":%.4f,\"sv_enablebunnyhopping\":%.4f,\"sv_autobunnyhopping\":%.4f,\"sys_ticrate\":%.4f,\"nav_areas\":%d,\"freeze_time\":%.4f}\n",
        CVAR_GET_FLOAT("sv_gravity"), CVAR_GET_FLOAT("sv_maxspeed"), CVAR_GET_FLOAT("sv_accelerate"), CVAR_GET_FLOAT("sv_airaccelerate"), CVAR_GET_FLOAT("sv_friction"), CVAR_GET_FLOAT("sv_stopspeed"), CVAR_GET_FLOAT("sv_stepsize"), CVAR_GET_FLOAT("sv_maxvelocity"), CVAR_GET_FLOAT("sv_enablebunnyhopping"), CVAR_GET_FLOAT("sv_autobunnyhopping"), CVAR_GET_FLOAT("sys_ticrate"), int(TheNavAreaList.size()), CVAR_GET_FLOAT("mp_freezetime")));
}
void Discover() {
    if (!Lab() || episode.active) { Error("requires idle isolated lab with one bot"); return; }
    if (CSGameRules()->IsFreezePeriod()) { Error("waiting for freeze period"); return; }
    bool ready=false;
    for (int i=1;i<=gpGlobals->maxClients;++i) {
        auto *bot=UTIL_PlayerByIndex(i);
        if(bot && bot->IsBot() && bot->IsAlive() && bot->pev->maxspeed>=100) ready=true;
    }
    if(!ready) { Error("waiting for a movable bot"); return; }
    routes.clear();
    std::vector<CNavArea *> areas(TheNavAreaList.begin(), TheNavAreaList.end());
    std::sort(areas.begin(), areas.end(), [](CNavArea *a,CNavArea *b){return a->GetID()<b->GetID();});
    for (auto *area : areas) {
        if (routes.size() >= 12) break;
        if (area->GetAttributes()) continue;
        Vector start = *area->GetCenter() + Vector(0,0,36);
        for (float length : {640.0f,480.0f,320.0f}) {
        bool found=false;
        for (int direction=0; direction<16; ++direction) {
            const float angle = direction*Pi/8;
            Vector goal = start+Vector(std::cos(angle)*length,std::sin(angle)*length,0);
            bool separate = true;
            const Vector midpoint = (start+goal)*.5f;
            for (const auto &r : routes)
                if ((midpoint-(r.start+r.goal)*.5f).Length2D()<400) separate=false;
            if (!separate || !Course(start,goal)) continue;
            routes.push_back({start,goal,length});
            found=true;break;
        }
        if(found)break;
        }
    }
    const size_t baseCount=routes.size();
    for(size_t i=0;i<baseCount;++i)routes[i].family=int(i);
    for(size_t i=0;i<baseCount;++i) {
        const Route base=routes[i];
        Route reverse=base;reverse.start=base.goal;reverse.goal=base.start;reverse.scenario="reverse";routes.push_back(reverse);
        for(float offset:{-90.0f,90.0f}) {
            Route angled=base;angled.yawOffset=offset;angled.scenario=offset<0?"turn_start_right":"turn_start_left";routes.push_back(angled);
        }
        Vector direction=base.goal-base.start;direction.z=0;direction.NormalizeInPlace();
        const Vector via=(base.start+base.goal)*.5f;
        for(float side:{-1.0f,1.0f}) {
            const Vector goal=via+Vector(-direction.y,direction.x,0)*(side*256);
            if(!Course(base.start,via)||!Course(via,goal))continue;
            Route corner=base;corner.via=via;corner.goal=goal;corner.corner=true;
            corner.length=(via-base.start).Length2D()+(goal-via).Length2D();
            corner.scenario=side<0?"corner_right":"corner_left";routes.push_back(corner);
        }
    }
    ReportEnvironment();
    for (size_t i=0; i<routes.size(); ++i) {
        const auto &r=routes[i];
        SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"route\",\"id\":%d,\"family\":%d,\"scenario\":\"%s\",\"length\":%.1f,\"start\":[%.3f,%.3f,%.3f],\"goal\":[%.3f,%.3f,%.3f]}\n",
            int(i),r.family,r.scenario,r.length,r.start.x,r.start.y,r.start.z,r.goal.x,r.goal.y,r.goal.z));
    }
    SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"routes_ready\",\"count\":%d}\n",int(routes.size())));
}
bool Number(int arg, float &out) {
    char *end=nullptr;
    out=std::strtof(CMD_ARGV(arg),&end);
    return end && end!=CMD_ARGV(arg) && !*end && std::isfinite(out);
}
#include "bhop_rl.inc"

void Finish(const char *reason, bool success) {
    if (!episode.active) return;
    episode.active=false;
    SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"duck_summary\",\"id\":%d,\"groundstrafe\":%s,\"duck_presses\":%d,\"duck_commands\":%d,\"crouched_commands\":%d,\"min_duck_press_speed\":%.3f,\"command_seconds\":%.4f}\n",episode.id,episode.gstrafe?"true":"false",episode.duckPresses,episode.duckCommands,episode.crouchedCommands,episode.minDuckPressSpeed,episode.commandTime));
    SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"result\",\"id\":%d,\"route\":%d,\"length\":%.1f,\"hop\":%s,\"success\":%s,\"reason\":\"%s\",\"seconds\":%.4f,\"progress\":%.3f,\"peak_speed\":%.3f,\"jumps\":%d,\"landings\":%d,\"collisions\":%d}\n",
        episode.id,episode.route,routes[episode.route].length,episode.hop?"true":"false",success?"true":"false",reason,
        episode.reset?0:gpGlobals->time-episode.startTime,episode.best,episode.peak,episode.jumps,episode.landings,episode.collisions));
}
void Start() {
    if (!Lab() || episode.active || (CMD_ARGC()!=9 && CMD_ARGC()!=14)) { Error("episode requires id route hop and five legacy or ten parameters in an idle lab"); return; }
    float args[13];
    for (int i=0;i<CMD_ARGC()-1;++i) if(!Number(i+1,args[i])) { Error("nonfinite or invalid argument"); return; }
    Parameters p;p.Load(args+3,CMD_ARGC()-4);
    if(args[0]<1 || args[0]>100000000 || args[0]!=std::floor(args[0]) || args[1]<0 || args[1]>=routes.size()
       || args[1]!=std::floor(args[1]) || (args[2]!=0 && args[2]!=1) || !p.Valid()) { Error("argument outside training bounds"); return; }
    for(int i=1;i<=gpGlobals->maxClients;++i) {
        auto *bot=UTIL_PlayerByIndex(i);
        if(!bot || !bot->IsBot() || !bot->IsAlive()) continue;
        episode={};episode.active=true;episode.reset=true;episode.id=int(args[0]);episode.route=int(args[1]);
        episode.hop=args[2]==1;episode.parameters=p;episode.slot=i;episode.userid=GETPLAYERUSERID(bot->edict());
        SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"accepted\",\"id\":%d}\n",episode.id));
        return;
    }
    Error("bot is not alive yet");
}
void StartGroundStrafe() {
    if(!Lab() || episode.active || CMD_ARGC()!=6){Error("groundstrafe requires idle lab: id route period hold min_speed");return;}
    float a[5];for(int i=0;i<5;++i)if(!Number(i+1,a[i])){Error("invalid groundstrafe argument");return;}
    GroundStrafeParameters p{a[2],a[3],a[4]};
    if(!p.Valid() || a[0]<1 || a[0]>100000000 || a[0]!=std::floor(a[0]) || a[1]<0 || a[1]>=routes.size() || a[1]!=std::floor(a[1])){Error("groundstrafe argument outside bounds");return;}
    for(int i=1;i<=gpGlobals->maxClients;++i) {
        auto *bot=UTIL_PlayerByIndex(i);if(!bot || !bot->IsBot() || !bot->IsAlive())continue;
        episode={};episode.active=true;episode.reset=true;episode.gstrafe=true;
        episode.id=int(a[0]);episode.route=int(a[1]);episode.groundParameters=p;
        episode.slot=i;episode.userid=GETPLAYERUSERID(bot->edict());
        SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"accepted\",\"id\":%d,\"mode\":\"groundstrafe\",\"period\":%.4f,\"hold\":%.4f,\"min_speed\":%.1f}\n",episode.id,p.period,p.hold,p.minSpeed));return;
    }
    Error("bot is not alive yet");
}
void StartSGS() {
    if(!Lab() || episode.active || (CMD_ARGC()!=4 && CMD_ARGC()!=9 && CMD_ARGC()!=14)){Error("sgs requires idle lab: id route mode_0_coarse_1_fine_2_fine_running");return;}
    float a[13];for(int i=0;i<CMD_ARGC()-1;++i)if(!Number(i+1,a[i])){Error("invalid sgs argument");return;}
    if(a[0]<1 || a[0]>100000000 || a[0]!=std::floor(a[0]) || a[1]<0 || a[1]>=routes.size() || a[1]!=std::floor(a[1]) || (a[2]!=0 && a[2]!=1 && a[2]!=2)){Error("sgs argument outside bounds");return;}
    Parameters steering;if(CMD_ARGC()>4)steering.Load(a+3,CMD_ARGC()-4);
    if(!steering.Valid()){Error("invalid sgs steering policy");return;}
    for(int i=1;i<=gpGlobals->maxClients;++i){
        auto *bot=UTIL_PlayerByIndex(i);if(!bot || !bot->IsBot() || !bot->IsAlive())continue;
        episode={};episode.active=true;episode.reset=true;episode.sgs=true;episode.substep=a[2]!=0;episode.sgsDuck=a[2]!=2;
        episode.parameters=steering;episode.id=int(a[0]);episode.route=int(a[1]);episode.slot=i;episode.userid=GETPLAYERUSERID(bot->edict());
        SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"accepted\",\"id\":%d,\"mode\":\"sgs\",\"substep\":%d}\n",episode.id,episode.substep));return;
    }
    Error("bot is not alive yet");
}
// Read-only geometry reproduction; unavailable outside the isolated lab.
void OccupancyProbe() {
    if(!Lab() || episode.active || CMD_ARGC()!=4){Error("requires idle lab and feet x y z");return;}
    float a[3];for(int i=0;i<3;++i)if(!Number(i+1,a[i]) || std::fabs(a[i])>8192){Error("invalid coordinate");return;}
    const Vector feet(a[0],a[1],a[2]);
    for(int mode=0;mode<2;++mode){
        const Vector center=feet+Vector(0,0,mode?18:36);TraceResult hull,up;
        UTIL_TraceHull(center,center,dont_ignore_monsters,mode?head_hull:human_hull,nullptr,&hull);
        UTIL_TraceLine(feet+Vector(0,0,1),feet+Vector(0,0,129),ignore_monsters,nullptr,&up);
        SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"occupancy_probe\",\"duck\":%d,\"start_solid\":%d,\"all_solid\":%d,\"headroom\":%.3f}\n",mode,hull.fStartSolid,hull.fAllSolid,up.flFraction*128));
    }
}
void SupportProbe() {
    if(!Lab() || episode.active || (CMD_ARGC()!=5 && CMD_ARGC()!=6 && CMD_ARGC()!=7 && CMD_ARGC()!=8)){Error("requires idle lab and x y z anchor_z [distance [yaw [duck]]]");return;}
    float a[4],distance=128,yaw=0;
    for(int i=0;i<4;++i)if(!Number(i+1,a[i]) || std::fabs(a[i])>8192){Error("invalid probe coordinate");return;}
    if(CMD_ARGC()>=6 && (!Number(5,distance) || distance<16 || distance>256)){Error("invalid probe distance");return;}
    if(CMD_ARGC()>=7 && (!Number(6,yaw) || std::fabs(yaw)>360)){Error("invalid probe yaw");return;}
    float duck=0;if(CMD_ARGC()==8 && (!Number(7,duck) || (duck!=0 && duck!=1))){Error("invalid duck selector");return;}
    const Vector origin(a[0],a[1],a[2]-(duck?18:0));
    int counts[2]={},masks[2]={},clearMask=0;
    const int rays=CMD_ARGC()==6?16:8;
    const int samples=std::max(CMD_ARGC()>=7?1:4,int(std::ceil(distance/32)));
    for(int mode=0;mode<2;++mode)for(int ray=0;ray<rays;++ray) {
        const float angle=yaw*float(M_PI/180)+ray*float(2*M_PI/rays);
        const Vector end=origin+Vector(std::cos(angle)*distance,std::sin(angle)*distance,0);
        TraceResult hull;UTIL_TraceHull(origin,end,ignore_monsters,duck?head_hull:human_hull,nullptr,&hull);
        if(hull.fStartSolid || hull.fAllSolid || hull.flFraction<.9999f)continue;
        clearMask|=1<<ray;
        bool supported=true;
        for(int n=1;n<=samples;++n) {
            Vector probe=origin+(end-origin)*(float(n)/samples);probe.z=(mode?a[2]:a[3])+2;
            TraceResult floor;UTIL_TraceLine(probe,probe-Vector(0,0,38+StepHeight),ignore_monsters,nullptr,&floor);
            if(floor.fStartSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f){supported=false;break;}
        }
        if(supported){++counts[mode];masks[mode]|=1<<ray;}
    }
    SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"support_probe\",\"anchor_safe\":%d,\"grounded_safe\":%d,\"distance\":%.1f,\"rays\":%d,\"clear_mask\":%d,\"anchor_mask\":%d,\"grounded_mask\":%d}\n",counts[0],counts[1],distance,rays,clearMask,masks[0],masks[1]));
}
void Stop() { Finish("cancelled",false); }
}
bool NoNavigation() { return IS_DEDICATED_SERVER() && (gpGlobals->maxClients==2 || (gpGlobals->maxClients==32 && std::getenv("BHOP_RL_VIEW") && !strcmp(std::getenv("BHOP_RL_VIEW"),"1"))) && ((rlEnabled.value==1 && enabled.value==1 && CVAR_GET_FLOAT("sv_lan")==1) || (std::getenv("BHOP_RL_NO_NAV") && !strcmp(std::getenv("BHOP_RL_NO_NAV"),"1"))); }
bool RLMove(CBot *bot) { return RLTick(bot); }
void Register() {
    CVAR_REGISTER(&enabled);
    CVAR_REGISTER(&rlEnabled);
    ADD_SERVER_COMMAND("bhop_rl_reset",[](){RLResetState(false);});
    ADD_SERVER_COMMAND("bhop_rl_view_reset",[](){RLResetState(true);});
    ADD_SERVER_COMMAND("bhop_rl_view_action",[](){RLActionState(true);});
    ADD_SERVER_COMMAND("bhop_rl_view_roster",RLViewRoster);
    ADD_SERVER_COMMAND("bhop_rl_view_generation",RLViewGeneration);
    ADD_SERVER_COMMAND("bhop_rl_action",[](){RLActionState(false);});
    ADD_SERVER_COMMAND("bhop_rl_stop",[](){rlTraining={};});
    ADD_SERVER_COMMAND("bhop_train_routes",Discover);
    ADD_SERVER_COMMAND("bhop_train_episode",Start);
    ADD_SERVER_COMMAND("bhop_train_stop",Stop);
    ADD_SERVER_COMMAND("bhop_train_support_probe",SupportProbe);
    ADD_SERVER_COMMAND("bhop_train_occupancy_probe",OccupancyProbe);
    ADD_SERVER_COMMAND("bhop_train_gstrafe",StartGroundStrafe);
    ADD_SERVER_COMMAND("bhop_train_sgs",StartSGS);
}
void Reset() { rlTraining={};for(auto &state:rlView)state={};rlViewNextCamera=rlViewNextFollow=0; Finish("map_reset",false);episode={};routes.clear(); }
void Frame() {
    RLViewFrame();
    if(!episode.active) return;
    auto *bot=UTIL_PlayerByIndex(episode.slot);
    if(!Lab()) Finish("lab_disabled_or_human_joined",false);
    else if(!bot || !bot->IsAlive() || GETPLAYERUSERID(bot->edict())!=episode.userid) Finish("bot_died_or_changed",false);
}
bool Owns(CBasePlayer *bot) {
    if(RLViewer())return bot && bot->IsBot() && bot->IsAlive();
    if (!Lab()) { Finish("lab_disabled_or_human_joined",false); return false; }
    return bot && bot->IsBot() && bot->IsAlive();
}
int CommandStepMSec(CBot *bot) {
    return episode.active && episode.sgs && episode.substep && Lab() && bot && bot->entindex()==episode.slot
        && GETPLAYERUSERID(bot->edict())==episode.userid?4:0;
}
void Command(CBot *bot, usercmd_s &cmd, float dt) {
    if (!Owns(bot)) return;
    cmd.forwardmove=cmd.sidemove=cmd.upmove=0;cmd.buttons=0;cmd.impulse=0;
    cmd.viewangles=bot->pev->v_angle;
    if (!episode.active) return;
    if (bot->entindex()!=episode.slot || GETPLAYERUSERID(bot->edict())!=episode.userid) { Finish("bot_changed",false); return; }
    if (bot->pev->flags & FL_FROZEN) return;
    auto &e=episode;
    const auto &r=routes[e.route];
    if (e.reset) {
        bot->StandUp();bot->Run();
        bot->pev->velocity=Vector(0,0,0);bot->pev->basevelocity=Vector(0,0,0);
        bot->pev->punchangle=Vector(0,0,0);bot->pev->oldbuttons=0;
        bot->pev->fuser2=0; // reset previous episode jump fatigue, not during an attempt
        bot->pev->flags &= ~(FL_ONGROUND|FL_DUCKING);
        bot->pev->groundentity=nullptr;
        UTIL_SetOrigin(bot->pev,r.start);
        bot->pev->v_angle=Vector(0,UTIL_VecToAngles((r.corner?r.via:r.goal)-r.start).y+r.yawOffset,0);
        cmd.viewangles=bot->pev->v_angle;
        e.startTime=gpGlobals->time;e.lastProgress=e.startTime;e.nextSample=e.startTime;e.reset=false;
        return; // let the engine establish ground contact; no velocity assistance
    }
    const float elapsed=gpGlobals->time-e.startTime;
    if(r.corner && !e.passedCorner && (r.via-bot->pev->origin).Length2D()<32)e.passedCorner=true;
    const Vector toGoal=(r.corner && !e.passedCorner?r.via:r.goal)-bot->pev->origin;
    const float distance=toGoal.Length2D();
    const float progress=r.length-distance-(r.corner && !e.passedCorner?(r.goal-r.via).Length2D():0);
    const float speed=bot->pev->velocity.Length2D();
    const bool ground=(bot->pev->flags & FL_ONGROUND)!=0;
    e.peak=std::max(e.peak,speed);
    if(progress>e.best+4){e.best=progress;e.lastProgress=gpGlobals->time;}
    if(ground && !e.grounded)++e.landings;
    // Count an impact only with preceding hull evidence, not normal landing fatigue.
    if(e.blocked && speed<e.previousSpeed*.65f && e.previousSpeed>150)++e.collisions;
    e.blocked = speed>50 && !ClearHull(bot->pev->origin,
        bot->pev->origin+Vector(bot->pev->velocity.x,bot->pev->velocity.y,0)*(16.0f/speed));
    e.grounded=ground;e.previousSpeed=speed;
    if((!r.corner || e.passedCorner) && distance<32 && std::fabs(toGoal.z)<48 && ground){Finish("reached",true);return;}
    if(elapsed>12 || gpGlobals->time-e.lastProgress>3 || std::fabs(toGoal.z)>128 || distance>r.length+256 || bot->pev->waterlevel>0) {
        Finish(elapsed>12?"timeout":"stuck_or_off_route",false);return;
    }
    Observation o{UTIL_VecToAngles(toGoal).y,UTIL_VecToAngles(bot->pev->velocity).y,speed,
        bot->pev->v_angle.y,dt,ground};
    const Vector senseOrigin=bot->pev->origin+Vector(0,0,(e.gstrafe || e.sgs) && (bot->pev->flags&FL_DUCKING)?18:0);
    // Geometry is sensed locally; no map names, enemy locations or learned coordinates.
    for (int i=0;i<5;++i) {
        const float radians=(o.goalYaw+(i-2)*30)*Pi/180;
        TraceResult tr;
        UTIL_TraceHull(senseOrigin,senseOrigin+Vector(std::cos(radians)*256,std::sin(radians)*256,0),
            ignore_monsters,human_hull,bot->edict(),&tr);
        o.clearance[i]=(tr.fStartSolid || tr.fAllSolid)?0:tr.flFraction*256;
    }
    TraceResult ceiling,floor;
    UTIL_TraceHull(senseOrigin,senseOrigin+Vector(0,0,64),ignore_monsters,human_hull,bot->edict(),&ceiling);
    o.ceiling=(ceiling.fStartSolid || ceiling.fAllSolid)?0:ceiling.flFraction*64;
    const float radians=o.goalYaw*Pi/180;
    const Vector ahead=bot->pev->origin+Vector(std::cos(radians)*64,std::sin(radians)*64,0);
    UTIL_TraceLine(ahead,ahead-Vector(0,0,160),ignore_monsters,bot->edict(),&floor);
    // While airborne allow for jump height; never grant a fresh jump over a drop.
    o.floorAhead=!floor.fStartSolid && floor.flFraction<1
        && bot->pev->origin.z-floor.vecEndPos.z <= (ground?54:112);
    auto a=e.controller.Step(o,e.parameters,(e.hop && distance>120) || (e.sgs && e.sgsDuck && !ground));
    if(e.sgs)a.jump=false;
    // Orient before accelerating from a deliberately misaligned stationary start.
    if(ground && speed<50 && std::fabs(Wrap(o.goalYaw-o.yaw))>35){a.forward=0;a.side=0;a.jump=false;}
    cmd.forwardmove=a.forward;cmd.sidemove=a.side;
    cmd.buttons=(a.forward>0?IN_FORWARD:0)|(a.side>0?IN_MOVERIGHT:a.side<0?IN_MOVELEFT:0)|(a.jump?IN_JUMP:0);
    cmd.viewangles=Vector(0,a.yaw,0);bot->pev->v_angle=cmd.viewangles;
    if(a.jump){++e.jumps;e.flightStarted=gpGlobals->time;}
    if(e.hop && !ground && BhopMatch::AirDuck(gpGlobals->time-e.flightStarted,bot->pev->velocity.z,e.parameters.crouchStart,e.parameters.crouchDuration))cmd.buttons|=IN_DUCK;
    if(e.gstrafe && e.groundController.Step(o,e.groundParameters))cmd.buttons|=IN_DUCK;
    if(e.sgs && e.sgsDuck && e.sgsController.Step(o))cmd.buttons|=IN_DUCK;
    const bool duck=(cmd.buttons&IN_DUCK)!=0;
    e.commandTime+=dt;
    if(e.sgs && duck!=e.previousDuck)SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"duck_edge\",\"id\":%d,\"command_time\":%.4f,\"dt\":%.4f,\"duck\":%s,\"grounded\":%s,\"crouching\":%s,\"speed\":%.3f,\"z\":%.3f,\"vz\":%.3f}\n",e.id,e.commandTime,dt,duck?"true":"false",ground?"true":"false",(bot->pev->flags&FL_DUCKING)?"true":"false",speed,bot->pev->origin.z,bot->pev->velocity.z));
    if(duck){++e.duckCommands;if(!e.previousDuck){++e.duckPresses;e.minDuckPressSpeed=e.minDuckPressSpeed==0?speed:std::min(speed,e.minDuckPressSpeed);}}
    if(bot->pev->flags&FL_DUCKING)++e.crouchedCommands;
    e.previousDuck=duck;
    if(gpGlobals->time>=e.nextSample) {
        e.nextSample=gpGlobals->time+.1f;
        SERVER_PRINT(UTIL_VarArgs("BHOP_TRAIN {\"type\":\"sample\",\"id\":%d,\"t\":%.3f,\"position\":[%.3f,%.3f,%.3f],\"speed\":%.3f,\"grounded\":%s,\"clearance\":[%.1f,%.1f,%.1f,%.1f,%.1f],\"ceiling\":%.1f,\"floor_ahead\":%s,\"yaw\":%.3f,\"forward\":%.1f,\"side\":%.1f,\"jump\":%s,\"duck\":%s,\"crouching\":%s}\n",
            e.id,elapsed,bot->pev->origin.x,bot->pev->origin.y,bot->pev->origin.z,speed,ground?"true":"false",o.clearance[0],o.clearance[1],o.clearance[2],o.clearance[3],o.clearance[4],o.ceiling,o.floorAhead?"true":"false",a.yaw,a.forward,a.side,a.jump?"true":"false",duck?"true":"false",(bot->pev->flags&FL_DUCKING)?"true":"false"));
    }
}
}
