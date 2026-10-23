# 1.6 Competitive bot port

This branch starts the migration from `yapb-comms` at `76b82b5` onto ReGameDLL
at `4a50c42`. It is an initial implementation, not full YaPB feature parity or
a production deployment. The sibling backend now selects the pinned native package for new CS 1.6 instances; CS 1.3 retains its separate YaPB provider.

## Navigation decision

Use ReGameDLL's existing `.nav` mesh, learning, pathfinding, ladders, movement
and view controller. Do not port YaPB `.graph` generation, physical/virtual
explorers, parkour editing or navigation turning. These were explicitly excluded
from this migration for now. Team commands use existing native bot states.
Named destination orders use actual NAV places (`BombsiteA`, `BombsiteB`,
`Middle`, `Apartments`) or reviewed map-size-verified callout coordinates, plus
a reachable native path. Unverified bomb-zone indices are never guessed. Existing NAV files need map-specific validation.

## Implemented first slice

Set `bot_competitive 1` to enable the extension. It defaults off so installing
the fork alone does not change a server's bot policy. The other switches are
`bot_team_orders`, `bot_text_chat`, and `bot_round_chat` (default 1).
`regamedll/extra/competitive_bots.cfg` provides an explicit opt-in server preset.
Enable the native bot system with `bot_enable 1` in `game_init.cfg` at startup.
Install the upstream bot profile/voice package under `regamedll/extra/zBot`.
Never read or modify user-owned `config.cfg` for this integration.

- Bounded exact/conservative team-order parser reused from the MIT-licensed
  YaPB fork. It rejects negation, conversational substrings, ambiguous sites,
  overlong messages and command separators.
- Living human team-chat orders can follow/trade, hold, jump, move to named
  places, eco/full/force buy, or request a nearby bot's owned or freshly purchased weapon.
  Push/rush/swing use a traced forward route through native NAV.
  Drops require a buy zone, replacement funds, proximity and a non-eco team.
  Unsupported recognized orders get an unavailable acknowledgement.
- Commands preserve planting, defusing, bomb carrying, hostage rescue, VIP
  and escape duties. Dead speakers and FFA cannot issue orders. Public chat only accepts the explicit captain site/split calls.
- One delayed acknowledgement; replies keep their public/team channel and use
  the normal game chat hooks and mute/dead/team delivery rules. Named replies
  require a unique name. Local identity answers disclose that the bot is a bot.
- Replies expire and are bound to round, user ID, team and alive/dead state.
  Disconnects, reused player slots, side changes and round changes invalidate
  queued replies. Tactical replies wait out firing/recent damage, within expiry.
- First real live-round greeting, optional round reactions, plant/defuse lines
  and post-match `gg`. Round restarts do not repeat the match greeting.
- Backend-owned `competitive_match_completed` stops bot movement, attacking,
  buying and event-driven tactics. Completion is latched until map/new match ID.
  `competitive_bot_buy_epoch` restarts native buying after backend money/loadouts
  settle; an epoch change cannot resume a completed match.
- Clear traces reject start-solid/all-solid results. Protected/invulnerable
  players cannot become visual targets. Acquisition, reaction-queue delivery,
  firing and fast upkeep recheck visibility; stale sensing does not authorize
  continued tracking or rapid fire through cover. Hearing remains native.

State lives outside `CCSBot`/`CCSBotManager`; no public object fields or virtual
methods were added. Linux and MSVC project source lists include the extension.

## Current additions and remaining port work

- `bot_captain_mode 1`: living human A/B calls (public or team chat) take priority.
  Exact splits such as `3a 2b`, `3a 2mid`, `3a 1mid 1b` must total the living team.
  Otherwise the highest-scoring living bot selects a reachable native NAV plan after
  freeze time. Its main group follows the majority route; smaller groups stay near
  their group leader. New rounds reset plans; a lost captain triggers election.
  Combat and plant/defuse/rescue/VIP duties take precedence. Missing native place
  labels make a human call follow that human. Automatic splits require real labels.
