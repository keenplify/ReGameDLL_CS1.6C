// SPDX-License-Identifier: MIT
#include "precompiled.h"
#include "bhop_match.h"
#include "bhop_race.h"
#include "bhop_match_policy.h"
#include "bhop_training_policy.h"
#include <cstdlib>

namespace BhopMatch {
namespace {
cvar_t enabled={"bot_bhop_match","0",FCVAR_SERVER,0,nullptr};
BhopTraining::Parameters policy;
bool loaded=false;
BhopTraining::GroundStrafeParameters groundPolicy{.06f,.005f,245};
struct State { BhopTraining::PostHopSGS sgs; unsigned sgsBursts=0; BhopTraining::GroundStrafeController groundController; BhopTraining::GroundStrafeParameters groundParameters; bool previousGroundDuck=false; unsigned groundCommands=0,groundDuckPresses=0,groundDuckCommands=0; int userid=0; bool flight=false; float last=0,flightStarted=0; BhopTraining::Controller controller; unsigned profileHeat=0,jumps=0,commands=0,movementAlignedJumps=0,airDuckCommands=0; } states[33];
void Load() {
    if(CMD_ARGC()!=6 && CMD_ARGC()!=11) { SERVER_PRINT("BHOP_MATCH invalid policy: expected five legacy or ten parameters\n");return; }
    float values[10];const int count=CMD_ARGC()-1;
    for(int i=0;i<count;++i) {
        const char *text=CMD_ARGV(i+1);char *end=nullptr;values[i]=std::strtof(text,&end);
        if(end==text || *end || !std::isfinite(values[i])) { SERVER_PRINT("BHOP_MATCH invalid policy number\n");return; }
    }
    BhopTraining::Parameters candidate;
    candidate.Load(values,count);
    if(!candidate.Valid()) { SERVER_PRINT("BHOP_MATCH policy outside bounds\n");return; }
    policy=candidate;loaded=true;
    for(auto &state:states)state={};
    SERVER_PRINT("BHOP_MATCH policy loaded\n");
}
void LoadGround() {
    if(CMD_ARGC()!=4){SERVER_PRINT("BHOP_MATCH invalid groundstrafe policy count\n");return;}
    float v[3];for(int i=0;i<3;++i){char *end=nullptr;v[i]=std::strtof(CMD_ARGV(i+1),&end);if(end==CMD_ARGV(i+1)||*end||!std::isfinite(v[i])){SERVER_PRINT("BHOP_MATCH invalid groundstrafe number\n");return;}}
    BhopTraining::GroundStrafeParameters p{v[0],v[1],v[2]};
    if(!p.Valid()){SERVER_PRINT("BHOP_MATCH groundstrafe policy outside bounds\n");return;}
    groundPolicy=p;SERVER_PRINT("BHOP_MATCH experimental groundstrafe policy queued for next heat\n");
}
void Status() {
    unsigned profileHeat=0,jumps=0,commands=0,movementAlignedJumps=0,airDuckCommands=0;
    unsigned groundCommands=0,groundDuckPresses=0,groundDuckCommands=0;
    for(const auto &s:states){groundCommands+=s.groundCommands;groundDuckPresses+=s.groundDuckPresses;groundDuckCommands+=s.groundDuckCommands;jumps+=s.jumps;commands+=s.commands;movementAlignedJumps+=s.movementAlignedJumps;airDuckCommands+=s.airDuckCommands;}
    SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH status enabled=%d loaded=%d commands=%u jumps=%u movement_aligned_jumps=%u air_duck_commands=%u ground_commands=%u ground_duck_presses=%u ground_duck_commands=%u\n",enabled.value==1,loaded,commands,jumps,movementAlignedJumps,airDuckCommands,groundCommands,groundDuckPresses,groundDuckCommands));
}
}
void Register() {
    CVAR_REGISTER(&enabled);
    ADD_SERVER_COMMAND("bot_bhop_policy",Load);
    ADD_SERVER_COMMAND("bot_gstrafe_policy",LoadGround);
    ADD_SERVER_COMMAND("bot_bhop_status",Status);
}
}

