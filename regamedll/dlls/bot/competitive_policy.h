// SPDX-License-Identifier: MIT
#pragma once

namespace CompetitiveBotPolicy {
struct LifecycleChange {
	bool ended, greet, rebuy;
};

struct Lifecycle {
	bool completed = false, greeted = false;
	int buyEpoch = 0;
	LifecycleChange Observe(bool terminal, bool liveOutsideFreeze, int epoch)
	{
		LifecycleChange change { terminal && !completed, false, false };
		completed = completed || terminal;
		change.greet = !completed && liveOutsideFreeze && !greeted;
		greeted = greeted || change.greet;
		change.rebuy = !completed && epoch != buyEpoch;
		buyEpoch = epoch;
		return change;
	}
};

// Independent of the engine so stale-delivery and visibility rules can be tested.
inline bool ClearTrace(float fraction, bool startSolid, bool allSolid)
{
	return fraction == 1.0f && !startSolid && !allSolid;
}

struct Recipient {
	int userId = 0;
	int team = 0;
	bool alive = false;
};

inline bool SameRecipient(const Recipient &before, const Recipient &now)
{
	return before.userId > 0 && before.userId == now.userId
		&& before.team == now.team && before.alive == now.alive;
}

inline bool DeliveryCurrent(float now, float issued, float deadline, unsigned round,
	unsigned currentRound, const Recipient &speaker, const Recipient &currentSpeaker,
	const Recipient &listener, const Recipient &currentListener)
{
	return now >= issued && now <= deadline && round == currentRound
		&& SameRecipient(speaker, currentSpeaker)
		&& (listener.userId == 0 || SameRecipient(listener, currentListener));
}

inline bool ActionsAllowed(bool enabled, bool completed, bool ffa,
	bool teamChat, bool humanAlive, bool sameTeam)
{
	return enabled && !completed && !ffa && teamChat && humanAlive && sameTeam;
}
inline float ReplyDelay(unsigned textBytes, float jitter)
{
 const unsigned length = textBytes > 60 ? 60 : textBytes;
 const float variation = jitter < 0 ? 0 : jitter > 0.65f ? 0.65f : jitter;
 return 1.3f + length * 0.018f + variation;
}

struct Sighting {
 bool enabled = false, completed = false, ffa = false, freeze = false, roundOver = false;
 bool observerAlive = false, opponentAlive = false, sameTeam = false;
 bool observed = false, visibleNow = false, protectedTarget = false;
 bool observerFiring = false, opponentFiring = false, underPressure = false;
};
inline bool SightingAllowed(const Sighting &s, float now, float botNext, float teamNext)
{
 return s.enabled && !s.completed && !s.ffa && !s.freeze && !s.roundOver
  && s.observerAlive && s.opponentAlive && !s.sameTeam && s.observed && s.visibleNow
  && !s.protectedTarget && !s.observerFiring && !s.opponentFiring && !s.underPressure
  && now >= botNext && now >= teamNext;
}

}
