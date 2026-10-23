// SPDX-License-Identifier: MIT
// Team phrase parser ported from keenplify/yapb-comms at 76b82b5 (MIT).
#include "precompiled.h"
#include "cs_bot_competitive.h"
#include "competitive_policy.h"
#include "competitive_ping.h"
#include "../../../query/query_packet.h"
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include "competitive_team_order.h"
#include "competitive_team_allocation.h"
#include "competitive_tactical_comms.h"
#include "competitive_lagging_bot.h"
#include <cstdio>
#include <cmath>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace CompetitiveBots {
namespace {
cvar_t enabled = { "bot_competitive", "0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t teamOrders = { "bot_team_orders", "1", FCVAR_SERVER, 0.0f, nullptr };
cvar_t textChat = { "bot_text_chat", "1", FCVAR_SERVER, 0.0f, nullptr };
cvar_t combatMovement = { "bot_combat_movement", "1", FCVAR_SERVER, 0.0f, nullptr };
cvar_t captainMode = { "bot_captain_mode", "1", FCVAR_SERVER, 0.0f, nullptr };
cvar_t pingComms = { "bot_ping_comms", "1", FCVAR_SERVER, 0.0f, nullptr };
cvar_t showLatency = { "bot_show_latency", "0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t pingMin = { "bot_ping_min", "20", FCVAR_SERVER, 0.0f, nullptr };
cvar_t highPing = { "bot_lagging_bot_percent", "15", FCVAR_SERVER, 0, nullptr };
cvar_t mediumPing = { "bot_medium_ping_bot_percent", "25", FCVAR_SERVER, 0, nullptr };
cvar_t pingMax = { "bot_ping_max", "60", FCVAR_SERVER, 0.0f, nullptr };
cvar_t aiBridge = { "bot_ai_bridge", "0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t queryHook = { "bot_enable_query_hook", "0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t roundChat = { "bot_round_chat", "1", FCVAR_SERVER, 0.0f, nullptr };

using CompetitiveBotPolicy::Recipient;
struct Reply {
	bool active = false;
	bool teamOnly = false;
	bool tactical = false;
	bool planAnnouncement = false;
	int listenerSlot = 0, subjectSlot = 0;
	Recipient subject;
	Recipient speaker, listener;
	float issued = 0, due = 0, deadline = 0, typingStart = 0;
	unsigned round = 0;
	char text[91] = {};
	char aiToken[64] = {};
};
struct Slot {
	int userId = 0;
	float nextChat = 0;
	float nextOrder = 0;
	float nextPing = 0, nextComplaint = 0;
 int hurtActor=0,hurtId=0,partnerSlot=0,partnerId=0;float hurtAt=0,partnerUntil=0;
	Reply reply;
};
Slot slots[MAX_CLIENTS + 1];
bool mobilityDecided[MAX_CLIENTS+1]={}, mobilityPreferred[MAX_CLIENTS+1]={};
bool eco[4] = {};
float nextTeamChat[4] = {};
float nextTeamPing[4] = {};
float nextComplaint[4] = {};
float humanCaptainUntil[4]={},lastHumanChat[4]={};
int humanLeaderSlot[4]={},humanLeaderId[4]={};
TacticalCommsGate tacticalGate[4];
float pingComplaintNext=0,placeNext[4][3]={},nextSpreeChat[4]={};
struct WatchOrder {int id=0;float expires=0;Vector goal {0,0,0};};
WatchOrder watches[MAX_CLIENTS+1];
struct Alias {char from[97]={},to[97]={};};
Alias aliases[256];int aliasCount=0;
struct Callout {char name[64]={};Vector point {0,0,0};};
Callout callouts[64];int calloutCount=0;
void LoadCallouts() {
 calloutCount=0;char dir[MAX_PATH],path[600];GET_GAME_DIR(dir);
 const char *map=STRING(gpGlobals->mapname);if(!map || strspn(map,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(map))return;
 Q_snprintf(path,sizeof(path),"%s/maps/%s.bsp",dir,map);struct stat info;if(stat(path,&info))return;
 Q_snprintf(path,sizeof(path),"%s/addons/regamedll/callouts/%s.cfg",dir,map);FILE *file=fopen(path,"rb");if(!file)return;
 char line[256];long expected=0;size_t bytes=0;
 while(calloutCount<64 && fgets(line,sizeof(line),file) && (bytes+=strlen(line))<=32768) {
  if(sscanf(line,"bsp_size %ld",&expected)==1)continue;
  Callout entry;float x,y,z;char extra;
  if(sscanf(line,"%63s %f %f %f %c",entry.name,&x,&y,&z,&extra)!=4 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || fabs(x)>32768 || fabs(y)>32768 || fabs(z)>32768)continue;
  if(strspn(entry.name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=strlen(entry.name))continue;
  entry.point=Vector(x,y,z);callouts[calloutCount++]=entry;
 }
 fclose(file);if(expected!=info.st_size)calloutCount=0;
}

unsigned roundSerial = 0;
CompetitiveBotPolicy::Lifecycle lifecycle;
float nextFrame = 0;
char matchId[80] = {};

CBasePlayer *Player(int slot)
{
	if (slot < 1 || slot > gpGlobals->maxClients || slot > MAX_CLIENTS) return nullptr;
	auto *p = UTIL_PlayerByIndex(slot);
	return p && p->IsPlayer() && !p->IsDormant() ? static_cast<CBasePlayer *>(p) : nullptr;
}

struct LatencySlot {
 int userId = 0;
 int base = 0;
 int ping = 0, category=0, categoryBase=0;
 float nextComplaint=0;PingTemperament temperament=PingTemperament::Quiet;
 float nextSend = 0;
};
LatencySlot latency[MAX_CLIENTS + 1];
float nextLatency = 0, lastLatencyTime = -1;
bool latencyEnabled = false;
void RefreshLatency() {
 bool active = showLatency.value > 0;
 bool changed = active != latencyEnabled || gpGlobals->time < lastLatencyTime;
 lastLatencyTime = gpGlobals->time;
 if (!changed && gpGlobals->time < nextLatency) return;
 nextLatency = gpGlobals->time + 1.0f;
 latencyEnabled = active;
 int low = CompetitivePing::Bound(pingMin.value, 20);
 int high = CompetitivePing::Bound(pingMax.value, 60);
 if (high < low) high = low;
 for (int i=1; i<=gpGlobals->maxClients && i<=MAX_CLIENTS; ++i) {
  auto *p = Player(i);
  if (!p) { latency[i] = LatencySlot {}; continue; }
  auto &state = latency[i];
  int id = GETPLAYERUSERID(p->edict());
  if (state.userId != id || changed) { state = LatencySlot {}; state.userId = id; }
  if (!p->IsBot()) continue;
  // Keep FL_FAKECLIENT and authoritative identity. Only change the scoreboard hint.
  char *info = GET_INFO_BUFFER(p->edict());
  const char *wanted = active ? "" : "1";
  if (Q_strcmp(g_engfuncs.pfnInfoKeyValue(info, "*bot"), wanted))
   SET_CLIENT_KEY_VALUE(i, info, "*bot", wanted);
  if (!active) { state.ping = 0; continue; }
  if(!state.categoryBase){int roll=RANDOM_LONG(0,99);state.category=roll<int(highPing.value)?1:roll<int(highPing.value+mediumPing.value)?2:0;state.categoryBase=state.category==1?RANDOM_LONG(120,220):state.category==2?RANDOM_LONG(60,90):1;state.temperament=pingTemperament(static_cast<CCSBot *>(p)->GetProfile()->GetAggression()>.7f?1:static_cast<CCSBot *>(p)->GetProfile()->GetAggression()<.35f?2:0,RANDOM_LONG(0,99));}
  if (state.base < low || state.base > high) state.base = RANDOM_LONG(low, high);
  state.ping = state.base + RANDOM_LONG(-2, 2);
  if (state.ping < low) state.ping = low;
  if (state.ping > high) state.ping = high;
  if((state.category==1 && highPing.value>0) || (state.category==2 && mediumPing.value>0))state.ping=laggingDisplayPing(state.categoryBase,RANDOM_LONG(state.category==1?-15:-8,state.category==1?15:8));
 }
}

Recipient Identity(CBasePlayer *p)
{
	return p ? Recipient { GETPLAYERUSERID(p->edict()), int(p->m_iTeam), bool(p->IsAlive()) } : Recipient {};
}

Slot &State(CBasePlayer *p)
{
	auto &state = slots[p->entindex()];
	int id = GETPLAYERUSERID(p->edict());
	if (state.userId != id) { state = Slot {}; state.userId = id; }
	return state;
}

bool CombatTeam(CBasePlayer *p) { return p && (p->m_iTeam == CT || p->m_iTeam == TERRORIST); }
bool Ffa() { return CSGameRules()->IsFreeForAll(); }
bool Flag(const char *name) { return CVAR_GET_FLOAT(name) > 0; }

bool CanHear(CBasePlayer *bot, CBasePlayer *human, bool teamOnly)
{
	return CombatTeam(bot) && human && !(human->pev->flags & (FL_FAKECLIENT | FL_PROXY))
		&& (!teamOnly || (!Ffa() && bot->m_iTeam == human->m_iTeam))
		&& (bool(bot->IsAlive()) == bool(human->IsAlive()))
		&& !CSGameRules()->m_VoiceGameMgr.PlayerHasBlockedPlayer(human, bot);
}

bool Queue(CCSBot *bot, CBasePlayer *human, bool teamOnly, const char *line, bool tactical = false)
{
	if (!textChat.value || !CombatTeam(bot) || (teamOnly && Ffa())) return false;
	auto &state = State(bot);
	if (state.reply.active || gpGlobals->time < state.nextChat) return false;
	if (human && !CanHear(bot, human, teamOnly)) return false;
	Reply reply;
	reply.active = true;
	reply.teamOnly = teamOnly;
	reply.tactical = tactical;
	reply.speaker = Identity(bot);
	reply.listener = Identity(human);
	reply.listenerSlot = human ? human->entindex() : 0;
	reply.issued = gpGlobals->time;
	reply.typingStart = gpGlobals->time + 0.25f;
	reply.due = gpGlobals->time + CompetitiveBotPolicy::ReplyDelay(Q_strlen(line), RANDOM_FLOAT(0.25f, 0.65f));
	reply.deadline = gpGlobals->time + 4.0f;
	reply.round = roundSerial;
	Q_strlcpy(reply.text, line);
	state.reply = reply;
	state.nextChat = gpGlobals->time + (human ? 2.0f : 10.0f);
	return true;
}

// One outstanding conversational request; native orders always remain authoritative.
char aiDirectory[512] = {};
float aiBusyUntil = 0;
unsigned aiSequence = 0;
void AiPath(char *out, size_t size, const char *file) {
 Q_snprintf(out, size, "%s/%s", aiDirectory, file);
}
bool AiDirectory() {
 if (*aiDirectory) return true;
 char game[256]; GET_GAME_DIR(game);
 Q_snprintf(aiDirectory, sizeof(aiDirectory), "%s/addons", game);
#ifdef _WIN32
 _mkdir(aiDirectory);
#else
 mkdir(aiDirectory, 0700);
#endif
 Q_strlcat(aiDirectory, "/regamedll");
#ifdef _WIN32
 _mkdir(aiDirectory);
#else
 mkdir(aiDirectory, 0700);
#endif
 Q_strlcat(aiDirectory, "/ai");
#ifdef _WIN32
 _mkdir(aiDirectory);
#else
 mkdir(aiDirectory, 0700);
#endif
 return true;
}
void CleanName(CBasePlayer *player, char (&out)[64]) {
 const char *name=player?STRING(player->pev->netname):"teammate";
 size_t used=0;
 for(const unsigned char *c=reinterpret_cast<const unsigned char *>(name);*c && used<63;++c)
  if(*c>=32 && *c!=127 && *c!='"' && *c!=';' && *c!='\\' && *c!='%' && *c!='{' && *c!='}') out[used++]=*c;
 out[used]=0;
 if(!used) Q_strlcpy(out,"teammate");
}
void AskAi(CCSBot *bot, CBasePlayer *human, bool teamOnly, const char *text, const char *kind="chat", CBasePlayer *subject=nullptr) {
 if (!aiBridge.value || !textChat.value || Completed() || gpGlobals->time < aiBusyUntil) return;
 auto &reply = State(bot).reply;
 if (!reply.active || !AiDirectory()) return;
 char clean[97] = {}; size_t used = 0;
 for (const unsigned char *c = reinterpret_cast<const unsigned char *>(text); *c && used < 96; ++c)
  if (*c >= 32 && *c != 127) clean[used++] = *c;
 char token[64]; Q_snprintf(token, sizeof(token), "%08x-%08x-%u", unsigned(RANDOM_LONG(1, 0x7fffffff)), unsigned(RANDOM_LONG(1, 0x7fffffff)), ++aiSequence);
 char tmp[600], target[600]; AiPath(tmp, sizeof(tmp), "request.tmp"); AiPath(target, sizeof(target), "request.tsv");
 FILE *f = fopen(tmp, "wb"); if (!f) return;
 char playerName[64],botName[64],subjectName[64];
 CleanName(human,playerName);CleanName(bot,botName);CleanName(subject?subject:human,subjectName);
 int written = fprintf(f, "3\t%s\t%d\t%d\t%s\t%s\t%d\t%s\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%u-%d-%d-%d-%d\n", token,
  bot->entindex()-1, human?human->entindex()-1:subject?subject->entindex()-1:bot->entindex()-1,
  teamOnly ? "team" : "all", Ffa() ? "ffa" : "teams", int(bot->m_iTeam), STRING(gpGlobals->mapname),
  int(bot->pev->health), bot->m_iAccount, clean, kind, playerName, botName, subjectName, roundSerial, GETPLAYERUSERID(bot->edict()), human?GETPLAYERUSERID(human->edict()):0,bot->IsAlive(),human?human->IsAlive():0);
 bool closed = fclose(f) == 0;
#ifdef _WIN32
 remove(target);
#endif
 if (written <= 0 || !closed || rename(tmp, target)) { remove(tmp); return; }
 Q_strlcpy(reply.aiToken, token);
 reply.due = gpGlobals->time + 7.0f;
 reply.typingStart = reply.due - 1.5f;
 reply.deadline = gpGlobals->time + 9.0f;
 aiBusyUntil = gpGlobals->time + 8.0f;
}
void PollAi() {
 if (!aiBridge.value || !*aiDirectory) return;
 char path[600]; AiPath(path, sizeof(path), "reply.tsv");
 FILE *f = fopen(path, "rb"); if (!f) return;
 char data[256] = {}; size_t n = fread(data, 1, sizeof(data)-1, f);
 bool valid = !ferror(f) && feof(f); fclose(f); remove(path);
 if (!valid || !n || memchr(data, 0, n)) return;
 if (data[n-1] == '\n') data[--n] = 0;
 char *tab = strchr(data, '\t'); if (!tab) return;
 *tab++ = 0; size_t len = strlen(tab);
 if (!len || len > 90) return;
 for (const unsigned char *c = reinterpret_cast<unsigned char *>(tab); *c; ++c)
  if (*c < 32 || *c == 127 || *c == '"' || *c == ';' || *c == '\\' || *c == '%') return;
 for (auto &slot : slots) {
  auto &r = slot.reply;
  if (r.active && *r.aiToken && !strcmp(r.aiToken, data) && gpGlobals->time <= r.deadline) {
   Q_strlcpy(r.text, tab);
   const float typed = r.issued + CompetitiveBotPolicy::ReplyDelay(len, RANDOM_FLOAT(0.25f, 0.65f));
   const float received = gpGlobals->time + RANDOM_FLOAT(0.45f, 0.8f);
   r.due = typed > received ? typed : received;
   r.typingStart = gpGlobals->time;
   r.aiToken[0] = 0; break;
  }
 }
}

void TeamLine(TeamName team, const char *line, bool tactical = false)
{
	if (Ffa() || gpGlobals->time < nextTeamChat[team]) return;
	for (int pass = 0; pass < 2; ++pass) for (int i = 1; i <= gpGlobals->maxClients; ++i) {
		auto *p = Player(i);
		if (p && p->IsBot() && bool(p->IsAlive()) == (pass == 0) && p->m_iTeam == team
			&& Queue(static_cast<CCSBot *>(p), nullptr, true, line, tactical)) {
			nextTeamChat[team] = gpGlobals->time + 3.0f;
			return;
		}
	}
}

bool Eligible(CCSBot *bot, CBasePlayer *human)
{
	return bot->m_iTeam == human->m_iTeam && bot->IsAlive()
		&& !bot->IsAttacking() && !bot->IsDefusingBomb()
		&& !bot->m_bIsVIP && bot->GetTask() != CCSBot::RESCUE_HOSTAGES
		&& bot->GetTask() != CCSBot::PLANT_BOMB && !bot->IsEscapingFromBomb();
}

// Resolve only actual NAV place names. Never guess which unlabelled zone is A/B.
CNavArea *Destination(TeamOrder order, const Vector &origin)
{
	const char *name = nullptr;
	switch (order) {
	case TeamOrder::LeaveB: case TeamOrder::GoA: case TeamOrder::AllA: case TeamOrder::RushA: case TeamOrder::RotateA: name = "BombsiteA"; break;
	case TeamOrder::LeaveA: case TeamOrder::GoB: case TeamOrder::AllB: case TeamOrder::RushB: case TeamOrder::RotateB: name = "BombsiteB"; break;
	case TeamOrder::AllMid: case TeamOrder::RushMid: case TeamOrder::WatchMid: name = "Middle"; break;
	case TeamOrder::WatchApps: name = "Apartments"; break;
	default: return nullptr;
	}
	CNavArea *best = nullptr;
	float distance = 1e30f;
	for (auto *area : TheNavAreaList) {
		const char *place = TheNavAreaGrid.IDToName(area->GetPlace());
		if (!place || Q_stricmp(place, name)) continue;
		float candidate = (*area->GetCenter() - origin).LengthSquared();
		if (candidate < distance) { best = area; distance = candidate; }
	}
 if(!best && name)for(int i=0;i<calloutCount;++i)if(!Q_stricmp(name,callouts[i].name)) {
  auto *area=TheNavAreaGrid.GetNearestNavArea(&callouts[i].point);
  if(area && (*area->GetCenter()-callouts[i].point).LengthSquared()<256*256)best=area;
 }
	return best;
}

bool Apply(CCSBot *bot, CBasePlayer *human, TeamOrder order, const char *text)
{
	if (!Eligible(bot, human)) return false;
	if (auto *area = Destination(order, bot->pev->origin)) {
		if (bot->IsBuying() || !bot->GetLastKnownArea()) return false;
		ShortestPathCost cost;
		if (NavAreaTravelDistance(bot->GetLastKnownArea(), area, cost) < 0) return false;
		bot->StopFollowing();
		bot->SetTask(CCSBot::SEEK_AND_DESTROY);
		bot->MoveTo(area->GetCenter(), FASTEST_ROUTE);
  ProtectHumanOrder(bot,45);
  if(order==TeamOrder::WatchMid || order==TeamOrder::WatchApps){auto &w=watches[bot->entindex()];w.id=GETPLAYERUSERID(bot->edict());w.goal=*area->GetCenter();w.expires=gpGlobals->time+45;}
		return true;
	}
	switch (order) {
	case TeamOrder::FollowMe: case TeamOrder::Trade:
		if (bot->IsBuying()) return false;
		bot->Follow(human); ProtectHumanOrder(bot); return true;
 case TeamOrder::Execute: case TeamOrder::Push: case TeamOrder::Rush: case TeamOrder::Swing: {
  Vector look=human->pev->v_angle;UTIL_MakeVectors(look);TraceResult tr;
  UTIL_TraceLine(human->EyePosition(),human->EyePosition()+gpGlobals->v_forward*600,ignore_monsters,human->edict(),&tr);
  Vector point=tr.vecEndPos;auto *area=TheNavAreaGrid.GetNearestNavArea(&point);if(!area || !bot->GetLastKnownArea())return false;
  ShortestPathCost cost;if(NavAreaTravelDistance(bot->GetLastKnownArea(),area,cost)<0)return false;
  bot->StopFollowing();bot->ForceRun(8);bot->MoveTo(area->GetCenter(),FASTEST_ROUTE);ProtectHumanOrder(bot);return true;
 }
 case TeamOrder::WatchBack: {
  if(bot->IsBuying())return false;Vector angle=human->pev->v_angle;angle.y+=180;UTIL_MakeVectors(angle);Vector back=human->EyePosition()+gpGlobals->v_forward*1000;
  if(!bot->TryToHide(bot->GetLastKnownArea(),45,150,true,true))return false;bot->SetLookAt("WatchBack",&back,PRIORITY_HIGH,45,false,5);ProtectHumanOrder(bot,45);return true;
 }
 case TeamOrder::Rotate: case TeamOrder::Retake: {
  const Vector *bomb=bot->GetGameState()->GetBombPosition();if(bot->m_iTeam!=CT || !bot->GetGameState()->IsBombPlanted() || !bomb)return false;
  auto *area=TheNavAreaGrid.GetNearestNavArea(bomb);if(!area)return false;bot->MoveTo(area->GetCenter(),FASTEST_ROUTE);ProtectHumanOrder(bot);return true;
 }
	case TeamOrder::Hold: case TeamOrder::HoldForMe: case TeamOrder::PlaySafe: case TeamOrder::Save:
		if (bot->IsBuying()) return false;
		ProtectHumanOrder(bot,15);return bot->TryToHide(bot->GetLastKnownArea(), 15.0f, 300.0f, true, true);
	case TeamOrder::Jump:
		return !bot->IsBuying() && (bot->pev->flags & FL_ONGROUND) && bot->Jump();
 case TeamOrder::Drop: case TeamOrder::DropWeapon:
  return RequestedDrop(bot,human,parseRequestedDropWeapon(text));

	default: return false;
	}
}

struct CaptainPlan {
 bool active=false,human=false;
 int leaderSlot=0,leaderId=0;
 unsigned revision=0, applied[MAX_CLIENTS+1]={};
 int ids[MAX_CLIENTS+1]={};
 TeamOrder roles[MAX_CLIENTS+1]={};
 float ready=0,nextDrive=0,stageStarted=0,slowUntil=0;
 bool staged=false;TeamOrder finish=TeamOrder::None;
};
CaptainPlan plans[4];
unsigned planRevision=0;
void ResetCaptains() {for(auto &p:plans) {p=CaptainPlan {};p.ready=gpGlobals->time+3;}}
bool PlanMember(CCSBot *bot) {
 return bot->IsAlive() && !bot->IsDefusingBomb() && !bot->IsEscapingFromBomb() && !bot->m_bIsVIP
  && bot->GetTask()!=CCSBot::PLANT_BOMB && bot->GetTask()!=CCSBot::RESCUE_HOSTAGES;
}
CCSBot *ElectCaptain(TeamName team) {
 CCSBot *best=nullptr;
 for(int i=1;i<=gpGlobals->maxClients;++i) {
  auto *p=Player(i);
  if(p && p->IsBot() && p->IsAlive() && p->m_iTeam==team
     && (!best || p->pev->frags>best->pev->frags)) best=static_cast<CCSBot *>(p);
 }
 return best;
}
bool MakePlan(CBasePlayer *leader,bool human,TeamAllocation allocation,TeamOrder single=TeamOrder::None,TeamOrder finish=TeamOrder::None) {
 auto team=leader->m_iTeam;CaptainPlan next;
 next.active=true;next.human=human;next.leaderSlot=leader->entindex();next.leaderId=GETPLAYERUSERID(leader->edict());
 next.revision=++planRevision;next.finish=finish;next.staged=finish!=TeamOrder::None;next.stageStarted=gpGlobals->time;
 int humans=0,bots=0;
 for(int i=1;i<=gpGlobals->maxClients;++i) {
  auto *p=Player(i);if(!p || !p->IsAlive() || p->m_iTeam!=team) continue;
  if(!p->IsBot()) ++humans;else ++bots;
 }
 int counts[3]={allocation.a,allocation.b,allocation.mid};
 TeamOrder routes[3]={TeamOrder::GoA,TeamOrder::GoB,TeamOrder::AllMid};
 if(single==TeamOrder::None) {
  if(allocation.a+allocation.b+allocation.mid!=humans+bots) return false;
  int largest=0;for(int i=1;i<3;++i) if(counts[i]>counts[largest]) largest=i;
  // Team split counts include living humans. Assign their intended group first;
  // human players remain free to move and are never controlled by the bot code.
  for(int i=0;i<humans;++i) {
   int selected=counts[largest]>0?largest:counts[0]>0?0:counts[1]>0?1:2;
   if(counts[selected]>0)--counts[selected];
  }
 }
 for(int scan=0;scan<=gpGlobals->maxClients;++scan) {
  int i=scan==0?leader->entindex():scan;
  if(scan!=0 && i==leader->entindex())continue;
  auto *p=Player(i);if(!p || !p->IsBot() || !p->IsAlive() || p->m_iTeam!=team) continue;
  TeamOrder order=single;
  if(order==TeamOrder::None) {
   int group=counts[0]>0?0:counts[1]>0?1:2;
   if(!human && p==leader) for(int route=0;route<3;++route) if(counts[route]>counts[group])group=route;
   if(counts[group]<=0) continue;
   order=routes[group];--counts[group];
  }
  auto *bot=static_cast<CCSBot *>(p);auto *area=Destination(order,bot->pev->origin);
  if(!area && !human) return false;
  if(finish!=TeamOrder::None) {
   auto *target=Destination(finish,p->pev->origin);if(!target)return false;
   ShortestPathCost cost;if(area && NavAreaTravelDistance(area,target,cost)<0)return false;
  }
  // A human call can fall back to following that human when place labels are absent.
  if(area && bot->GetLastKnownArea()) {
   ShortestPathCost cost;if(NavAreaTravelDistance(bot->GetLastKnownArea(),area,cost)<0) return false;
  }
  next.ids[i]=GETPLAYERUSERID(p->edict());next.roles[i]=order;
 }
 // Keep the C4 in a site group; swap roles rather than changing the split counts.
 for(int i=1;i<=gpGlobals->maxClients;++i) {
  auto *p=Player(i);if(!p || !p->IsBot() || !p->IsAlive() || p->m_iTeam!=team || !static_cast<CCSBot *>(p)->IsCarryingBomb())continue;
  if(next.roles[i]!=TeamOrder::AllMid || next.staged)continue;
  int replacement=0;
  for(int j=1;j<=gpGlobals->maxClients;++j)if(next.ids[j] && (next.roles[j]==TeamOrder::GoA || next.roles[j]==TeamOrder::GoB)) {
   if(!replacement || (next.roles[j]==TeamOrder::GoA && allocation.a>=allocation.b))replacement=j;
  }
  if(replacement){next.roles[i]=next.roles[replacement];next.roles[replacement]=TeamOrder::AllMid;}
  else next.roles[i]=allocation.b>allocation.a?TeamOrder::GoB:TeamOrder::GoA;
 }
 plans[team]=next;return true;
}
bool HumanCaptainCall(CBasePlayer *human,const char *text) {
 if(!captainMode.value || !Enabled() || Completed() || Ffa() || !human->IsAlive()) return false;
 char phrase[97];bool question=false;
 if(!normalizeTeamPhrase(text,phrase,question) || question) return false;
 TeamAllocation allocation;
 bool split=parseTeamAllocation(text,allocation);
 TeamOrder order=parseTeamOrder(text);
 if(!Q_strcmp(phrase,"a"))order=TeamOrder::GoA;
 if(!Q_strcmp(phrase,"b"))order=TeamOrder::GoB;
 bool site=order==TeamOrder::GoA || order==TeamOrder::GoB || order==TeamOrder::AllA || order==TeamOrder::AllB
  || order==TeamOrder::RushA || order==TeamOrder::RushB || order==TeamOrder::RotateA || order==TeamOrder::RotateB;
 if(!split && !site) return false;
 bool accepted=MakePlan(human,true,allocation,split?TeamOrder::None:order);
 if(accepted){humanCaptainUntil[human->m_iTeam]=gpGlobals->time+600;humanLeaderSlot[human->m_iTeam]=human->entindex();humanLeaderId[human->m_iTeam]=GETPLAYERUSERID(human->edict());}
 auto *bot=ElectCaptain(human->m_iTeam);
 if(bot) Queue(bot,human,true,accepted?"roger, moving together":"that split isn't available",true);
 return true;
}
void MaintainCaptains() {
 if(!captainMode.value || Completed() || Ffa() || CSGameRules()->m_iRoundWinStatus!=WINSTATUS_NONE
    || TheCSBots()->IsBombPlanted() || TheCSBots()->GetScenario()!=CCSBotManager::SCENARIO_DEFUSE_BOMB) return;
 for(auto team:{TERRORIST,CT}) {
  auto &plan=plans[team];
  if(CSGameRules()->IsFreezePeriod()) {plan.ready=gpGlobals->time+2;continue;}
  auto *leader=Player(plan.leaderSlot);
  if(plan.active && (!leader || !leader->IsAlive() || leader->m_iTeam!=team || GETPLAYERUSERID(leader->edict())!=plan.leaderId)) {
   plan=CaptainPlan {};plan.ready=gpGlobals->time+2;
  }
  if(!plan.active) {
   if(gpGlobals->time<plan.ready || HasHumanCaptain(team) || gpGlobals->time-lastHumanChat[team]<12)continue;
   auto *captain=ElectCaptain(team);if(!captain)continue;
   int alive=0;for(int i=1;i<=gpGlobals->maxClients;++i) {auto *p=Player(i);if(p && p->IsAlive() && p->m_iTeam==team)++alive;}
   if(alive<2)continue;
   int style=RANDOM_LONG(0,2);TeamAllocation split;
   split.a=(alive*3+4)/5;split.b=style==1?0:alive-split.a;split.mid=alive-split.a-split.b;
   if(style==2 && split.b>1){--split.b;++split.mid;}
   bool accepted=false;char line[64]={};
   bool playingTime=team==TERRORIST && RANDOM_LONG(0,99)<10;
   if(team==TERRORIST) {
    int budget=0;for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(p && p->IsAlive() && p->m_iTeam==team && !p->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] && p->m_iAccount<=2000)++budget;}
    bool economical=eco[team] || TeamEco(team) || budget*2>=alive;
    auto attack=chooseAttackPlan(economical,CSGameRules()->m_iNumConsecutiveTerroristLoses,playingTime,RANDOM_LONG(0,99),RANDOM_LONG(0,3));
    if(attack!=AttackPlan::Split) {
     bool a=attack==AttackPlan::AllA || attack==AttackPlan::MidToA;
     bool mid=attack==AttackPlan::MidToA || attack==AttackPlan::MidToB;
     auto finish=a?TeamOrder::GoA:TeamOrder::GoB;
     split=a?TeamAllocation {alive,0,0}:TeamAllocation {0,alive,0};
     accepted=MakePlan(captain,false,split,mid?TeamOrder::AllMid:finish,mid?finish:TeamOrder::None);
     if(accepted)Q_snprintf(line,sizeof(line),mid?(a?"all mid to A":"all mid to B"):(a?"all A":"all B"));
    } else accepted=MakePlan(captain,false,split);
   } else accepted=MakePlan(captain,false,split);
   if(!accepted) {split={alive,0,0};accepted=MakePlan(captain,false,split);}
   if(!accepted) {split={0,alive,0};accepted=MakePlan(captain,false,split);}
   if(!accepted) {plan.ready=gpGlobals->time+5;continue;}
   if(!line[0])formatTeamAllocation(split,line,sizeof(line));
   if(playingTime)plan.slowUntil=gpGlobals->time+12;
   AnnouncePlan(captain,line);
   leader=captain;
  }
  if(gpGlobals->time<plan.nextDrive)continue;
  plan.nextDrive=gpGlobals->time+1;
  if(plan.slowUntil>0 && gpGlobals->time>=plan.slowUntil) {
   plan.slowUntil=0;
   for(int slot=1;slot<=gpGlobals->maxClients;++slot){auto *p=Player(slot);if(p && p->IsBot() && p->IsAlive() && p->m_iTeam==team && GETPLAYERUSERID(p->edict())==plan.ids[slot])static_cast<CCSBot *>(p)->Run();}
  }
  if(plan.staged) {
   int members=0,nearby=0;bool bombNearby=true;
   for(int slot=1;slot<=gpGlobals->maxClients;++slot) {
    auto *p=Player(slot);if(!p || !p->IsAlive() || !p->IsBot() || p->m_iTeam!=team || GETPLAYERUSERID(p->edict())!=plan.ids[slot])continue;
    auto *middle=Destination(TeamOrder::AllMid,p->pev->origin);bool near=middle && (*middle->GetCenter()-p->pev->origin).LengthSquared()<400*400;
    ++members;if(near)++nearby;if(static_cast<CCSBot *>(p)->IsCarryingBomb() && !near)bombNearby=false;
   }
   if(midStageReady(members,nearby,bombNearby,gpGlobals->time-plan.stageStarted)) {
    plan.staged=false;plan.revision=++planRevision;
    for(int slot=1;slot<=MAX_CLIENTS;++slot)if(plan.ids[slot])plan.roles[slot]=plan.finish;
   }
  }
  for(int i=1;i<=gpGlobals->maxClients;++i) {
   auto *p=Player(i);if(!p || !p->IsBot() || p->m_iTeam!=team || GETPLAYERUSERID(p->edict())!=plan.ids[i]) continue;
   auto *bot=static_cast<CCSBot *>(p);
   if(bot->IsCarryingBomb() && (!plan.staged || bot->IsAtBombsite())) {
    if(!bot->IsAlive() || bot->IsAttacking() || bot->IsEnemyVisible() || bot->GetTimeSinceAttacked()<2
       || bot->IsBuying() || IsManagingGrenade(bot))continue;
    if(bot->IsAtBombsite()) {
     if(bot->GetTask()!=CCSBot::PLANT_BOMB){bot->StopFollowing();bot->SetTask(CCSBot::PLANT_BOMB);bot->PlantBomb();}
     continue;
    }
    // Once committed, let native planting/path recovery finish without reissuing MoveTo.
    if(bot->GetTask()==CCSBot::PLANT_BOMB && bot->IsMovingTo()
       && plan.applied[i]==plan.revision)continue;
    auto *start=bot->GetLastKnownArea();if(!start)continue;
    auto *site=Destination(plan.roles[i],bot->pev->origin);
    ShortestPathCost cost;
    const auto *zone=TheCSBots()->GetClosestZone(site?site:start,cost);
    const Vector *point=zone?TheCSBots()->GetRandomPositionInZone(zone):nullptr;
    if(!point)continue;
    auto *goal=TheNavAreaGrid.GetNearestNavArea(point);
    if(!goal || NavAreaTravelDistance(start,goal,cost)<0)continue;
    bot->StopFollowing();bot->SetTask(CCSBot::PLANT_BOMB);bot->Run();bot->MoveTo(point,FASTEST_ROUTE);
    plan.applied[i]=plan.revision;continue;
   }
   if((!PlanMember(bot) && !(plan.staged && bot->IsCarryingBomb() && bot->IsAlive())) || (HasHumanOrder(bot) && !plan.human) || bot->IsAttacking() || bot->IsEnemyVisible() || bot->GetTimeSinceAttacked()<2
      || (bot->IsBuying() && bot->CanPlayerBuy(false)) || IsManagingGrenade(bot) || PauseForTyping(bot))continue;
   auto *area=Destination(plan.roles[i],bot->pev->origin);
   if(!area) {if(plan.human && leader && bot!=leader && !bot->IsFollowing())bot->Follow(leader);continue;}
   if((*area->GetCenter()-bot->pev->origin).LengthSquared()<180*180) continue;
   // Main groups stay close to their route's leader instead of trickling in alone.
   CCSBot *groupLeader=nullptr;
   for(int j=1;j<=gpGlobals->maxClients;++j) {
    auto *q=Player(j);
    if(q && q->IsBot() && q->IsAlive() && q->m_iTeam==team && plan.roles[j]==plan.roles[i]
       && GETPLAYERUSERID(q->edict())==plan.ids[j] && (!groupLeader || q->pev->frags>groupLeader->pev->frags))groupLeader=static_cast<CCSBot *>(q);
   }
   if(groupLeader && groupLeader!=bot && (groupLeader->pev->origin-bot->pev->origin).LengthSquared()>500*500)bot->Follow(groupLeader);
   else {
    if(plan.applied[i]==plan.revision && bot->IsMovingTo())continue;
    if(!bot->GetLastKnownArea())continue;
    ShortestPathCost cost;if(NavAreaTravelDistance(bot->GetLastKnownArea(),area,cost)<0)continue;
    bot->StopFollowing();bot->SetTask(CCSBot::SEEK_AND_DESTROY);
    if(plan.slowUntil>gpGlobals->time)bot->Walk();else bot->Run();
    bot->MoveTo(area->GetCenter(),plan.slowUntil>gpGlobals->time?SAFEST_ROUTE:FASTEST_ROUTE);
   }
   plan.applied[i]=plan.revision;
  }
 }
}

bool Word(const char *text, const char *word)
{
	size_t length = Q_strlen(word);
	if (!length) return false;
	for (const char *p = text; (p = Q_strstr(p, word)) != nullptr; ++p)
		if ((p == text || p[-1] == ' ') && (p[length] == 0 || p[length] == ' ')) return true;
	return false;
}
}

bool InitiatesChat(CCSBot *bot) {
 char *info=GET_INFO_BUFFER(bot->edict());const char *style=g_engfuncs.pfnInfoKeyValue(info,"_16c_style");
 if(style && !Q_strcmp(style,"quiet"))return false;
 int role=0;for(int i=1;i<bot->entindex();++i){auto *p=Player(i);if(p && p->IsBot() && p->m_iTeam==bot->m_iTeam)++role;}
 return role<2;
}
bool AnnouncePlan(CCSBot *bot,const char *line) {
 if(InitiatesChat(bot)) {
  if(!Queue(bot,nullptr,true,line,true))return false;
  State(bot).reply.planAnnouncement=true;return true;
 }
 bot->Radio("%!MRAD_GO","#Go_go_go");return true;
}
bool Speak(CCSBot *bot,CBasePlayer *listener,bool teamOnly,const char *line,bool tactical) {
 return bot && (listener || InitiatesChat(bot)) && Queue(bot,listener,teamOnly,line,tactical);
}
bool ReactToSpree(CBasePlayer *player,int kills) {
 if(!Enabled() || Completed() || !roundChat.value || !textChat.value || !CombatTeam(player))return false;
 int channel=Ffa()?0:int(player->m_iTeam);if(gpGlobals->time<nextSpreeChat[channel])return false;
 char name[64],line[128];CleanName(player,name);
 // Personal reactions are separate from the two tactical reporter roles.
 for(int pass=0;pass<2;++pass)for(int i=1;i<=gpGlobals->maxClients;++i){
  auto *p=Player(i);if(!p || !p->IsBot() || !p->IsAlive() || (!Ffa() && p->m_iTeam!=player->m_iTeam) || ((p==player)!=(pass==1)))continue;
  char *info=GET_INFO_BUFFER(p->edict());const char *style=g_engfuncs.pfnInfoKeyValue(info,"_16c_style");if(style && !Q_strcmp(style,"quiet"))continue;
  if(p==player)Q_snprintf(line,sizeof(line),Ffa()?"%d in a row":"got %d this round",kills);
  else if(!Ffa() && kills>=5)Q_snprintf(line,sizeof(line),"nice ace, %s!",name);
  else Q_snprintf(line,sizeof(line),"nice %dk, %s!",kills,name);
  if(Queue(static_cast<CCSBot *>(p),nullptr,!Ffa(),line,false)){nextSpreeChat[channel]=gpGlobals->time+8;return true;}
 }
 return false;
}
bool HasHumanCaptain(TeamName team) {
 if(team<1 || team>2)return false;
 int slot=plans[team].human?plans[team].leaderSlot:humanLeaderSlot[team],id=plans[team].human?plans[team].leaderId:humanLeaderId[team];
 auto *human=Player(slot);return human && !human->IsBot() && human->IsAlive() && human->m_iTeam==team && GETPLAYERUSERID(human->edict())==id && (plans[team].human || gpGlobals->time<humanCaptainUntil[team]);
}
bool Tactical(CCSBot *bot,const char *trigger,const char *radio) {
 if(!Enabled() || Completed() || Ffa() || !bot || !bot->IsAlive())return false;
 int team=bot->m_iTeam,index=tacticalPhraseIndex(trigger);
 if(team<1 || team>2 || !tacticalGate[team].available(gpGlobals->time,index))return false;
 int role=0;for(int i=1;i<bot->entindex();++i){auto *p=Player(i);if(p && p->IsBot() && p->IsAlive() && p->m_iTeam==team)++role;}
 bool chat=InitiatesChat(bot) && (role==0 || (role==1 && RANDOM_LONG(0,1))) && bot->GetTimeSinceAttacked()>=2;
 bool sent=false;
 if(chat && index>=0){int choice=(tacticalGate[team].previousVariant[index]+RANDOM_LONG(1,2))%3;sent=Queue(bot,nullptr,true,tacticalPhrases[index].variants[choice],true);if(sent)tacticalGate[team].previousVariant[index]=choice;}
 else {bot->Radio(radio);sent=true;}
 if(sent){tacticalGate[team].record(gpGlobals->time,index);if(chat)AskAi(bot,nullptr,true,State(bot).reply.text,"event");}
 return sent;
}
bool PingComplaint(CCSBot *bot,bool death) {
 if(!ShowsLatency() || !roundChat.value || gpGlobals->time<pingComplaintNext)return false;
 auto &s=latency[bot->entindex()];char *info=GET_INFO_BUFFER(bot->edict());
 const char *style=g_engfuncs.pfnInfoKeyValue(info,"_16c_style");
 int personality=bot->GetProfile()->GetAggression()<.35f?2:bot->GetProfile()->GetAggression()>.7f?1:0;
 auto mood=pingTemperamentForStyle(s.temperament,personality,style?style:"");
 if(!shouldComplainAboutPing(mood,s.ping,death,gpGlobals->time,s.nextComplaint,RANDOM_LONG(0,99)))return false;
 char line[80];Q_snprintf(line,sizeof(line),"%d ping, rough timing",s.ping);
 if(!Queue(bot,nullptr,!death && !Ffa(),line))return false;
 s.nextComplaint=gpGlobals->time+RANDOM_FLOAT(90,150);pingComplaintNext=gpGlobals->time+30;return true;
}
void LoadAliases() {
 aliasCount=0;char dir[MAX_PATH],path[600];GET_GAME_DIR(dir);Q_snprintf(path,sizeof(path),"%s/addons/regamedll/team_aliases.cfg",dir);
 FILE *file=fopen(path,"rb");if(!file)return;
 char line[256];size_t bytes=0;
 while(aliasCount<256 && fgets(line,sizeof(line),file) && (bytes+=strlen(line))<=32768) {
  if(line[0]=='#' || line[0]==';' || !Q_strncmp(line,"//",2))continue;
  char *eq=strchr(line,'=');if(!eq)continue;*eq++=0;eq[strcspn(eq,"\r\n")]=0;
  bool question=false;Alias parsed;
  if(!normalizeTeamPhrase(line,parsed.from,question) || question || !normalizeTeamPhrase(eq,parsed.to,question) || question || parseTeamOrder(parsed.to)==TeamOrder::None)continue;
  aliases[aliasCount++]=parsed;
 }
 fclose(file);
}

void RegisterCVars()
{
	RegisterGrenadeCVars(); RegisterTacticsCVars(); LoadAliases();
	CVAR_REGISTER(&highPing);CVAR_REGISTER(&mediumPing);CVAR_REGISTER(&combatMovement); CVAR_REGISTER(&captainMode); CVAR_REGISTER(&pingComms); CVAR_REGISTER(&showLatency); CVAR_REGISTER(&pingMin); CVAR_REGISTER(&pingMax);
	CVAR_REGISTER(&aiBridge); CVAR_REGISTER(&queryHook); CVAR_REGISTER(&enabled); CVAR_REGISTER(&teamOrders); CVAR_REGISTER(&textChat); CVAR_REGISTER(&roundChat);
}
bool ShowsLatency() { return showLatency.value > 0; }
void UpdateLatency(const edict_t *recipient) {
 // Some clients open the scoreboard entirely client-side without forwarding IN_SCORE.
 // Keep their ping cache fresh with a bounded update instead of relying on that bit.
 if (!ShowsLatency() || !recipient || recipient->free || !recipient->pvPrivateData
     || (recipient->v.flags & (FL_FAKECLIENT | FL_PROXY))) return;
 int slot = ENTINDEX(recipient);
 if (slot < 1 || slot > MAX_CLIENTS) return;
 auto &state = latency[slot];
 if (gpGlobals->time < state.nextSend && state.nextSend - gpGlobals->time < 1) return;
 state.nextSend = gpGlobals->time + 0.25f;
 CompetitivePing::Packet packet;
 for (int i=1; i<=gpGlobals->maxClients && i<=MAX_CLIENTS; ++i) {
  auto *bot = Player(i);
  if (bot && bot->IsBot() && latency[i].userId == GETPLAYERUSERID(bot->edict()) && latency[i].ping > 0)
   packet.Add(unsigned(i-1), unsigned(latency[i].ping));
 }
 if (!packet.count) return;
 size_t length = packet.Finish();
 constexpr int svcPings = 17;
 MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, svcPings, nullptr, const_cast<edict_t *>(recipient));
 for (size_t i=0; i<length; ++i) WRITE_BYTE(packet.bytes[i]);
 MESSAGE_END();
}
bool PlayerPingsEnabled() {
 return Enabled() && pingComms.value > 0
  && Flag("competitive_player_ping") && Flag("competitive_bot_reports");
}
void ReportEnemyPing(CCSBot *bot) {
 if (!Enabled() || pingComms.value<=0 || !CombatTeam(bot)) return;
 auto &state = State(bot);
 if (gpGlobals->time < state.nextPing || gpGlobals->time < nextTeamPing[bot->m_iTeam]) return;
 auto *enemy = bot->GetEnemy();
 if (!enemy || !CombatTeam(enemy)) return;
 CompetitiveBotPolicy::Sighting sighting;
 sighting.enabled = true;
 sighting.completed = Completed();
 sighting.ffa = Ffa();
 sighting.freeze = CSGameRules()->IsFreezePeriod();
 sighting.roundOver = CSGameRules()->m_iRoundWinStatus != WINSTATUS_NONE;
 sighting.observerAlive = bot->IsAlive();
 sighting.opponentAlive = enemy->IsAlive();
 sighting.sameTeam = bot->m_iTeam == enemy->m_iTeam;
 sighting.observed = bot->IsEnemyVisible();
 sighting.protectedTarget = !TargetAllowed(enemy);
 sighting.observerFiring = ((bot->pev->button | bot->pev->oldbuttons) & IN_ATTACK) != 0;
 sighting.opponentFiring = ((enemy->pev->button | enemy->pev->oldbuttons) & IN_ATTACK) != 0;
 sighting.underPressure = bot->GetTimeSinceAttacked() < 2.0f;
 // Cheap gates precede the engine visibility trace. Memory/hearing alone never ping.
 sighting.visibleNow = true;
 if (!CompetitiveBotPolicy::SightingAllowed(sighting, gpGlobals->time, state.nextPing, nextTeamPing[bot->m_iTeam])) return;
 if (!bot->IsVisible(enemy, true)) return;
 int count=0,placeIndex=-1;const char *label=nullptr;
 auto *area=TheNavAreaGrid.GetNearestNavArea(&enemy->pev->origin);
 if(area && (*area->GetCenter()-enemy->pev->origin).Length2D()<450) {
  const char *name=TheNavAreaGrid.IDToName(area->GetPlace());
  if(name && !Q_stricmp(name,"BombsiteA")){label="a";placeIndex=0;}
  else if(name && !Q_stricmp(name,"BombsiteB")){label="b";placeIndex=1;}
  else if(name && !Q_stricmp(name,"Middle")){label="mid";placeIndex=2;}
  if(label)for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || !p->IsAlive() || p->m_iTeam==bot->m_iTeam || !TargetAllowed(p) || !bot->IsVisible(p,true))continue;
   if((p->pev->origin-enemy->pev->origin).LengthSquared()>500*500)continue;auto *other=TheNavAreaGrid.GetNearestNavArea(&p->pev->origin);if(other && other->GetPlace()==area->GetPlace())++count;}
 }
 if(!label) {
  float nearest=450*450;int selected=-1;
  for(int i=0;i<calloutCount;++i){float d=(callouts[i].point-enemy->pev->origin).LengthSquared();if(d<nearest && (!Q_stricmp(callouts[i].name,"BombsiteA") || !Q_stricmp(callouts[i].name,"BombsiteB") || !Q_stricmp(callouts[i].name,"Middle"))){nearest=d;selected=i;}}
  if(selected>=0){const auto &callout=callouts[selected];placeIndex=!Q_stricmp(callout.name,"BombsiteA")?0:!Q_stricmp(callout.name,"BombsiteB")?1:2;label=placeIndex==0?"a":placeIndex==1?"b":"mid";
   for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(p && p->IsAlive() && p->m_iTeam!=bot->m_iTeam && TargetAllowed(p) && (p->pev->origin-callout.point).LengthSquared()<450*450 && bot->IsVisible(p,true))++count;}
  }
 }
 bool reported=false;
 if(InitiatesChat(bot) && count>=2 && placeIndex>=0 && gpGlobals->time>=placeNext[bot->m_iTeam][placeIndex]) {
  char line[48];Q_snprintf(line,sizeof(line),"%d %s",count,label);reported=Queue(bot,nullptr,true,line,true);
  if(reported)placeNext[bot->m_iTeam][placeIndex]=gpGlobals->time+12;
 }
 if(reported){state.nextPing=gpGlobals->time+12;nextTeamPing[bot->m_iTeam]=gpGlobals->time+5;return;}
 if(!PlayerPingsEnabled()){bot->Radio("%!MRAD_ENEMYSPOT","#Enemy_spotted");state.nextPing=gpGlobals->time+12;nextTeamPing[bot->m_iTeam]=gpGlobals->time+5;return;}
 char command[80];
 // Same server-only endpoint as YaPB. The core revalidates this and owns marker expiry,
 // team visibility, static target location, sounds and the managed 16CPing HUD message.
 Q_snprintf(command, sizeof(command), "16competitive_bot_report %d %d 2\n", bot->entindex(), enemy->entindex());
 SERVER_COMMAND(command);
 state.nextPing = gpGlobals->time + 12.0f;
 nextTeamPing[bot->m_iTeam] = gpGlobals->time + 5.0f;
}

