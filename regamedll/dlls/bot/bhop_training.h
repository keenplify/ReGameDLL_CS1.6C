// SPDX-License-Identifier: MIT
#pragma once
class CBot;
class CBasePlayer;
struct usercmd_s;
namespace BhopTraining {
bool NoNavigation();
bool RLMove(CBot *bot);
void Register();
void Reset();
void Frame();
bool Owns(CBasePlayer *bot);
int CommandStepMSec(CBot *bot);
void Command(CBot *bot, usercmd_s &command, float dt);
}
