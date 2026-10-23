# YaPB → native bot port audit

Reviewed the clean YaPB fork at `76b82b5`, its 30 fork-specific commits after
`4967a22`, `TEAM_COMMS.md`, and changed implementation/helpers. Compared against
the current uncommitted ReGameDLL fork, its Python workers and the sibling
16competitive adapter/AMXX integration. This tracks custom fork behavior;
stock ZBot behavior can exist without implementing the same YaPB enhancement.

## Implemented ports (2026-10-24)

| Area | Native implementation and limits | Source evidence |
| --- | --- | --- |
| Roster gameplay personalities | Backend generates instance-owned named BotProfile.db profiles with careful/normal/rusher aggression and teamwork; AMXX preserves name-to-roster identity. Five execution levels are separate from native profile selection. | `956ee49`, `899f41c`; native profile integration vs YaPB named-personality spawning |
| Mechanical aim | Five execution profiles, nonzero early-fire reaction, sampled tracking, target-stable head/body choices, opening error and recoil recovery pauses. Classic weapon physics stay unchanged. Match balance is not calibrated by these tests. | `f9bf31f`, `c8203fc`; `inc/combat_behavior.h`, `src/combat.cpp` |
| Reload/damage cover | Brief native retreat after reload/heavy damage, including FFA; occasional safe reposition after a kill. Objective duties and human orders take priority. | `f9bf31f`; `src/combat.cpp`, `TEAM_COMMS.md` |
| Support and death awareness | Support a visible firing teammate, escort a visible carrier, and delay 1.5 seconds before avoiding a witnessed-death route for up to 15 seconds; reachable native NAV alternatives only. | `f9bf31f`; `src/botlib.cpp`, `src/combat.cpp` |
| Learned holding utility | Personally witnessed holding-angle memory survives into the next round, with native trajectory safety and three utility throws per round. | `956ee49`, later tactical commits; `inc/holding_utility.h`, `src/combat.cpp:2479`, `src/manager.cpp:2548` |
| Plant utility | Smoke toward a personally known approach while a visible teammate is actually arming C4; preserve planter and native safety checks. | `956ee49`; `src/manager.cpp` |
| Post-plant regroup | Distant Terrorists move toward their own known bomb position while enough time remains, with four-second retry spacing. | `956ee49`; `src/manager.cpp`, `TEAM_COMMS.md` |
| Automatic saves | Outnumbered, poor-economy late-round saves with carrier/defuser/objective exceptions and one team announcement. | `956ee49`; `src/manager.cpp` |
| Team economy captain | Team buy/eco assessment, captain announcement before purchase, 2.5-second wait plus stagger, human buy/save orders and late-buy acknowledgement. | `956ee49`, later captain changes; `TEAM_COMMS.md`, `src/manager.cpp` |
| Proactive donations | Delayed poor-bot request, rich-bot offer and nearby confirmed primary drop plus replacement buying; once per team/round, rechecking player IDs, range and economy. | `956ee49`; `src/manager.cpp`, `src/botlib.cpp` |
| Requested weapon buying | Named weapon requests may buy first, including pistols, through native purchase/drop APIs; funds, buy-zone, proximity and objective gates apply. | `6047e82`, `956ee49`; `src/manager.cpp`, `TEAM_COMMS.md` |
| Full command actions | Native reachable forward push/execute/swing, personally known-bomb retake/rotation, leave-site routing and timed watch-mid/apps/back holds. | `6047e82`, `956ee49`; `inc/team_order.h`, `src/manager.cpp` |
| Command aliases | Bounded reviewed team_aliases.cfg, with built-in parsing taking priority; packaged for standalone and backend instances. | `6047e82`; `cfg/addons/yapb/conf/team_aliases.cfg` |
| Captain parity | Human site/split/tactical/radio priority, user-ID fences, highest-frag fallback, human-chat silence timing and a second-bot acknowledgement. | `956ee49`, `f4dae4c`; `TEAM_COMMS.md`, `src/manager.cpp` |
| Sighting counts and callouts | Visible-only nearby A/B/Mid counts, native NAV place labels and optional map-size-verified callout fallback; exporter and configs included. | `c72ed77`, `1ed8962`; `inc/map_callouts.h`, `tools/export_cz_callouts.py`, callout configs |
| Damage estimates | Delayed named low-hp/one-shot estimates from personally confirmed visible damage; no hidden enemy-health query. | `c72ed77`, `1ed8962`; `inc/enemy_damage.h` |
| Reporting channel choice | One selected count/chat, radio or ping path; two connected initiating roles, alternating second role and radio under damage pressure. | `1ed8962`, `899f41c`; `inc/radio_comms.h`, `src/manager.cpp` |
| Tactical language variety | Three local non-repeating semantic variants and shared team cooldowns for custom tactical reports and hooked native kill/distress chatter. Native objective information memes remain native. | `f9bf31f`; `inc/tactical_comms.h` |
| Kill/death/score reactions | Delayed human/bot multi-kill praise with bounded retries and independent non-quiet speakers, FFA per-life streaks, score feelings/milestones, knife/HE and death/lag reactions under shared cooldowns. | `69efe10`, `119af76`, `956ee49`; `inc/score_reaction.h`, `src/manager.cpp` |
| Apology follow-up | Actual friendly-fire actor remembered for 20 seconds; sorry/sry/my bad acknowledged independently of warning cooldown. | `6047e82`, `899f41c`; `src/manager.cpp:3733` |
| Dialogue context | Two short turns/120 seconds in worker memory with map/team/channel/round/life/user-ID fences; unique-first-name matching and short followups; local Tagalog identity/fallback wording. | `26353c9`, `6047e82`; `inc/chat_intent.h`, `tools/ai_sidecar.py:163` |
| Event paraphrases/cache variety | Fact-preserving free event variants, eight private variants/key, shared PostgreSQL variants and anti-repeat sampling. No extra paid paraphrase calls. Context-free keys retain the existing database namespace. | `37b989f`, `26353c9`, `f9bf31f`; `tools/ai_sidecar.py` |
| Ping personalities | Persistent low/medium/high displayed-ping profiles, style/personality-aware pre-round/death complaints; map-change cooldown resets. | `899f41c` and connection-personality changes; `inc/lagging_bot.h`, `src/fakeping.cpp` |
| Idle presentation | Native idle view drift retained; redundant tactics-owned drift removed. Stationary holds require visible NAV approaches, with a bounded geometry-only open-angle fallback and persistent watch duration. | `899f41c`; `inc/idle_aim.h` |
| Publishing | Linux x86 packaging/checksums and fork CI workflow added and packaging tested locally. Public release and Windows query/runtime work remain outstanding. | `0dec600`, `372fdec`; `.github/workflows/comms-linux-x86.yml`, `scripts/release-local.mjs` |

