#include "antiafk.h"
// -----------------------------------------------------------------------------
//  features/antiafk/antiafk.cpp  --  a spacebar tap every few minutes
// -----------------------------------------------------------------------------
//  Ported from jatosm/RobloxExternal-base's misc/antiafk. Roblox disconnects a
//  client that reports no input for ~20 minutes, which is the whole feature: it
//  taps space on an interval so an idle session survives.
//
//  Four decisions worth stating, because each one is a way this feature can be
//  worse than nothing:
//
//    1. FOREGROUND ONLY. The tap goes through the same SendInput path the
//       triggerbot's click uses, and SendInput delivers to whatever window has
//       focus at that instant. Given focus, the tap lands in Roblox. Without it,
//       the tap lands in whatever the user switched to - a browser, an editor,
//       a chat - which is both useless and disruptive. So the window is checked
//       immediately before the tap, not once at enable time.
//    2. THE TIMER RESETS ON (RE)ENABLE, ATTACH AND DEFOCUS. Turning the feature
//       on with a stale timer would tap the moment it was switched on; leaving
//       it focused-only without resetting on focus loss would tap on the first
//       frame after the user comes back - i.e. the one moment when a random
//       space press is most obviously not theirs.
//    3. THE INTERVAL IS CLAMPED (1..15 minutes). 0 would be a spacebar macro;
//       the reference clamps for the same reason.
//    4. IT IS A TAP, NOT A HOLD: KEYDOWN and KEYUP in one SendInput batch, so
//       the character jumps once instead of holding the key down while the
//       feature runs. (A jump is the visible side effect, and it is harmless:
//       the character is idle or it would not need this.)
// -----------------------------------------------------------------------------
#include <cstdint>

#include "globals.h"
#include "memory.h"
#include "platform/platform.h"

namespace features {

    namespace {

        struct State {
            uint64_t last_tap_ms = 0;
            bool armed = false;   // the timer has been started and no tap is pending
        };
        State s_state;

        // How long between taps, in ms, from the minutes setting. Clamped here so
        // a hand-edited cfg cannot turn this into a key macro.
        uint64_t IntervalMs() {
            float minutes = antiafk_interval;
            if (!(minutes > 1.0f)) minutes = 1.0f;
            if (minutes > 15.0f) minutes = 15.0f;
            return (uint64_t)(minutes * 60000.0f);
        }

    }  // namespace

    void RunAntiafk() {
        const uint64_t now = platform::NowMs();

        // Anything that should keep the timer from firing rearms it instead.
        if (!antiafk_enabled || !g_base_address) {
            s_state.armed = false;
            return;
        }

        if (!platform::GameWindowForeground()) {
            // Defocused: rearm, so the first tap after the user comes back is a
            // full interval away - see decision 2.
            s_state.armed = false;
            s_state.last_tap_ms = now;
            return;
        }

        if (!s_state.armed) {
            s_state.armed = true;
            s_state.last_tap_ms = now;
            return;   // no tap on the frame it was armed
        }

        if (now - s_state.last_tap_ms < IntervalMs()) return;

        platform::TapKey(platform::key::SPACE);
        s_state.last_tap_ms = now;
    }

}  // namespace features