bool Enabled() { return enabled.value > 0; }
bool Completed() { return Enabled() && (lifecycle.completed || Flag("competitive_match_completed")); }
bool Saving(CCSBot *bot) { return Enabled() && CombatTeam(bot) && (eco[bot->m_iTeam] || TeamEco(bot->m_iTeam)); }

bool TargetAllowed(CBasePlayer *player)
{
	return player && player->IsAlive() && player->pev->takedamage != DAMAGE_NO
		&& !(player->pev->flags & (FL_NOTARGET | FL_GODMODE)) && !(player->pev->effects & EF_NODRAW)
		&& player->CSPlayer()->m_flSpawnProtectionEndTime <= gpGlobals->time;
}

bool Pause(CCSBot *bot)
{
	if (!Completed()) return false;
	CancelGrenade(bot);
	bot->StopRapidFire(); bot->ClearMovement();
	return true;
}

bool MobileCombatWeapon(CCSBot *bot)
{
 if(!Enabled() || !combatMovement.value || !bot || !bot->IsAlive() || Completed()
    || bot->IsOnLadder() || !(bot->pev->flags & FL_ONGROUND) || bot->IsBlind()
    || bot->IsReloading() || bot->IsDefusingBomb() || bot->IsEscapingFromBomb()
    || bot->m_bIsVIP || bot->GetTask()==CCSBot::PLANT_BOMB
    || bot->GetTask()==CCSBot::HOLD_POSITION || bot->GetTask()==CCSBot::SNIPING) return false;
 auto *enemy=bot->GetEnemy();auto *weapon=bot->GetActiveWeapon();
 if(!enemy || !weapon || !TargetAllowed(enemy) || !bot->IsEnemyVisible() || !bot->IsVisible(enemy,true)) return false;
 int id=weapon->m_iId;
 bool smg=id==WEAPON_MP5N || id==WEAPON_TMP || id==WEAPON_P90 || id==WEAPON_MAC10 || id==WEAPON_UMP45;
 bool shotgun=id==WEAPON_M3 || id==WEAPON_XM1014;
 float range=(enemy->pev->origin-bot->pev->origin).Length2D();
 return (smg && range>=120 && range<=900) || (shotgun && range>=100 && range<=450);
}

