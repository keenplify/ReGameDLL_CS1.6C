// SPDX-License-Identifier: MIT
#include "competitive_policy.h"
#include "competitive_team_order.h"
#include "competitive_team_allocation.h"
#include "competitive_execution.h"
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
 for(int level=0;level<5;++level){CHECK(CompetitiveExecution::Reaction(level,true)>=.12f);CHECK(CompetitiveExecution::Get(level).tracking>0);if(level)CHECK(CompetitiveExecution::Get(level).error<CompetitiveExecution::Get(level-1).error);}
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
 CompetitiveSpree::Pending spree;
 spree.Kill(1,false);CHECK(!spree.Ready(2));
 spree.Kill(2,false);CHECK(!spree.Ready(2.5f));CHECK(spree.Ready(3.5f));
 spree.Retry(3);CHECK(spree.count==2 && !spree.Ready(3.2f));CHECK(spree.Ready(3.6f));
 spree.Kill(3.6f,false);CHECK(spree.count==3);spree.Delivered();CHECK(!spree.Ready(4));
 spree.Kill(4,true);spree.Died(true);CHECK(spree.kills==0 && spree.count==4);
 spree.Retry(15);CHECK(spree.count==0);
 CompetitiveSpree::Pending dm;for(int i=0;i<10;++i)dm.Kill(float(i),true);CHECK(dm.count==10);
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
	if (!failures) std::puts("competitive bot policy tests passed");
	return failures ? 1 : 0;
}
