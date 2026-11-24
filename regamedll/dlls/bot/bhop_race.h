// SPDX-License-Identifier: MIT
#pragma once
class CCSBot;
class CBaseEntity;
class CBaseDoor;
class CBasePlayer;
class CNavArea;
struct usercmd_s;
namespace BhopRace {
// Only the synchronous bot simulation/traces see ghost peers. Restore even on
// early returns; ordinary matches and human movement keep normal solidity.
class GhostPeers {
public:
    explicit GhostPeers(CCSBot *bot);
    ~GhostPeers();
    GhostPeers(const GhostPeers &)=delete;
    GhostPeers &operator=(const GhostPeers &)=delete;
private:
    CBasePlayer *players[32]{};
    int solids[32]{},count=0;
};
bool SpeedPractice(const CCSBot *bot);
bool GroundStrafePractice(const CCSBot *bot);
bool SGSPractice(const CCSBot *bot);
bool UsesGroundStrafe(const CCSBot *bot);
bool LocalRecovery(const CCSBot *bot);
float LookPitch(CCSBot *bot,float distance,float goalZ,float feetZ);
int MovementVariant(const CCSBot *bot);
unsigned MovementHeat();
float RouteMultiplier(const CCSBot *bot,const CNavArea *area);
bool AllowDoorActivation(CBaseDoor *door,CBaseEntity *activator);
void Register();
void Reset();
void Frame();
bool Owns(const CCSBot *bot);
void Command(CCSBot *bot,usercmd_s &cmd);
}