void BeginCombatMovement(CCSBot *bot)
{
 mobilityDecided[bot->entindex()]=mobilityPreferred[bot->entindex()]=false;
}
bool PreferMobileCombat(CCSBot *bot)
{
 // Attack entry can precede visual recognition. Decide once the engagement is visible.
 if(!MobileCombatWeapon(bot)) return false;
 const int slot=bot->entindex();
 if(!mobilityDecided[slot]) {
  mobilityDecided[slot]=true;
  mobilityPreferred[slot]=RANDOM_FLOAT(0,100)<75;
 }
 return mobilityPreferred[slot];
}

bool SafeCombatStrafe(CCSBot *bot,bool left)
{
 if(!Enabled() || !combatMovement.value) return true; // Preserve stock behavior when opted out.
 if(!bot->IsAlive() || Completed() || bot->IsOnLadder() || !(bot->pev->flags & FL_ONGROUND)
    || bot->pev->waterlevel>1 || !bot->IsEnemyVisible()) return false;
 // Probe a short sideways corridor using the player's hull, then check the floor.
 float yaw=(bot->pev->v_angle.y+(left?90.0f:-90.0f))*float(M_PI/180.0);
 Vector side(std::cos(yaw),std::sin(yaw),0);
 Vector next=bot->pev->origin+side*72;
 TraceResult tr;
 UTIL_TraceHull(bot->pev->origin,next,dont_ignore_monsters,
                bot->IsCrouching()?head_hull:human_hull,bot->edict(),&tr);
 if(!CompetitiveBotPolicy::ClearTrace(tr.flFraction,tr.fStartSolid,tr.fAllSolid)) return false;
 UTIL_TraceLine(next,next-Vector(0,0,72),ignore_monsters,bot->edict(),&tr);
 return !tr.fStartSolid && !tr.fAllSolid && tr.flFraction<1 && tr.vecPlaneNormal.z>=.7f;
}

