#pragma once
#include <atomic>

// -----------------------------------------------------------------------------
//  Raw mouse deltas, for the features that need to look around
// -----------------------------------------------------------------------------
//  Freecam used to derive mouselook from GetCursorPos() deltas. That cannot work
//  while Roblox is the foreground window: in first/third person the game LOCKS
//  the cursor to the centre of its client area every frame. So the deltas we
//  read were "distance from wherever the game just re-centred it", which is
//  near-zero when the player is not moving and a sudden jump when they are - the
//  camera snapping to nothing and then flinging itself is exactly the reported
//  "camera bugs out".
//
//  WM_INPUT reports the mouse's actual movement, before any cursor clipping,
//  acceleration or re-centring. The overlay registers for it with
//  RIDEV_INPUTSINK so it arrives even though the overlay is never focused, and
//  accumulates it here. Consumers TAKE the accumulator (read-and-zero), so no
//  motion is counted twice and none is lost between polls.
//
//  It is in its own header, not in overlay.h, because that header drags d3d11
//  into every translation unit that includes it.
// -----------------------------------------------------------------------------

namespace input {

inline std::atomic<long> g_raw_dx{ 0 };
inline std::atomic<long> g_raw_dy{ 0 };
// Set while the overlay is actually receiving WM_INPUT. A consumer that finds
// this false must not fall back to cursor deltas - it should simply not turn.
inline std::atomic<bool> g_raw_ok{ false };

inline void AddRawMouse(long dx, long dy)
{
    g_raw_dx.fetch_add(dx, std::memory_order_relaxed);
    g_raw_dy.fetch_add(dy, std::memory_order_relaxed);
}

// Read-and-zero. Returns false when raw input is not available.
inline bool TakeRawMouse(float& dx, float& dy)
{
    dx = (float)g_raw_dx.exchange(0, std::memory_order_relaxed);
    dy = (float)g_raw_dy.exchange(0, std::memory_order_relaxed);
    return g_raw_ok.load(std::memory_order_relaxed);
}

inline void DropRawMouse()
{
    g_raw_dx.store(0, std::memory_order_relaxed);
    g_raw_dy.store(0, std::memory_order_relaxed);
}

} // namespace input