## Remaining implementation / publishing gaps

- Windows dedicated query interception is not implemented; Windows runtime testing remains outstanding.
- No public release was published and production nodes were not deployed or restarted.
- Free local event paraphrases cover the ported tactical events. There is no extra paid model call to grow tactical variants automatically; existing cached variants are reused.
- This ports the custom decisions onto native controllers; it does not reproduce every stock YaPB combat/pathfinding behavior.

## Deliberately retained or excluded

- Native `.nav`, movement/pathfinding/ladder and view controllers remain. YaPB
  graph exploration, damage-volume avoidance during graph generation, manual
  parkour recording, navigation-turn smoothing and movement/view preservation
  were explicitly excluded from this migration (`67c3726`, `76b82b5`, `c72ed77`,
  `899f41c`). Native NAV safety must be assessed separately.
- Optional difficulty-scaled/downhill bunny-hop bursts and experimental ground
  strafe/test modes are not ported (`2d95509`). Combat strafing with SMGs/shotguns
  is a separate native change, not a bunny-hop port.
- CS 1.3 legacy join handling stays in the separate legacy provider/adapter
  (`1df5605`). ReGameDLL is not being made into a CS 1.3 game DLL.
- Python paid-request caps remain off at the user's request. Recreating YaPB's
  old mandatory budgets would contradict the current preference.
- AI supplies speech; native parsing owns actions. The older external LLM action
  bridge is not being enabled as arbitrary model-controlled gameplay.
- Native identity answers disclose bots when directly asked. Older ambiguous
  YaPB identity modes are not a behavior to reproduce.
- Dedicated query bot-count rewriting exists on Linux; connection times are
  actual elapsed bot time, rather than YaPB's randomized appearance. Windows
  query interception is missing.

## Already implemented; do not count as missing

Native sensing/visibility/protection guards; backend lifecycle terminal latch and
rebuy epochs; A/B and exact split captain plans with highest-frag fallback; basic
team orders/eco/buy/existing-primary drops; native last-seen HE/flash/smoke preview;
Mouse3-style AMXX enemy ping handoff; basic simulated scoreboard latency; standalone
DeepSeek key setup and backend private-file key loading; uncapped economical speech;
subject templates and shared/private cache; flash/team-damage complaints; delayed
replies and safe typing pauses; opinionated agreement/disagreement; mobile SMG and
close shotgun firing preference; zero-count routes omitted from announcements;
native provider selection, per-instance DLL isolation, roster name/chat-style
handoff and checksummed local/backend package installation.

## Still needs live verification

Steam scoreboard rendering, map-specific captain cohesion and unreachable paths,
real-player chat/complaint/mute behavior and typing feel, long-match halftime/OT
rebuy, a complete FFA join/refill/reconnect cycle, and wider utility/map safety.
Controlled engine checks and policy tests do not establish complete match balance.
Production servers have not been deployed/restarted as part of this audit.

Validation for this batch: four native CTests, 24 Python tests, 56 focused backend tests, TypeScript checking and both AMXX compilations pass. A real PostgreSQL test ran two fresh workers with different names against one persisted template: both were cache hits with no provider call. Isolated backend engine startup and MP5 firing/movement checks passed. The database test deletes only its own random test row; existing rows remain intact.