- `bot_smart_grenades 1`: copies personally visible enemy positions, then considers
  HE/flash/smoke for up to five seconds after sight is lost. It checks range,
  teammates, objectives, pressure, native throw physics, walls, ceilings and liquids.
  Native aim, pin pull and release remain engine-owned. This is a conservative
  trajectory preview, not learned map lineups or full YaPB utility parity.
- Replies take roughly 1.5–3 seconds. A safe bot pauses movement during the final
  typing interval; combat, objectives, blindness, ladders and falling override it.
- Actual teammate flash/damage events queue named, throttled team complaints.
  Names bind to the actual actor and are invalidated on slot/team/life changes.
- Cache templates bind `{{s}}` (subject), `{{p}}` (speaker), `{{b}}` (responding bot).
  Whole-name matching avoids substring substitutions. No expression evaluation occurs.
  Flash/damage templates are free and do not call the model.
- Backend names remain owned by AMXX roster assignment. Instance-owned native
  profiles carry each roster personality. `bot_competitive_difficulty 0..4` selects
  five execution profiles; `-1` follows native difficulty. `bot_advanced_tactics 1`
  adds team economy, visible support, cover, saves, donations and post-plant regroup.
- Learned witnessed holding-angle utility survives into the following round;
  planting smoke uses personally known approaches and native safety checks.
- Optional aliases/callouts, radio captaincy, visible counts, damage estimates,
  apology followups, score/streak reactions, ping personalities and idle drift
  are implemented. See [YAPB_PORT_AUDIT.md](YAPB_PORT_AUDIT.md) for exact scope.
- Dialogue stores at most two short turns for 120 seconds, fenced by round/life/IDs.
  PostgreSQL templates are checked before private SQLite, shared across match workers
  and subject names. Context-free cache hashes remain compatible with the installed
  v5 database entries; contextual followups have separate hashes. Existing database
  rows are not cleared or migrated. Free variants and paid replies are written back
  through the authenticated backend API; standalone use can remain local-only.

Remaining implementation/publishing work: Windows dedicated query interception,
Windows runtime verification, a public release, and production deployment. Linux x86
CI/artifact packaging has been added; a local artifact is available under `dist/`.
Native NAV/movement remains deliberately retained. No extra paid event-paraphrase
calls were introduced.

Implemented but still requiring live human verification: captain calls and cohesion
on real map NAVs, typing feel, complaint attribution/muting under real gameplay,
Steam scoreboard numeric ping, halftime/overtime rebuy and a full backend FFA
join/refill/reconnect cycle. Pure policy tests are not proof of those live flows.

## Backend config mapping

The backend generates mode-specific settings in `src/game-adapters/cs16/cs16.adapter.ts`;
updating only a template config is insufficient.

| Current YaPB setting | Native counterpart / work needed |
| --- | --- |
| `yb_quota`, `yb_quota_mode`, `yb_autovacate` | `bot_quota`, `bot_quota_mode`, `bot_auto_vacate` |
| `yb_join_after_player`, `yb_join_team`, `yb_join_delay` | `bot_join_after_player`, `bot_join_team`, `bot_join_delay` |
| `yb_difficulty 0..4` + `difficulty.cfg` | `bot_difficulty 0..3` + `BotProfile.db`; native mapping `[0,1,1,2,3]` plus five execution profiles; balance calibration pending |
| `yb add ... name/personality` | `bot_add[_t/_ct]` selects a profile name; generate profiles instead of assuming YaPB argument syntax |
| `yb_radio_mode 1` | `bot_chatter radio` |
| `yb_round_chat 1` | `bot_round_chat 1` with `bot_competitive 1` |
| `yb_chat`, `yb_ai_bridge` | `bot_text_chat` plus opt-in `bot_ai_bridge` and the external sidecar |
| `yb_restricted_weapons weapon_shield` | `bot_allow_shield 0` |
| `yb_csdm_mode 2` | Native `bot_deathmatch` plus existing server FFA rules; full FFA refill needs live verification |
| `yb_shoots_thru_walls 0` | Competitive visibility gates in this fork; no direct cvar alias |
| `yb_show_latency` / fake-ping settings | `bot_show_latency 1`, `bot_ping_min`, `bot_ping_max`; basic stable per-bot simulated scoreboard ping |
| YaPB graph analyzer settings | Native NAV tooling; no direct translation |

