// SPDX-License-Identifier: MIT
#include "precompiled.h"
#include "bhop_race.h"
#include "bhop_race_score.h"
#include "bhop_route_policy.h"
#include "bhop_door_policy.h"
#include <map>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

namespace BhopRace {
namespace {
cvar_t enabled={"bot_bhop_race","0",FCVAR_SERVER,0,nullptr};
cvar_t heatLimit={"bot_bhop_race_heats","12",FCVAR_SERVER,12,nullptr};
cvar_t mapPool={"bot_bhop_race_maps","de_dust2 de_inferno de_nuke",FCVAR_SERVER,0,nullptr};
cvar_t objective={"bot_bhop_race_objective","0",FCVAR_SERVER,0,nullptr};
bool SpeedObjective(){return objective.value==1 || objective.value==2;}
const char *ObjectiveName(){return objective.value==3?"arrival_sgs":objective.value==2?"groundstrafe":SpeedObjective()?"speed":"arrival";}
int completedHeats=0;
std::map<int,RouteTrials> routeTrials;
int routeKey=0;
int winnerSlot=0,winnerUser=0,heatWinner=0,heatWinnerUser=0;
float winningScore=-1,nextSpectate=0,nextCameraSwitch=0;
int cameraMode=OBS_IN_EYE;
std::string winnerName;
void RestoreWinner() {
    auto *p=UTIL_PlayerByIndex(winnerSlot);
    if(p && winnerUser && GETPLAYERUSERID(p->edict())==winnerUser)
        SET_CLIENT_KEY_VALUE(winnerSlot,GET_INFO_BUFFER(p->edict()),"name",winnerName.c_str());
    winnerSlot=winnerUser=0;winnerName.clear();
}
void PublishWinner() {
    auto *p=UTIL_PlayerByIndex(heatWinner);
    if(!p || !heatWinnerUser || GETPLAYERUSERID(p->edict())!=heatWinnerUser)return;
    if(winnerSlot!=heatWinner || winnerUser!=heatWinnerUser) {
        RestoreWinner();winnerSlot=heatWinner;winnerUser=heatWinnerUser;
        winnerName=STRING(p->pev->netname);
    }
    const std::string tagged="[Last Win] "+winnerName.substr(0,21);
    SET_CLIENT_KEY_VALUE(winnerSlot,GET_INFO_BUFFER(p->edict()),"name",tagged.c_str());
    nextSpectate=0;
}
bool IsStillRacing(CBasePlayer *player);
void FollowWinner() {
    if(gpGlobals->time<nextSpectate)return;
    nextSpectate=gpGlobals->time+.25f;
    if(nextCameraSwitch==0)nextCameraSwitch=gpGlobals->time+5;
    else if(gpGlobals->time>=nextCameraSwitch) {
        cameraMode=cameraMode==OBS_IN_EYE?OBS_CHASE_FREE:OBS_IN_EYE;
        nextCameraSwitch=gpGlobals->time+5;
    }
    auto *winner=winnerUser?UTIL_PlayerByIndex(winnerSlot):nullptr;
    if(winner && GETPLAYERUSERID(winner->edict())!=winnerUser){RestoreWinner();winner=nullptr;}
    if(!winner || !winner->IsAlive() || !IsStillRacing(winner)) {
        winner=nullptr;
        for(int i=1;i<=gpGlobals->maxClients;++i) {
            auto *candidate=UTIL_PlayerByIndex(i);
            if(candidate && candidate->IsBot() && !candidate->IsDormant() && candidate->IsAlive() && IsStillRacing(candidate)){winner=candidate;break;}
        }
    }
    for(int slot=1;slot<=gpGlobals->maxClients;++slot) {
        auto *viewer=UTIL_PlayerByIndex(slot);
        if(!viewer || viewer->IsDormant() || viewer->IsBot() || (viewer->pev->flags&FL_PROXY)
            )continue;
        if(viewer->m_iTeam==UNASSIGNED && viewer->m_iJoiningState!=READINGLTEXT)viewer->CSPlayer()->JoinTeam(SPECTATOR);
        if(viewer->m_iTeam!=SPECTATOR || !winner)continue;
        if(viewer->m_hObserverTarget!=winner || viewer->pev->iuser1!=cameraMode) {
            viewer->m_flNextFollowTime=0;
            viewer->Observer_FindNextPlayer(false,STRING(winner->pev->netname));
            viewer->Observer_SetMode(cameraMode);
        }
    }
}
struct LadderSnapshot {int state=-1;bool attached=false;Vector top {0,0,0},bottom {0,0,0},goal {0,0,0};};
struct MotionSample {LadderSnapshot ladder;float time=0,forward=0,side=0;int buttons=0;Vector pos,velocity,view;};
struct DoorDecision {int entity=0,state=-1;float distance=-1,escapeClearance=-1,floorDepth=-1;bool attempted=false,allowed=false;Vector normal {0,0,0};};
struct Racer {float nextLookReport=0,lookGroundZ=0;bool lookGroundValid=false;TravelCoverage coverage;float nextRoam=0,roamUntil=0;Vector roamTarget,orbitCenter;SpeedWindow speedWindow;float qualifiedSpeed=0;LadderSnapshot ladder;DoorDecision door;MotionSample history[12];int sampleCount=0;float nextSample=0;int userid=0,variant=0,breakable=0;bool finished=false,planned=false,practice=false;float orbitRadius=96,peakSpeed=0,distance=0,nextPath=0,nextReport=0,nextDoorUse=0,nextBreakLog=0,nextShortcut=0,localUntil=0,nextLocalProbe=0;bool doorUse=false;Vector start,localTarget;ProgressWatch progress;};
Racer racers[33];
std::map<int,DoorUseGate> doorUseGates;
std::map<int,float> doorDenialReports;
enum class Phase {Waiting,Running,Results,ChangingMap};
Phase phase=Phase::Waiting;
bool IsStillRacing(CBasePlayer *player) {
    if(!player || phase!=Phase::Running)return false;
    const int slot=player->entindex();
    return slot>0 && slot<=32 && racers[slot].userid==GETPLAYERUSERID(player->edict()) && !racers[slot].finished;
}
Vector goal;
int heat=0,roster=0,arrivals=0;
float deadline=0,started=0,settle=0;
const char *kind="holding position";
int targetId=0;
bool Active() {return enabled.value==1 && IS_DEDICATED_SERVER() && CVAR_GET_FLOAT("sv_lan")==1 && CVAR_GET_FLOAT("bot_bhop_training")==0;}
CCSBot *Bot(int slot) {
    auto *p=UTIL_PlayerByIndex(slot);
    return p && p->IsBot() && !p->IsDormant()?static_cast<CCSBot *>(p):nullptr;
}
void Announce(const char *text) {
    const std::string message(text);
    SERVER_PRINT(UTIL_VarArgs("BHOP_RACE %s\n",message.c_str()));
    UTIL_ClientPrintAll(HUD_PRINTTALK,message.c_str());
}
bool RotateMap() {
    std::vector<std::string> maps;
    std::istringstream input(mapPool.string?mapPool.string:"");std::string name;
    while(input>>name) {
        if(name.size()>31 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)continue;
        if(IS_MAP_VALID(name.c_str()))maps.push_back(name);
    }
    if(maps.empty())return false;
    auto current=std::find(maps.begin(),maps.end(),STRING(gpGlobals->mapname));
    const size_t next=current==maps.end()?0:(size_t(current-maps.begin())+1)%maps.size();
    Announce(UTIL_VarArgs("[Bhop race] %d heats complete. Moving to %s; spectators follow automatically.\n",completedHeats,maps[next].c_str()));
    CVAR_SET_FLOAT("bot_bhop_race_objective",objective.value==0?1:objective.value==1?2:objective.value==2?3:0);
    phase=Phase::ChangingMap;
    SERVER_COMMAND(UTIL_VarArgs("changelevel %s\n",maps[next].c_str()));
    return true;
}
void RespawnAttempt(CCSBot *bot,bool practice) {
    auto &r=racers[bot->entindex()];
    const int userid=r.userid,variant=r.variant;const Vector start=r.start;
    const float distance=r.distance,radius=r.orbitRadius;
    r={};r.userid=userid;r.variant=variant;r.start=start;r.orbitCenter=start;r.coverage.Visit(start.x,start.y);r.distance=distance;r.orbitRadius=radius;
    r.finished=practice;r.practice=practice;
    bot->RoundRespawn();bot->ClearMovement();bot->DestroyPath();bot->StopAttacking();bot->Idle();bot->StandUp();bot->Run();
    UTIL_SetOrigin(bot->pev,start);bot->pev->velocity=Vector(0,0,0);bot->pev->basevelocity=Vector(0,0,0);
    bot->pev->fuser2=0;bot->pev->oldbuttons=0;bot->EquipKnife();bot->ResetStuckMonitor();
    r.progress.Reset(start.x,start.y,gpGlobals->time,start.z);
    SERVER_PRINT(UTIL_VarArgs("BHOP_RACE respawn slot=%d practice=%d peak_reset=1 remaining=%.1f\n",
        bot->entindex(),practice,std::max(0.0f,(SpeedObjective()?30.0f:90.0f)-(gpGlobals->time-started))));
}
void Finish(CCSBot *bot,bool completed,const char *reason="DNF") {
    auto &r=racers[bot->entindex()];if(r.finished)return;
    if(!completed)bot->ReportBhopFailure(reason);
    r.finished=true;
    const float seconds=std::max(.001f,gpGlobals->time-started);
    const float score=ObjectiveScore(SpeedObjective(),seconds,SpeedObjective()?r.coverage.Score(r.qualifiedSpeed):r.peakSpeed,completed);
    if(SpeedObjective())SERVER_PRINT(UTIL_VarArgs("BHOP_RACE speed_score slot=%d raw_peak=%.3f qualified_average=%.3f minimum_span=192 window=1 places=%d travel_bonus=%.2f\n",bot->entindex(),r.peakSpeed,r.qualifiedSpeed,r.coverage.count,r.qualifiedSpeed>0?r.coverage.Score(r.qualifiedSpeed)/r.qualifiedSpeed:1));
    routeTrials[routeKey].Record(r.variant,score);
    if(completed && score>0 && score>winningScore){winningScore=score;heatWinner=bot->entindex();heatWinnerUser=r.userid;}
    const int place=completed?++arrivals:0;
    bot->pev->frags+=std::floor(score+.5f);
    MESSAGE_BEGIN(MSG_ALL,gmsgScoreInfo);WRITE_BYTE(bot->entindex());WRITE_SHORT(int(bot->pev->frags));
    WRITE_SHORT(bot->m_iDeaths);WRITE_SHORT(0);WRITE_SHORT(bot->m_iTeam);MESSAGE_END();
    Announce(UTIL_VarArgs("[Bhop race: %s] %s: %s | %.2fs, peak %.1f u/s = %.1f points (arrival #%d)\n",
        ObjectiveName(),STRING(bot->pev->netname),completed?"finished":reason,seconds,r.peakSpeed,score,place));
    UTIL_LogPrintf("BHOP_RACE heat=%d userid=%d finished=%d arrival=%d distance=%.2f seconds=%.3f score=%.3f variant=%d target=%d objective=%s peak=%.3f reason=%s\n",
        heat,r.userid,completed,place,r.distance,seconds,score,r.variant,routeKey,ObjectiveName(),r.peakSpeed,completed?"finished":reason);
    const Retry retry=RetryAfterResult(SpeedObjective(),completed,gpGlobals->time-started);
    if(retry!=Retry::None){RespawnAttempt(bot,retry==Retry::Practice);return;}
    // Scoring is already final. Park finishers away from the shared destination.
    if(bot->IsAlive()) {
        UTIL_SetOrigin(bot->pev,r.start);bot->pev->velocity=Vector(0,0,0);
        bot->pev->basevelocity=Vector(0,0,0);bot->ClearMovement();bot->DestroyPath();
    }
}
bool ChooseGoal(const Vector &start) {
    struct Destination {Vector pos;int id;const char *kind;};
    std::vector<Destination> choices;
    for(int i=0;i<TheCSBots()->GetZoneCount();++i) {
        const auto *zone=TheCSBots()->GetZone(i);
        if(zone->m_areaCount>0) {
            Vector pos=*zone->m_area[0]->GetCenter();
            if((pos-start).Length2D()>1200)choices.push_back({pos,i,"bombsite"});
        }
    }
    for(auto *spot:TheHidingSpotList) {
        const auto &pos=*spot->GetPosition();
        if((pos-start).Length2D()<1400)continue;
        TraceResult tr;const Vector origin=pos+Vector(0,0,36);
        UTIL_TraceHull(origin,origin,ignore_monsters,human_hull,nullptr,&tr);
        if(!tr.fStartSolid && !tr.fAllSolid)choices.push_back({pos,int(spot->GetID()),"holding position"});
    }
    // Compact maps may have no destination 1400 units away.
    if(choices.empty())for(auto *spot:TheHidingSpotList) {
        const Vector pos=*spot->GetPosition();
        if((pos-start).Length2D()<512)continue;
        TraceResult tr;const Vector origin=pos+Vector(0,0,36);
        UTIL_TraceHull(origin,origin,ignore_monsters,human_hull,nullptr,&tr);
        if(!tr.fStartSolid && !tr.fAllSolid)choices.push_back({pos,int(spot->GetID()),"holding position"});
    }
    if(choices.empty())return false;
    // Alternate bombsites and distinct NAV holding spots across heats.
    const auto &selected=choices[((heat-1)/2)%std::min(size_t(6),choices.size())];goal=selected.pos;kind=selected.kind;targetId=selected.id;routeKey=selected.id+(selected.kind[0]=='b'?0:100000)+(heat%2==0?200000:0);
    return true;
}
std::vector<Vector> OpenSpeedStarts() {
    std::vector<CNavArea *> areas;
    for(auto *area:TheNavAreaList) {
        if(!(area->GetAttributes()&(NAV_CROUCH|NAV_JUMP|NAV_NO_JUMP)))areas.push_back(area);
    }
    std::vector<Vector> starts;
    if(areas.empty())return starts;
    // Bounded sampling once per heat, never an unbounded map-wide trace pass.
    for(int attempt=0;attempt<96 && starts.size()<10;++attempt) {
        auto *area=areas[RANDOM_LONG(0,int(areas.size())-1)];
        Vector point=*area->GetCenter();
        const float rx=std::min(128.0f,std::max(0.0f,area->GetSizeX()*.5f-24));
        const float ry=std::min(128.0f,std::max(0.0f,area->GetSizeY()*.5f-24));
        point.x+=RANDOM_FLOAT(-rx,rx);point.y+=RANDOM_FLOAT(-ry,ry);point.z=area->GetZ(&point)+36;
        bool distinct=true;for(const auto &old:starts)if((old-point).Length2D()<96)distinct=false;
        if(!distinct)continue;
        TraceResult ground,ceiling;
        UTIL_TraceHull(point+Vector(0,0,2),point-Vector(0,0,StepHeight),ignore_monsters,human_hull,nullptr,&ground);
        if(ground.fStartSolid || ground.fAllSolid || ground.flFraction>=1 || ground.vecPlaneNormal.z<.7f)continue;
        point=ground.vecEndPos;
        UTIL_TraceHull(point,point+Vector(0,0,64),ignore_monsters,human_hull,nullptr,&ceiling);
        if(ceiling.fStartSolid || ceiling.fAllSolid || ceiling.flFraction<.9999f)continue;
        bool open=true;
        for(int ray=0;ray<8 && open;++ray) {
            const float angle=ray*float(M_PI/4);const Vector end=point+Vector(std::cos(angle)*128,std::sin(angle)*128,0);
            TraceResult room,floor;
            UTIL_TraceHull(point,end,ignore_monsters,human_hull,nullptr,&room);
            if(room.fStartSolid || room.fAllSolid || room.flFraction<.9999f){open=false;break;}
            // Check both half and full radius to reject nearby holes and ledges.
            for(int sample=1;sample<=2;++sample) {
                const Vector probe=point+(end-point)*(sample*.5f);
                UTIL_TraceLine(probe,probe-Vector(0,0,36+StepHeight),ignore_monsters,nullptr,&floor);
                if(floor.fStartSolid || floor.fAllSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f){open=false;break;}
            }
        }
        if(open)starts.push_back(point);
    }
    return starts;
}
void Begin() {
    doorUseGates.clear();doorDenialReports.clear();
    ++heat;
    const bool ctStart=heat%2==0;
    const char *side=ctStart?"CT":"T";
    std::vector<Vector> spawns;
    CBaseEntity *entity=nullptr;
    while((entity=UTIL_FindEntityByClassname(entity,ctStart?"info_player_start":"info_player_deathmatch")))spawns.push_back(entity->pev->origin);
    if(SpeedObjective()) {
        auto open=OpenSpeedStarts();
        if(!open.empty()){spawns=std::move(open);side="random open-space";}
        else Announce("[Bhop race] No verified open start found; using normal spawns for this speed heat.\n");
        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE speed_starts candidates=%u random=%d\n",unsigned(spawns.size()),side[0]=='r'));
    }
    if(spawns.empty()) {Announce(UTIL_VarArgs("[Bhop race] No %s spawn positions available; skipping map.\n",side));if(!RotateMap())enabled.value=0;return;}
    if(!SpeedObjective() && !ChooseGoal(spawns[0])){Announce("[Bhop race] No distant objective or holding position available; skipping map.\n");if(!RotateMap())enabled.value=0;return;}
    if(SpeedObjective()){goal=spawns[0];kind="open space";targetId=0;routeKey=0;}
    for(auto &r:racers)r={};
    int lane=0;
    for(int i=1;i<=gpGlobals->maxClients && i<=32;++i) {
        auto *bot=Bot(i);if(!bot)continue;
        bot->RoundRespawn();bot->ClearMovement();bot->StopAttacking();bot->Idle();bot->StandUp();bot->Run();
        const Vector start=spawns[(lane++ + heat-1)%spawns.size()];
        UTIL_SetOrigin(bot->pev,start);bot->pev->velocity=Vector(0,0,0);bot->pev->basevelocity=Vector(0,0,0);
        bot->pev->fuser2=0;bot->pev->oldbuttons=0;bot->EquipKnife();bot->ResetStuckMonitor();
        racers[i].userid=GETPLAYERUSERID(bot->edict());racers[i].start=start;racers[i].orbitCenter=start;racers[i].coverage.Visit(start.x,start.y);racers[i].progress.Reset(start.x,start.y,gpGlobals->time,start.z);
        racers[i].distance=(goal-start).Length2D();
        racers[i].variant=routeTrials[routeKey].Select(true);
        if(SpeedObjective()) {
            float clearance=288;
            for(int ray=0;ray<8;++ray) {
                const float angle=ray*float(M_PI/4);Vector direction(std::cos(angle),std::sin(angle),0);
                TraceResult wall;UTIL_TraceHull(start,start+direction*288,ignore_monsters,human_hull,bot->edict(),&wall);
                float room=(wall.fStartSolid||wall.fAllSolid)?0:wall.flFraction*288;
                for(int step=1;step<=4 && step*64<=room;++step) {
                    Vector probe=start+direction*float(step*64);TraceResult floor;
                    UTIL_TraceLine(probe,probe-Vector(0,0,36+StepHeight),ignore_monsters,bot->edict(),&floor);
                    if(floor.fStartSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f){room=float((step-1)*64);break;}
                }
                clearance=std::min(clearance,room);
            }
            racers[i].orbitRadius=std::max(32.0f,std::min(256.0f,clearance-32));
            // Explore different circle sizes and both turning directions.
            racers[i].orbitRadius*=racers[i].variant%3==0?.8f:1.0f;
        }
    }
    arrivals=0;heatWinner=heatWinnerUser=0;winningScore=-1;phase=Phase::Running;started=gpGlobals->time;
    Announce(UTIL_VarArgs("[Bhop race] Heat %d: %s spawn, knives out to %s %d. Objective: %s. GO!\n",heat,side,kind,targetId+1,
        objective.value==3?"arrival + post-bhop SGS (experimental)":objective.value==2?"ground-strafe test (experimental); 30 seconds, speed + new-place bonus":SpeedObjective()?"30-second speed attempt; best 1-second speed + new-place bonus":"first to holding spot; bhop"));
    SERVER_PRINT(UTIL_VarArgs("BHOP_RACE objective=%s heat=%d\n",ObjectiveName(),heat));
}
}
unsigned MovementHeat(){return unsigned(heat);}
bool SpeedPractice(const CCSBot *bot){return Owns(bot) && SpeedObjective() && phase==Phase::Running;}
bool GroundStrafePractice(const CCSBot *bot){return SpeedPractice(bot) && objective.value==2;}
bool SGSPractice(const CCSBot *bot){return Owns(bot) && phase==Phase::Running && objective.value==3;}
bool UsesGroundStrafe(const CCSBot *bot){return Owns(bot) && phase==Phase::Running && objective.value==2;}
bool LocalRecovery(const CCSBot *bot) {
    if (!Owns(bot)) return false;
    const int slot=ENTINDEX(bot->pev->pContainingEntity);
    return slot>=1 && slot<=32 && racers[slot].localUntil>gpGlobals->time;
}
float LookPitch(CCSBot *bot,float distance,float goalZ,float feetZ) {
    const int slot=bot->entindex();
    if(!Owns(bot)||slot<1||slot>32)return 0;
    auto &r=racers[slot];
    if(bot->pev->flags&FL_ONGROUND){r.lookGroundZ=feetZ;r.lookGroundValid=true;}
    // Precise interactions retain their own aim; ladder overrides also remain
    // in UpdateLookAngles. Do not make every hop oscillate the terrain target.
    if(bot->IsOnLadder()||bot->IsUsingLadder()||LocalRecovery(bot)||r.breakable||r.doorUse)return 0;
    const float pitch=TravelPitch(distance,r.lookGroundValid?goalZ-r.lookGroundZ:0,
        gpGlobals->time-started,unsigned(slot),unsigned(heat));
    if(gpGlobals->time>=r.nextLookReport) {
        r.nextLookReport=gpGlobals->time+10;
        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE look slot=%d target_pitch=%.2f actual_pitch=%.2f\n",slot,pitch,bot->pev->v_angle.x));
    }
    return pitch;
}

int MovementVariant(const CCSBot *bot) {
    const int slot=ENTINDEX(bot->pev->pContainingEntity);
    return Owns(bot) && slot>0 && slot<=32 ? racers[slot].variant : 0;
}
float RouteMultiplier(const CCSBot *bot,const CNavArea *area) {
    if(!Owns(bot) || !area)return 1;
    const int slot=ENTINDEX(bot->pev->pContainingEntity);if(slot<1 || slot>32)return 1;
    const int variant=racers[slot].variant;if(variant==0)return 1;
    // Coherent regional preferences explore corridors, not random per-frame turns.
    const Vector &p=*area->GetCenter();
    unsigned h=unsigned(int(std::floor(p.x/512)))*73856093u
        ^ unsigned(int(std::floor(p.y/512)))*19349663u ^ unsigned(variant)*83492791u;
    h^=h>>13;h*=1274126177u;h^=h>>16;
    const float strength=variant>=6 ? 20.0f : 3.0f;
    return 1+float(h%1001)/1000*strength;
}
GhostPeers::GhostPeers(CCSBot *bot) {
    if(!Owns(bot))return;
    for(int slot=1;slot<=gpGlobals->maxClients && slot<=32;++slot) {
        auto *peer=Bot(slot);
        if(!peer || peer==bot || peer->pev->solid==SOLID_NOT)continue;
        players[count]=peer;solids[count++]=peer->pev->solid;
        peer->pev->solid=SOLID_NOT;
    }
}
GhostPeers::~GhostPeers() {
    for(int i=0;i<count;++i)players[i]->pev->solid=solids[i];
}
void Register(){
    CVAR_REGISTER(&enabled);CVAR_REGISTER(&heatLimit);CVAR_REGISTER(&mapPool);CVAR_REGISTER(&objective);
    ADD_SERVER_COMMAND("bot_bhop_race_heat",[](){
        if(!Active() || CMD_ARGC()!=2 || TheNavAreaList.empty())return;
        const int requested=atoi(CMD_ARGV(1));
        if(requested<1 || requested>1000)return;
        heat=requested-1;Begin();
    });
}
void Reset(){doorUseGates.clear();doorDenialReports.clear();completedHeats=0;RestoreWinner();heatWinner=heatWinnerUser=0;nextSpectate=nextCameraSwitch=0;cameraMode=OBS_IN_EYE;routeTrials.clear();phase=Phase::Waiting;roster=0;settle=0;heat=0;for(auto &r:racers)r={};}
bool Owns(const CCSBot *bot){return Active() && bot != nullptr;}
bool AllowDoorActivation(CBaseDoor *door,CBaseEntity *activator) {
    if(!Active() || !activator || !activator->IsPlayer() || !static_cast<CBasePlayer *>(activator)->IsBot())return true;
    const int slot=activator->entindex();
    // Distance to the brush bounds works for large doors and touch activation.
    const auto axisDistance=[](float value,float low,float high) {
        return value<low?low-value:value>high?value-high:0.0f;
    };
    const auto &point=activator->pev->origin;
    const auto &low=door->pev->absmin;
    const auto &high=door->pev->absmax;
    const float dx=axisDistance(point.x,low.x,high.x);
    const float dy=axisDistance(point.y,low.y,high.y);
    const float dz=axisDistance(point.z,low.z,high.z);
    const float distanceSquared=dx*dx+dy*dy+dz*dz;
    const bool closed=DoorClosed(door->m_toggle_state==TS_AT_BOTTOM,
        door->m_toggle_state==TS_AT_TOP,(door->pev->spawnflags&SF_DOOR_START_OPEN)!=0);
    auto &gate=doorUseGates[door->entindex()];
    const bool newLease=gpGlobals->time>=gate.retryAt || gate.owner!=slot;
    if(!gate.Acquire(slot,gpGlobals->time,closed,distanceSquared<=64*64)) {
        if(gpGlobals->time>=doorDenialReports[door->entindex()]) {
            doorDenialReports[door->entindex()]=gpGlobals->time+3;
            SERVER_PRINT(UTIL_VarArgs("BHOP_RACE door_denied slot=%d door=%d time=%.3f closed=%d distance2=%.1f retry=%.3f\n",
                slot,door->entindex(),gpGlobals->time,closed,distanceSquared,doorUseGates[door->entindex()].retryAt));
        }
        return false;
    }
    if(newLease)SERVER_PRINT(UTIL_VarArgs("BHOP_RACE door_authorized slot=%d door=%d time=%.3f state=%d starts_open=%d\n",
        slot,door->entindex(),gpGlobals->time,int(door->m_toggle_state),(door->pev->spawnflags&SF_DOOR_START_OPEN)!=0));
    return true;
}
void Frame() {
    if(!Active())return;
    FollowWinner();
    if(phase==Phase::Waiting) {
        int count=0;for(int i=1;i<=gpGlobals->maxClients && i<=32;++i)if(Bot(i))++count;
        if(count!=roster){roster=count;settle=gpGlobals->time+3;}
        if(count>0 && gpGlobals->time>=settle && !TheNavAreaList.empty())Begin();
    } else if(phase==Phase::Running) {
        bool all=true;
        for(int i=1;i<=gpGlobals->maxClients && i<=32;++i) {
            auto *bot=Bot(i);auto &r=racers[i];
            if(!r.userid)continue;
            if(!bot || GETPLAYERUSERID(bot->edict())!=r.userid){r.finished=true;continue;}
            if(r.finished) {
                if(r.practice) {
                    const Vector delta=goal-Vector(bot->pev->origin.x,bot->pev->origin.y,bot->GetFeetZ());
                    if(!bot->IsAlive() || (r.planned && delta.Length2D()<64 && std::fabs(delta.z)<48)
                        || r.progress.Stuck(bot->pev->origin.x,bot->pev->origin.y,gpGlobals->time,bot->pev->origin.z,bot->IsUsingLadder()||bot->IsOnLadder()))
                        RespawnAttempt(bot,true);
                }
                continue; // practice never delays completion of the scored heat
            }
            const float speed=bot->pev->velocity.Length2D();
            if(bot->IsAlive() && std::isfinite(speed))r.peakSpeed=std::max(r.peakSpeed,speed);
            if(SpeedObjective() && bot->IsAlive()){r.coverage.Visit(bot->pev->origin.x,bot->pev->origin.y);r.qualifiedSpeed=std::max(r.qualifiedSpeed,r.speedWindow.Update(bot->pev->origin.x,bot->pev->origin.y,gpGlobals->time,speed));}
            const Vector delta=goal-Vector(bot->pev->origin.x,bot->pev->origin.y,bot->GetFeetZ());
            if(!bot->IsAlive() || gpGlobals->time-started>90)Finish(bot,false);
            else if(!SpeedObjective() && r.planned && delta.Length2D()<64 && std::fabs(delta.z)<48)Finish(bot,true);
            else if(r.progress.Stuck(bot->pev->origin.x,bot->pev->origin.y,gpGlobals->time,bot->pev->origin.z,bot->IsUsingLadder()||bot->IsOnLadder()))Finish(bot,false,"DQ: stuck (6s without progress)");
            else if(SpeedObjective() && gpGlobals->time-started>=30)Finish(bot,true);
            else all=false;
            if(!r.finished)all=false; // a speed retry still belongs to this timed heat
        }
        if(all){PublishWinner();++completedHeats;
            if(completedHeats>=std::max(1,int(heatLimit.value)) && RotateMap())return;
            phase=Phase::Results;deadline=gpGlobals->time;Announce("[Bhop race] Heat complete. Tab shows cumulative race points. Starting next heat.\n");}
    } else if(phase==Phase::Results && gpGlobals->time>=deadline)Begin();
}
void Command(CCSBot *bot,usercmd_s &cmd) {
    if(!Owns(bot))return;
    const int slot=bot->entindex();
    cmd.buttons&=~(IN_ATTACK|IN_ATTACK2|IN_RELOAD);
    if(slot<1 || slot>32 || !racers[slot].doorUse)cmd.buttons&=~IN_USE;
    if(slot>=1 && slot<=32 && phase==Phase::Running && (!racers[slot].finished || racers[slot].practice) && bot->IsUsingKnife()) {
        const int index=racers[slot].breakable;
        edict_t *obstacle=index>0?INDEXENT(index):nullptr;
        if(obstacle && !FNullEnt(obstacle) && FClassnameIs(obstacle,"func_breakable")
            && obstacle->v.takedamage!=DAMAGE_NO && obstacle->v.health>0) {
            const Vector eye=bot->GetEyePosition();
            Vector aim;
            for(int axis=0;axis<3;++axis)aim[axis]=std::max(obstacle->v.absmin[axis],std::min(eye[axis],obstacle->v.absmax[axis]));
            Vector direction=aim-eye;const float distance=direction.NormalizeInPlace();
            if(distance>0 && distance<48) {
                TraceResult hit;UTIL_TraceLine(eye,eye+direction*48,dont_ignore_monsters,bot->edict(),&hit);
                // Layered windows can put another damageable glass/frame piece
                // ahead of the hull-selected entity. Attack that verified blocker.
                if(hit.pHit && FClassnameIs(hit.pHit,"func_breakable")
                    && hit.pHit->v.takedamage!=DAMAGE_NO && hit.pHit->v.health>0) {
                    const Vector angles=UTIL_VecToAngles(direction);
                    cmd.viewangles.x=-angles.x;cmd.viewangles.y=angles.y;
                    bot->pev->v_angle=cmd.viewangles;cmd.buttons|=IN_ATTACK;
                    if(gpGlobals->time>=racers[slot].nextBreakLog) {
                        racers[slot].nextBreakLog=gpGlobals->time+1;
                        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE breakable_attack slot=%d entity=%d health=%.0f\n",slot,ENTINDEX(hit.pHit),hit.pHit->v.health));
                    }
                    cmd.buttons&=~IN_JUMP;
                }
            }
        }
    }
    if(slot>=1 && slot<=32 && gpGlobals->time>=racers[slot].nextSample) {
        auto &r=racers[slot];auto &sample=r.history[r.sampleCount++%12];
        sample.time=gpGlobals->time;sample.pos=bot->pev->origin;sample.velocity=bot->pev->velocity;
        sample.forward=cmd.forwardmove;sample.side=cmd.sidemove;sample.buttons=cmd.buttons;sample.view=cmd.viewangles;sample.ladder=r.ladder;
        r.nextSample=gpGlobals->time+.5f;
    }
    if(slot<1 || slot>32 || phase!=Phase::Running || (racers[slot].finished && !racers[slot].practice) || !racers[slot].planned) {
        cmd.forwardmove=cmd.sidemove=cmd.upmove=0;cmd.buttons=0;
    }
}
}

