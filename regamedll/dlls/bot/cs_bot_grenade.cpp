// SPDX-License-Identifier: MIT
#include "precompiled.h"
#include "cs_bot_competitive.h"
#include "competitive_utility.h"
#include "competitive_holding_utility.h"
#include <algorithm>

namespace CompetitiveBots {
namespace {
using namespace CompetitiveUtility;
cvar_t debug = { "bot_grenade_debug", "0", 0, 0.0f, nullptr };
cvar_t smart = { "bot_smart_grenades", "1", FCVAR_SERVER, 0.0f, nullptr };
struct Memory {
 int userId=0, targetSlot=0, targetId=0;
 float seen=-100, nextConsider=0;
 Vector position {0,0,0};
 int stage=0, weapon=0, ammo=0, throws=0;bool learned=false;
 float started=0, expires=0, pinTime=0;
 Vector target {0,0,0}, aim {0,0,0}, landing {0,0,0};
};
Memory memory[MAX_CLIENTS+1];
struct Learned {int id=0,team=0;unsigned round=0;bool valid=false,used=false;Vector point {0,0,0};};
Learned learned[MAX_CLIENTS+1];unsigned utilityRound=0;
float nextPlan=0, teamNext[4]={};
Vec V(const Vector &v) {return {v.x,v.y,v.z};}
Vector E(const Vec &v) {return {v.x,v.y,v.z};}
Kind Type(int weapon) {return weapon==WEAPON_FLASHBANG?Kind::Flash:weapon==WEAPON_SMOKEGRENADE?Kind::Smoke:Kind::HE;}
CBasePlayer *Opponent(int slot) {
 auto *p=slot>=1 && slot<=gpGlobals->maxClients?UTIL_PlayerByIndex(slot):nullptr;
 return p && p->IsPlayer() && !p->IsDormant()?static_cast<CBasePlayer *>(p):nullptr;
}
CBasePlayerWeapon *Carried(CCSBot *bot,int id) {
 auto *item=bot->GetItemById(static_cast<WeaponIdType>(id));
 auto *weapon=item?static_cast<CBasePlayerWeapon *>(item->GetWeaponPtr()):nullptr;
 return weapon && weapon->m_iPrimaryAmmoType>=0 && weapon->m_iPrimaryAmmoType<MAX_AMMO_SLOTS
  && bot->m_rgAmmo[weapon->m_iPrimaryAmmoType]>0?weapon:nullptr;
}
bool Safe(CCSBot *bot,const Vector &point,int weapon) {
 if(!SafeDistance(V(point),V(bot->pev->origin),Type(weapon))) return false;
 if(CSGameRules()->IsFreeForAll()) return true;
 for(int i=1;i<=gpGlobals->maxClients;++i) {
  auto *p=Opponent(i);
  if(p && p!=bot && p->IsAlive() && p->m_iTeam==bot->m_iTeam) {
   // Account for teammates already moving toward the landing zone.
   Vector near=p->pev->origin+p->pev->velocity*.4f;
   if(!SafeDistance(V(point),V(p->pev->origin),Type(weapon))
      || !SafeDistance(V(point),V(near),Type(weapon))) return false;
  }
 }
 return true;
}
void Cancel(CCSBot *bot,Memory &m) {
 if(m.stage) {
  bot->ClearPrimaryAttack();bot->StopRapidFire();bot->ClearLookAt();
  // Normal weapon holstering cancels an unthrown pin pull; never mutate grenade internals.
  if(bot->IsUsingGrenade()) bot->EquipBestWeapon(MUST_EQUIP);
 }
 m.stage=0;m.nextConsider=gpGlobals->time+1;
}
Context Conditions(CCSBot *bot,const Memory &m) {
 Context c;
 c.enabled=Enabled() && smart.value>0 && TheCSBots()->AllowGrenades();
 c.alive=bot->IsAlive();c.completed=Completed();
 c.freeze=CSGameRules()->IsFreezePeriod();c.roundOver=CSGameRules()->m_iRoundWinStatus!=WINSTATUS_NONE;
 c.busy=(bot->IsBuying() && bot->CanPlayerBuy(false)) || bot->IsDefusingBomb() || bot->IsEscapingFromBomb() || bot->m_bIsVIP
  || bot->GetTask()==CCSBot::PLANT_BOMB || bot->GetTask()==CCSBot::RESCUE_HOSTAGES;
 c.blind=bot->IsBlind();c.reloading=bot->IsReloading();c.underPressure=bot->GetTimeSinceAttacked()<1.5f;
 // Do not pull utility in the middle of a visible gunfight.
 c.visibleThreat=bot->IsEnemyVisible() || bot->GetRecognizedEnemy()!=nullptr;
 c.now=gpGlobals->time;c.seen=m.seen;
 c.range=(m.position-bot->pev->origin).Length2D();c.height=m.position.z-bot->pev->origin.z;
 return c;
}
bool Preview(CCSBot *bot,const Vector &target,int weapon,float pitch,float yaw,Vector &landing) {
 float gravity=CVAR_GET_FLOAT("sv_gravity")*(weapon==WEAPON_HEGRENADE?.55f:.5f);
 auto launch=NativeLaunch(V(bot->GetEyePosition()),V(bot->pev->velocity),pitch,yaw);
 auto trace=[&](const Vec &a,const Vec &b) {
  int content=UTIL_PointContents(E(b));
  if(content==CONTENTS_WATER || content==CONTENTS_SLIME || content==CONTENTS_LAVA) return Hit {0,true,{0,0,1}};
  TraceResult tr;UTIL_TraceLine(E(a),E(b),dont_ignore_monsters,bot->edict(),&tr);
  return Hit {tr.flFraction,bool(tr.fStartSolid || tr.fAllSolid),V(tr.vecPlaneNormal)};
 };
 Vec end;
 if(!Predict(launch,gravity,weapon==WEAPON_HEGRENADE?.7f:.8f,
             weapon==WEAPON_SMOKEGRENADE?2.0f:1.5f,trace,end)) return false;
 landing=E(end);
 const float tolerance=weapon==WEAPON_HEGRENADE?130.0f:180.0f;
 return (landing-target).LengthSquared()<=tolerance*tolerance && Safe(bot,landing,weapon);
}
bool FindThrow(CCSBot *bot,Memory &m) {
 Vector eye=bot->GetEyePosition(),direction=m.target-eye;
 float yaw=UTIL_VecToAngles(direction).y;
 TraceResult ground;
 UTIL_TraceLine(m.target,m.target-Vector(0,0,160),ignore_monsters,bot->edict(),&ground);
 if(ground.fStartSolid || ground.fAllSolid || ground.flFraction>=1 || ground.vecPlaneNormal.z<.7f) { if(debug.value>1) ALERT(at_console,"[Bot utility] invalid floor frac=%.2f solid=%d z=%.0f normal=%.1f\n",ground.flFraction,ground.fStartSolid,ground.vecEndPos.z,ground.vecPlaneNormal.z); return false; }
 float floor=ground.vecEndPos.z;
 struct Candidate {float error,pitch;};
 Candidate candidates[18];int count=0;
 auto floorTrace=[&](const Vec &a,const Vec &b) {
  if(a.z<floor-.1f) return Hit {0,true,{0,0,1}};
  if(b.z<floor && a.z>=floor) return Hit {(a.z-floor)/(a.z-b.z),false,{0,0,1}};
  return Hit {};
 };
 float gravity=CVAR_GET_FLOAT("sv_gravity")*(m.weapon==WEAPON_HEGRENADE?.55f:.5f);
 for(float pitch=-65;pitch<=20;pitch+=5) {
  Vec end;
  if(!Predict(NativeLaunch(V(eye),V(bot->pev->velocity),pitch,yaw),gravity,
              m.weapon==WEAPON_HEGRENADE?.7f:.8f,m.weapon==WEAPON_SMOKEGRENADE?2.0f:1.5f,floorTrace,end)) continue;
  candidates[count++]={(end-V(m.target)).LengthSq(),pitch};
 }
 std::sort(candidates,candidates+count,[](const Candidate&a,const Candidate&b){return a.error<b.error;});
 // Check candidates in error order; stop at the first safe collision-tested arc.
 for(int i=0;i<count;++i) {
  if(!Preview(bot,m.target,m.weapon,candidates[i].pitch,yaw,m.landing)) {if(debug.value>1) ALERT(at_console,"[Bot utility] rejected pitch=%.0f predicted_error=%.0f\n",candidates[i].pitch,std::sqrt(candidates[i].error));continue;}
  m.aim=eye+E(Direction(candidates[i].pitch,yaw))*1000;
  return true;
 }
 return false;
}
}
void RegisterGrenadeCVars() {CVAR_REGISTER(&smart);CVAR_REGISTER(&debug);}
bool SmartGrenades() {return Enabled() && smart.value>0;}
bool IsManagingGrenade(CCSBot *bot) {return bot && memory[bot->entindex()].stage!=0;}
void ResetGrenades(bool map) {
 if(map){utilityRound=0;for(auto &l:learned)l=Learned {}; }else ++utilityRound;
 for(auto &m:memory) m=Memory {};
 nextPlan=0;for(auto &t:teamNext)t=0;
}
void CancelGrenade(CCSBot *bot) {Cancel(bot,memory[bot->entindex()]);}

void RememberHoldingAngle(CCSBot *bot) {
 auto &m=memory[bot->entindex()];
 if(m.seen>gpGlobals->time || gpGlobals->time-m.seen>2 || !m.targetId || RANDOM_LONG(0,99)>=50)return;
 auto &l=learned[bot->entindex()];l.id=GETPLAYERUSERID(bot->edict());l.team=bot->m_iTeam;l.round=utilityRound;l.point=m.position;l.valid=true;l.used=false;
}
bool UpdateGrenade(CCSBot *bot) {
 auto &m=memory[bot->entindex()];
 int id=GETPLAYERUSERID(bot->edict());
 if(m.userId!=id || m.seen>gpGlobals->time) {m=Memory {};m.userId=id;}
 if(!SmartGrenades()) {if(m.stage) Cancel(bot,m);return false;}
 if(!m.stage && bot->IsUsingGrenade()) bot->EquipBestWeapon(MUST_EQUIP);
 static float nextDebug[MAX_CLIENTS+1]={};
 if(debug.value>1 && gpGlobals->time>=nextDebug[bot->entindex()]) {
  nextDebug[bot->entindex()]=gpGlobals->time+1;
  ALERT(at_console,"[Bot utility] state bot=%d enemy=%d visible=%d alive=%d buy=%d freeze=%d stage=%d\n",
   bot->entindex(),bot->GetEnemy()?bot->GetEnemy()->entindex():0,bot->IsEnemyVisible(),bot->IsAlive(),bot->IsBuying(),CSGameRules()->IsFreezePeriod(),m.stage);
 }
 auto *enemy=bot->GetEnemy();
 if(enemy && bot->IsEnemyVisible() && TargetAllowed(enemy) && bot->IsVisible(enemy,true)) {
  // Copy sighting once; never read live hidden movement for utility targeting.
  if (debug.value>0 && (m.targetId!=GETPLAYERUSERID(enemy->edict()) || gpGlobals->time-m.seen>.5f))
   ALERT(at_console,"[Bot utility] saw bot=%d target=%d\n",bot->entindex(),enemy->entindex());
  m.position=enemy->pev->origin;m.seen=gpGlobals->time;m.learned=false;
  m.targetSlot=enemy->entindex();m.targetId=GETPLAYERUSERID(enemy->edict());
 }
 auto &past=learned[bot->entindex()];
 if(!m.stage && !bot->IsEnemyVisible() && past.valid && !past.used && past.id==id && past.team==bot->m_iTeam && learnedHoldingSpotAvailable(int(past.round),int(utilityRound),past.team,bot->m_iTeam,TheCSBots()->GetElapsedRoundTime(),past.used) && (past.point-bot->pev->origin).Length2D()>=400) {
  m.position=past.point;m.seen=gpGlobals->time-.5f;m.learned=true;m.targetSlot=0;m.targetId=0;
 }
 auto *remembered=Opponent(m.targetSlot);
 if(!m.learned && (!remembered || GETPLAYERUSERID(remembered->edict())!=m.targetId || !remembered->IsAlive()
    || (!CSGameRules()->IsFreeForAll() && remembered->m_iTeam==bot->m_iTeam))) {
  if(m.stage) Cancel(bot,m);
  m.seen=-100;return false;
 }
 Context c=Conditions(bot,m);
 if(debug.value>1 && !c.visibleThreat && m.targetId && gpGlobals->time>=m.nextConsider) {
  ALERT(at_console,"[Bot utility] gate bot=%d enabled=%d busy=%d pressure=%d blind=%d reload=%d end=%d age=%.2f range=%.0f height=%.0f\n",
   bot->entindex(),c.enabled,c.busy,c.underPressure,c.blind,c.reloading,c.roundOver,c.now-c.seen,c.range,c.height);
 }
 if(!CanConsider(c)) {if(m.stage) Cancel(bot,m);return false;}
 if(!m.stage) {
  if(m.throws>=3)return false;
  if(debug.value>1 && gpGlobals->time>=m.nextConsider)
   ALERT(at_console,"[Bot utility] resources bot=%d ground=%d ladder=%d water=%d he=%d flash=%d smoke=%d\n",bot->entindex(),!!(bot->pev->flags&FL_ONGROUND),bot->IsOnLadder(),bot->pev->waterlevel,Carried(bot,WEAPON_HEGRENADE)!=nullptr,Carried(bot,WEAPON_FLASHBANG)!=nullptr,Carried(bot,WEAPON_SMOKEGRENADE)!=nullptr);
  if(gpGlobals->time<m.nextConsider || gpGlobals->time<nextPlan || gpGlobals->time<teamNext[bot->m_iTeam]
     || bot->IsOnLadder() || bot->pev->waterlevel>1 || !(bot->pev->flags&FL_ONGROUND)) return false;
  m.nextConsider=gpGlobals->time+1;
  int choices[]={WEAPON_HEGRENADE,WEAPON_FLASHBANG,WEAPON_SMOKEGRENADE};
  m.weapon=0;
  for(int choice:choices) if(Carried(bot,choice)) {m.weapon=choice;break;}
  if(Carried(bot,WEAPON_SMOKEGRENADE) && !CSGameRules()->IsFreeForAll())for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Opponent(i);if(!p || p==bot || p->m_iTeam!=bot->m_iTeam || !p->IsAlive() || !bot->IsVisible(p,true))continue;auto *item=p->m_pActiveItem?static_cast<CBasePlayerWeapon *>(p->m_pActiveItem->GetWeaponPtr()):nullptr;if(item && item->m_iId==WEAPON_C4 && static_cast<CC4 *>(item)->m_bStartedArming){m.weapon=WEAPON_SMOKEGRENADE;break;}}
  m.weapon=ChooseCarried(m.weapon,choices,[&](int choice){return Carried(bot,choice) && Safe(bot,m.position,choice);});
  if(!m.weapon)return false;
  m.target=m.position;m.landing=m.target;m.stage=1;m.started=gpGlobals->time;m.expires=gpGlobals->time+3.5f;
  nextPlan=gpGlobals->time+.25f;
  bot->StopAttacking();bot->StopAiming();bot->StopRapidFire();bot->ClearPrimaryAttack();
 }
 bot->ClearMovement();bot->ResetStuckMonitor();bot->StopAiming();
 if(gpGlobals->time>m.expires || !Safe(bot,m.landing.LengthSquared()>0?m.landing:m.target,m.weapon)) {
  Cancel(bot,m);return false;
 }
 if(m.stage==1) {
  // Stand still before solving: movement velocity contributes to actual native throws.
  if(bot->pev->velocity.Length2D()>35 && gpGlobals->time<m.started+.5f) return true;
  const int choices[]={WEAPON_HEGRENADE,WEAPON_FLASHBANG,WEAPON_SMOKEGRENADE};
  const int preferred=m.weapon;
  const int selected=bot->pev->velocity.Length2D()>35?0:ChooseCarried(preferred,choices,[&](int choice){
   if(!Carried(bot,choice) || !Safe(bot,m.target,choice))return false;
   m.weapon=choice;
   return FindThrow(bot,m);
  });
  if(!selected) {
   if(debug.value>0) ALERT(at_console,"[Bot utility] no safe trajectory bot=%d\n",bot->entindex());
   Cancel(bot,m);return false;
  }
  if(debug.value>0) ALERT(at_console,"[Bot utility] plan bot=%d weapon=%d age=%.2f target=(%.0f %.0f %.0f) landing=(%.0f %.0f %.0f)\n",
   bot->entindex(),m.weapon,gpGlobals->time-m.seen,m.target.x,m.target.y,m.target.z,m.landing.x,m.landing.y,m.landing.z);
  m.weapon=selected;
  bot->SelectItem(STRING(Carried(bot,m.weapon)->pev->classname));
  m.stage=2;
 }
 auto *weapon=bot->GetActiveWeapon();
 if(!weapon || weapon->m_iId!=m.weapon) {Cancel(bot,m);return false;}
 bot->SetLookAt("LastSeenGrenade",&m.aim,PRIORITY_UNINTERRUPTABLE,.4f,false,1.5f);
 if(m.stage==2) {
  bot->ClearPrimaryAttack();
  if(!bot->IsLookingAtPosition(&m.aim,1.5f) || bot->m_flNextAttack>UTIL_WeaponTimeBase()) return true;
  Vector landing;
  if(!Preview(bot,m.target,m.weapon,bot->pev->v_angle.x+bot->pev->punchangle.x,
              bot->pev->v_angle.y+bot->pev->punchangle.y,landing)) {Cancel(bot,m);return false;}
  m.ammo=bot->m_rgAmmo[weapon->m_iPrimaryAmmoType];m.stage=3;m.pinTime=gpGlobals->time;
 }
 if(m.stage==3) {
  bot->PrimaryAttack();
  if(!weapon->m_flStartThrow) return true;
  if(gpGlobals->time-weapon->m_flStartThrow<.6f) return true;
  Vector landing;
  if(!bot->IsLookingAtPosition(&m.aim,1.5f) || !Preview(bot,m.target,m.weapon,
     bot->pev->v_angle.x+bot->pev->punchangle.x,bot->pev->v_angle.y+bot->pev->punchangle.y,landing)) {
   Cancel(bot,m);return false;
  }
  bot->ClearPrimaryAttack();m.stage=4;m.pinTime=gpGlobals->time;
  if(debug.value>0) ALERT(at_console,"[Bot utility] release bot=%d weapon=%d\n",bot->entindex(),m.weapon);
  teamNext[bot->m_iTeam]=gpGlobals->time+3;++m.throws;if(m.learned)past.used=true;
 }
 if(m.stage==4) {
  bot->ClearPrimaryAttack();
  if(bot->m_rgAmmo[weapon->m_iPrimaryAmmoType]<m.ammo) {
   bool flash=m.weapon==WEAPON_FLASHBANG;
   Vector away=bot->GetEyePosition()+(bot->pev->origin-m.target).Normalize()*1000;
   Cancel(bot,m);m.nextConsider=gpGlobals->time+6;m.seen=-100;
   if(flash) bot->SetLookAt("AvoidOwnFlash",&away,PRIORITY_HIGH,1.5f,false,5);
   return true;
  }
  if(gpGlobals->time>m.pinTime+.8f) {Cancel(bot,m);return false;}
 }
 return true;
}
}
