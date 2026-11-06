// Disposable engine fixture for as_oldlabratory_csneo BSP SHA-256:
// 67517e259c115f2e0f10344679ba144ec7741aea103914d2633676b707b679c7
// Checks the narrow wooden exit walkway, lethal liquid beside it, and the
// non-breakable player clip behind the yellow mid railing. Never run in a match.
#include <amxmodx>
#include <fakemeta>
new failures;
public plugin_init() {
 register_plugin("Laboratory navigation geometry", "1", "16C");
 set_task(2.0, "check_geometry");
}
stock require_clear(Float:from[3], Float:to[3], bool:expected) {
 new tr=create_tr2();
 engfunc(EngFunc_TraceHull,from,to,IGNORE_MONSTERS,HULL_HEAD,0,tr);
 new Float:f;get_tr2(tr,TR_flFraction,f);
 new bool:clear=f>=1.0 && !get_tr2(tr,TR_StartSolid) && !get_tr2(tr,TR_AllSolid);
 if(clear!=expected)failures++;
 server_print("LAB_NAV trace %.0f %.0f -> %.0f %.0f fraction=%.3f expectedClear=%d",from[0],from[1],to[0],to[1],f,expected);
 free_tr2(tr);
}
public check_geometry() {
 new map[64];get_mapname(map,charsmax(map));
 if(!equal(map,"as_oldlabratory_csneo")) {
  server_print("LAB_NAV FAIL wrong map");server_cmd("quit");return;
 }
 // Player hull traverses the turn onto the final dry strip, then the exit.
 new Float:a[3]={-2950.0,2400.0,-433.8};
 new Float:b[3]={-2950.0,2435.0,-433.8};
 new Float:c[3]={-3150.0,2435.0,-433.8};
 require_clear(a,b,true);require_clear(b,c,true);
 new Float:dry[3]={-3050.0,2435.0,-450.9};
 new Float:basin[3]={-3050.0,2400.0,-454.9};
 new dryContents=engfunc(EngFunc_PointContents,dry);
 new basinContents=engfunc(EngFunc_PointContents,basin);
 if(dryContents!=CONTENTS_EMPTY || basinContents!=CONTENTS_LAVA)failures++;
 server_print("LAB_NAV dryContents=%d basinContents=%d",dryContents,basinContents);
 // Both endpoints are free; crossing the solid mid rail is blocked even with
 // a normal step lift. Seeing through its masked texture does not permit travel.
 new Float:railA[3]={236.0,1544.0,-172.0};
 new Float:railB[3]={236.0,1608.0,-172.0};
 require_clear(railA,railA,true);require_clear(railB,railB,true);
 require_clear(railA,railB,false);
 railA[2]+=18.0;railB[2]+=18.0;
 require_clear(railA,railB,false);
 server_print("LAB_NAV %s",failures ? "FAIL" : "PASS dry exit walkway, lethal basin and solid railing");
 server_cmd("quit");
}
