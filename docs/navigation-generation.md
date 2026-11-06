# Player collision and hazards during NAV generation

CS 1.6 generation samples at 10-unit spacing and uses crouched player hulls for
node sampling and generated drop links. The hull determines fit; reducing grid
spacing does not permit narrower player collisions. The old 25-unit grid missed
parts of the wooden zigzag VIP exit walkway in `as_oldlabratory_csneo`, where
safe player-center positions pass between its grid lines. Preserve the physical
100-unit drop-search range independently of the sampling interval.

Eye-height visibility is insufficient for solid player clips behind transparent
railings. Drop links now check the lift, horizontal crossing, and landing with
the player hull, allowing only a normal step before leaving the ledge. Flat
routes do not receive a synthetic crouch-jump over a fence. Actual floor rises
retain jump clearance checks. Movement physics and runtime floor selection are
unchanged.

At learning start, snapshot active, positive-damage `trigger_hurt` volumes that
affect players and the current bounds of slime/lava `func_water` brushes. Damage
triggers use the player envelope. Liquid damage uses GoldSrc's water-level
sample at feet + 1 (see `PM_CheckWater`), rather than a full box intersection.
Use the liquid's model bounds, excluding engine broad-phase padding. This keeps
thin dry walkways above liquid traversable while excluding the lower basin.
Check endpoints, complete routes and ladder dismount seeds. Healing triggers,
triggers excluding clients, inactive triggers and ordinary water are not
classified as active damage. The snapshot does not model future button-controlled
liquid movement.

`tests/nav_generation_test.cpp` covers solid rails, drops, normal steps and rises,
shallow damage strips, and dry walkway versus submerged floor samples. Its CTest
assertions stay enabled in release builds. The 32-bit Linux game DLL builds.
Windows execution has not been verified for this change.

`tests/engine/laboratory_navigation_native.sma` is a disposable native-engine
fixture for the matching BSP. It checks both legs of the final dry turn, actual
empty/lava contents, and a solid mid railing with clear endpoints that blocks
the player hull both normally and with step lift. It passed on Linux ReHLDS
build 4583 / ReGameDLL 5.30.0.849-dev+m.

The laboratory BSP SHA-256 is
`67517e259c115f2e0f10344679ba144ec7741aea103914d2633676b707b679c7`.
A regenerated topology contains 1,960 areas; all 14 CT spawns can reach the real
VIP safety trigger along the dry walkway. Static player-envelope checks found
zero overlaps with initially active damage triggers, versus 34 overlapping
areas in the old 705-area NAV, and zero sampled solid-barrier crossings.
These checks do not establish every combat route or future dynamic-map state.

`tests/engine/laboratory_vip_walk_native.sma` starts a native VIP bot at the dry
wooden approach with a frozen opposing bot. It checks harmful-liquid contents
on every VIP player frame and requires the native bot to touch the actual
`func_vip_safetyzone`. It passed with the full analyzed NAV, not just an early
topology snapshot. This exercises the final approach, not a complete contested
round from every spawn.

The full analyzed NAV SHA-256 is
`c8e4809376d1f0fdef76ccd8987dc7e6ca153cc3eb93ede6b40a471e2728d0ea`.
The backend curated NAV and BSP-bound manifest were updated locally, with the
review recorded in `src/game-assets/cs16/navs/laboratory-review-20261107.json`.
Regional catalogs and already-running matches have not been changed by this work.
