// Disposable native-bot fixture for the BSP bound in laboratory_navigation_native.sma.
// Starts at the dry wooden approach; a frozen T keeps the native round active.
// The native VIP must reach the real escape trigger. Never load in a match.
#include <amxmodx>
#include <fakemeta>
#include <cstrike>
new placed,ticks,tracked,failed;
public plugin_init(){register_plugin("VIP dry walkway fixture","1","16C");register_forward(FM_Touch,"on_touch");register_forward(FM_PlayerPreThink,"on_prethink");set_task(2.0,"add_bots");set_task(0.25,"monitor",0,"",0,"b");}
public add_bots(){server_cmd("bot_add_ct;bot_add_t");server_exec();}
public monitor(){
 ticks++;if(ticks>200){server_print("LAB_VIP FAIL timeout");server_cmd("quit");return;}
 for(new id=1;id<=get_maxplayers();id++){
  if(!is_user_alive(id)||!is_user_bot(id))continue;
  if(cs_get_user_team(id)==CS_TEAM_T){set_pev(id,pev_flags,pev(id,pev_flags)|FL_FROZEN);continue;}
  if(!cs_get_user_vip(id))continue;
  if(!placed){new Float:start[3]={-2400.0,2400.0,-411.9};engfunc(EngFunc_SetOrigin,id,start);placed=1;tracked=id;server_print("LAB_VIP start");return;}
  new Float:p[3],Float:mins[3];pev(id,pev_origin,p);pev(id,pev_mins,mins);new Float:foot[3];foot=p;foot[2]+=mins[2]+1.0;
  new contents=engfunc(EngFunc_PointContents,foot);
  if(!(ticks%4))server_print("LAB_VIP progress %.1f %.1f %.1f contents=%d",p[0],p[1],p[2],contents);
  if(contents==CONTENTS_LAVA||contents==CONTENTS_SLIME){server_print("LAB_VIP FAIL entered liquid");server_cmd("quit");return;}
  if(p[0]>=-3216.0 && p[0]<=-3088.0 && p[1]>=2412.0 && p[1]<=2484.0 && foot[2]<=-368.0){server_print("LAB_VIP PASS reached exit without liquid");server_cmd("quit");return;}
 }
}
public on_touch(ent,id){
 if(failed || !tracked || id!=tracked || !pev_valid(ent))return FMRES_IGNORED;
 new classname[64];pev(ent,pev_classname,classname,charsmax(classname));
 if(equal(classname,"func_vip_safetyzone") && cs_get_user_vip(id)){
  server_print("LAB_VIP PASS native VIP touched escape trigger without harmful liquid");server_cmd("quit");
 }
 return FMRES_IGNORED;
}

public on_prethink(id){
 if(id!=tracked || !placed || failed || !is_user_alive(id))return FMRES_IGNORED;
 new Float:p[3],Float:mins[3];pev(id,pev_origin,p);pev(id,pev_mins,mins);p[2]+=mins[2]+1.0;
 new contents=engfunc(EngFunc_PointContents,p);
 if(contents==CONTENTS_LAVA || contents==CONTENTS_SLIME){
  failed=1;server_print("LAB_VIP FAIL harmful liquid during player frame");server_cmd("quit");
 }
 return FMRES_IGNORED;
}