The existing AMXX `competitive_match_id`, `competitive_match_live`,
`competitive_match_completed` and `competitive_bot_buy_epoch` contract is reused.
AMXX continues to own match scores/results, roster/quota and public FFA refill.
Do not enable YaPB and native quota controllers simultaneously. The current
backend host supervisor watches both providers. Native workers use isolated
`addons/regamedll/ai` mailboxes; YaPB workers retain their own transport.

## Build and validation

On the local Fedora toolchain (modern CMake/GCC), the upstream non-PIC link
fails with `R_386_GOT32X`; this build uses explicit PIC:

```sh
rtk proxy cmake -S . -B build-competitive -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS=-fPIC
rtk proxy cmake --build build-competitive -j4
rtk proxy ctest --test-dir build-competitive --output-on-failure
```

The game DLL remains Linux x86 (32-bit). The standalone policy test covers
malformed/ambiguous commands, visibility boundaries, expired/reused-slot
delivery, dead/team/round changes, lifecycle terminal latching and rebuy epochs.
These tests do not establish aim balance, map-specific behavior, Windows runtime
support or complete backend integration.

Before switching providers, exercise team/public/dead chat with real humans,
named NAV commands, unreachable places, eco then full buy, halftime/OT rebuy,
rapid corner peeks, spawn protection, round cancellation, final match completion,
and public FFA quota/refill in a disposable server. Never infer a completed match
from the physical round score or silently change core weapon/movement rules.

## Standalone DeepSeek setup (no 1.6 Competitive account)

Python 3 and outbound HTTPS are sufficient. No third-party Python packages are needed.
Run from the fork checkout:

```sh
python3 tools/backend_ai_bridge.py --setup-key
python3 tools/backend_ai_bridge.py --queue-dir /path/to/Half-Life/cstrike/addons/regamedll/ai
```

The setup command prompts without echoing your key and atomically saves it with
owner-only permissions at `~/.config/regamedll/deepseek.env`. An explicit
`--env-file /private/path.env` selects another file. Alternatively supply
`DEEPSEEK_API_KEY` in the server process environment; environment values override
saved settings. Do not put credentials in game cvars or commit them to the fork.
The default provider is DeepSeek `deepseek-flash`; `YAPB_AI_API_URL` and
`YAPB_AI_MODEL` configure custom endpoints (HTTPS, or loopback HTTP for local models/testing). Official DeepSeek requests are pinned to the cheapest documented tier, `deepseek-flash`, regardless of a more expensive model override.

Enable `bot_competitive 1`, `bot_text_chat 1` and `bot_ai_bridge 1` on the game
server. Address a bot by its profile name. The bot sends a bounded request to
`addons/regamedll/ai/request.tsv`; the sidecar atomically returns `reply.tsv`.
The files contain the latest addressed message/reply, so restrict directory access.
No network or provider call runs on the game thread. One request can be outstanding;
there is a six-second provider timeout, seven-second fallback and nine-second
final delivery expiry. Local checks still enforce speaker/user ID, team, life,
round and mute rules. The model only supplies conversation; native orders own
movement/buying. Identity questions retain honest local responses. Flash/damage complaints use free subject templates. Other ported tactical events use fact-preserving free variants. Dialogue keeps at most two short turns, fenced by the native round/life/user-ID context.

For installed copies, place the Python files and `run-ai.sh` together under
`cstrike/addons/regamedll/tools/`. Then run `run-ai.sh --setup-key` once and
`run-ai.sh` alongside the game/server. The game preset leaves AI disabled until
configured. No key is supplied by this repository, and no real API request was
made during development verification.

