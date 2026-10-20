#include "hipheight.h"
// -----------------------------------------------------------------------------
//  features/hipheight/hipheight.cpp  --  where the character's feet sit
// -----------------------------------------------------------------------------
//  Ported from jatosm/RobloxExternal-base's misc/hipheight. HipHeight is how far
//  the HumanoidRootPart floats above the ground: raising it makes the character
//  stand taller (and lets it walk over low geometry the collision box used to
//  catch on), lowering it pushes the character into the floor - the engine usually
//  wins that argument, which is why the slider starts at 0 and not below.
//
//  The save/restore is the feature (features/misc/body_rules.h), with the respawn
//  rule made explicit: the Humanoid is a DIFFERENT instance after a respawn, and
//  writing the old original into the new character's Humanoid is a bug this repo
//  has already had once in mover's save/restore. So the saved value is dropped when
//  the humanoid address changes, and restored only into the humanoid it came from.
//
//  This one does NOT go through mover: mover owns the character's *motion* (the
//  CFrames and velocities) and a hip height is a property of the body that does not
//  fight a position write. What it shares with mover is the rule, not the writer.
//
//  It reports, for the same reason gravity does: the README row is an unverified live
//  round trip, and the two things that make it unverified - does the write survive a
//  few physics steps, and does the original come back on release (or on a respawn) -
//  are both read-backs of the field. It says so in the log instead of leaving the user
//  to guess from how tall their character looks.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>

#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "misc/body_rules.h"
#include "platform/platform.h"

namespace features {

    namespace {

        struct State {
            bool      active = false;
            bool      saved_valid = false;
            float     saved = 2.0f;
            uintptr_t humanoid = 0;
            uint64_t  readback_ms = 0;
            bool      readback_done = true;
        };
        State s_state;

        // A real hip height lives in here; 2.0 is what a default R15 avatar uses.
        const float kHipLo = 0.0f;
        const float kHipHi = 50.0f;
        const float kHipDefault = 2.0f;

        void Restore(uintptr_t humanoid) {
            if (s_state.saved_valid && is_valid_address(humanoid)) {
                if (Offsets::Humanoid::HipHeight) {
                    write<float>(humanoid + Offsets::Humanoid::HipHeight, s_state.saved);
                }
                LogLine("hip height: restored %.2f (the value this Humanoid had before the feature)",
                        s_state.saved);
            }
            s_state.active = false;
            s_state.saved_valid = false;
            s_state.humanoid = 0;
        }

    }  // namespace

    void RunHipHeight() {
        const bool want = hipheight_enabled &&
                          (hipheight_keybind == 0 || platform::IsKeyDown(hipheight_keybind));

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        const uintptr_t humanoid = lp.valid ? lp.humanoid_address : 0;

        if (!want) {
            // Restore into the humanoid we saved from, if either is still there.
            if (s_state.active) Restore(is_valid_address(humanoid) ? humanoid : s_state.humanoid);
            return;
        }

        if (!is_valid_address(humanoid)) {
            s_state.saved_valid = false;
            s_state.active = false;
            return;
        }

        if (humanoid != s_state.humanoid) {
            // Respawn / character swap: the number belongs to the old body.
            if (s_state.saved_valid)
                LogLine("hip height: the Humanoid changed (0x%llX -> 0x%llX, a respawn) - the "
                        "saved %.2f belongs to the old body, dropped rather than written into "
                        "the new one",
                        (unsigned long long)s_state.humanoid, (unsigned long long)humanoid,
                        s_state.saved);
            s_state.humanoid = humanoid;
            s_state.saved_valid = false;
            s_state.active = false;
        }

        const float cur = read<float>(humanoid + Offsets::Humanoid::HipHeight);
        if (!s_state.active) {
            s_state.saved = RememberOriginal(cur, kHipLo, kHipHi, hipheight_value,
                                             0.0f, kHipDefault);
            s_state.saved_valid = true;
            s_state.active = true;
            s_state.readback_ms = platform::NowMs() + 250;   // several physics steps
            s_state.readback_done = false;
        }

        float value = hipheight_value;
        if (!(value >= 0.0f)) value = 0.0f;
        if (value > 20.0f) value = 20.0f;
        if (cur != value)
            if (Offsets::Humanoid::HipHeight) {
                write<float>(humanoid + Offsets::Humanoid::HipHeight, value);
            }

        if (!s_state.readback_done && platform::NowMs() >= s_state.readback_ms) {
            s_state.readback_done = true;
            const float back = read<float>(humanoid + Offsets::Humanoid::HipHeight);
            if (fabsf(back - value) > 0.01f)
                LogLine("hip height: wrote %.2f (original was %.2f) and read back %.2f after "
                        "250ms - the write is NOT sticking, the Humanoid is re-writing it",
                        value, s_state.saved, back);
            else
                LogLine("hip height: wrote %.2f (original %.2f), read back %.2f - round trip ok",
                        value, s_state.saved, back);
        }
    }

}  // namespace features