void CCSBot::ApplyBhopMovement(usercmd_t &cmd,float dt) {
    using namespace BhopTraining;
    using namespace BhopMatch;
    if(enabled.value!=1 || !loaded || CVAR_GET_FLOAT("bot_bhop_training")!=0) return;
    const int slot=entindex();if(slot<1 || slot>32)return;
    auto &state=states[slot];
    const int userid=GETPLAYERUSERID(edict());
    if(state.userid!=userid || gpGlobals->time<state.last || gpGlobals->time-state.last>.2f) {
        state.controller={};state.groundController={};state.sgs={};state.previousGroundDuck=false;state.flight=false;state.userid=userid;
    }
    state.last=gpGlobals->time;
    // A committed local escape must not be steered back toward the old NAV goal.
    if(BhopRace::LocalRecovery(this)){state.controller={};state.groundController={};state.sgs={};state.previousGroundDuck=false;state.flight=false;return;}
    const bool race=BhopRace::Owns(this);
    const unsigned heat=race?BhopRace::MovementHeat():0;
    const auto selectedPolicy=race?Explore(policy,unsigned(slot-1),heat):policy;
    if(race && state.profileHeat!=heat) {
        state.profileHeat=heat;state.groundParameters=groundPolicy;state.groundController={};state.sgs={};state.previousGroundDuck=false;
        if(BhopRace::UsesGroundStrafe(this))SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH groundstrafe_profile slot=%d heat=%u experimental=1 period=%.3f hold=%.3f min_speed=%.1f\n",slot,heat,state.groundParameters.period,state.groundParameters.hold,state.groundParameters.minSpeed));
        SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH profile slot=%d heat=%u group=%s values=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            slot,heat,(slot-1)%5==0?"champion":(slot-1)%5==4?"extreme":"moderate",
            selectedPolicy.airAngle,selectedPolicy.weave,selectedPolicy.period,selectedPolicy.takeoffSpeed,selectedPolicy.jumpDelay,
            selectedPolicy.lookAhead,selectedPolicy.turnRate,selectedPolicy.strafeStrength,selectedPolicy.crouchStart,selectedPolicy.crouchDuration));
    }
    const bool ground=(pev->flags&FL_ONGROUND)!=0;
    if(!ground && !state.flight)return;
    const Vector delta=m_goalPosition-Vector(pev->origin.x,pev->origin.y,GetFeetZ());
    const float goalYaw=UTIL_VecToAngles(delta).y;
    const bool speedPractice=BhopRace::SpeedPractice(this);
    const bool validPath=m_pathLength>0 && m_pathIndex>=0 && m_pathIndex<m_pathLength;
    const bool special=(speedPractice && m_currentArea && (m_currentArea->GetAttributes()&(NAV_CROUCH|NAV_JUMP|NAV_NO_JUMP))) || (validPath && (m_path[m_pathIndex].ladder || !m_path[m_pathIndex].area
        || (m_path[m_pathIndex].area->GetAttributes() & (NAV_CROUCH|NAV_JUMP|NAV_NO_JUMP))));
    // Winter carries an HE-slot snowball throughout the round. Its charge
    // movement cap (65% of running speed) and a pending toss still block hops.
    const bool carryingSnowball = IsUsingHEGrenade() && CVAR_GET_FLOAT("sw_toggle") == 1
        && !IsThrowingGrenade() && pev->maxspeed >= 200.0f;
    // The native grenade AI holds primary attack while travelling. Winter owns the actual throw.
    const int blockedButtons=IN_ATTACK2|IN_USE|IN_RELOAD|IN_DUCK|IN_JUMP|(carryingSnowball?0:IN_ATTACK);
    const bool buttons=(cmd.buttons & blockedButtons)!=0;
    const float viewHeadingError=std::fabs(Wrap(goalYaw-cmd.viewangles.y));
    // During flight, judge the actual trajectory rather than the last look angle.
    const float headingError=ground?MovementHeadingError(goalYaw,cmd.viewangles.y,cmd.forwardmove,cmd.sidemove)
        :std::fabs(Wrap(goalYaw-UTIL_VecToAngles(pev->velocity).y));
    Context context{IsAlive(),IsRunning(),IsCrouching(),IsAttacking()||IsEnemyVisible(),
        buttons||IsBlind()||IsReloading()||IsBuying()||(IsUsingGrenade() && !carryingSnowball)||IsHiding()||IsDefusingBomb()
            ||GetTask()==PLANT_BOMB||GetTask()==DEFUSE_BOMB||CSGameRules()->IsFreezePeriod()||(pev->flags&FL_FROZEN),
        IsUsingLadder()||IsOnLadder(),pev->waterlevel>0,m_isStuck,m_isWaitingBehindFriend,
        validPath,special,GetTimeSinceLastSawEnemy(),delta.Length2D(),std::max(0.0f,std::fabs(delta.z)-(ground?0.0f:64.0f)),
        headingError,std::sqrt(cmd.forwardmove*cmd.forwardmove+cmd.sidemove*cmd.sidemove)};
    if(!Eligible(context,speedPractice,carryingSnowball)) {state.controller={};state.groundController={};state.sgs={};state.previousGroundDuck=false;state.flight=false;return;}
    // Keep normal navigation in charge of stairs, drops, tight turns and player avoidance.
    TraceResult path;
    const Vector routeGoal(m_goalPosition.x,m_goalPosition.y,pev->origin.z);
    UTIL_TraceHull(pev->origin,routeGoal,dont_ignore_monsters,human_hull,edict(),&path);
    if(path.fStartSolid || path.fAllSolid || path.flFraction<.9999f) {state.controller={};state.groundController={};state.sgs={};state.previousGroundDuck=false;state.flight=false;return;}
    Observation o{goalYaw,UTIL_VecToAngles(pev->velocity).y,pev->velocity.Length2D(),cmd.viewangles.y,dt,
        (pev->flags&FL_ONGROUND)!=0};
    for(int i=0;i<5;++i) {
        const float radians=(goalYaw+(i-2)*30)*Pi/180;TraceResult tr;
        UTIL_TraceHull(pev->origin,pev->origin+Vector(std::cos(radians)*256,std::sin(radians)*256,0),
            dont_ignore_monsters,human_hull,edict(),&tr);
        o.clearance[i]=(tr.fStartSolid||tr.fAllSolid)?0:tr.flFraction*256;
    }
    TraceResult ceiling,floor;
    UTIL_TraceHull(pev->origin,pev->origin+Vector(0,0,64),dont_ignore_monsters,human_hull,edict(),&ceiling);
    o.ceiling=(ceiling.fStartSolid||ceiling.fAllSolid)?0:ceiling.flFraction*64;
    const float radians=goalYaw*Pi/180;
    const Vector ahead=pev->origin+Vector(std::cos(radians)*64,std::sin(radians)*64,0);
    UTIL_TraceLine(ahead,ahead-Vector(0,0,160),ignore_monsters,edict(),&floor);
    o.floorAhead=!floor.fStartSolid && floor.flFraction<1 && pev->origin.z-floor.vecEndPos.z<=(o.grounded?54:112);
    if(!o.floorAhead || o.ceiling<(o.grounded?48:8) || o.clearance[2]<(o.grounded?std::max(96.0f,o.speed*selectedPolicy.lookAhead):48)) {state.controller={};state.groundController={};state.sgs={};state.previousGroundDuck=false;state.flight=false;return;}
    bool sgsBurst=false;
    if(BhopRace::SGSPractice(this)) {
        const bool wasActive=state.sgs.active;
        const bool duck=state.sgs.Step(o);
        sgsBurst=state.sgs.active;
        if(!wasActive && sgsBurst){++state.sgsBursts;SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH sgs_begin slot=%d speed=%.2f dt=%.3f burst=%u experimental=1\n",slot,o.speed,dt,state.sgsBursts));}
        if(wasActive && !sgsBurst)SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH sgs_end slot=%d speed=%.2f\n",slot,o.speed));
        if(duck)cmd.buttons|=IN_DUCK;
    }
    if(BhopRace::UsesGroundStrafe(this)) {
        const bool duck=state.groundController.Step(o,state.groundParameters);
        if(duck){cmd.buttons|=IN_DUCK;++state.groundDuckCommands;if(!state.previousGroundDuck){++state.groundDuckPresses;if(state.groundDuckPresses<=3)SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH ground_tap slot=%d speed=%.2f entry_speed=%.2f command_dt=%.3f\n",slot,o.speed,state.groundController.entrySpeed,dt));}}
        state.previousGroundDuck=duck;++state.groundCommands;
        if(duck || BhopRace::GroundStrafePractice(this)){state.flight=false;return;}
        // Arrival mode may bhop on released commands; never jump and ground-tap together.
    }
    auto movementPolicy=race?selectedPolicy:WideSwing(policy,o,BhopRace::MovementVariant(this));
    // The lab uses a 250 u/s weapon. Preserve its takeoff fraction for slower weapons.
    movementPolicy.takeoffSpeed=Clamp(selectedPolicy.takeoffSpeed*Clamp(pev->maxspeed,100.0f,250.0f)/250.0f,100.0f,250.0f);
    const auto action=state.controller.Step(o,movementPolicy,!sgsBurst);
    // Ground acceleration/direction remain the navigation controller's responsibility.
    if(!o.grounded) {
        cmd.forwardmove=action.forward;cmd.sidemove=action.side;
        cmd.viewangles.y=action.yaw;pev->v_angle.y=action.yaw;
        m_lookYaw=action.yaw;m_lookYawVel=0;
        cmd.buttons &= ~(IN_FORWARD|IN_BACK|IN_MOVELEFT|IN_MOVERIGHT);
        cmd.buttons |= action.side>0?IN_MOVERIGHT:action.side<0?IN_MOVELEFT:0;
    }
    if(action.jump){
        cmd.buttons|=IN_JUMP;if(BhopRace::SGSPractice(this))state.sgs.Arm();state.flight=true;state.flightStarted=gpGlobals->time;++state.jumps;
        if(viewHeadingError>=35) {
            ++state.movementAlignedJumps;
            // Bound evidence logging while retaining per-map aggregate counts.
            if(state.movementAlignedJumps<=3)
                SERVER_PRINT(UTIL_VarArgs("BHOP_MATCH aligned_takeoff slot=%d pos=%.0f,%.0f,%.0f speed=%.1f view_error=%.1f move_error=%.1f\n",
                    slot,pev->origin.x,pev->origin.y,pev->origin.z,o.speed,viewHeadingError,headingError));
        }
    }
    else if(ground && !sgsBurst)state.flight=false;
    // A short airborne tuck; release on descent or ground contact so it cannot
    // turn into crouch-walking. Engine collision still controls uncrouching.
    if(!sgsBurst && (BhopRace::Owns(this) || carryingSnowball) && state.flight && !ground
        && AirDuck(gpGlobals->time-state.flightStarted,pev->velocity.z,selectedPolicy.crouchStart,selectedPolicy.crouchDuration)) {
        cmd.buttons|=IN_DUCK;++state.airDuckCommands;
    }
    ++state.commands;
}