## Optional backend provider adapter

Add `--backend-tools /path/to/16competitive/tools/yapb-ai` to use the existing
backend language validation and compatible settings. The ReGameDLL
transport remains the new request/reply protocol, not `yb ai` command injection.
The sibling backend ships a native host worker under `tools/regamedll-ai/`.
`YAPB_CACHE_TOKEN` and `BOT_TEMPLATE_CACHE_URL` optionally connect templates to
its authenticated `/internal/yapb/templates` API, backed by the existing PostgreSQL
cache table. Context hashes separate map/mode/channel/style/provider/model. Reads
and writes have short timeouts and fall back to private per-instance SQLite.
HLDS receives neither provider keys nor cache credentials. Python requests remain
uncapped unless the operator explicitly passes optional limits.

## Dedicated query compatibility

`bot_enable_query_hook 1` enables the native-bot query option (default 0).
On Linux x86 dedicated servers, preload `build-competitive/libregamedll_query.so`
when launching `hlds_linux`. The optional installed `run-dedicated.sh` expects
it at `cstrike/addons/regamedll/libregamedll_query.so` and the Linux HLDS executable
at the game root. Steam's ordinary Half-Life client does not supply that executable.

The companion rewrites unsplit A2S_INFO bot counts and matching A2S_PLAYER bot
connection times. Times use elapsed native-bot connection time, not YaPB's
randomized fake time. The query hook does not alter in-game BOT markers, client flags, or
player count. Malformed/unknown/split packets pass through unchanged. A stale
snapshot disables rewriting after two seconds. Disabling the cvar restores normal
replies on the next snapshot. No engine code patching or system-wide preload is
installed. New Game/listen servers ignore the setting, like YaPB. Windows query
interception is not implemented; the ordinary bot/AI code retains Windows guards.

Validated with an isolated loopback HLDS: de_dust2 started, two native bots joined,
A2S_INFO reported bot count 2 with the option off and 0 with it on, and the server
exited normally. Policy and packet-boundary CTests pass. Python tests exercise
malformed input, unsafe output and a complete standalone mock-provider request.
Live human chat, real DeepSeek credentials and the Steam GUI remain unverified.

## Economy defaults

The standalone request uses a short plain-speech prompt, at most two short dialogue turns,
32 output tokens maximum, and explicitly disabled reasoning.
The optional backend adapter uses this same bounded network request before applying
backend validation. No automatic retries or upgrades to a more expensive model occur.
The game still accepts ordinary punctuation and up to 90 UTF-8 bytes of safe speech.

Python-side requests are uncapped by default. Optional `--hourly-limit N`,
`--daily-limit N`, and `--min-interval SECONDS` enable persistent spending limits;
zero means no limit. Failures count against an explicitly selected limit.
`ai_cost.sqlite3` preserves attempt history and caches up to eight variants per key (2,048 variants total) for
24 hours; legacy single-row cache reads remain compatible. Cache hits do not call the provider. Keys hash the message/context;
the cache stores reply text, never credentials. Keep the queue directory private.
The native mailbox still admits one in-flight conversation and has its existing
eight-second request spacing; it does not spend credits on concurrent requests.

DeepSeek's published pricing checked 2026-10-24 lists Flash cheaper than Pro:
https://api-docs.deepseek.com/quick_start/pricing . Future model/pricing changes
require checking that source again; the sidecar does not infer future prices.
Restart the sidecar after installing an update; the game need not restart.

## Simulated scoreboard latency

`bot_show_latency 1` replaces native bots' scoreboard BOT hint with numeric,
simulated ping. `bot_ping_min 20` and `bot_ping_max 60` set the default range
in milliseconds. Each native bot keeps a base value with up to 2 ms of jitter;
values update once per second. Standard GoldSrc `svc_pings` packets are sent
to real connected clients at most four times per second, so client-side scoreboard
toggles do not depend on the server receiving `IN_SCORE`. The maximum payload is
101 bytes per update for 32 bots.
This is display-only: no packet delay, gameplay handicap, or change to
`FL_FAKECLIENT`, authentication or server-side bot identity. It is independent
of `bot_enable_query_hook`, AI, and `bot_competitive`. Set `bot_show_latency 0`
to restore the stock BOT hint. The extension defaults off; the supplied opt-in
preset enables it. New Game and dedicated servers use the same implementation.