bool PauseForTyping(CCSBot *bot)
{
 if (!Enabled() || !bot || !bot->IsAlive() || Completed() || IsManagingGrenade(bot)
     || bot->IsOnLadder() || !(bot->pev->flags & FL_ONGROUND) || bot->IsUsingGrenade() || bot->IsBusy() || bot->IsBlind() || bot->IsEscapingFromBomb()
     || bot->IsCarryingBomb() || bot->m_bIsVIP || bot->IsEnemyVisible()
     || bot->GetTimeSinceAttacked() < 2 || (bot->pev->button & IN_ATTACK)
     || bot->GetRecognizedEnemy()) return false;
 auto &reply = State(bot).reply;
 if (!reply.active || gpGlobals->time < reply.typingStart || gpGlobals->time >= reply.due) return false;
 auto *human = Player(reply.listenerSlot);
 if (!CompetitiveBotPolicy::DeliveryCurrent(gpGlobals->time, reply.issued, reply.deadline,
    reply.round, roundSerial, reply.speaker, Identity(bot), reply.listener, Identity(human))) return false;
 bot->ResetStuckMonitor();
 bot->ClearMovement();
 return true;
}

void Reset()
{
	LoadCallouts();
 pingComplaintNext=0;for(auto &time:nextSpreeChat)time=0;for(auto &team:placeNext)for(auto &time:team)time=0;
 for(auto &time:lastHumanChat)time=-100;
	ResetGrenades(true);
	ResetCaptains();
 ResetTactics(true);
 for(auto &gate:tacticalGate)gate=TacticalCommsGate {};
 for(auto &time:humanCaptainUntil)time=0;
 for(auto &watch:watches)watch=WatchOrder {};
	for (auto &state : slots) state = Slot {};
	for (auto &save : eco) save = false;
	for (auto &time : nextTeamChat) time = 0;
	for (auto &time : nextTeamPing) time = 0;
	for (auto &time : nextComplaint) time = 0;
	lifecycle = CompetitiveBotPolicy::Lifecycle {}; nextFrame = 0;
	matchId[0] = 0; aiBusyUntil = 0; ++roundSerial;
}