bool CCSBot::UpdateBhopRace() {
    using namespace BhopRace;
    if(!Owns(this))return false;
    // Race updates bypass normal player-avoidance expiry; ghosted peers must
    // not leave stale avoidance that suppresses lateral path movement.
    m_avoid=nullptr;
    GhostPeers ghostPeers(this);
    StopAttacking();StopRapidFire();ClearPrimaryAttack();m_isEnemyVisible=false;m_lastSawEnemyTimestamp=-1000;
    ClearMovement();Run();
    if(!IsUsingKnife())EquipKnife();
    const int slot=entindex();if(slot<1 || slot>32)return true;
    auto &r=racers[slot];r.doorUse=false;r.breakable=0;r.door={};
    r.ladder={};r.ladder.attached=IsOnLadder();r.ladder.goal=m_goalPosition;
    if(m_pathLadder){r.ladder.state=int(m_pathLadderState);r.ladder.top=m_pathLadder->m_top;r.ladder.bottom=m_pathLadder->m_bottom;}
    if(phase!=Phase::Running || (r.finished && !r.practice) || r.userid!=GETPLAYERUSERID(edict()))return true;
    if(SpeedObjective()) {
        // Spawn origins can settle onto a lower floor. Keep support probes tied
        // to the actual grounded hull, never to the height of an airborne hop.
        if(pev->flags&FL_ONGROUND)r.orbitCenter.z=pev->origin.z;
        if(!r.planned){DestroyPath();r.planned=true;
            SERVER_PRINT(UTIL_VarArgs("BHOP_RACE speed_circle slot=%d radius=%.0f direction=%d\n",slot,r.orbitRadius,r.variant%2?1:-1));}
        const float direction=r.variant%2?1.0f:-1.0f;
        const float angle=std::atan2(pev->origin.y-r.orbitCenter.y,pev->origin.x-r.orbitCenter.x);
        const float lead=std::max(.35f,std::min(.8f,pev->velocity.Length2D()*.25f/std::max(32.0f,r.orbitRadius)));
        Vector target(r.orbitCenter.x+std::cos(angle+direction*lead)*r.orbitRadius,
            r.orbitCenter.y+std::sin(angle+direction*lead)*r.orbitRadius,r.orbitCenter.z-36);
        auto safe=[&](const Vector &point,float *descent=nullptr) {
            const Vector end(point.x,point.y,pev->origin.z);TraceResult room;
            UTIL_TraceHull(pev->origin,end,dont_ignore_monsters,human_hull,edict(),&room);
            if(room.fStartSolid || room.fAllSolid || room.flFraction<.9999f)return false;
            const int samples=std::max(1,int(std::ceil((end-pev->origin).Length2D()/32)));
            float startFloor=0,lastFloor=0;bool sloped=false,descending=true;
            if(descent)*descent=0;
            for(int n=descent?0:1;n<=samples;++n) {
                Vector probe=pev->origin+(end-pev->origin)*(float(n)/samples);probe.z=r.orbitCenter.z+2;TraceResult floor;
                UTIL_TraceLine(probe,probe-Vector(0,0,38+StepHeight),ignore_monsters,edict(),&floor);
                if(floor.fStartSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f)return false;
                if(descent) {
                    if(n==0)startFloor=floor.vecEndPos.z;
                    else if(floor.vecEndPos.z>lastFloor+1)descending=false;
                    sloped=sloped || floor.vecPlaneNormal.z<.995f;
                    lastFloor=floor.vecEndPos.z;
                }
            }
            if(descent && descending && sloped)*descent=std::max(0.0f,startFloor-lastFloor);
            return true;
        };
        // Commit to a new local destination periodically instead of orbiting forever.
        // Only engine-checked, supported same-level paths may leave the current circle.
        if((pev->flags&FL_ONGROUND) && gpGlobals->time>=r.nextRoam) {
            r.nextRoam=gpGlobals->time+3;
            float best=-1,selectedDrop=0;Vector selected;bool found=false;
            const float heading=pev->velocity.Length2D()>50?UTIL_VecToYaw(pev->velocity):pev->v_angle.y;
            for(float distance:{256.0f,128.0f})for(int turn:{0,45,-45,90,-90,135,-135,180}) {
                const float angle=(heading+turn)*float(M_PI/180);
                Vector point=pev->origin+Vector(std::cos(angle)*distance,std::sin(angle)*distance,0);point.z=GetFeetZ();
                const float novelty=r.coverage.Novelty(point.x,point.y);
                const float merit=novelty+distance*distance*.1f-std::fabs(float(turn));
                if(objective.value!=1 && merit<=best)continue;
                float drop=0;
                if(!safe(point,objective.value==1?&drop:nullptr))continue;
                // A bounded route preference, not a change to the speed score.
                // Novel travel still matters more than circling a small slope.
                const float ranked=merit*(1+(drop>=4?.25f*std::min(1.0f,drop/18.0f):0));
                if(ranked<=best)continue;
                best=ranked;selected=point;selectedDrop=drop;found=true;
            }
            if(found){r.roamTarget=selected;r.roamUntil=gpGlobals->time+2;
                SERVER_PRINT(UTIL_VarArgs("BHOP_RACE speed_roam slot=%d target=%.0f,%.0f,%.0f places=%d downhill_drop=%.2f objective=%d\n",slot,selected.x,selected.y,selected.z,r.coverage.count,selectedDrop,int(objective.value)));}
        }
        if(r.roamUntil>gpGlobals->time) {
            if((r.roamTarget-pev->origin).Length2D()<40 && (pev->flags&FL_ONGROUND)) {
                r.orbitCenter=pev->origin;r.roamUntil=0;
            } else if(safe(r.roamTarget))target=r.roamTarget;
            else {r.roamUntil=0;r.nextRoam=std::min(r.nextRoam,gpGlobals->time+.5f);}
        }
        if(!safe(target)) {
            // Recenter rather than continue a circle into a wall or unsupported floor.
            target=r.orbitCenter;target.z-=36;
            if(!safe(target)){r.nextRoam=std::min(r.nextRoam,gpGlobals->time+.5f);return true;}
        }
        m_goalPosition=target;m_lookAheadAngle=UTIL_VecToYaw(target-pev->origin);
        const float turn=(m_lookAheadAngle-pev->v_angle.y)*float(M_PI/180);
        if(std::cos(turn)>.25f)MoveForward();else if(std::cos(turn)<-.25f)MoveBackward();
        if(std::sin(turn)>.25f)StrafeLeft();else if(std::sin(turn)<-.25f)StrafeRight();
        m_lookYaw=m_lookAheadAngle;m_lookPitch=0;
        return true;
    }
    if(!r.planned) {
        if(gpGlobals->time<r.nextPath)return true;
        m_currentArea=TheNavAreaGrid.GetNearestNavArea(&pev->origin);m_lastKnownArea=m_currentArea;
        Idle();SetTask(SEEK_AND_DESTROY);MoveTo(&goal,FASTEST_ROUTE);
        m_repathTimer.Invalidate();
        if(!ComputePath(TheNavAreaGrid.GetNearestNavArea(&goal),&goal,FASTEST_ROUTE)){if(r.practice)RespawnAttempt(this,true);else Finish(this,false);racers[slot].nextPath=gpGlobals->time+1;return true;}
        float planned=0;
        Vector previous=pev->origin;previous.z=GetFeetZ();
        for(int i=0;i<m_pathLength;++i){planned+=(m_path[i].pos-previous).Length();previous=m_path[i].pos;}
        planned+=(goal-previous).Length();r.planned=true;r.nextPath=gpGlobals->time+1;
        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE planned slot=%d heat=%d reference=%.2f planned=%.2f variant=%d knife=%d\n",slot,heat,r.distance,planned,r.variant,IsUsingKnife()));
    }
    // XY overlap alone can retain the upstairs area after falling beneath it.
    // Re-anchor to the floor actually occupied before asking A* for a way back.
    if((pev->flags&FL_ONGROUND) && !IsUsingLadder() && !IsOnLadder()) {
        Vector feet(pev->origin.x,pev->origin.y,GetFeetZ()+2);
        CNavArea *landed=TheNavAreaGrid.GetNavArea(&feet,StepHeight+2);
        if(landed && m_lastKnownArea && landed!=m_lastKnownArea
            && m_lastKnownArea->GetZ(&feet)-GetFeetZ()>JumpCrouchHeight) {
            m_currentArea=m_lastKnownArea=landed;
            DestroyPath();m_repathTimer.Invalidate();r.nextPath=gpGlobals->time;
            m_isJumpCrouching=false;StandUp();ResetStuckMonitor();
            SERVER_PRINT(UTIL_VarArgs("BHOP_RACE fall_repath slot=%d floor_area=%u\n",slot,landed->GetID()));
        }
    }
    if(m_pathLength<=0 && gpGlobals->time>=r.nextPath) {
        ComputePath(TheNavAreaGrid.GetNearestNavArea(&goal),&goal,FASTEST_ROUTE);r.nextPath=gpGlobals->time+1;
    }
    auto steerLocal=[&](const Vector &target) {
        const float yaw=UTIL_VecToYaw(target-pev->origin);
        const float turn=(yaw-pev->v_angle.y)*float(M_PI/180);
        if(std::cos(turn)>.25f)MoveForward();else if(std::cos(turn)<-.25f)MoveBackward();
        if(std::sin(turn)>.25f)StrafeLeft();else if(std::sin(turn)<-.25f)StrafeRight();
        m_lookYaw=m_lookAheadAngle=yaw;m_lookPitch=0;
    };
    if(r.localUntil>gpGlobals->time && (r.localTarget-pev->origin).Length2D()>10) {
        Vector end(r.localTarget.x,r.localTarget.y,pev->origin.z);TraceResult room;
        UTIL_TraceHull(pev->origin,end,dont_ignore_monsters,human_hull,edict(),&room);
        if(!room.fStartSolid && !room.fAllSolid && room.flFraction>=.9999f){steerLocal(r.localTarget);return true;}
        r.localUntil=0;
    }
    r.localUntil=0;
    if(IsUsingLadder() || IsOnLadder()) {
        // Skip only a descending ladder with a bounded, supported landing and
        // conservative fall-damage margin. Keep all ascending behavior intact.
        if(m_pathLadder && m_pathIndex>=0 && m_pathIndex<m_pathLength
            && m_path[m_pathIndex].how==GO_LADDER_DOWN && m_path[m_pathIndex].area) {
            Vector landing=m_pathLadder->m_bottom;
            AddDirectionVector(&landing,m_pathLadder->m_dir,48);
            landing.z=m_path[m_pathIndex].area->GetZ(&landing);
            const float drop=GetFeetZ()-landing.z;
            if(drop>64 && drop<=256 && GetApproximateFallDamage(drop+JumpHeight)<pev->health-25
                && m_path[m_pathIndex].area->Contains(&landing) && (landing-pev->origin).Length2D()<=160) {
                const Vector raised=pev->origin+Vector(0,0,JumpHeight);
                const Vector across(landing.x,landing.y,raised.z);TraceResult up,room,floor;
                UTIL_TraceHull(pev->origin,raised,dont_ignore_monsters,human_hull,edict(),&up);
                UTIL_TraceHull(raised,across,dont_ignore_monsters,human_hull,edict(),&room);
                UTIL_TraceHull(across,Vector(landing.x,landing.y,landing.z+34),dont_ignore_monsters,human_hull,edict(),&floor);
                if(!up.fStartSolid && !up.fAllSolid && up.flFraction>=.9999f
                    && !room.fStartSolid && !room.fAllSolid && room.flFraction>=.9999f
                    && !floor.fStartSolid && !floor.fAllSolid && floor.flFraction<1 && floor.vecPlaneNormal.z>=.7f
                    && std::fabs(floor.vecEndPos.z-36-landing.z)<=StepHeight && Jump(MUST_JUMP)) {
                    m_pathLadder=nullptr;r.localTarget=landing;r.localUntil=gpGlobals->time+1.5f;
                    steerLocal(landing);
                    SERVER_PRINT(UTIL_VarArgs("BHOP_RACE ladder_drop slot=%d drop=%.0f estimated_damage=%.1f\n",slot,drop,GetApproximateFallDamage(drop+JumpHeight)));
                    return true;
                }
            }
        }
        UpdatePathMovement(false);return true;
    }
    // Treat NAV as a guide: skip a measurable local detour when the direct
    // standing-hull route and every 24-unit floor sample are supported.
    if((pev->flags&FL_ONGROUND) && !m_isJumpCrouching && m_pathIndex>=0
        && m_pathIndex<m_pathLength && gpGlobals->time>=r.nextShortcut) {
        r.nextShortcut=gpGlobals->time+.25f;
        int floorChecks=0;
        for(int i=std::min(m_pathLength-1,m_pathIndex+8);i>m_pathIndex;--i) {
            float pathDistance=0;Vector previous=pev->origin;previous.z=GetFeetZ();
            bool special=false;
            for(int j=m_pathIndex;j<=i;++j) {
                const auto &node=m_path[j];
                if(node.ladder || !node.area || (node.area->GetAttributes()&(NAV_CROUCH|NAV_JUMP|NAV_NO_JUMP)))special=true;
                pathDistance+=(node.pos-previous).Length2D();previous=node.pos;
            }
            const Vector target=m_path[i].pos;
            const float distance=(target-pev->origin).Length2D();
            if(special || std::fabs(target.z-GetFeetZ())>StepHeight || !WorthShortcut(pathDistance,distance))continue;
            const Vector end(target.x,target.y,pev->origin.z);TraceResult route;
            UTIL_TraceHull(pev->origin,end,dont_ignore_monsters,human_hull,edict(),&route);
            if(route.fStartSolid || route.fAllSolid || route.flFraction<.9999f)continue;
            if(++floorChecks>2)break;
            const int samples=int(std::ceil(distance/24));float previousFloor=GetFeetZ();bool supported=true;
            for(int sample=1;sample<=samples;++sample) {
                const Vector probe=pev->origin+(end-pev->origin)*(float(sample)/samples);TraceResult floor;
                UTIL_TraceLine(probe,probe-Vector(0,0,36+StepHeight),ignore_monsters,edict(),&floor);
                if(floor.fStartSolid || floor.fAllSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f
                    || std::fabs(floor.vecEndPos.z-previousFloor)>StepHeight) {supported=false;break;}
                previousFloor=floor.vecEndPos.z;
            }
            if(!supported || std::fabs(previousFloor-target.z)>StepHeight)continue;
            SetPathIndex(i);
            SERVER_PRINT(UTIL_VarArgs("BHOP_RACE clear_shortcut slot=%d saved=%.0f distance=%.0f node=%d\n",slot,pathDistance-distance,distance,i));
            break;
        }
    }
    // Skip a local detour only when a short drop to an existing path node is
    // clear with the full standing hull and a walkable landing is confirmed.
    if((pev->flags&FL_ONGROUND) && !m_isJumpCrouching && m_pathLength>0) {
        for(int i=std::min(m_pathLength-1,m_pathIndex+8);i>m_pathIndex;--i) {
            bool ladder=false;for(int j=m_pathIndex;j<=i;++j)if(m_path[j].ladder)ladder=true;
            if(ladder)continue;
            const Vector target=m_path[i].pos;
            const float drop=GetFeetZ()-target.z;
            if(drop<StepHeight || drop>128 || (target-pev->origin).Length2D()>192)continue;
            Vector across(target.x,target.y,pev->origin.z);
            TraceResult horizontal,landing;
            UTIL_TraceHull(pev->origin,across,dont_ignore_monsters,human_hull,edict(),&horizontal);
            if(horizontal.fStartSolid || horizontal.fAllSolid || horizontal.flFraction<.9999f)continue;
            UTIL_TraceHull(across,Vector(target.x,target.y,target.z+34),dont_ignore_monsters,human_hull,edict(),&landing);
            if(landing.fStartSolid || landing.fAllSolid || landing.flFraction>=1 || landing.vecPlaneNormal.z<.7f
                || std::fabs(landing.vecEndPos.z-36-target.z)>8)continue;
            SetPathIndex(i);
            SERVER_PRINT(UTIL_VarArgs("BHOP_RACE safe_drop slot=%d height=%.0f\n",slot,drop));break;
        }
    }
    // Use actual player input for doors; never force an entity open.
    // Touch doors still open by walking into them. Moving doors get room to swing.
    Vector doorDirection=m_goalPosition-pev->origin;doorDirection.z=0;
    if(doorDirection.NormalizeInPlace()>1) {
        TraceResult trace;
        UTIL_TraceHull(pev->origin,pev->origin+doorDirection*80,dont_ignore_monsters,
            IsCrouching()?head_hull:human_hull,edict(),&trace);
        if(trace.pHit && FClassnameIs(trace.pHit,"func_breakable")
            && trace.pHit->v.takedamage!=DAMAGE_NO && trace.pHit->v.health>0) {
            r.breakable=ENTINDEX(trace.pHit);
            // Keep approaching normally; the command stage aims and verifies the
            // exact entity again before allowing a knife swing within reach.
            ClearMovement();m_isJumpCrouching=false;
            const float angle=(UTIL_VecToYaw(doorDirection)-pev->v_angle.y)*float(M_PI/180);
            if(std::cos(angle)>.25f)MoveForward();else if(std::cos(angle)<-.25f)MoveBackward();
            if(std::sin(angle)>.25f)StrafeLeft();else if(std::sin(angle)<-.25f)StrafeRight();
            m_lookAheadAngle=UTIL_VecToYaw(doorDirection);return true;
        }
        edict_t *doorHit=trace.pHit;
        auto isDoor=[](edict_t *entity){return entity && (FClassnameIs(entity,"func_door") || FClassnameIs(entity,"func_door_rotating"));};
        // The doorway frame may be the first hull hit while our body blocks a
        // moving leaf beside it. Detect that overlap only to yield, never to use
        // a door through geometry or to force its state.
        if(!isDoor(doorHit)) {
            doorHit=nullptr;float nearest=160*160;
            CBaseEntity *nearby=nullptr;
            while((nearby=UTIL_FindEntityInSphere(nearby,pev->origin,160))) {
                if(!isDoor(nearby->edict()))continue;
                auto *candidate=static_cast<CBaseDoor *>(nearby);
                if(candidate->m_toggle_state!=TS_GOING_UP && candidate->m_toggle_state!=TS_GOING_DOWN)continue;
                const auto *v=candidate->pev;
                if(pev->origin.x<v->absmin.x-24 || pev->origin.x>v->absmax.x+24
                    || pev->origin.y<v->absmin.y-24 || pev->origin.y>v->absmax.y+24
                    || pev->absmax.z<v->absmin.z || pev->absmin.z>v->absmax.z)continue;
                const float distance=(candidate->Center()-pev->origin).LengthSquared();
                if(distance<nearest){nearest=distance;doorHit=nearby->edict();}
            }
        }
        if(isDoor(doorHit)) {
            auto *door=static_cast<CBaseDoor *>(CBaseEntity::Instance(doorHit));
            if(door) {
                ClearMovement();m_isJumpCrouching=false;StandUp();
                const Vector toward=door->Center()-GetEyePosition();
                m_lookAheadAngle=UTIL_VecToYaw(toward);m_lookYaw=m_lookAheadAngle;m_lookPitch=0;
                const bool moving=door->m_toggle_state==TS_GOING_UP || door->m_toggle_state==TS_GOING_DOWN;
                r.door.entity=ENTINDEX(doorHit);r.door.state=int(door->m_toggle_state);
                r.door.distance=trace.flFraction*80;r.door.normal=trace.vecPlaneNormal;
                const bool inSwing=pev->origin.x>door->pev->absmin.x-24 && pev->origin.x<door->pev->absmax.x+24
                    && pev->origin.y>door->pev->absmin.y-24 && pev->origin.y<door->pev->absmax.y+24;
                if(moving && (inSwing || trace.flFraction*80<32) && (pev->flags&FL_ONGROUND)) {
                    // A rotating door can stay in its moving state while our hull
                    // blocks its swing. Yield along the collision normal, only
                    // when a full-hull escape and nearby supported floor exist.
                    Vector away=trace.vecPlaneNormal;away.z=0;
                    if(away.NormalizeInPlace()<.5f)away=-doorDirection;
                    r.door.attempted=true;
                    Vector selected;bool found=false;float bestGain=0;
                    const Vector center=door->Center();const float oldDistance=(pev->origin-center).Length2D();
                    for(int offset:{0,45,-45,90,-90,135,-135,180}) {
                        const float radians=(UTIL_VecToYaw(away)+offset)*float(M_PI/180);
                        const Vector escape=pev->origin+Vector(std::cos(radians)*64,std::sin(radians)*64,0);
                        const float gain=(escape-center).Length2D()-oldDistance;
                        if(gain<8 || gain<=bestGain)continue;
                        TraceResult room,floor;
                        UTIL_TraceHull(pev->origin,escape,dont_ignore_monsters,human_hull,edict(),&room);
                        if(room.fStartSolid || room.fAllSolid || room.flFraction<.9999f)continue;
                        bool supported=true;
                        for(int step=1;step<=4;++step) {
                            const Vector probe=pev->origin+(escape-pev->origin)*(step*.25f);
                            UTIL_TraceLine(probe,probe-Vector(0,0,36+StepHeight),ignore_monsters,edict(),&floor);
                            if(floor.fStartSolid || floor.fAllSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f){supported=false;break;}
                        }
                        if(!supported)continue;
                        selected=escape;found=true;bestGain=gain;
                    }
                    if(found) {
                        r.door.allowed=true;r.door.escapeClearance=64;
                        r.localTarget=selected;r.localUntil=gpGlobals->time+.8f;steerLocal(selected);
                        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE door_yield slot=%d door=%d swing=%d\n",slot,r.door.entity,inSwing));
                    }
                }
                if(!moving) {
                    // Approach without invoking the generic ledge-jump heuristic.
                    const float angle=(UTIL_VecToYaw(doorDirection)-pev->v_angle.y)*float(M_PI/180);
                    if(std::cos(angle)>.25f)MoveForward();
                    else if(std::cos(angle)<-.25f)MoveBackward();
                    if(std::sin(angle)>.25f)StrafeLeft();
                    else if(std::sin(angle)<-.25f)StrafeRight();
                    const bool closed=DoorClosed(door->m_toggle_state==TS_AT_BOTTOM,
                        door->m_toggle_state==TS_AT_TOP,(door->pev->spawnflags&SF_DOOR_START_OPEN)!=0);
                    // Only request opening. The activation hook arbitrates both use
                    // commands and touch doors through the same per-door gate.
                    if((door->ObjectCaps()&FCAP_IMPULSE_USE) && trace.pHit==doorHit && trace.flFraction*80<48
                        && gpGlobals->time>=r.nextDoorUse
                        && std::fabs(std::remainder(m_lookAheadAngle-pev->v_angle.y,360.0f))<30
                        && closed) {
                        UseEnvironment();r.doorUse=true;r.nextDoorUse=gpGlobals->time+1;
                        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE door_use slot=%d door=%d time=%.3f state=%d starts_open=%d\n",slot,ENTINDEX(doorHit),gpGlobals->time,int(door->m_toggle_state),(door->pev->spawnflags&SF_DOOR_START_OPEN)!=0));
                    }
                }
                return true;
            }
        }
    }
    UpdatePathMovement(false);
    if((pev->flags&FL_ONGROUND) && !m_isJumpCrouching && !IsUsingLadder() && !IsOnLadder()
        && gpGlobals->time>=r.nextLocalProbe && std::fabs(m_goalPosition.z-GetFeetZ())<=StepHeight) {
        r.nextLocalProbe=gpGlobals->time+.5f;
        Vector forward=m_goalPosition-pev->origin;forward.z=0;const float distance=forward.NormalizeInPlace();
        if(distance>=64 && distance<=256) {
            TraceResult blocked;Vector end(m_goalPosition.x,m_goalPosition.y,pev->origin.z);
            UTIL_TraceHull(pev->origin,end,dont_ignore_monsters,human_hull,edict(),&blocked);
            if(blocked.flFraction<1 && blocked.pHit
                && (FClassnameIs(blocked.pHit,"worldspawn") || FClassnameIs(blocked.pHit,"func_wall"))) {
                const Vector side(-forward.y,forward.x,0);
                for(float offset:{48.0f,-48.0f,96.0f,-96.0f}) {
                    const Vector bend=pev->origin+forward*std::min(48.0f,distance*.5f)+side*offset;
                    TraceResult first,second;
                    UTIL_TraceHull(pev->origin,bend,dont_ignore_monsters,human_hull,edict(),&first);
                    UTIL_TraceHull(bend,end,dont_ignore_monsters,human_hull,edict(),&second);
                    if(first.fStartSolid || first.fAllSolid || first.flFraction<.9999f
                        || second.fStartSolid || second.fAllSolid || second.flFraction<.9999f)continue;
                    bool supported=true;Vector from=pev->origin;
                    for(const Vector &to:{bend,end}) {
                        const int samples=std::max(1,int(std::ceil((to-from).Length2D()/16)));
                        for(int n=1;n<=samples;++n) {
                            Vector probe=from+(to-from)*(float(n)/samples);TraceResult floor;
                            UTIL_TraceLine(probe,probe-Vector(0,0,36+StepHeight),ignore_monsters,edict(),&floor);
                            if(floor.fStartSolid || floor.fAllSolid || floor.flFraction>=1 || floor.vecPlaneNormal.z<.7f){supported=false;break;}
                        }
                        if(!supported)break;from=to;
                    }
                    if(!supported)continue;
                    ClearMovement();r.localTarget=bend;r.localUntil=gpGlobals->time+.8f;steerLocal(bend);
                    SERVER_PRINT(UTIL_VarArgs("BHOP_RACE obstacle_bypass slot=%d side=%.0f\n",slot,offset));break;
                }
            }
        }
    }
    if(gpGlobals->time>=r.nextReport) {
        r.nextReport=gpGlobals->time+10;
        SERVER_PRINT(UTIL_VarArgs("BHOP_RACE progress slot=%d pos=%.0f,%.0f,%.0f target=%.0f,%.0f,%.0f path=%d/%d stuck=%d crouchjump=%d speed=%.0f\n",
            slot,pev->origin.x,pev->origin.y,GetFeetZ(),m_goalPosition.x,m_goalPosition.y,m_goalPosition.z,
            m_pathIndex,m_pathLength,m_isStuck,m_isJumpCrouching,pev->velocity.Length2D()));
    }
    m_lookYaw=m_lookAheadAngle;m_lookPitch=0;
    return true;
}