Packet tests decode all 32 slots, exact bit widths, ping limits, packet termination,
and invalid settings. In-game visual behavior still needs verification in the
Steam client after restarting with the new DLL.

## 1.6 Competitive Mouse3-style pings from native bots

`bot_ping_comms 1` enables native enemy-sighting handoff when `bot_competitive 1`
and the core's `competitive_player_ping`/`competitive_bot_reports` gates are on.
The default and opt-in preset enable it. No Python sidecar or LLM is involved.

The coordinator uses the native bot's personally observed current enemy,
rechecks line of sight and FOV, and submits the server-only command
`16competitive_bot_report <bot-slot> <enemy-slot> 2`. It never synthesizes a
human keypress or exposes a client-accessible command for impersonating bots.
The core revalidates identity, life/team, view cone, visibility, pressure and
cooldowns, then uses the same marker, sound, team-only HUD transport and
eight-second lifetime as Mouse3. Markers sample the position once, never track.
The fork gates freeze/ended rounds, completion, FFA, protected targets, firing
on either side and recent damage. Reports are limited to once per bot per
12 seconds and once per team per five seconds. Native enemy-sighting chatter
is suppressed while this coordinated path is available, avoiding a second
announcement for the same sighting. Other native chatter remains unchanged.

The backend AMXX fallback poller skips native reports while `bot_competitive`
and `bot_ping_comms` are enabled, avoiding duplicate sources. Marker assets,
permissions, lifetime and transport remain owned by the existing core.

An isolated native-bot/ReGameDLL/AMXX test passed the existing `ping_bots.sma`
regression with native bot creation: human crosshair hit/snap, off-crosshair
visibility, behind-target/FFA/firing/pressure rejection, chat-only mode,
server-command handoff, red markers, cooldowns, unknown callouts, entity
replacement, static sampled positions and repeated-ping rate limits. Native
policy tests separately cover the coordinator's visibility/lifecycle gates.
This is integration support for the existing core, not a bundled standalone
AMXX installation. Without that core the fork retains ordinary bot chatter
and does not attempt to create the custom markers.

## Native utility engine check

An isolated Linux x86 ReHLDS + AMXX fixture on `de_dust2` revealed a real target,
then hid it and moved it 500 units away. A native HE throw detonated 112 units from
the saved position and 395 units from the hidden current position. A blocked cover
case refused an unsafe trajectory. Liquid paths are rejected because native
water drag invalidates an air-only preview. This verifies one controlled HE case,
not all maps or flash/smoke effectiveness. Test fixture: `tests/engine/utility_native.sma`.

The backend package contains a checksum manifest at
`cstrike/addons/regamedll/provider.json`. New instances own their DLL inode,
Metamod plugin list and AI mailbox. The native Linux executable receives only a
small runtime environment plus its query preload; API/database secrets stay outside
HLDS. Existing match processes and production servers are not restarted by this work.

Latest verification: four native CTests, 24 Python tests, 56 focused backend tests,
TypeScript check and AMXX compilation passed. A backend-prepared isolated server
loaded the native provider without YaPB, answered A2S_INFO, and assigned the two
AMXX roster names without replacement churn. No paid provider calls were used.

## Opinionated conversation and mobile firing

AI may agree or politely disagree with CS preferences; it must not contradict facts
for variety, invent live game knowledge, or cancel human captain orders. Cache
version `speech-v5-independent-opinions` avoids reusing the previous prompt's replies.
No extra model request or larger output limit is added.

