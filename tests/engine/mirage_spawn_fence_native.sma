// Run only on the matching de_mirage BSP (4773592 bytes).
#include <amxmodx>
#include <fakemeta>
public plugin_init() {
 register_plugin("Mirage spawn fence collision","1","16C");
 set_task(1.0,"check_fence");
}
public check_fence() {
 new failures;
 new Float:ys[4]={-200.0,-100.0,0.0,100.0};
 for(new i=0;i<4;i++) {
  new Float:from[3]={1040.0,0.0,64.0},Float:to[3]={960.0,0.0,64.0};
  from[1]=ys[i];to[1]=ys[i];new tr=create_tr2();
  engfunc(EngFunc_TraceHull,from,to,IGNORE_MONSTERS,HULL_HEAD,0,tr);
  new Float:f;get_tr2(tr,TR_flFraction,f);
  new startSolid=get_tr2(tr,TR_StartSolid),allSolid=get_tr2(tr,TR_AllSolid);
  if(f>=1.0 || startSolid || allSolid)failures++;
  server_print("MIRAGE_FENCE y=%.0f fraction=%.3f startsolid=%d allsolid=%d",ys[i],f,startSolid,allSolid);
  free_tr2(tr);
 }
 server_print("MIRAGE_FENCE %s",failures?"FAIL":"PASS all four fence crossings block the player hull");
 server_cmd("quit");
}
