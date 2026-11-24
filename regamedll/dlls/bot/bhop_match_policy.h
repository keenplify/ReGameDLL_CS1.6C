// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <algorithm>
namespace BhopMatch {
// GoldSrc positive sidemove points right (negative yaw). Ground takeoff
// follows navigation input, independently of the bot looking around.
inline float MovementHeadingError(float goalYaw, float viewYaw, float forward, float side) {
    if (!std::isfinite(goalYaw) || !std::isfinite(viewYaw)
        || !std::isfinite(forward) || !std::isfinite(side)
        || forward*forward+side*side<1) return 180;
    const float moveYaw=viewYaw+std::atan2(-side,forward)*180.0f/3.14159265358979323846f;
    return std::fabs(std::remainder(goalYaw-moveYaw,360.0f));
}
inline bool AirDuck(float elapsed, float verticalSpeed, float start=.05f, float duration=.25f) {
    return elapsed>=start && elapsed<std::min(.30f,start+duration) && verticalSpeed>0;
}
struct Context {
    bool alive, running, crouching, combat, busy, ladder, water, stuck, waiting, validPath, specialArea;
    float sinceEnemy, goalDistance, goalHeight, headingError, moveSpeed;
};
inline bool Eligible(const Context &c, bool speedPractice=false, bool winterTravel=false) {
    return c.alive && c.running && !c.crouching && !c.combat && !c.busy && !c.ladder
        && !c.water && !c.stuck && !c.waiting && (c.validPath || speedPractice) && !c.specialArea
        && c.sinceEnemy>(winterTravel?0.5f:3.0f) && c.goalDistance>(speedPractice?32:(winterTravel?96:180)) && c.goalHeight<18
        && c.headingError<35 && c.moveSpeed>100;
}
}
