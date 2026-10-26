// Sustained-fire regression on an isolated de_dust2 server only.
// Keeps an armed bot and a visible enemy 350 units apart without resetting encounters.
// A single opening shot is insufficient: require hits and shots after ten seconds.
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
public plugin_init() {
 register_plugin("Sustained fire regression","1","16C");
 RegisterHookChain(RG_CBasePlayer_TakeDamage,"on_damage",true);
 set_task(1.0,"start_bots");set_task(7.0,"place_bots");
 RegisterHam(Ham_Weapon_PrimaryAttack,FIXTURE_WEAPON,"before_fire",0);RegisterHam(Ham_Weapon_PrimaryAttack,FIXTURE_WEAPON,"after_fire",1);set_task(28.0,"timeout_test");
}
public start_bots() {server_cmd("developer 1;bot_quota 2;bot_stop 0;bot_difficulty 0;bot_competitive 1;bot_smart_grenades 1;bot_grenade_debug 2;bot_knives_only;bot_allow_rifles 1;bot_allow_sub_machine_guns 1;bot_allow_shotguns 1;bot_allow_grenades 0;mp_freezetime 0");server_exec();}
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
 if(!found){server_print("SUSTAINED_FIRE FAIL no flat visible fixture");server_cmd("quit");return;}
 engfunc(EngFunc_SetOrigin,observer,origin);engfunc(EngFunc_SetOrigin,target,enemy);
 set_pev(observer,pev_v_angle,angles);set_pev(observer,pev_angles,angles);set_pev(observer,pev_fixangle,1);
 angles[1]=180.0;set_pev(target,pev_v_angle,angles);set_pev(target,pev_angles,angles);set_pev(target,pev_fixangle,1);
 server_cmd("bot_competitive;bot_smart_grenades;bot_grenade_debug;bot_stop;bot_zombie;bot_quota;status");server_exec();
 rg_remove_all_items(observer);rg_give_item(observer,"weapon_knife");rg_give_item(observer,FIXTURE_WEAPON);rg_set_user_bpammo(observer,FIXTURE_WEAPON_ID,200);engclient_cmd(observer,FIXTURE_WEAPON);set_pev(target,pev_health,10000.0);
 rg_remove_all_items(target);rg_give_item(target,"weapon_knife");
 set_pev(target,pev_effects,0);start=get_gametime();phase=1;
 server_print("SUSTAINED_FIRE placed origin %.0f %.0f %.0f target %.0f %.0f %.0f",origin[0],origin[1],origin[2],enemy[0],enemy[1],enemy[2]);
 set_task(0.05,"hold_fixture",0,_,_,"b");
}
public hold_fixture() {
 if(!phase)return;
 engfunc(EngFunc_SetOrigin,target,enemy);
 engfunc(EngFunc_SetOrigin,observer,origin);
 new Float:to[3],Float:look[3],Float:where[3];pev(observer,pev_origin,where);xs_vec_sub(where,enemy,to);engfunc(EngFunc_VecToAngles,to,look);look[0]=-look[0];set_pev(target,pev_v_angle,look);set_pev(target,pev_angles,look);set_pev(target,pev_flags,pev(target,pev_flags)|FL_FROZEN);
 new Float:zero[3];set_pev(target,pev_velocity,zero);set_pev(target,pev_health,10000.0);set_pev(observer,pev_health,10000.0);set_member(target,m_flNextAttack,10000.0);
 static Float:nextDebug;
 if(get_gametime()>nextDebug) {
  nextDebug=get_gametime()+1.0;
  new weapon=get_member(observer,m_pActiveItem);
  new Float:o[3];pev(observer,pev_origin,o);
  server_print("SUSTAINED_FIRE state alive=%d weapon=%d clip=%d dist=%.0f buttons=%d shots=%d",is_user_alive(observer),weapon>0?get_member(weapon,m_iId):0,weapon>0?get_member(weapon,m_Weapon_iClip):0,get_distance_f(o,enemy),pev(observer,pev_button),shots);
 }
}
public before_fire(entity) {if(entity<2048)previousClip[entity]=get_member(entity,m_Weapon_iClip);}
public after_fire(entity) {
 new id=pev(entity,pev_owner);
 if(id!=observer || !phase || entity>=2048 || get_member(entity,m_Weapon_iClip)>=previousClip[entity])return;
 shots++;
 if(get_gametime()-start>=10.0)lateShots++;
}
public on_damage(victim,inflictor,attacker,Float:damage,bits) {
 if(phase && victim==target && attacker==observer && damage>0.0)hits++;
}
public timeout_test(){
 server_print("SUSTAINED_FIRE shots=%d late_shots=%d hits=%d",shots,lateShots,hits);
 if(shots>=5 && lateShots>=2 && hits>=2)server_print("SUSTAINED_FIRE PASS repeated ranged fire and hits");
 else server_print("SUSTAINED_FIRE FAIL no sustained ranged combat");
 server_cmd("quit");
}