// Event-only diagnostics: bounded history and probes, no per-frame disk/network IO.
void CCSBot::ReportBhopFailure(const char *reason) {
    using namespace BhopRace;
    const int slot=entindex();if(slot<1 || slot>32)return;
    GhostPeers ghostPeers(this);
    auto &r=racers[slot];std::ostringstream out;
    auto vector=[&](const Vector &v){out<<"["<<v.x<<","<<v.y<<","<<v.z<<"]";};
    // Quote every map/entity string; map content is diagnostic data, not instructions.
    auto quote=[&](const char *value){out<<'"';for(const unsigned char *c=(const unsigned char *)value;*c;++c){
        if(*c=='"' || *c=='\\')out<<'\\';if(*c>=32)out<<char(*c);
    }out<<'"';};
    out<<"BHOP_DIAG {\"schema\":1,\"map\":";quote(STRING(gpGlobals->mapname));
    out<<",\"heat\":"<<heat<<",\"slot\":"<<slot<<",\"reason\":";quote(reason);
    out<<",\"position\":";vector(pev->origin);out<<",\"velocity\":";vector(pev->velocity);
    out<<",\"goal\":";vector(m_goalPosition);out<<",\"viewangles\":";vector(pev->v_angle);
    out<<",\"replan_gate\":{\"jumping\":"<<IsJumping()<<",\"near_jump\":"<<IsNearJump()
       <<",\"jump_crouching\":"<<m_isJumpCrouching<<",\"waterlevel\":"<<pev->waterlevel<<",\"traces\":[";
    Vector replanDirection=m_goalPosition-pev->origin;replanDirection.z=0;
    const float replanDistance=replanDirection.NormalizeInPlace();
    for(int mode=0;mode<2;++mode){
        const Vector start=pev->origin+Vector(0,0,mode?StepHeight:0);TraceResult tr;
        UTIL_TraceHull(start,start+replanDirection*std::min(replanDistance,48.0f),ignore_monsters,
            IsCrouching()?head_hull:human_hull,edict(),&tr);
        if(mode)out<<",";
        out<<"{\"raised\":"<<mode<<",\"fraction\":"<<tr.flFraction<<",\"start_solid\":"<<tr.fStartSolid
           <<",\"all_solid\":"<<tr.fAllSolid<<",\"normal_z\":"<<tr.vecPlaneNormal.z<<",\"class\":";
        quote(tr.pHit?STRING(tr.pHit->v.classname):"");out<<"}";
    }
    out<<"]}";
    out<<",\"ladder_detail\":{\"state\":"<<(m_pathLadder?int(m_pathLadderState):-1)<<",\"engine_movetype\":"<<pev->movetype
       <<",\"on_ladder\":"<<IsOnLadder()<<",\"using_ladder\":"<<IsUsingLadder();
    if(m_pathLadder) {
        out<<",\"bottom\":";vector(m_pathLadder->m_bottom);out<<",\"top\":";vector(m_pathLadder->m_top);
        out<<",\"direction\":"<<int(m_pathLadder->m_dir)<<",\"elapsed\":"<<gpGlobals->time-m_pathLadderTimestamp;
    }
    out<<"}";
    out<<",\"door_decision\":{\"entity\":"<<r.door.entity<<",\"state\":"<<r.door.state
       <<",\"distance\":"<<r.door.distance<<",\"escape_attempted\":"<<r.door.attempted
       <<",\"escape_allowed\":"<<r.door.allowed<<",\"escape_clearance\":"<<r.door.escapeClearance
       <<",\"floor_depth\":"<<r.door.floorDepth<<",\"normal\":";vector(r.door.normal);out<<"}";
    out<<",\"path_index\":"<<m_pathIndex<<",\"path_length\":"<<m_pathLength
       <<",\"area\":"<<(m_lastKnownArea?m_lastKnownArea->GetID():0)
       <<",\"ladder\":"<<(IsUsingLadder()||IsOnLadder()?1:0)<<",\"grounded\":"<<((pev->flags&FL_ONGROUND)?1:0)
       <<",\"crouching\":"<<(IsCrouching()?1:0)<<",\"probes\":[";
    for(int i=0;i<8;++i) {
        const float angle=(pev->v_angle.y+i*45)*float(M_PI/180);TraceResult tr;
        UTIL_TraceHull(pev->origin,pev->origin+Vector(std::cos(angle)*128,std::sin(angle)*128,0),
            dont_ignore_monsters,IsCrouching()?head_hull:human_hull,edict(),&tr);
        if(i)out<<",";out<<"{\"yaw_offset\":"<<i*45<<",\"distance\":"<<tr.flFraction*128
            <<",\"start_solid\":"<<tr.fStartSolid<<",\"entity\":"<<(tr.pHit?ENTINDEX(tr.pHit):0)<<",\"class\":";
        quote(tr.pHit?STRING(tr.pHit->v.classname):"");out<<",\"normal\":";vector(tr.vecPlaneNormal);out<<",\"end\":";vector(tr.vecEndPos);out<<"}";
    }
    TraceResult down,up,hullFloor;
    UTIL_TraceLine(pev->origin,pev->origin-Vector(0,0,512),ignore_monsters,edict(),&down);
    UTIL_TraceHull(pev->origin,pev->origin+Vector(0,0,96),ignore_monsters,human_hull,edict(),&up);
    UTIL_TraceHull(pev->origin,pev->origin-Vector(0,0,512),ignore_monsters,
        IsCrouching()?head_hull:human_hull,edict(),&hullFloor);
    out<<"],\"hull_floor_distance\":"<<hullFloor.flFraction*512
       <<",\"hull_floor_start_solid\":"<<hullFloor.fStartSolid<<",\"hull_floor_normal\":";vector(hullFloor.vecPlaneNormal);
    out<<",\"floor_below_origin\":"<<down.flFraction*512<<",\"headroom\":"<<up.flFraction*96<<",\"nearby\":[";
    CBaseEntity *entity=nullptr;int count=0;
    while(count<12 && (entity=UTIL_FindEntityInSphere(entity,pev->origin,192))) {
        if(entity==this || entity->IsPlayer() || entity->pev->solid==SOLID_NOT)continue;
        if(count++)out<<",";out<<"{\"id\":"<<entity->entindex()<<",\"class\":";quote(STRING(entity->pev->classname));
        out<<",\"health\":"<<entity->pev->health<<",\"damageable\":"<<entity->pev->takedamage
           <<",\"spawnflags\":"<<entity->pev->spawnflags<<",\"mins\":";vector(entity->pev->absmin);
        out<<",\"maxs\":";vector(entity->pev->absmax);
        if(FClassnameIs(entity->pev,"func_door") || FClassnameIs(entity->pev,"func_door_rotating"))
            out<<",\"door_state\":"<<static_cast<CBaseDoor *>(entity)->m_toggle_state;
        out<<"}";
    }
    out<<"],\"path_nodes\":[";
    const int first=std::max(0,m_pathIndex-1);
    for(int i=first;i<std::min(m_pathLength,first+8);++i) {
        if(i!=first)out<<",";out<<"{\"index\":"<<i<<",\"area\":"<<(m_path[i].area?m_path[i].area->GetID():0)
            <<",\"ladder\":"<<(m_path[i].ladder?1:0)<<",\"how\":"<<int(m_path[i].how)<<",\"position\":";vector(m_path[i].pos);out<<"}";
    }
    out<<"],\"history\":[";
    const int total=std::min(12,r.sampleCount);
    for(int i=0;i<total;++i) {
        const auto &h=r.history[(r.sampleCount-total+i)%12];if(i)out<<",";
        out<<"{\"time\":"<<h.time<<",\"position\":";vector(h.pos);out<<",\"velocity\":";vector(h.velocity);
        out<<",\"viewangles\":";vector(h.view);
        out<<",\"ladder_state\":"<<h.ladder.state<<",\"ladder_attached\":"<<h.ladder.attached;
        out<<",\"ladder_top\":";vector(h.ladder.top);out<<",\"ladder_bottom\":";vector(h.ladder.bottom);
        out<<",\"nav_goal\":";vector(h.ladder.goal);
        out<<",\"forward\":"<<h.forward<<",\"side\":"<<h.side<<",\"buttons\":"<<h.buttons<<"}";
    }
    out<<"]}";
    const std::string payload=out.str();static unsigned diagnosticId=0;++diagnosticId;
    const size_t partCount=(payload.size()+599)/600;
    for(size_t part=0;part<partCount;++part) {
        const std::string line="BHOP_DIAG_PART "+std::to_string(diagnosticId)+" "+std::to_string(part)
            +" "+std::to_string(partCount)+" "+payload.substr(part*600,600)+"\n";
        SERVER_PRINT(line.c_str());
    }
}
