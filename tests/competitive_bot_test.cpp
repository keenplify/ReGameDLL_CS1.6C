// SPDX-License-Identifier: MIT
#include "competitive_policy.h"
#include "competitive_peek.h"
#include "competitive_nav_block.h"
#include "competitive_idle.h"
#include "competitive_away.h"
#include "competitive_arena_spread.h"
#include "competitive_team_order.h"
#include "competitive_team_allocation.h"
#include "competitive_execution.h"
#include "competitive_lock_in.h"
#include "competitive_traffic.h"
#include "competitive_watch.h"
#include "competitive_spree.h"
#include "competitive_enemy_damage.h"
#include "competitive_tactical_comms.h"
#include "competitive_holding_utility.h"
#include <cstdio>
#include <string>

int main()
{
	int failures = 0;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
 {
  CompetitiveIdleRecovery idle;
  CHECK(!idle.Update(10,true,false));
  CHECK(!idle.Update(17.9f,true,false));
  CHECK(idle.Update(18,true,false));
  CHECK(!idle.Update(19,true,false));
  CHECK(!idle.Update(20,false,false)); // guarding/objective/order cancels timer
  CHECK(!idle.Update(40,true,false));
  CHECK(!idle.Update(47,true,true)); // movement restarts timer
  CHECK(!idle.Update(50,true,false));
  CHECK(idle.Update(55,true,false));
  using namespace CompetitiveBotPolicy;
  CHECK(RememberThreat(5.9f));
  CHECK(!RememberThreat(6));
  CHECK(!RememberThreat(-1));
  CHECK(!RememberThreat(NAN));
  CHECK(HoldLostAngle(1.2f));
  CHECK(!HoldLostAngle(1.25f));
  CHECK(PeekParticipation(0));
  CHECK(PeekParticipation(1));
  CHECK(!PeekParticipation(2));
  CHECK(RifleTapReady(0,.1f,0,true));
  CHECK(!RifleTapReady(80,.1f,0,true));
  CHECK(!RifleTapReady(0,1,0,true));
  CHECK(!RifleTapReady(0,.1f,1,true));
  CHECK(!RifleTapReady(0,.1f,0,false));
  CHECK(RifleVolleySize(350,0)==1);
  CHECK(RifleVolleySize(350,50)==2);
  CHECK(RifleVolleySize(350,99)==3);
  CHECK(RifleVolleySize(800,99)==2);
  CHECK(RifleVolleySize(1400,99)==1);
  CHECK(!RifleVolleyComplete(30,29,3));
  CHECK(RifleVolleyComplete(30,27,3));
  CHECK(RifleVolleyComplete(1,0,3)); // empty clip ends a partial volley
  CHECK(!RifleVolleyComplete(-1,20,1));
  int roles[3]={};
  for(int roll=0;roll<100;++roll) ++roles[CompetitiveBuyRole(roll)];
  CHECK(roles[0]==85 && roles[1]==10 && roles[2]==5);
  NavBlockMemory blocks;
  blocks.Add(1,2,10);
  CHECK(blocks.Contains(1,2,10));
  CHECK(!blocks.Contains(2,1,10)); // do not poison the reverse route
  CHECK(!blocks.Contains(1,3,10)); // alternatives remain available
  CHECK(!blocks.Contains(1,2,30)); // moving barriers can be retried
  CHECK(!blocks.Contains(1,2,0)); // reset map clock
  blocks.Add(3,3,10);
  CHECK(!blocks.Contains(3,3,10)); // do not disable a whole nav area
  PeekProgress progress;
  CHECK(!progress.Stalled(10,60));
  CHECK(!progress.Stalled(10.2f,59));
  CHECK(progress.Stalled(10.36f,59)); // blocked despite movement input
  progress.Reset();
  CHECK(!progress.Stalled(11,60));
  CHECK(!progress.Stalled(11.3f,50)); // real progress renews deadline
  CHECK(!progress.Stalled(11.6f,40));
  CHECK(progress.Stalled(12,41)); // sliding sideways is not progress
  progress.Reset();
  CHECK(!progress.Stalled(13,80)); // new leg gets its own budget
  PeekCycle peek;
  peek.Start(10);
  CHECK(peek.Step(10.2f,true,false,.3f));
  CHECK(peek.phase==PeekPhase::Retreat); // arrival alone is not cover
  CHECK(peek.Step(10.3f,true,true,.3f));
  CHECK(peek.phase==PeekPhase::Hidden);
  CHECK(peek.Step(10.5f,true,true,.3f));
  CHECK(peek.phase==PeekPhase::Hidden); // no immediate re-exposure
  CHECK(peek.Step(10.7f,true,true,.3f));
  CHECK(peek.phase==PeekPhase::Expose);
  CHECK(peek.Step(10.8f,false,false,.3f));
  CHECK(peek.phase==PeekPhase::Expose); // do not shoot while moving out
  CHECK(peek.Step(11,true,false,.3f));
  CHECK(peek.phase==PeekPhase::Fire);
  CHECK(peek.Step(12.6f,true,false,.3f));
  CHECK(peek.phase==PeekPhase::Retreat); // bounded exposure even if unable to fire
  CHECK(!peek.Step(14,false,false,.3f)); // blocked route cannot stick indefinitely
  peek.Start(20);
  CHECK(peek.Step(20.1f,true,true,.3f));
  CHECK(!peek.Step(20.2f,true,false,.3f)); // cover no longer protects us
 }
 CHECK(arenaSpreadAllowed(2,true,false,false));
 CHECK(!arenaSpreadAllowed(0,true,false,false));
 CHECK(!arenaSpreadAllowed(1,true,false,false));
 CHECK(!arenaSpreadAllowed(2,false,false,false));
 CHECK(!arenaSpreadAllowed(2,true,true,false));
 CHECK(!arenaSpreadAllowed(2,true,false,true));
 CHECK(arenaSpreadScore(500,700,0)>arenaSpreadScore(50,400,1));
 CHECK(arenaSpreadScore(500,600,0)>arenaSpreadScore(500,2000,0));
 CHECK(arenaSpreadScore(700,1000,0)==arenaSpreadScore(5000,1000,0));
 CHECK(simulatedAwayLease(100,280,42,42,1,true,true,false));
 CHECK(simulatedAwayLease(100,279,42,42,2,true,true,false));
 CHECK(!simulatedAwayLease(100,281,42,42,1,true,true,false));
 CHECK(!simulatedAwayLease(280,280,42,42,1,true,true,false));
 CHECK(!simulatedAwayLease(100,200,42,43,1,true,true,false));
 CHECK(!simulatedAwayLease(100,200,42,42,0,true,true,false));
 CHECK(!simulatedAwayLease(100,200,42,42,1,false,true,false));
 CHECK(!simulatedAwayLease(100,200,42,42,1,true,false,false));
 CHECK(!simulatedAwayLease(100,200,42,42,1,true,true,true));
 CHECK(!simulatedAwayLease(100,NAN,42,42,1,true,true,false));
 CHECK(!simulatedAwayLease(100,INFINITY,42,42,1,true,true,false));
	CHECK(parseTeamOrder("guys, rotate to B now") == TeamOrder::RotateB);
	CHECK(parseTeamOrder("bot, follow me") == TeamOrder::FollowMe);
	CHECK(parseTeamOrder("can you jump?") == TeamOrder::Jump);
	CHECK(parseTeamOrder("everyone go B") == TeamOrder::AllB);
	CHECK(parseTeamOrder("please do not rush b") == TeamOrder::None);
	CHECK(parseTeamOrder("push was bad") == TeamOrder::None);
	CHECK(parseTeamOrder("should we rush b?") == TeamOrder::None);
	CHECK(parseTeamOrder("go a b") == TeamOrder::None);
	CHECK(parseTeamOrder("go b; quit") == TeamOrder::None);
	CHECK(parseTeamOrder("hold\nquit") == TeamOrder::None);
	CHECK(parseTeamOrder(std::string(4096, 'a').c_str()) == TeamOrder::None);
	CHECK(parseTeamOrder(nullptr) == TeamOrder::None);
	CHECK(parseRequestedDropWeapon("drop awm pls") && std::strcmp(parseRequestedDropWeapon("drop awm pls"), "weapon_awp") == 0);
	CHECK(parseRequestedDropWeapon("don't drop awp") == nullptr);
 char announcement[64];
 CHECK(formatTeamAllocation({3,2,0},announcement,sizeof(announcement)) && std::strcmp(announcement,"3a 2b")==0);
 CHECK(formatTeamAllocation({3,0,2},announcement,sizeof(announcement)) && std::strcmp(announcement,"3a 2mid")==0);
 CHECK(formatTeamAllocation({3,1,1},announcement,sizeof(announcement)) && std::strcmp(announcement,"3a 1b 1mid")==0);
 CHECK(formatTeamAllocation({5,0,0},announcement,sizeof(announcement)) && std::strcmp(announcement,"5a")==0);
 CHECK(!formatTeamAllocation({0,0,0},announcement,sizeof(announcement)) && !announcement[0]);
 char small[3];CHECK(!formatTeamAllocation({3,2,0},small,sizeof(small)) && !small[0]);
 TeamAllocation split;
 CHECK(parseTeamAllocation("3 A 2 B", split) && split.a==3 && split.b==2 && split.mid==0);
 CHECK(parseTeamAllocation("3a 2mid", split) && split.a==3 && split.mid==2);
 CHECK(parseTeamAllocation("3a 1mid 1b", split) && split.a==3 && split.mid==1 && split.b==1);
 for(auto bad:{"3a 2a","0a 5b","3a 2b?","3a 2b;quit","go 3a 2b","9a 9b"}) CHECK(!parseTeamAllocation(bad,split));
	using namespace CompetitiveBotPolicy;
 CHECK(ReplyDelay(2, 0.25f) > 1.5f);
 CHECK(ReplyDelay(90, 0.65f) < 3.1f);
 CHECK(ReplyDelay(90, 0.65f) == ReplyDelay(1000, 0.65f));
 CHECK(ReplyDelay(30, 0.25f) > ReplyDelay(2, 0.25f));
 for(int level=0;level<5;++level){CHECK(CompetitiveExecution::Reaction(level,true)>=.108f-.0001f);CHECK(CompetitiveExecution::Get(level).tracking>0);if(level)CHECK(CompetitiveExecution::Get(level).error<CompetitiveExecution::Get(level-1).error);}
 CHECK(std::abs(CompetitiveExecution::Reaction(0,false)-.405f)<.0001f);
 CHECK(std::abs(CompetitiveExecution::Reaction(4,false)-.135f)<.0001f);
 CHECK(std::abs(CompetitiveExecution::Reaction(4,true)-.108f)<.0001f);
 for(int level=0;level<5;++level){
  float cap=CompetitiveExecution::AimOffset(level,100000,true);
  CHECK(cap<=18 && cap>=.25f);
  CHECK(CompetitiveExecution::AimOffset(level,20,true)<=cap);
  CHECK(CompetitiveExecution::AimOffset(level,20,false)<=CompetitiveExecution::AimOffset(level,20,true));
 }
 CHECK(CompetitiveExecution::AimOffset(0,100000,true)==18);
 CHECK(CompetitiveExecution::AimOffset(4,100000,true)==4);

 for(int mmr:{0,1000,1300,1301,1500,2000,3000,10000}) {
  CHECK(CompetitiveExecution::HasRatedAim(mmr));
  const float duration=CompetitiveExecution::OpeningDuration(mmr);
  CHECK(duration>=.35f && duration<=3.2f);
  for(float range:{64.0f,500.0f,2000.0f}) {
   const float start=CompetitiveExecution::OpeningOffset(mmr,0,range);
   CHECK(start>=32 && start<=96);
   CHECK(CompetitiveExecution::OpeningOffset(mmr,duration*.5f,range)<start);
   CHECK(CompetitiveExecution::OpeningOffset(mmr,duration,range)==0);
   CHECK(CompetitiveExecution::OpeningOffset(mmr,duration+1,range)==0);
   const float tenDegrees=std::cos(10.0f*float(3.141592653589793/180.0));
   CHECK(tenDegrees>CompetitiveExecution::OpeningFireTolerance(range,false,true,mmr));
   CHECK(CompetitiveExecution::OpeningFireTolerance(range,true,true,mmr)==CompetitiveExecution::FireTolerance(range,true,true));
   CHECK(CompetitiveExecution::OpeningFireTolerance(range,false,false,mmr)==CompetitiveExecution::FireTolerance(range,false,false));
  }
 }
 for(int mmr:{-1,-1000}) {
  CHECK(!CompetitiveExecution::HasRatedAim(mmr));
  CHECK(CompetitiveExecution::OpeningOffset(mmr,0,500)==0);
  CHECK(CompetitiveExecution::OpeningFireTolerance(500,false,true,mmr)==CompetitiveExecution::FireTolerance(500,false,true));
 }
 CHECK(std::abs(CompetitiveExecution::OpeningDuration(1000)-2.6f)<.0001f);
 CHECK(std::abs(CompetitiveExecution::OpeningDuration(2000)-1.261f)<.0001f);
 for(int mmr:{0,500,1000,1100}) {
  const float original=std::max(.35f,std::min(3.2f,2.6f*std::pow(.5f,(float(mmr)-1000)/1000)));
  CHECK(std::abs(CompetitiveExecution::OpeningDuration(mmr)-original)<.0001f);
 }
 for(int mmr=1101;mmr<5000;++mmr) {
  const float original=std::max(.35f,std::min(3.2f,2.6f*std::pow(.5f,(float(mmr)-1000)/1000)));
  CHECK(CompetitiveExecution::OpeningDuration(mmr)<=original);
  CHECK(CompetitiveExecution::OpeningDuration(mmr)>=original*.9699f);
 }
 CHECK(CompetitiveExecution::IndependentPlanChance(-1)==0);
 CHECK(CompetitiveExecution::IndependentPlanChance(1300)==0);
 CHECK(std::abs(CompetitiveExecution::IndependentPlanChance(1100)-.2f)<.0001f);
 CHECK(CompetitiveExecution::IndependentPlanChance(0)==.45f);
 for(int mmr=0;mmr<2000;++mmr) {
  CHECK(CompetitiveExecution::IndependentPlanChance(mmr+1)<=CompetitiveExecution::IndependentPlanChance(mmr));
  CHECK(!CompetitiveExecution::TakesIndependentRoute(mmr,0,true,false));
  CHECK(!CompetitiveExecution::TakesIndependentRoute(mmr,0,false,true));
 }
 CHECK(CompetitiveExecution::TakesIndependentRoute(1100,.19f,false,false));
 CHECK(!CompetitiveExecution::TakesIndependentRoute(1100,.21f,false,false));
 CHECK(CompetitiveExecution::OpeningDuration(0)==3.2f);
 CHECK(CompetitiveExecution::OpeningDuration(10000)==.35f);
 for(int mmr=0;mmr<10000;mmr+=100)
  CHECK(CompetitiveExecution::OpeningDuration(mmr+100)<=CompetitiveExecution::OpeningDuration(mmr));
 CHECK(std::abs(CompetitiveExecution::OpeningDuration(1300)-CompetitiveExecution::OpeningDuration(1301))<.002f);
 CHECK(CompetitiveExecution::FireTolerance(1000,false,true)>.999f);
 CHECK(CompetitiveExecution::FireTolerance(1000,false,true)>CompetitiveExecution::FireTolerance(100,false,true));
 CHECK(CompetitiveExecution::FireTolerance(1000,true,false)>CompetitiveExecution::FireTolerance(1000,false,false));
 CHECK(CompetitiveExecution::FireTolerance(1000,false,true)<CompetitiveExecution::FireTolerance(1000,false,false));
 for(int level=0;level<5;++level){
  CHECK(CompetitiveLockIn::Duration(level,0)>=.08f);
  CHECK(CompetitiveLockIn::Duration(level,1)<.29f);
  if(level)CHECK(CompetitiveLockIn::Duration(level,.5f)>CompetitiveLockIn::Duration(level-1,.5f));
  for(float roll:{0.0f,.5f,.999f}){
   auto window=CompetitiveLockIn::Plan(level,0,roll,.5f,.5f);
   CHECK(window.phase>=0 && window.phase<=2);
   CHECK(window.start<window.end && window.end<=1.0f);
   CHECK(!CompetitiveLockIn::Active(window,window.start-.001f));
   CHECK(CompetitiveLockIn::Active(window,window.start));
   CHECK(!CompetitiveLockIn::Active(window,window.end));
  }
 }
 CHECK(CompetitiveLockIn::Plan(2,0,0,.5f,.5f).phase==0);
 CHECK(CompetitiveLockIn::Plan(2,0,.5f,.5f,.5f).phase==1);
 CHECK(CompetitiveLockIn::Plan(2,0,.999f,.5f,.5f).phase==2);
 CHECK(CompetitiveLockIn::Plan(2,.6f,0,.5f,.5f).phase==2);
 CHECK(CompetitiveLockIn::Plan(2,.9f,0,.5f,.5f).start>=.9f);
 CHECK(CompetitiveTraffic::Yields(3,2,1,2));CHECK(!CompetitiveTraffic::Yields(2,3,2,1));
 CHECK(CompetitiveTraffic::Yields(1,0,1,2));CHECK(!CompetitiveTraffic::Yields(0,1,2,1));
 CHECK(CompetitiveTraffic::Yields(2,2,8,7));CHECK(!CompetitiveTraffic::Yields(2,2,7,8));
 CHECK(CompetitiveTraffic::HeadOn(70,5,0,-1));
 CHECK(!CompetitiveTraffic::HeadOn(70,5,0,1)); // convoy
 CHECK(!CompetitiveTraffic::HeadOn(-70,5,0,-1)); // behind us
 CHECK(!CompetitiveTraffic::HeadOn(70,40,0,-1)); // adjacent route
 CHECK(!CompetitiveTraffic::HeadOn(70,0,40,-1)); // separate floors
 OpeningPlanGate opening;CHECK(opening.available(2));CHECK(!opening.available(9));
 opening.chosen=true;CHECK(!opening.available(3));CHECK(!opening.available(55));
 CHECK(opening.deliver(5));CHECK(!opening.deliver(11));
 opening=OpeningPlanGate {};CHECK(opening.available(2));
 CHECK(CompetitiveExecution::ShouldSave(1,4,1000,true,false,false,20));
 CHECK(!CompetitiveExecution::ShouldSave(1,4,1000,true,true,false,20));
 CHECK(!CompetitiveExecution::ShouldSave(1,4,1000,true,false,true,20));
 CHECK(!CompetitiveExecution::ShouldSave(3,4,1000,true,false,false,20));
 CHECK(!CompetitiveExecution::ShouldSave(1,4,5000,true,false,false,20));
 EnemyDamageEstimate damage;damage.hit(60,10);CHECK(!damage.ready(10.7f));CHECK(damage.ready(10.8f));CHECK(std::strcmp(damage.label(),"low hp")==0);damage.reportedTier=damage.tier();CHECK(!damage.ready(11));damage.hit(25,11);CHECK(damage.ready(12));CHECK(std::strcmp(damage.label(),"one shot")==0);damage.hit(10,30);CHECK(!damage.ready(31));
 TacticalCommsGate gate;CHECK(gate.available(10,0));gate.record(10,0);CHECK(!gate.available(11,1));CHECK(gate.available(13,1));CHECK(!gate.available(13,0));CHECK(gate.available(22,0));CHECK(gate.available(1,0));
 CHECK(learnedHoldingSpotAvailable(1,2,1,1,10,false));CHECK(!learnedHoldingSpotAvailable(1,3,1,1,10,false));CHECK(!learnedHoldingSpotAvailable(1,2,1,2,10,false));CHECK(!learnedHoldingSpotAvailable(1,2,1,1,10,true));
 CHECK(CompetitiveWatch::Approach(400,20,1,false));
 CHECK(!CompetitiveWatch::Approach(400,20,.2f,false)); // wall between bot and NAV point
 CHECK(!CompetitiveWatch::Approach(30,20,1,false)); // point beside the crate
 CHECK(!CompetitiveWatch::Approach(200,400,1,false)); // not a useful horizontal angle
 CHECK(!CompetitiveWatch::Approach(400,20,1,true));
 CHECK(CompetitiveWatch::OpenScore(30,1,false)<0);
 CHECK(CompetitiveWatch::OpenScore(768,0,false)>CompetitiveWatch::OpenScore(300,1,false));
 CHECK(CompetitiveWatch::OpenScore(768,1,true)<0);
 CHECK(CompetitiveBotPolicy::RoundTacticsAllowed(false,false,false,false)); // standalone
 CHECK(CompetitiveBotPolicy::RoundTacticsAllowed(false,false,true,true)); // live match
 CHECK(!CompetitiveBotPolicy::RoundTacticsAllowed(false,false,true,false)); // warmup
 CHECK(!CompetitiveBotPolicy::RoundTacticsAllowed(true,false,false,true)); // engine FFA
 CHECK(!CompetitiveBotPolicy::RoundTacticsAllowed(false,true,true,true)); // managed deathmatch
 CompetitiveSpree::Pending spree;
 spree.Kill(1,false);CHECK(!spree.Ready(2));
 spree.Kill(2,false);CHECK(!spree.Ready(3.5f));CHECK(spree.count==0);
 spree.Kill(3,false);CHECK(!spree.Ready(4));CHECK(spree.Ready(4.5f));
 spree.Retry(4.5f);CHECK(spree.count==3 && !spree.Ready(4.8f));CHECK(spree.Ready(5));
 spree.Kill(5,false);spree.Kill(5.1f,false);CHECK(spree.count==5);
 spree.Delivered();spree.Kill(7,false);CHECK(spree.count==6);
 CHECK(CompetitiveSpree::Ace(true,5,0,5));
 CHECK(CompetitiveSpree::Ace(true,3,0,3));
 CHECK(!CompetitiveSpree::Ace(true,5,1,5));
 CHECK(!CompetitiveSpree::Ace(true,6,1,5));
 CHECK(!CompetitiveSpree::Ace(true,5,0,4));
 CHECK(!CompetitiveSpree::Ace(true,2,0,2));
 CHECK(!CompetitiveSpree::Ace(false,5,0,5));
 spree=CompetitiveSpree::Pending {};CHECK(spree.kills==0 && !spree.Ready(10));
 for(int i=0;i<5;++i)spree.Kill(float(i),true);
 spree.Died(true);CHECK(spree.kills==0 && spree.count==0 && !spree.Ready(6));
 spree.Kill(7,true);CHECK(spree.kills==1 && spree.count==0);
 CompetitiveSpree::Pending dm;for(int i=0;i<10;++i)dm.Kill(float(i),true);CHECK(dm.count==10);
 dm.Retry(20);CHECK(dm.count==0);
	Lifecycle lifecycle;
	CHECK(!lifecycle.Observe(false, false, 0).greet);
	CHECK(lifecycle.Observe(false, true, 0).greet);
	CHECK(!lifecycle.Observe(false, true, 0).greet);
	CHECK(lifecycle.Observe(false, true, 1).rebuy);
	CHECK(!lifecycle.Observe(false, true, 1).rebuy);
	CHECK(!lifecycle.Observe(false, false, 1).greet);
	CHECK(!lifecycle.Observe(false, true, 1).greet); // halftime/restart
	auto ended = lifecycle.Observe(true, true, 2);
	CHECK(ended.ended && !ended.rebuy && !ended.greet);
	CHECK(!lifecycle.Observe(false, true, 3).rebuy); // terminal latch
	CHECK(lifecycle.completed);
	lifecycle = Lifecycle {};
	CHECK(lifecycle.Observe(false, true, 0).greet); // next match
	CHECK(ClearTrace(1.0f, false, false));
	CHECK(!ClearTrace(0.99999f, false, false));
	CHECK(!ClearTrace(1.0f, true, false));
	CHECK(!ClearTrace(1.0f, false, true));
	Recipient bot { 5, 1, true }, player { 9, 1, true };
	CHECK(DeliveryCurrent(11, 10, 14, 2, 2, bot, bot, player, player));
	CHECK(!DeliveryCurrent(15, 10, 14, 2, 2, bot, bot, player, player));
	CHECK(!DeliveryCurrent(9, 10, 14, 2, 2, bot, bot, player, player));
	CHECK(!DeliveryCurrent(11, 10, 14, 2, 3, bot, bot, player, player));
	CHECK(!DeliveryCurrent(11, 10, 14, 2, 2, bot, { 6, 1, true }, player, player));
	CHECK(!DeliveryCurrent(11, 10, 14, 2, 2, bot, bot, player, { 10, 1, true }));
	CHECK(!DeliveryCurrent(11, 10, 14, 2, 2, bot, { 5, 2, true }, player, player));
	CHECK(!DeliveryCurrent(11, 10, 14, 2, 2, bot, bot, player, { 9, 1, false }));
	CHECK(!DeliveryCurrent(11, 10, 14, 2, 2, bot, bot, player, {}));
	CHECK(ActionsAllowed(true, false, false, true, true, true));
	CHECK(!ActionsAllowed(false, false, false, true, true, true));
	CHECK(!ActionsAllowed(true, true, false, true, true, true));
	CHECK(!ActionsAllowed(true, false, true, true, true, true));
	CHECK(!ActionsAllowed(true, false, false, false, true, true));
	CHECK(!ActionsAllowed(true, false, false, true, false, true));
	CHECK(!ActionsAllowed(true, false, false, true, true, false));

 Sighting contact;
 contact.enabled = contact.observerAlive = contact.opponentAlive = true;
 contact.observed = contact.visibleNow = true;
 CHECK(SightingAllowed(contact, 100, 100, 100));
 CHECK(!SightingAllowed(contact, 100, 101, 100));
 CHECK(!SightingAllowed(contact, 100, 100, 101));
 for (auto deny : { &Sighting::completed, &Sighting::ffa, &Sighting::freeze, &Sighting::roundOver,
                    &Sighting::sameTeam, &Sighting::protectedTarget, &Sighting::observerFiring,
                    &Sighting::opponentFiring, &Sighting::underPressure }) {
  auto blocked = contact; blocked.*deny = true;
  CHECK(!SightingAllowed(blocked, 100, 0, 0));
 }
 for (auto required : { &Sighting::enabled, &Sighting::observerAlive, &Sighting::opponentAlive,
                       &Sighting::observed, &Sighting::visibleNow }) {
  auto blocked = contact; blocked.*required = false;
  CHECK(!SightingAllowed(blocked, 100, 0, 0));
 }
 // Check the distribution across every roll, plus actual staged-execute gates.
 for(int context=0;context<4;++context) {
  int splits=0;
  for(int roll=0;roll<100;++roll)if(chooseAttackPlan(context==1,context==2?2:0,context==3,roll,0)==AttackPlan::Split)++splits;
  CHECK(splits==(context==0?5:context==1?35:context==2?45:60));
 }
 CHECK(chooseAttackPlan(false,0,false,99,0)==AttackPlan::AllA);
 CHECK(chooseAttackPlan(false,0,false,99,1)==AttackPlan::AllB);
 CHECK(chooseAttackPlan(false,0,false,99,2)==AttackPlan::MidToA);
 CHECK(chooseAttackPlan(false,0,false,99,3)==AttackPlan::MidToB);
 CHECK(!midStageReady(5,5,true,3));
 CHECK(!midStageReady(5,4,false,8));
 CHECK(!midStageReady(5,3,true,8));
 CHECK(midStageReady(5,4,true,8));
 CHECK(midStageReady(5,0,false,20));
	if (!failures) std::puts("competitive bot policy tests passed");
	return failures ? 1 : 0;
}