void RoundRestart()
{
	ResetGrenades();
	ResetCaptains();
 ResetTactics(false);
 for(auto &time:humanCaptainUntil)time=0;
 for(auto &time:lastHumanChat)time=gpGlobals->time;
 for(auto &watch:watches)watch=WatchOrder {};
	++roundSerial;
	for (auto &state : slots) { state.reply.active = false; state.nextOrder = 0; state.nextPing = 0; }
	for (auto &time : nextTeamPing) time = 0;
	for (auto &save : eco) save = false;
}

void Frame()
{
 RefreshLatency();
#ifndef _WIN32
 // Dedicated companion receives a copied snapshot; no engine calls on network threads.
 using Publish = void (*)(int, const BotQuery::Bot *, size_t);
 static Publish publish = reinterpret_cast<Publish>(dlsym(RTLD_DEFAULT, "regamedll_query_snapshot_v1"));
 static bool warned = false;
 static float nextQuery = 0;
 static int ids[MAX_CLIENTS + 1] = {};
 static float joined[MAX_CLIENTS + 1] = {};
 if (queryHook.value > 0 && IS_DEDICATED_SERVER() && !publish && !warned) {
  ALERT(at_console, "bot_enable_query_hook needs libregamedll_query.so preloaded by the dedicated launcher.\n");
  warned = true;
 }
 if (publish && (gpGlobals->time >= nextQuery || nextQuery - gpGlobals->time > 1.0f)) {
  nextQuery = gpGlobals->time + 0.1f;
  BotQuery::Bot list[MAX_CLIENTS] = {}; size_t count = 0;
  for (int i = 1; i <= gpGlobals->maxClients && i <= MAX_CLIENTS; ++i) {
   auto *p = Player(i);
   if (!p || !p->IsBot()) { ids[i] = 0; continue; }
   int id = GETPLAYERUSERID(p->edict());
   if (ids[i] != id || joined[i] > gpGlobals->time) { ids[i] = id; joined[i] = gpGlobals->time; }
   Q_strlcpy(list[count].name, STRING(p->pev->netname));
   list[count++].seconds = gpGlobals->time - joined[i];
  }
  publish(queryHook.value > 0 && IS_DEDICATED_SERVER(), list, count);
 }
#endif
 if(TheNavAreaList.empty() && !TheCSBots()->IsLearningMap())for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(p && p->IsBot() && p->IsAlive()){TryNavigationLearning(static_cast<CCSBot *>(p));break;}}
	if (!Enabled()) { Reset(); return; }
	if (gpGlobals->time < nextFrame && nextFrame - gpGlobals->time < 1.0f) return;
	nextFrame = gpGlobals->time + 0.1f;
	PollAi();
	const char *id = CVAR_GET_STRING("competitive_match_id");
	if (Q_strcmp(id, matchId)) { Reset(); Q_strlcpy(matchId, id); }
	auto change = lifecycle.Observe(Flag("competitive_match_completed"),
		Flag("competitive_match_live") && !CSGameRules()->IsFreezePeriod(),
		int(CVAR_GET_FLOAT("competitive_bot_buy_epoch")));
	if (change.ended) {
		++roundSerial;
		for (auto &state : slots) { state.reply.active = false; state.nextChat = 0; }
		if (roundChat.value) { TeamLine(TERRORIST, "gg"); TeamLine(CT, "gg"); }
	}
	MaintainCaptains();
	if (change.greet) {
		if (roundChat.value) { TeamLine(TERRORIST, "glhf"); TeamLine(CT, "glhf"); }
	}
 UpdateSpreeReactions();
	for (int i = 1; i <= gpGlobals->maxClients; ++i) {
		auto *p = Player(i);
		if (!p || !p->IsBot()) continue;
		auto *bot = static_cast<CCSBot *>(p);
  auto &watch=watches[i];
  if(watch.id==GETPLAYERUSERID(bot->edict()) && watch.expires>gpGlobals->time && bot->IsAlive() && !bot->IsEnemyVisible() && (watch.goal-bot->pev->origin).LengthSquared()<150*150) {
   bot->TryToHide(bot->GetLastKnownArea(),watch.expires-gpGlobals->time,150,true,true);watch.expires=0;
  }
  UpdateTactics(bot);
		ReportEnemyPing(bot);
		if (change.rebuy && bot->IsAlive()) bot->RestartCompetitiveBuying();
		auto &reply = State(bot).reply;
		if (!reply.active || gpGlobals->time < reply.due) continue;
		auto *human = Player(reply.listenerSlot);
		if (!CompetitiveBotPolicy::DeliveryCurrent(gpGlobals->time, reply.issued, reply.deadline,
			reply.round, roundSerial, reply.speaker, Identity(bot), reply.listener, Identity(human))
			|| (human && !CanHear(bot, human, reply.teamOnly)) || (reply.teamOnly && Ffa())
			|| (reply.subjectSlot && !CompetitiveBotPolicy::SameRecipient(reply.subject, Identity(Player(reply.subjectSlot))))
            || (reply.tactical && Completed())) { reply.active = false; continue; }
		if (reply.tactical && (bot->IsAttacking() || bot->GetTimeSinceAttacked() < 2.0f
			|| (bot->pev->button & IN_ATTACK) || (human && (human->pev->button & IN_ATTACK)))) continue;
		// Use the normal game chat delivery path, including mute/dead/team rules and hooks.
		const char *format = reply.teamOnly ? (bot->IsAlive() ? "#Cstrike_Chat_T" : "#Cstrike_Chat_T_Dead")
			: (bot->IsAlive() ? "#Cstrike_Chat_All" : "#Cstrike_Chat_AllDead");
		reply.active = false;
		SendSayMessage(bot, reply.teamOnly ? "say_team" : "say", reply.teamOnly,
			reply.text, format, nullptr, !bot->IsAlive(), nullptr, false);
  // Independent typing delays must not let an acknowledgement overtake its order.
  if(reply.planAnnouncement && bot->IsAlive() && plans[bot->m_iTeam].active
     && plans[bot->m_iTeam].leaderId==GETPLAYERUSERID(bot->edict())) {
   char captainName[64],ack[91];CleanName(bot,captainName);
   Q_snprintf(ack,sizeof(ack),"roger, %s",captainName);
   for(int slot=1;slot<=gpGlobals->maxClients;++slot) {
    auto *member=Player(slot);
    if(!member || member==bot || !member->IsBot() || !member->IsAlive() || member->m_iTeam!=bot->m_iTeam)continue;
    auto *teammate=static_cast<CCSBot *>(member);
    if(!InitiatesChat(teammate) || !Queue(teammate,nullptr,true,ack,true))continue;
    State(teammate).reply.subjectSlot=bot->entindex();State(teammate).reply.subject=Identity(bot);
    break;
   }
  }
	}
}