`bot_combat_movement 1` (with `bot_competitive 1`) favors mobile engagements for
MP5, TMP, P90, MAC-10 and UMP45 at 120–900 units, and M3/XM1014 at 100–450 units.
Once visual recognition makes the engagement eligible, 75% favor native strafing over crouch-and-hold. The choice is held for that engagement.
Native steady pauses, aim, recoil, weapon fire rates and burst logic remain intact.
The extension avoids special jump dodges for these mobile weapons. Side steps use
player-hull corridor traces and floor checks to avoid walls, teammates and drops.
Explicit hold/sniping duties, ladder, reload, blindness and critical objective duties do not
receive this preference. Setting the cvar to 0 restores the native movement choices.

Captain announcements omit empty routes: `3a 2b`, `3a 2mid`, `5a`.

Detailed history/source comparison: [YAPB_PORT_AUDIT.md](YAPB_PORT_AUDIT.md).

Controlled combat fixture confirmed actual clip-decrementing shots while moving:
MP5: 58/90 shots above 30 units/s; M3: 2/8 shots above 30 units/s across repeated
encounters. XM1014: 11/21 shots above 30 units/s. These are fixture observations, not promised gameplay rates.
`tests/engine/combat_native.sma` selects the test weapon using two defines.

## Shared database cache regression

`RUN_BOT_CACHE_DB_TEST=1 bun test src/controllers/yapb-reply-cache.database.test.ts`
in the backend runs the opt-in PostgreSQL integration check. It creates one random
test entry, starts two independent Python workers, verifies that the subject template
renders for two different names without contacting a provider, and removes only its
own entry. Existing cached replies are untouched. The API token and provider key never
enter HLDS. The AI host holds a process lease to prevent duplicate development-reload
watchers; each native mailbox also retains its exclusive worker lock.

## Linux artifact

Run `python3 tools/package_release.py` after a successful build. It creates a
sorted tar with fixed packaging metadata and SHA256SUMS. This verifies deterministic
packaging of the same inputs; compiler/toolchain changes may change binary output.
The CI workflow builds, tests and uploads this artifact. It does not publish a release
or establish Windows runtime support. Valve bot profiles/voices must be supplied
separately as described above.

## Holding angles and spree delivery fixes

Competitive stationary watch points must have a clear eye trace, useful horizontal
range and bounded elevation. Blocked NAV approach points are skipped. If none are
usable, a bounded 16-ray geometry scan selects an open angle. A selected watch lasts
until the next reconsideration; changing geometry triggers reevaluation. Combat,
noise, ladder movement and other higher-priority look tasks retain control.
The extra tactics-owned idle drift was removed; the native view controller already
adds small drift, so no second cached wall-facing angle overwrites it.

Spree reactions track both human and bot killers, retain pending reactions briefly
when a speaker is busy, and can use any non-quiet bot independently of tactical
reporter roles. Team games count round kills; FFA counts consecutive kills per life.
Another bot may praise a spree after the killer dies. Team messages remain team-only;
FFA praise uses public chat. Reactions keep normal delayed delivery, mute rules,
round/user-ID fences and an eight-second channel cooldown. No paid AI call is needed.

## Persistent native navigation

The backend seeds reviewed matching NAVs, then links each instance map NAV to a
persistent cache keyed by map name and BSP SHA-256. Learned files survive instance
cleanup. NAVs from a different BSP size are rejected instead of merely warned about.
A per-target process lock serializes first-time generation; waiting instances load
the published file. Native learning remains unchanged. Generated NAVs publish through
a temporary file and atomic rename, preserving the cache link and preventing readers
from loading an in-progress file. Map change, completion and process exit release
generation ownership. Existing NAVs load directly without regeneration.

Captain acknowledgements are queued only after the actual captain chat line is
delivered and name that captain. Failed or radio-only plan announcements do not
produce detached text acknowledgements. C4 carriers take a bombsite role in
splits, commit to a reachable point inside the actual planting zone, and retain
the native planting task rather than following roaming group leaders. A new
human site call can redirect the approach before planting begins.
