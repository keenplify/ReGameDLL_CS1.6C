// SPDX-License-Identifier: MIT
#include "precompiled.h"
#include "cs_bot_competitive.h"
#include "competitive_execution.h"
#include "competitive_enemy_damage.h"
#include "competitive_score_reaction.h"
#include "competitive_spree.h"
#include <cmath>
namespace CompetitiveBots {
namespace {
cvar_t level={"bot_competitive_difficulty","-1",FCVAR_SERVER,0,nullptr};
cvar_t tactics={"bot_advanced_tactics","1",FCVAR_SERVER,0,nullptr};
struct HitMemory {int id=0;EnemyDamageEstimate estimate;};
struct State {
 int id=0,targetId=0,targetSlot=0,kills=0,milestone=0,lastHealth=100;
 bool early=false,head=false,hasSample=false,saving=false,reloading=false;
 float recoilUntil=0,acquired=0,nextSample=0,nextTick=0,nextCover=0,nextSupport=0,nextPlant=0,avoidUntil=0;
 float orderUntil=0,streakDue=0;
 Vector sampled {0,0,0},avoid {0,0,0};
 HitMemory hits[MAX_CLIENTS+1];
};
State states[MAX_CLIENTS+1];
struct SpreeState {int id=0,team=0;CompetitiveSpree::Pending pending;int victims[MAX_CLIENTS+1]={};};
SpreeState sprees[MAX_CLIENTS+1];
struct TeamState {bool economy=false,eco=false,donated=false,saveCall=false;float buyAt=0,donationDue=0;int donorSlot=0,donorId=0,recipientSlot=0,recipientId=0,donationPhase=0;};
TeamState teams[4];
float nextBanter=0;
CBasePlayer *Player(int i) {auto *p=i>0 && i<=gpGlobals->maxClients?UTIL_PlayerByIndex(i):nullptr;return p && p->IsPlayer() && !p->IsDormant()?static_cast<CBasePlayer *>(p):nullptr;}
State &Get(CCSBot *b) {auto &s=states[b->entindex()];int id=GETPLAYERUSERID(b->edict());if(s.id!=id){s=State {};s.id=id;}return s;}
int Level() {if(level.value>=0)return int(level.value);return int(cv_bot_difficulty.value)*4/3;}
bool Ffa() {return CSGameRules()->IsFreeForAll();}
bool Busy(CCSBot *b) {return b->IsDefusingBomb() || b->IsEscapingFromBomb() || b->m_bIsVIP || b->IsCarryingBomb()
 || b->GetTask()==CCSBot::PLANT_BOMB || b->GetTask()==CCSBot::RESCUE_HOSTAGES || IsManagingGrenade(b);}
bool Path(CCSBot *b,const Vector &point) {
 auto *a=TheNavAreaGrid.GetNearestNavArea(&point);if(!a || !b->GetLastKnownArea())return false;
 ShortestPathCost cost;if(NavAreaTravelDistance(b->GetLastKnownArea(),a,cost)<0)return false;
 b->StopFollowing();b->SetTask(CCSBot::SEEK_AND_DESTROY);b->MoveTo(a->GetCenter(),FASTEST_ROUTE);return true;
}
int Alive(TeamName team) {int n=0;for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(p && p->IsAlive() && p->m_iTeam==team)++n;}return n;}
CCSBot *Captain(TeamName team) {CCSBot *best=nullptr;for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(p && p->IsBot() && p->IsAlive() && p->m_iTeam==team && (!best || p->pev->frags>best->pev->frags))best=static_cast<CCSBot *>(p);}return best;}
}
void RegisterTacticsCVars(){CVAR_REGISTER(&level);CVAR_REGISTER(&tactics);}
void ResetTactics(bool map) {
 for(auto &s:states){if(map)s=State {};else {s.recoilUntil=0;s.kills=0;s.targetId=0;s.hasSample=false;s.nextTick=0;s.streakDue=0;s.orderUntil=0;s.saving=false;s.avoidUntil=0;for(auto &hit:s.hits)hit=HitMemory {};}}
 for(auto &spree:sprees)spree=SpreeState {};
 for(auto &t:teams){t=TeamState {};t.buyAt=gpGlobals->time+3;}
 if(map)nextBanter=0;
}
void ProtectHumanOrder(CCSBot *b,float seconds){Get(b).orderUntil=gpGlobals->time+seconds;}
bool HasHumanOrder(CCSBot *b){return gpGlobals->time<Get(b).orderUntil;}
void Notice(CCSBot *b,CBasePlayer *target) {
 if(!Enabled() || !target || !TargetAllowed(target) || !b->IsVisible(target,true))return;
 auto &s=Get(b);int id=GETPLAYERUSERID(target->edict());
 if(s.targetId!=id || s.targetSlot!=target->entindex()) {
  s.targetId=id;s.targetSlot=target->entindex();s.acquired=gpGlobals->time;s.hasSample=false;
  auto p=CompetitiveExecution::Get(Level());s.early=RANDOM_LONG(0,99)<p.early;s.head=RANDOM_FLOAT(0,1)<p.skill*.65f;
 }
}
float ExecutionSkill(CCSBot *b){return Enabled()?CompetitiveExecution::Get(Level()).skill:b->GetProfile()->GetSkill();}
float ExecutionReaction(CCSBot *b){return Enabled()?CompetitiveExecution::Reaction(Level(),Get(b).early):b->GetProfile()->GetReactionTime();}
bool AimAtHead(CCSBot *b){Notice(b,b->GetEnemy());return !Enabled() || Get(b).head;}
void SampleAim(CCSBot *b,Vector &aim){
 if(!Enabled() || !b->IsEnemyVisible())return;Notice(b,b->GetEnemy());auto &s=Get(b);
 if(!s.hasSample || gpGlobals->time>=s.nextSample){s.sampled=aim;s.hasSample=true;s.nextSample=gpGlobals->time+CompetitiveExecution::Get(Level()).tracking;}
 aim=s.sampled;
}
float AimError(CCSBot *b,float nativeError){if(!Enabled())return nativeError;auto &s=Get(b);auto p=CompetitiveExecution::Get(Level());return CompetitiveExecution::AimOffset(Level(),nativeError,s.early && gpGlobals->time-s.acquired<p.settle);}
bool EarlyFire(CCSBot *b){if(!Enabled() || b->IsUsingSniperRifle() || b->IsUsingKnife())return false;auto &s=Get(b);return s.early && gpGlobals->time-s.acquired>=ExecutionReaction(b) && gpGlobals->time-s.acquired<CompetitiveExecution::Get(Level()).settle;}
bool RecoverRecoil(CCSBot *b) {
 if(!Enabled() || !b->IsEnemyVisible() || b->IsUsingPistol() || b->IsUsingSniperRifle() || b->IsUsingKnife() || b->IsUsingGrenade())return false;
 auto *weapon=b->GetActiveWeapon();if(!weapon || weapon->m_iId==WEAPON_M3 || weapon->m_iId==WEAPON_XM1014)return false;
 auto &s=Get(b);if(gpGlobals->time<s.recoilUntil)return true;
 if(Level()<=1 && s.early && gpGlobals->time-s.acquired<CompetitiveExecution::Get(Level()).settle)return false;
 float punch=std::sqrt(b->pev->punchangle.x*b->pev->punchangle.x+b->pev->punchangle.y*b->pev->punchangle.y);
 if(punch<3.5f)return false;
 s.recoilUntil=gpGlobals->time+CompetitiveExecution::Get(Level()).tracking+.16f;return true;
}
bool WaitForTeamBuy(CCSBot *b) {
 if(!Enabled() || !tactics.value || Ffa() || b->m_iTeam<1 || b->m_iTeam>2)return false;
 auto &t=teams[b->m_iTeam];
 if(!t.economy) {
  t.economy=true;int members=0,poor=0;
  for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || !p->IsAlive() || p->m_iTeam!=b->m_iTeam)continue;++members;if(p->m_iAccount<2200 && !p->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT])++poor;}
  t.eco=members>=2 && poor*2>members;
  auto *captain=Captain(b->m_iTeam);if(captain && !HasHumanCaptain(b->m_iTeam))AnnouncePlan(captain,t.eco?"eco this round":"buy this round");
  t.buyAt=gpGlobals->time+2.5f;
 }
 return gpGlobals->time<t.buyAt+(b->entindex()%5)*.12f;
}
void SetTeamEconomy(TeamName team,bool eco){if(team>=1 && team<=2){teams[team].economy=true;teams[team].eco=eco;teams[team].buyAt=gpGlobals->time;}}
bool TeamEco(TeamName team){return team>=1 && team<=2 && teams[team].eco;}
bool RequestedDrop(CCSBot *b,CBasePlayer *human,const char *requested) {
 if(!b || !human || Busy(b) || !b->CanPlayerBuy(false) || b->m_iAccount<6000 || Saving(b)
  || TheCSBots()->IsBombPlanted() || (human->pev->origin-b->pev->origin).LengthSquared()>300*300)return false;
 auto *info=requested?GetWeaponInfo(requested):nullptr;
 auto *item=requested?b->GetItemByName(requested):b->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT];
 if(requested && (!info || info->id==WEAPON_C4 || info->id==WEAPON_HEGRENADE || info->id==WEAPON_FLASHBANG || info->id==WEAPON_SMOKEGRENADE || info->id==WEAPON_KNIFE))return false;
 if(!item && info) {
  if(b->m_iAccount<info->cost+3500)return false;
  if(!BuyWeaponByWeaponID(b,static_cast<WeaponIdType>(info->id)))return false;
  item=b->GetItemByName(requested);
 }
 if(!item)return false;
 char name[64];Q_strlcpy(name,STRING(item->pev->classname));b->DropPlayerItem(name);
 if(b->GetItemByName(name)==item)return false;
 b->RestartCompetitiveBuying();return true;
}
bool IsRoundAce(CBasePlayer *player) {
 if(!player || !RoundTacticsAllowed())return false;
 auto &spree=sprees[player->entindex()];
 if(spree.id!=GETPLAYERUSERID(player->edict()) || spree.team!=int(player->m_iTeam))return false;
 int enemies=0,alive=0,credited=0;
 for(int i=1;i<=gpGlobals->maxClients;++i) {
  auto *enemy=Player(i);
  if(!enemy || (enemy->m_iTeam!=CT && enemy->m_iTeam!=TERRORIST) || enemy->m_iTeam==player->m_iTeam)continue;
  ++enemies;if(enemy->IsAlive())++alive;
  if(spree.victims[i]==GETPLAYERUSERID(enemy->edict()))++credited;
 }
 return CompetitiveSpree::Ace(true,enemies,alive,credited);
}
void UpdateSpreeReactions() {
 if(!Enabled() || !tactics.value || Completed())return;
 for(int i=1;i<=gpGlobals->maxClients;++i) {
  auto &spree=sprees[i];auto &pending=spree.pending;
  if(!pending.count)continue;
  auto *player=Player(i);
  if(!player || GETPLAYERUSERID(player->edict())!=spree.id || int(player->m_iTeam)!=spree.team){spree=SpreeState {};continue;}
  if(gpGlobals->time>pending.expires){pending.Retry(gpGlobals->time);continue;}
  if(pending.Ready(gpGlobals->time)) {
   if(ReactToSpree(player,pending.count))pending.Delivered();
   else pending.Retry(gpGlobals->time);
  }
 }
}
void UpdateTactics(CCSBot *b) {
 if(!Enabled() || !tactics.value || Completed() || !b->IsAlive())return;
 auto &s=Get(b);if(gpGlobals->time<s.nextTick)return;s.nextTick=gpGlobals->time+.5f;
 // Damage reports use confirmed visible hits, never an enemy-health query.
 for(int i=1;i<=gpGlobals->maxClients;++i){auto &h=s.hits[i];auto *p=Player(i);if(h.estimate.ready(gpGlobals->time) && p && p->IsAlive() && GETPLAYERUSERID(p->edict())==h.id){char line[128];Q_snprintf(line,sizeof(line),"%s %s",STRING(p->pev->netname),h.estimate.label());if(Speak(b,nullptr,true,line,true))h.estimate.reportedTier=h.estimate.tier();}}

 int threshold=Ffa()?40:20;
 if(b->pev->frags>=threshold && int(b->pev->frags)/10>s.milestone && gpGlobals->time>=nextBanter){s.milestone=int(b->pev->frags)/10;nextBanter=gpGlobals->time+30;Speak(b,nullptr,!Ffa(),b->m_iDeaths>b->pev->frags+2?"still getting cooked":"feeling good today");}
 if(CSGameRules()->IsFreezePeriod() || CSGameRules()->m_iRoundWinStatus!=WINSTATUS_NONE || Busy(b))return;
 bool pressure=b->GetTimeSinceAttacked()<1.5f && b->pev->health<s.lastHealth-15;
 s.lastHealth=int(b->pev->health);
 if((b->IsReloading() || pressure) && !HasHumanOrder(b) && gpGlobals->time>=s.nextCover){s.nextCover=gpGlobals->time+5;if(b->TryToRetreat())Tactical(b,b->IsReloading()?"Reloading. Cover me.":"Need backup.","%!MRAD_BACKUP");}
 if(Ffa())return;
 if(!HasHumanOrder(b) && !b->IsEnemyVisible() && !b->IsAttacking() && gpGlobals->time>=s.nextPlant && TheCSBots()->IsBombPlanted()) {
  s.nextPlant=gpGlobals->time+4;const Vector *bomb=b->GetGameState()->GetBombPosition();
  if(b->m_iTeam==TERRORIST && bomb && (*bomb-b->pev->origin).LengthSquared()>700*700 && TheCSBots()->GetBombTimeLeft()>10) {
   if(Path(b,*bomb))Tactical(b,"Rotating.");return;
  }
 }
 int friends=Alive(b->m_iTeam),enemies=Alive(b->m_iTeam==CT?TERRORIST:CT);
 float remaining=b->m_iTeam==CT && TheCSBots()->IsBombPlanted()?TheCSBots()->GetBombTimeLeft():CSGameRules()->GetRoundRemainingTime();
 if(!HasHumanOrder(b) && !b->IsEnemyVisible() && CompetitiveExecution::ShouldSave(friends,enemies,b->m_iAccount,b->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]!=nullptr,b->IsCarryingBomb(),TheCSBots()->GetBombDefuser()!=nullptr,remaining)
    && ((b->m_iTeam==CT && TheCSBots()->IsBombPlanted()) || (b->m_iTeam==TERRORIST && !TheCSBots()->IsBombPlanted()))) {
  if(!s.saving && b->TryToHide(b->GetLastKnownArea(),20,750,true,true)){s.saving=true;if(!teams[b->m_iTeam].saveCall){teams[b->m_iTeam].saveCall=true;Speak(b,nullptr,true,"save your weapons",true);}}return;
 }
 if(s.avoidUntil>gpGlobals->time && gpGlobals->time>=s.nextSupport && !HasHumanOrder(b) && !b->IsAttacking() && !b->IsEnemyVisible()) {
  s.nextSupport=gpGlobals->time+3;
  for(auto *area:TheNavAreaList){Vector goal=*area->GetCenter();float distance=(goal-s.avoid).Length2D();if(distance<650 || distance>1100 || (goal-b->pev->origin).Length2D()>1000)continue;if(Path(b,goal)){s.avoidUntil=0;break;}}
 }
 auto &donation=teams[b->m_iTeam];
 if(donation.donorSlot==b->entindex() && gpGlobals->time>=donation.donationDue) {
  auto *recipient=Player(donation.recipientSlot);
  bool eligible=GETPLAYERUSERID(b->edict())==donation.donorId && recipient && GETPLAYERUSERID(recipient->edict())==donation.recipientId && recipient->IsAlive() && recipient->m_iTeam==b->m_iTeam
   && !recipient->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] && !HasHumanOrder(b) && !b->IsEnemyVisible() && !b->IsAttacking() && !Saving(b) && !TheCSBots()->IsBombPlanted() && b->CanPlayerBuy(false)
   && (recipient->pev->origin-b->pev->origin).LengthSquared()<200*200;
  if(!eligible)donation.donorSlot=0;
  else if(donation.donationPhase==0){Speak(b,recipient,true,"i can drop, stay close",true);donation.donationPhase=1;donation.donationDue=gpGlobals->time+3;}
  else {if(RequestedDrop(b,recipient,nullptr)){donation.donated=true;Speak(b,recipient,true,"gun on the ground",true);}donation.donorSlot=0;}
 }
 if(HasHumanOrder(b) || s.saving || b->IsBuying() || b->IsEnemyVisible() || b->IsAttacking())return;
 if(gpGlobals->time>=s.nextSupport) {
  s.nextSupport=gpGlobals->time+5;
  for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || p==b || p->m_iTeam!=b->m_iTeam || !CompetitiveExecution::CanSupport(b->IsVisible(p,true),p->IsAlive(),false,false,(p->pev->origin-b->pev->origin).Length2D()))continue;
   if(p->m_bHasC4 || (!p->IsBot() && (p->pev->button&IN_ATTACK)) || (p->IsBot() && static_cast<CCSBot *>(p)->IsAttacking())){b->Follow(p);Tactical(b,"Coming to help.","%!MRAD_FOLLOWME");break;}}
 }
 auto &team=teams[b->m_iTeam];
 if(!team.donated && !team.donorSlot && !Saving(b) && b->CanPlayerBuy(false) && TheCSBots()->GetElapsedRoundTime()<15 && b->m_iAccount>=6000 && b->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]) {
  for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || !p->IsBot() || !p->IsAlive() || p->m_iTeam!=b->m_iTeam || p->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] || p->m_iAccount>2000 || !b->IsVisible(p,true))continue;
   if((p->pev->origin-b->pev->origin).LengthSquared()>200*200)continue;
   team.donorSlot=b->entindex();team.donorId=GETPLAYERUSERID(b->edict());team.recipientSlot=p->entindex();team.recipientId=GETPLAYERUSERID(p->edict());team.donationPhase=0;team.donationDue=gpGlobals->time+3;Speak(static_cast<CCSBot *>(p),b,true,"can you drop a gun?",true);break;}
 }
}
void ConfirmedDamage(CBasePlayer *victim,CBaseEntity *attacker,float amount) {
 if(!Enabled() || Completed() || !victim || !attacker || !attacker->IsPlayer() || amount<=0)return;
 auto *p=static_cast<CBasePlayer *>(attacker);if(!p->IsBot() || p==victim || (!Ffa() && p->m_iTeam==victim->m_iTeam))return;
 auto *b=static_cast<CCSBot *>(p);if(!b->IsVisible(victim,true))return;
 auto &h=Get(b).hits[victim->entindex()];int id=GETPLAYERUSERID(victim->edict());if(h.id!=id){h=HitMemory {};h.id=id;}h.estimate.hit(int(amount),gpGlobals->time);
}
void TacticsEvent(GameEventType event,CBaseEntity *entity,CBaseEntity *other) {
 if(!Enabled() || !tactics.value || Completed() || !entity || !entity->IsPlayer())return;
 auto *victim=static_cast<CBasePlayer *>(entity);
 if(event==EVENT_PLAYER_DIED) {
  if(victim->IsBot())RememberHoldingAngle(static_cast<CCSBot *>(victim));
  auto *killer=other && other->IsPlayer()?static_cast<CBasePlayer *>(other):nullptr;
  if(!RoundTacticsAllowed())sprees[victim->entindex()].pending.Died(true);
  if(killer && killer!=victim && (Ffa() || killer->m_iTeam!=victim->m_iTeam)) {
   auto &spree=sprees[killer->entindex()];int id=GETPLAYERUSERID(killer->edict());
   if(spree.id!=id || spree.team!=int(killer->m_iTeam)){spree=SpreeState {};spree.id=id;spree.team=int(killer->m_iTeam);}
   spree.victims[victim->entindex()]=GETPLAYERUSERID(victim->edict());
   spree.pending.Kill(gpGlobals->time,!RoundTacticsAllowed());
   if(killer->IsBot()){auto *b=static_cast<CCSBot *>(killer);auto &s=Get(b);++s.kills;if(gpGlobals->time>=s.nextCover){s.nextCover=gpGlobals->time+5;if(!Busy(b) && !HasHumanOrder(b) && RANDOM_LONG(0,99)<25)b->TryToHide(b->GetLastKnownArea(),2,300,false,true);}auto *w=b->GetActiveWeapon();if(victim->m_bKilledByGrenade || (w && w->m_iId==WEAPON_KNIFE))Speak(b,nullptr,false,victim->m_bKilledByGrenade?"nade did the job":"knife work");}
   if(victim->IsBot() && !killer->IsBot() && gpGlobals->time>=nextBanter && !PingComplaint(static_cast<CCSBot *>(victim),true) && RANDOM_LONG(0,99)<30){nextBanter=gpGlobals->time+30;auto feeling=scoreFeeling(int(victim->pev->frags),victim->m_iDeaths);Speak(static_cast<CCSBot *>(victim),nullptr,false,victim->m_bKilledByGrenade?"that nade got me":(killer->m_pActiveItem && killer->m_pActiveItem->m_iId==WEAPON_KNIFE)?"got knifed, fair":feeling==ScoreFeeling::Struggling?"im getting cooked":feeling==ScoreFeeling::Confident?"still doing alright":"nice shot");}
  }
  if(!Ffa())for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || !p->IsBot() || !p->IsAlive() || p->m_iTeam!=victim->m_iTeam)continue;auto *b=static_cast<CCSBot *>(p);if((b->pev->origin-victim->pev->origin).LengthSquared()>800*800 || !b->IsVisible(victim,true))continue;auto &s=Get(b);s.avoid=victim->pev->origin;s.avoidUntil=gpGlobals->time+15;s.nextSupport=gpGlobals->time+1.5f;}
 }
}
}
