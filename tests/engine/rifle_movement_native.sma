// Rifle movement regression on an isolated de_dust2 server only.
// Observer moves freely; the target alone is held in place. No locked-in phase is enabled.
#include <amxmodx>
#include <engine>
#include <fakemeta>
#include <reapi>
#include <xs>
#include <hamsandwich>
#define FIXTURE_WEAPON "weapon_m4a1"
#define FIXTURE_WEAPON_ID WEAPON_M4A1
#define FIXTURE_RANGE 350.0
new previousClip[2048];
new observer,target,phase,attempts,shots,lateShots,hits;
new Float:start,Float:origin[3],Float:enemy[3];
new volleys, movedVolleys, shortBursts, movingShots, volleyShots;
new Float:lastShot,Float:lastVolley[3];
#if defined RIFLE_BARRIER
new barriers[2];
new Float:blockedSince,Float:longestBlocked;
public add_barriers() {
 new Float:center[3],Float:mins[3]={-128.0,-4.0,-36.0},Float:maxs[3]={128.0,4.0,96.0};
 pev(observer,pev_origin,center);
 for(new i=0;i<2;i++) {
  barriers[i]=create_entity("info_target");
  set_pev(barriers[i],pev_classname,"rifle_test_barrier");
  set_pev(barriers[i],pev_movetype,MOVETYPE_NONE);
  set_pev(barriers[i],pev_solid,SOLID_BBOX);
  set_pev(barriers[i],pev_rendermode,kRenderTransTexture);
  set_pev(barriers[i],pev_renderamt,0.0);
  new Float:where[3];xs_vec_copy(center,where);where[1]+=i?100.0:-100.0;
  engfunc(EngFunc_SetSize,barriers[i],mins,maxs);engfunc(EngFunc_SetOrigin,barriers[i],where);
 }
 server_print("RIFLE_MOVEMENT transparent solid barriers installed");
}
#endif
public plugin_init() {
 register_plugin("Rifle movement regression","1","16C");
 RegisterHookChain(RG_CBasePlayer_TakeDamage,"on_damage",true);
 set_task(1.0,"start_bots");set_task(7.0,"place_bots");
 RegisterHam(Ham_Weapon_PrimaryAttack,FIXTURE_WEAPON,"before_fire",0);RegisterHam(Ham_Weapon_PrimaryAttack,FIXTURE_WEAPON,"after_fire",1);set_task(28.0,"timeout_test");
}
public start_bots() {server_cmd("developer 1;bot_join_after_player 0;bot_quota 2;bot_stop 0;bot_difficulty 0;bot_competitive 1;bot_smart_grenades 1;bot_grenade_debug 0;bot_cover_peek 1;bot_combat_movement 1;bot_knives_only;bot_allow_rifles 1;bot_allow_sub_machine_guns 1;bot_allow_shotguns 1;bot_allow_grenades 0;mp_freezetime 0");server_exec();}
public place_bots() {
 new bots[32],count;get_players(bots,count,"d");
 if(count<2) {if(++attempts<15)set_task(0.5,"place_bots");return;}
 observer=bots[0];target=bots[1];rg_set_user_team(observer,TEAM_CT);rg_set_user_team(target,TEAM_TERRORIST);
 rg_round_respawn(observer);rg_round_respawn(target);
 new Float:angles[3],Float:end[3],Float:eyes[3],bool:found;
 for(new i=1;i<2000 && !found;i++) {
  // Wide open CT spawn courtyard on Dust2. Probe positions rather than assume a floor.
  origin[0]=float(-2000+(i%40)*100);origin[1]=float(-2000+(i/40)*100);origin[2]=2000.0;
  xs_vec_copy(origin,end);end[2]=-1000.0;new tr=create_tr2();engfunc(EngFunc_TraceLine,origin,end,IGNORE_MONSTERS,observer,tr);
  new Float:f;get_tr2(tr,TR_flFraction,f);get_tr2(tr,TR_vecEndPos,origin);free_tr2(tr);if(f>=1.0)continue;origin[2]+=36.0;
  xs_vec_copy(origin,enemy);enemy[0]+=FIXTURE_RANGE;
  xs_vec_copy(enemy,end);end[2]-=300.0;tr=create_tr2();engfunc(EngFunc_TraceLine,enemy,end,IGNORE_MONSTERS,target,tr);get_tr2(tr,TR_flFraction,f);get_tr2(tr,TR_vecEndPos,enemy);free_tr2(tr);if(f>=1.0)continue;enemy[2]+=36.0;
  xs_vec_copy(origin,eyes);eyes[2]+=28.0;xs_vec_copy(enemy,end);end[2]+=28.0;
  tr=create_tr2();engfunc(EngFunc_TraceLine,eyes,end,IGNORE_MONSTERS,observer,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);
  if(f>=1.0 && floatabs(origin[2]-enemy[2])<24.0) {
   xs_vec_copy(origin,end);end[2]+=250.0;tr=create_tr2();engfunc(EngFunc_TraceLine,origin,end,IGNORE_MONSTERS,observer,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);if(f<1.0)continue;
   xs_vec_copy(enemy,end);end[2]+=250.0;tr=create_tr2();engfunc(EngFunc_TraceLine,enemy,end,IGNORE_MONSTERS,target,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);if(f>=1.0) {
    new bool:dry=true;
    for(new k=0;k<=10;k++) {
     xs_vec_copy(origin,end);end[0]+=(FIXTURE_RANGE/10.0)*float(k);end[2]+=20.0;
     if(engfunc(EngFunc_PointContents,end)!=CONTENTS_EMPTY){dry=false;break;}
    }
    if(dry) {
     for(new side=-1;side<=1;side+=2) {
      xs_vec_copy(origin,end);end[1]+=float(side)*96.0;tr=create_tr2();engfunc(EngFunc_TraceHull,origin,end,IGNORE_MONSTERS,HULL_HUMAN,observer,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);
      if(f<1.0){dry=false;break;}
      xs_vec_copy(end,eyes);eyes[2]-=72.0;tr=create_tr2();engfunc(EngFunc_TraceLine,end,eyes,IGNORE_MONSTERS,observer,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);if(f>=1.0){dry=false;break;}
     }
     if(dry)found=true;
    }
   }
  }
 }
 if(!found){server_print("RIFLE_MOVEMENT FAIL no flat visible fixture");server_cmd("quit");return;}
 engfunc(EngFunc_SetOrigin,observer,origin);engfunc(EngFunc_SetOrigin,target,enemy);
 set_pev(observer,pev_v_angle,angles);set_pev(observer,pev_angles,angles);set_pev(observer,pev_fixangle,1);
 angles[1]=180.0;set_pev(target,pev_v_angle,angles);set_pev(target,pev_angles,angles);set_pev(target,pev_fixangle,1);
 server_cmd("bot_competitive;bot_smart_grenades;bot_grenade_debug;bot_stop;bot_zombie;bot_quota;status");server_exec();
 rg_remove_all_items(observer);rg_give_item(observer,"weapon_knife");rg_give_item(observer,FIXTURE_WEAPON);rg_set_user_bpammo(observer,FIXTURE_WEAPON_ID,200);engclient_cmd(observer,FIXTURE_WEAPON);set_pev(target,pev_health,10000.0);
 rg_remove_all_items(target);rg_give_item(target,"weapon_knife");
 set_pev(target,pev_effects,0);start=get_gametime();phase=1;
 server_print("RIFLE_MOVEMENT placed origin %.0f %.0f %.0f target %.0f %.0f %.0f",origin[0],origin[1],origin[2],enemy[0],enemy[1],enemy[2]);
 set_task(0.05,"hold_fixture",0,_,_,"b");
}
public hold_fixture() {
 if(!phase)return;
#if defined RIFLE_BARRIER
 new Float:velocity[3];pev(observer,pev_velocity,velocity);velocity[2]=0.0;
 if((pev(observer,pev_button)&(IN_FORWARD|IN_BACK|IN_MOVELEFT|IN_MOVERIGHT)) && vector_length(velocity)<5.0) {
  if(blockedSince==0.0)blockedSince=get_gametime();
  longestBlocked=floatmax(longestBlocked,get_gametime()-blockedSince);
 } else blockedSince=0.0;
#endif
 engfunc(EngFunc_SetOrigin,target,enemy);
 new Float:to[3],Float:look[3],Float:where[3];pev(observer,pev_origin,where);xs_vec_sub(where,enemy,to);engfunc(EngFunc_VecToAngles,to,look);look[0]=-look[0];set_pev(target,pev_v_angle,look);set_pev(target,pev_angles,look);set_pev(target,pev_flags,pev(target,pev_flags)|FL_FROZEN);
 new Float:zero[3];set_pev(target,pev_velocity,zero);set_pev(target,pev_health,10000.0);set_pev(observer,pev_health,10000.0);set_member(target,m_flNextAttack,10000.0);
 static Float:nextDebug;
 if(get_gametime()>nextDebug) {
  nextDebug=get_gametime()+1.0;
  new weapon=get_member(observer,m_pActiveItem);
  new Float:o[3];pev(observer,pev_origin,o);
  server_print("RIFLE_MOVEMENT state alive=%d weapon=%d clip=%d dist=%.0f buttons=%d shots=%d",is_user_alive(observer),weapon>0?get_member(weapon,m_iId):0,weapon>0?get_member(weapon,m_Weapon_iClip):0,get_distance_f(o,enemy),pev(observer,pev_button),shots);
 }
}
public before_fire(entity) {if(entity<2048)previousClip[entity]=get_member(entity,m_Weapon_iClip);}
public after_fire(entity) {
 new id=pev(entity,pev_owner);
 if(id!=observer || !phase || entity>=2048 || get_member(entity,m_Weapon_iClip)>=previousClip[entity])return;
 new Float:now=get_gametime(),Float:where[3],Float:velocity[3];
 pev(observer,pev_origin,where);pev(observer,pev_velocity,velocity);
 if(now-lastShot>0.4) {
  if(volleyShots>=2 && volleyShots<=3)shortBursts++;
  if(volleys && get_distance_f(where,lastVolley)>24.0)movedVolleys++;
  xs_vec_copy(where,lastVolley);volleys++;volleyShots=0;
 }
 if(vector_length(velocity)>30.0){movingShots++;server_print("RIFLE_MOVEMENT moving shot t=%.2f speed=%.1f",now-start,vector_length(velocity));}
 volleyShots++;lastShot=now;
 shots++;
#if defined RIFLE_BARRIER
 if(shots==1)add_barriers();
#endif
 if(get_gametime()-start>=10.0)lateShots++;
}
public on_damage(victim,inflictor,attacker,Float:damage,bits) {
 if(phase && victim==target && attacker==observer && damage>0.0)hits++;
}
public timeout_test(){
 server_print("RIFLE_MOVEMENT shots=%d late_shots=%d hits=%d",shots,lateShots,hits);
 server_print("RIFLE_MOVEMENT volleys=%d moved=%d bursts=%d moving_shots=%d",volleys,movedVolleys,shortBursts,movingShots);
 new bool:obstaclesOk=true;
 new bool:fireOk=movingShots==0;
#if defined RIFLE_BARRIER
 fireOk=true; // Stock combat may resume after a blocked managed cycle.
 obstaclesOk=barriers[0]>0 && barriers[1]>0 && longestBlocked<0.5;
 server_print("RIFLE_MOVEMENT longest blocked input=%.2f",longestBlocked);
#endif
 if(obstaclesOk && shots>=5 && lateShots>=2 && hits>=2 && movedVolleys>=3 && shortBursts>=1 && fireOk) {
#if defined RIFLE_BARRIER
  server_print("RIFLE_MOVEMENT PASS obstacle recovery and continued combat");
#else
  server_print("RIFLE_MOVEMENT PASS movement between accurate volleys");
#endif
 }
 else server_print("RIFLE_MOVEMENT FAIL shots, movement, bursts or stationary-fire requirement");
 server_cmd("quit");
}