void Chat(CBasePlayer *speaker, bool teamOnly, const char *text)
{
	if (!Enabled() || !speaker || speaker->IsBot() || !CombatTeam(speaker)
		|| (speaker->pev->flags & (FL_FAKECLIENT | FL_PROXY))) return;
	char phrase[97]; bool question = false;
	if (!normalizeTeamPhrase(text, phrase, question)) return;
	lastHumanChat[speaker->m_iTeam]=gpGlobals->time;
 if (HumanCaptainCall(speaker,text)) return;
	auto &humanState = State(speaker);
 bool tagalog=Word(phrase,"ka") || Word(phrase,"ikaw") || Word(phrase,"ba") || Word(phrase,"ano");
 if(!Q_strcmp(phrase,"sorry") || !Q_strcmp(phrase,"sry") || !Q_strcmp(phrase,"my bad")) {
  for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || !p->IsBot() || !p->IsAlive())continue;auto *b=static_cast<CCSBot *>(p);auto &hurt=State(b);
   if(hurt.hurtActor!=speaker->entindex() || hurt.hurtId!=GETPLAYERUSERID(speaker->edict()) || gpGlobals->time-hurt.hurtAt>20 || p->m_iTeam!=speaker->m_iTeam)continue;
   float next=hurt.nextChat;hurt.nextChat=0;if(Queue(b,speaker,true,"all good, just watch your fire")){hurt.hurtAt=-100;return;}hurt.nextChat=next;
  }
 }

	if (gpGlobals->time < humanState.nextOrder) return;
 TeamOrder order=parseTeamOrder(text);
 if(order==TeamOrder::None)for(int i=0;i<aliasCount;++i)if(!Q_strcmp(phrase,aliases[i].from)){order=parseTeamOrder(aliases[i].to);break;}
	if (teamOrders.value && order != TeamOrder::None
		&& CompetitiveBotPolicy::ActionsAllowed(Enabled(), Completed(), Ffa(), teamOnly, speaker->IsAlive(), true)) {
		humanState.nextOrder = gpGlobals->time + 2.0f;
  humanCaptainUntil[speaker->m_iTeam]=gpGlobals->time+600;humanLeaderSlot[speaker->m_iTeam]=speaker->entindex();humanLeaderId[speaker->m_iTeam]=GETPLAYERUSERID(speaker->edict());plans[speaker->m_iTeam].active=false;
		CCSBot *ack = nullptr; bool applied = false;
		bool economy = order == TeamOrder::Eco || order == TeamOrder::Force || order == TeamOrder::FullBuy;
		bool alreadyBought=false;
		bool buyTime = !TheCSBots()->IsBombPlanted() && (CSGameRules()->IsFreezePeriod() || TheCSBots()->GetElapsedRoundTime() < 15.0f);
  if(order==TeamOrder::Save && buyTime){order=TeamOrder::Eco;economy=true;}
		if (economy && buyTime) {eco[speaker->m_iTeam] = order == TeamOrder::Eco;SetTeamEconomy(speaker->m_iTeam,order==TeamOrder::Eco);}
		for (int i = 1; i <= gpGlobals->maxClients; ++i) {
			auto *p = Player(i);
			if (!p || !p->IsBot() || !CanHear(p, speaker, true)) continue;
			auto *bot = static_cast<CCSBot *>(p);
			if (!ack) ack = bot;
			if (economy && buyTime && Eligible(bot, speaker)) {
				if(!bot->IsBuying() && bot->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT])alreadyBought=true;
    else if (order != TeamOrder::Eco) bot->RestartCompetitiveBuying();
				applied = true;
			} else if (!economy && Apply(bot, speaker, order, text)) {
				applied = true; ack = bot;
				if (order == TeamOrder::Drop || order == TeamOrder::DropWeapon || order == TeamOrder::Jump) break;
			}
		}
		if (ack) Queue(ack, speaker, true, applied ? (alreadyBought?"i already bought, sorry":"roger") : "can't do that rn", true);
		return;
	}
	// Select a unique explicitly addressed bot; never substitute a different speaker.
	CCSBot *selected = nullptr; int matches = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i) {
		auto *p = Player(i);
		if (!p || !p->IsBot() || !CanHear(p, speaker, teamOnly)) continue;
		char name[97]; bool unused;
		if (normalizeTeamPhrase(STRING(p->pev->netname), name, unused) && Word(phrase, name)) {
			selected = static_cast<CCSBot *>(p); ++matches;
		}
	}
	if(!selected) {
  for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(!p || !p->IsBot() || !CanHear(p,speaker,teamOnly))continue;
   char name[97];bool unused;if(!normalizeTeamPhrase(STRING(p->pev->netname),name,unused))continue;char *space=strchr(name,' ');if(space)*space=0;
   if(Q_strlen(name)>=3 && Word(phrase,name)){selected=static_cast<CCSBot *>(p);++matches;}
  }
 }
 if (matches > 1) return;
 bool identity=(Word(phrase,"bot") || Word(phrase,"bots") || Word(phrase,"human") || Word(phrase,"ai") || Word(phrase,"tao"))
  && (Word(phrase,"you") || Word(phrase,"ka") || Word(phrase,"ikaw")) && (question || Word(phrase,"are") || tagalog);
 if(!selected && humanState.partnerUntil>gpGlobals->time && Q_strlen(phrase)<=60) {
  auto *p=Player(humanState.partnerSlot);if(p && p->IsBot() && GETPLAYERUSERID(p->edict())==humanState.partnerId && CanHear(p,speaker,teamOnly))selected=static_cast<CCSBot *>(p);
 }
	bool greeting = Word(phrase, "hello") || Word(phrase, "hi") || Word(phrase, "hey") || Word(phrase, "yo");
	if (!selected && (identity || greeting || question || Word(phrase, "bot") || Word(phrase, "bots"))) {
		for (int i = 1; i <= gpGlobals->maxClients; ++i) {
			auto *p = Player(i);
			if (p && p->IsBot() && CanHear(p, speaker, teamOnly)) { selected = static_cast<CCSBot *>(p); break; }
		}
	}
	if (selected && Queue(selected, speaker, teamOnly, identity ? (tagalog?"oo, bot ako":"yes i'm a bot") : greeting ? (tagalog?"uy, kamusta":"hey") : (tagalog?"nandito ako":"i'm here"))) {
		if (!identity) AskAi(selected, speaker, teamOnly, text);
  humanState.partnerSlot=selected->entindex();humanState.partnerId=GETPLAYERUSERID(selected->edict());humanState.partnerUntil=gpGlobals->time+20;
		humanState.nextOrder = gpGlobals->time + 2.0f;
	}
}

