// SPDX-License-Identifier: MIT
#pragma once
namespace BhopRace {
// One nearby opener per door; release ownership after one second without contact.
struct DoorUseGate {
    int owner = 0;
    float retryAt = 0;
    bool Acquire(int slot, float now, bool closed, bool eligible) {
        if (!closed || !eligible || slot < 1 || slot > 32 || (now < retryAt && owner != slot)) return false;
        owner = slot;
        retryAt = now + 1.0f;
        return true;
    }
};
inline bool DoorClosed(bool atBottom, bool atTop, bool startsOpen) {
    return startsOpen ? atTop : atBottom;
}
}
