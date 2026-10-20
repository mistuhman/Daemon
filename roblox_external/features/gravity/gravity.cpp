#include "gravity.h"
// -----------------------------------------------------------------------------
//  features/gravity/gravity.cpp  --  Workspace gravity, with the original put back
// -----------------------------------------------------------------------------
//  Ported from jatosm/RobloxExternal-base's misc/gravity. The value lives in the
//  World instance behind Workspace::World, not on Workspace itself - the reference
//  reads it the same way, and this repo already had the pair of offsets
//  (Workspace::World, World::Gravity) from the world probe.
//
//  Everything hard about this feature is in the save/restore, and all of it is in
//  features/misc/body_rules.h's RememberOriginal:
//
//    * the second pass reads back OUR value, and remembering that as "the
//      original" is how a character ends up permanently at 20 studs/s;
//    * a place change hands out a NEW World instance - the saved number belongs to
//      the old one, so it is dropped rather than restored into an unrelated place;
//    * a value the game would never have (0, negative, 1e9) is not an original
//      either, so the standard 196.2 stands in rather than being restored.
//
//  Restoring is unconditional on release, because "gravity stayed at 20" is a
//  feature that does not turn off.
//
//  It REPORTS, because the README row for it is an unverified live round trip and a
//  silent feature cannot answer one. Two lines, both read-backs of the field itself:
//  the value it wrote 250ms later (a write the engine owns the field of is only worth
//  anything if it is still there after a few physics steps) and the restore, so "did
//  the original come back" has an answer in the log rather than in the character's
//  jump height. Same shape as the FOV changer's read-back.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>

#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "camera/camera_drive.h"
#include "misc/body_rules.h"
#include "platform/platform.h"

namespace features {

    namespace {

        struct State {
            bool      active = false;
            bool      saved_valid = false;
            float     saved = 196.2f;
            uintptr_t world = 0;
            // The read-back: when to look, and whether this activation has looked yet.
            uint64_t  readback_ms = 0;
            bool      readback_done = true;
        };
        State s_state;

        // Roblox's own default and the reference's fallback: a real place can set
        // anything, but a value outside this range was not a real setting.
        const float kGravityLo = 1.0f;
        const float kGravityHi = 2000.0f;
        const float kGravityDefault = 196.2f;

        uintptr_t World() {
            const uintptr_t ws = cam::WorkspaceAddr();
            if (!is_valid_address(ws)) return 0;
            const uintptr_t world = read<uintptr_t>(ws + Offsets::Workspace::World);
            return is_valid_address(world) ? world : 0;
        }

        void Restore() {
            if (s_state.saved_valid && is_valid_address(s_state.world)) {
                if (Offsets::World::Gravity) {
                    write<float>(s_state.world + Offsets::World::Gravity, s_state.saved);
                }
                LogLine("gravity: restored %.1f (the value the place had before the feature)",
                        s_state.saved);
            }
            s_state.active = false;
            s_state.saved_valid = false;
            s_state.world = 0;
        }

    }  // namespace

    void RunGravity() {
        const bool want = gravity_enabled &&
                          (gravity_keybind == 0 || platform::IsKeyDown(gravity_keybind));

        if (!want) {
            if (s_state.active) Restore();
            return;
        }

        const uintptr_t world = World();
        if (!is_valid_address(world)) {
            // Nothing to write into. Do not keep a save from a place that is gone.
            s_state.saved_valid = false;
            s_state.active = false;
            return;
        }

        if (world != s_state.world) {
            if (s_state.saved_valid)
                LogLine("gravity: the World instance changed (0x%llX -> 0x%llX) - the saved %.1f "
                        "belongs to the place that is gone, dropped rather than restored into "
                        "an unrelated place",
                        (unsigned long long)s_state.world, (unsigned long long)world, s_state.saved);
            s_state.world = world;
            s_state.saved_valid = false;
            s_state.active = false;
        }

        const float cur = read<float>(world + Offsets::World::Gravity);
        if (!s_state.active) {
            s_state.saved = RememberOriginal(cur, kGravityLo, kGravityHi, gravity_value,
                                             0.0f, kGravityDefault);
            s_state.saved_valid = true;
            s_state.active = true;
            s_state.readback_ms = platform::NowMs() + 250;   // several physics steps
            s_state.readback_done = false;
        }

        float value = gravity_value;
        if (!(value >= kGravityLo)) value = kGravityLo;
        if (value > kGravityHi) value = kGravityHi;
        if (cur != value)
            if (Offsets::World::Gravity) {
                write<float>(world + Offsets::World::Gravity, value);
            }

        if (!s_state.readback_done && platform::NowMs() >= s_state.readback_ms) {
            s_state.readback_done = true;
            const float back = read<float>(world + Offsets::World::Gravity);
            if (fabsf(back - value) > 0.01f)
                LogLine("gravity: wrote %.1f (original was %.1f) and read back %.1f after 250ms "
                        "- the write is NOT sticking, something else owns World::Gravity",
                        value, s_state.saved, back);
            else
                LogLine("gravity: wrote %.1f (original %.1f), read back %.1f - round trip ok",
                        value, s_state.saved, back);
        }
    }

}  // namespace features
