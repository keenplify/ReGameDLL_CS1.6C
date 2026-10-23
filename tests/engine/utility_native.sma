// Isolated-server fixture only. Never load on a player server.
#include <amxmodx>
#include <engine>
#include <fakemeta>
#include <reapi>
#include <xs>
new observer,target,wall,phase,attempts;
new Float:start,Float:origin[3],Float:enemy[3],Float:remembered[3];
public plugin_init() {
 register_plugin("Native utility fixture","1","16C");
 set_task(1.0,"start_bots");set_task(3.0,"place_bots");
 register_forward(FM_Think,"grenade_think");set_task(23.0,"timeout_test");
}
public start_bots() {server_cmd("developer 1;bot_quota 2;bot_stop 0;bot_difficulty 0;bot_competitive 1;bot_smart_grenades 1;bot_grenade_debug 2;bot_knives_only;bot_allow_grenades 1;mp_freezetime 0");server_exec();}
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
  xs_vec_copy(origin,enemy);enemy[0]+=650.0;
  xs_vec_copy(enemy,end);end[2]-=300.0;tr=create_tr2();engfunc(EngFunc_TraceLine,enemy,end,IGNORE_MONSTERS,target,tr);get_tr2(tr,TR_flFraction,f);get_tr2(tr,TR_vecEndPos,enemy);free_tr2(tr);if(f>=1.0)continue;enemy[2]+=36.0;
  xs_vec_copy(origin,eyes);eyes[2]+=28.0;xs_vec_copy(enemy,end);end[2]+=28.0;
  tr=create_tr2();engfunc(EngFunc_TraceLine,eyes,end,IGNORE_MONSTERS,observer,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);
  if(f>=1.0 && floatabs(origin[2]-enemy[2])<24.0) {
   xs_vec_copy(origin,end);end[2]+=250.0;tr=create_tr2();engfunc(EngFunc_TraceLine,origin,end,IGNORE_MONSTERS,observer,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);if(f<1.0)continue;
   xs_vec_copy(enemy,end);end[2]+=250.0;tr=create_tr2();engfunc(EngFunc_TraceLine,enemy,end,IGNORE_MONSTERS,target,tr);get_tr2(tr,TR_flFraction,f);free_tr2(tr);if(f>=1.0) {
    new bool:dry=true;
    for(new k=0;k<=10;k++) {
     xs_vec_copy(origin,end);end[0]+=65.0*float(k);end[2]+=20.0;
     if(engfunc(EngFunc_PointContents,end)!=CONTENTS_EMPTY){dry=false;break;}
    }
    if(dry)found=true;
   }
  }
 }
 if(!found){server_print("UTILITY_TEST FAIL no flat visible fixture");server_cmd("quit");return;}
 engfunc(EngFunc_SetOrigin,observer,origin);engfunc(EngFunc_SetOrigin,target,enemy);
 set_pev(observer,pev_v_angle,angles);set_pev(observer,pev_angles,angles);set_pev(observer,pev_fixangle,1);
 angles[1]=180.0;set_pev(target,pev_v_angle,angles);set_pev(target,pev_angles,angles);set_pev(target,pev_fixangle,1);
 server_cmd("bot_competitive;bot_smart_grenades;bot_grenade_debug;bot_stop;bot_zombie;bot_quota;status");server_exec();
 rg_remove_all_items(observer);rg_give_item(observer,"weapon_knife");server_print("UTILITY_TEST give HE entity=%d",rg_give_item(observer,"weapon_hegrenade"));rg_set_user_bpammo(observer,WEAPON_HEGRENADE,1);
 rg_remove_all_items(target);rg_give_item(target,"weapon_knife");
 set_pev(target,pev_effects,EF_NODRAW);xs_vec_copy(enemy,remembered);start=get_gametime();phase=1;
 server_print("UTILITY_TEST placed origin %.0f %.0f %.0f target %.0f %.0f %.0f",origin[0],origin[1],origin[2],enemy[0],enemy[1],enemy[2]);
 set_task(0.05,"hold_fixture",0,_,_,"b");
}
public hold_fixture() {
 if(!phase)return;
 engfunc(EngFunc_SetOrigin,observer,origin);engfunc(EngFunc_SetOrigin,target,enemy);
 set_pev(observer,pev_flags,pev(observer,pev_flags)|FL_ONGROUND);
 new Float:zero[3];set_pev(observer,pev_velocity,zero);set_pev(target,pev_velocity,zero);
 if(phase==1 && get_gametime()>start+4.0) {
  set_pev(target,pev_effects,0);phase=4;start=get_gametime();server_print("UTILITY_TEST target revealed");
 }
 if(phase==4 && get_gametime()>start+3.0) {
  server_print("UTILITY_TEST HE remaining=%d",rg_get_user_bpammo(observer,WEAPON_HEGRENADE));rg_give_item(observer,"weapon_hegrenade");rg_set_user_bpammo(observer,WEAPON_HEGRENADE,1);
  set_pev(target,pev_effects,EF_NODRAW);
  wall=create_entity("info_target");set_pev(wall,pev_classname,"utility_test_wall");
  new Float:pos[3],Float:mins[3]={-12.0,-128.0,0.0},Float:maxs[3]={12.0,128.0,108.0};xs_vec_copy(origin,pos);pos[0]+=230.0;pos[2]-=36.0;
  engfunc(EngFunc_SetSize,wall,mins,maxs);engfunc(EngFunc_SetOrigin,wall,pos);set_pev(wall,pev_solid,SOLID_NOT);set_pev(wall,pev_movetype,MOVETYPE_NONE);engfunc(EngFunc_SetOrigin,wall,pos);
  enemy[0]+=500.0;phase=2;server_print("UTILITY_TEST hid and moved target; last seen remains fixed");
 }
}
public grenade_think(entity) {
 if(phase<2 || !pev_valid(entity) || pev(entity,pev_owner)!=observer)return FMRES_IGNORED;
 new name[32];pev(entity,pev_classname,name,charsmax(name));if(!equal(name,"grenade"))return FMRES_IGNORED;
 new Float:due;pev(entity,pev_dmgtime,due);if(due>get_gametime())return FMRES_IGNORED;
 new Float:point[3];pev(entity,pev_origin,point);
 new Float:oldDistance=get_distance_f(point,remembered),Float:liveDistance=get_distance_f(point,enemy);
 server_print("UTILITY_TEST grenade detonation old=%.0f live=%.0f",oldDistance,liveDistance);
 if(oldDistance<220.0 && liveDistance>300.0)server_print("UTILITY_TEST PASS native HE used frozen last-seen position");
 else server_print("UTILITY_TEST FAIL inaccurate grenade");
 phase=3;server_cmd("quit");return FMRES_IGNORED;
}
public timeout_test(){server_print("UTILITY_TEST FAIL no completed grenade");server_cmd("quit");}