void Complain(CBasePlayer *victim,CBasePlayer *actor,const char *kind)
{
 if (!Enabled() || Completed() || Ffa() || !victim || !actor || victim==actor || !victim->IsBot()
     || !victim->IsAlive() || !actor->IsAlive() || !CombatTeam(victim) || victim->m_iTeam!=actor->m_iTeam) return;
 auto *bot=static_cast<CCSBot *>(victim);auto &state=State(bot);
 if(gpGlobals->time<state.nextComplaint || gpGlobals->time<nextComplaint[bot->m_iTeam]) return;
 char subject[64],line[91];CleanName(actor,subject);
 Q_snprintf(line,sizeof(line),!Q_strcmp(kind,"team_flash")?"hey %s, please stop blinding us":"%s, watch your fire please",subject);
 auto *listener=actor->IsBot()?nullptr:actor;
 if(!Queue(bot,listener,true,line,true)) return;
 state.reply.subjectSlot=actor->entindex();state.reply.subject=Identity(actor);
 state.hurtActor=actor->entindex();state.hurtId=GETPLAYERUSERID(actor->edict());state.hurtAt=gpGlobals->time;
 state.nextComplaint=gpGlobals->time+20;nextComplaint[bot->m_iTeam]=gpGlobals->time+8;
 AskAi(bot,listener,true,line,kind,actor); // Sidecar caches a free subject template, no model request.
}
void Blinded(CBasePlayer *victim,CBaseEntity *actor,float hold,int alpha)
{
 if(actor && actor->IsPlayer() && alpha>=200 && hold>=.2f)
  Complain(victim,static_cast<CBasePlayer *>(actor),"team_flash");
}

