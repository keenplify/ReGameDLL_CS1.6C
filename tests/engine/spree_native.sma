// Isolated-server regression. Never load on a player server.
#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
new killer,victim,attempts,kills;
public plugin_init() {
 register_plugin("Native spree regression","1","16C");
 set_task(1.0,"start_bots");set_task(8.0,"prepare_bots");set_task(24.0,"finish_test");
}
public start_bots() {
 server_cmd("log on;bot_competitive 1;bot_advanced_tactics 1;bot_ai_bridge 0;bot_text_chat 1;bot_round_chat 1;bot_quota 5;bot_zombie 1;bot_allow_grenades 0;mp_freezetime 0;mp_autoteambalance 0;mp_limitteams 0");server_exec();
}
public prepare_bots() {
 new bots[32],count;get_players(bots,count,"d");
 if(count<5){if(++attempts<20)set_task(0.5,"prepare_bots");return;}
 killer=bots[0];victim=bots[1];
 for(new i=0;i<5;i++) {
  rg_set_user_team(bots[i],(i==1 || i==3)?TEAM_TERRORIST:TEAM_CT);rg_round_respawn(bots[i]);
  set_user_info(bots[i],"_16c_style",i==4?"normal":"quiet");
 }
 server_print("SPREE_TEST killer=%d victim=%d only third CT may initiate event speech",killer,victim);
 set_task(1.0,"kill_target");
}
public kill_target() {
 if(!is_user_alive(victim))rg_round_respawn(victim);
 set_entvar(victim,var_health,100.0);set_entvar(victim,var_armorvalue,0.0);
 ExecuteHamB(Ham_TakeDamage,victim,killer,killer,1000.0,DMG_BULLET);
 server_print("SPREE_TEST kill %d victim_dead=%d",++kills,!is_user_alive(victim));
 if(kills<3)set_task(1.0,"kill_target");
}
public finish_test(){server_print("SPREE_TEST complete; inspect normal say_team log for praise");server_cmd("quit");}
