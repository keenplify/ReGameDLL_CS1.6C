// SPDX-License-Identifier: MIT
#pragma once

class CCSBot;
class CBasePlayer;
class CBaseEntity;
class CNavArea;

// State is external to CCSBot/CCSBotManager to preserve the public class layout.
namespace CompetitiveBots {
void RegisterCVars();
void TryNavigationLearning(CCSBot *bot);
void ResetNavigationLearning();
void RegisterTacticsCVars();
void ResetTactics(bool map);
void ResetTraffic();
void UpdateTactics(CCSBot *bot);
void UpdateSpreeReactions();
bool RoundTacticsAllowed();
bool IsRoundAce(CBasePlayer *player);
bool ReactToSpree(CBasePlayer *player,int kills);
void TacticsEvent(GameEventType event,CBaseEntity *entity,CBaseEntity *other);
void ConfirmedDamage(CBasePlayer *victim,CBaseEntity *attacker,float amount);
void Notice(CCSBot *bot,CBasePlayer *target);
float ExecutionSkill(CCSBot *bot);
float ExecutionReaction(CCSBot *bot);
bool AimAtHead(CCSBot *bot);
void SampleAim(CCSBot *bot,Vector &aim);
float AimError(CCSBot *bot,float nativeError);
bool EarlyFire(CCSBot *bot);
bool RecoverRecoil(CCSBot *bot);
bool WaitForTeamBuy(CCSBot *bot);
bool TeamEco(TeamName team);
bool HasHumanCaptain(TeamName team);
bool AnnouncePlan(CCSBot *bot,const char *line,bool opening=false);
bool InitiatesChat(CCSBot *bot);
bool Speak(CCSBot *bot,CBasePlayer *listener,bool teamOnly,const char *line,bool tactical=false);
bool Banter(CCSBot *bot,CBasePlayer *subject,bool teamOnly,const char *line);
bool Tactical(CCSBot *bot,const char *trigger,const char *radio="%!MRAD_AFFIRM");
bool RequestedDrop(CCSBot *bot,CBasePlayer *human,const char *requested);
void ProtectHumanOrder(CCSBot *bot,float duration=12);
bool HasHumanOrder(CCSBot *bot);
bool ArenaSpreadEnabled();
CNavArea *ArenaHuntArea(CCSBot *bot);
void RegisterGrenadeCVars();
void ResetGrenades(bool map=false);
void RememberHoldingAngle(CCSBot *bot);
void SetTeamEconomy(TeamName team,bool eco);
void CancelGrenade(CCSBot *bot);
bool SmartGrenades();
bool IsManagingGrenade(CCSBot *bot);
bool UpdateGrenade(CCSBot *bot);
bool PauseForTyping(CCSBot *bot);
bool MobileCombatWeapon(CCSBot *bot);
void BeginCombatMovement(CCSBot *bot);
bool PreferMobileCombat(CCSBot *bot);
bool SafeCombatStrafe(CCSBot *bot, bool left);
void Reset();
void RoundRestart();
void Frame();
void Chat(CBasePlayer *speaker, bool teamOnly, const char *text);
void Blinded(CBasePlayer *victim, CBaseEntity *actor, float hold, int alpha);
void Event(GameEventType event, CBaseEntity *entity, CBaseEntity *other);
bool PlayerPingsEnabled();
bool ShowsLatency();
bool PingComplaint(CCSBot *bot,bool death);
void UpdateLatency(const edict_t *recipient);
bool Enabled();
bool Completed();
bool Pause(CCSBot *bot);
bool SimulatedAway(CCSBot *bot);
bool Saving(CCSBot *bot);
bool TargetAllowed(CBasePlayer *player);
}