void Event(GameEventType event, CBaseEntity *entity, CBaseEntity *other)
{
 if(!Enabled() || Completed())return;
 TacticsEvent(event,entity,other);
 if(event>=EVENT_RADIO_COVER_ME && event<=EVENT_RADIO_STORM_THE_FRONT && entity && entity->IsPlayer()) {
  auto *human=static_cast<CBasePlayer *>(entity);if(!human->IsBot() && human->IsAlive() && CombatTeam(human)){humanCaptainUntil[human->m_iTeam]=gpGlobals->time+600;humanLeaderSlot[human->m_iTeam]=human->entindex();humanLeaderId[human->m_iTeam]=GETPLAYERUSERID(human->edict());plans[human->m_iTeam].active=false;}
 }
 if(Ffa())return;
 if(event==EVENT_PLAYER_TOOK_DAMAGE && entity && other && entity->IsPlayer() && other->IsPlayer())
  Complain(static_cast<CBasePlayer *>(entity),static_cast<CBasePlayer *>(other),"team_damage");
 if(!roundChat.value) return;
	if(event==EVENT_ROUND_START)for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=Player(i);if(p && p->IsBot() && p->IsAlive())PingComplaint(static_cast<CCSBot *>(p),false);}
	if (event == EVENT_BOMB_PLANTED) TeamLine(TERRORIST, "hold site pls", true);
	if (event == EVENT_BOMB_DEFUSING && entity && entity->IsPlayer() && static_cast<CBasePlayer *>(entity)->IsBot())
		Queue(static_cast<CCSBot *>(entity), nullptr, true, "cover me", true);
	if (event == EVENT_TERRORISTS_WIN || event == EVENT_CTS_WIN) {
		TeamName winner = event == EVENT_TERRORISTS_WIN ? TERRORIST : CT;
		if (RANDOM_LONG(1, 100) <= 35) TeamLine(winner, "nice");
		if (RANDOM_LONG(1, 100) <= 35) TeamLine(winner == CT ? TERRORIST : CT, "nt");
	}
}
}
