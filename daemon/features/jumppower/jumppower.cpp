#include "jumppower.h"
// -----------------------------------------------------------------------------
//  features/jumppower/jumppower.cpp  --  the reference's misc/jumppower
// -----------------------------------------------------------------------------
//  Three fields, one unit:
//
//    Humanoid::JumpPower    studs/s the jump impulse produces
//    Humanoid::JumpHeight   studs of apex, the OTHER model
//    Humanoid::UseJumpPower which of the two the controller reads
//
//  The reference sets UseJumpPower = 1 and writes JumpPower plus a proportional
//  JumpHeight (value * 0.144), so the feature behaves the same whichever model
//  the place uses. It saves all three on the first pass with the humanoid and
//  puts them back on release, on respawn, on detach and on exit.
//
//  Two rules this repo adds, both learned from the physics trio:
//
//    * A CAPTURE IS ONLY TRUSTED WHEN IT READ. If JumpPower comes back 0 or
//      outside 1..1000 (a failed read), the fallback is the client's default
//      rather than a restore of zero - the same "remember only plausible
//      originals" rule features/misc/body_rules.h carries for gravity.
//    * A RESPAWN IS A NEW HUMANOID. When the cache publishes a different
//      humanoid address the saved values are dropped, not written into the new
//      body: that is how a restored jump power used to become a 0-jump body.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>

#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "cache.h"
#include "platform/platform.h"
#include "misc/body_rules.h"

namespace features {

    namespace {
        struct Saved {
            uintptr_t humanoid = 0;
            bool  captured = false;
            float power = 50.0f;
            float height = 7.2f;
            uint8_t use_power = 1;
        };
        Saved s_saved;
        bool  s_said = false;

        void Capture(uintptr_t humanoid) {
            const float p = read<float>(humanoid + Offsets::Humanoid::JumpPower);
            const float h = read<float>(humanoid + Offsets::Humanoid::JumpHeight);
            const uint8_t u = read<uint8_t>(humanoid + Offsets::Humanoid::UseJumpPower);
            s_saved.humanoid = humanoid;
            s_saved.power = PlausibleValue(p, 1.0f, 1000.0f) ? p : 50.0f;
            s_saved.height = PlausibleValue(h, 0.1f, 500.0f) ? h : 7.2f;
            s_saved.use_power = (u == 0 || u == 1) ? u : 1;
            s_saved.captured = true;
            LogLine("jump power: saved %.1f / height %.2f / model %s", (double)s_saved.power,
                    (double)s_saved.height, s_saved.use_power ? "power" : "height");
        }

        void Restore() {
            if (s_saved.captured && is_valid_address(s_saved.humanoid)) {
                const uintptr_t h = s_saved.humanoid;
                // Only a body that is still the same instance: a respawned
                // humanoid must not inherit the previous character's numbers.
                const cache::LocalPlayerData lp = cache::GetLocalPlayer();
                if (lp.valid && lp.humanoid_address == h) {
                    if (Offsets::Humanoid::JumpPower) {
                        write<float>(h + Offsets::Humanoid::JumpPower, s_saved.power);
                    }
                    if (Offsets::Humanoid::JumpHeight) {
                        write<float>(h + Offsets::Humanoid::JumpHeight, s_saved.height);
                    }
                    if (Offsets::Humanoid::UseJumpPower) {
                        write<uint8_t>(h + Offsets::Humanoid::UseJumpPower, s_saved.use_power);
                    }
                    LogLine("jump power: restored %.1f / %.2f", (double)s_saved.power,
                            (double)s_saved.height);
                } else if (lp.valid) {
                    // The same shape as gravity's World drop: the saved numbers
                    // belong to a body that is gone, and writing them into the
                    // new one is how a character ends up permanently odd.
                    LogLine("jump power: the Humanoid changed (0x%llX -> 0x%llX, a respawn) - "
                            "the saved %.1f / %.2f belongs to the old body, dropped rather "
                            "than written into the new one",
                            (unsigned long long)h, (unsigned long long)lp.humanoid_address,
                            (double)s_saved.power, (double)s_saved.height);
                }
            }
            s_saved = Saved{};
            s_said = false;
        }
    }  // namespace

    void ReleaseJumpPower() { Restore(); }

    void RunJumpPower() {
        const bool on = jumppower_enabled &&
                        (jumppower_keybind == 0 || platform::IsKeyDown(jumppower_keybind));
        if (!on) {
            if (s_saved.captured) Restore();
            return;
        }

        const cache::LocalPlayerData lp = cache::GetLocalPlayer();
        if (!lp.valid || !is_valid_address(lp.humanoid_address)) return;

        if (s_saved.captured && s_saved.humanoid != lp.humanoid_address)
            Restore();                       // a new character: the old save is dead
        if (!s_saved.captured) Capture(lp.humanoid_address);

        float value = jumppower_value;
        if (!(value >= 10.0f)) value = 10.0f;
        if (value > 500.0f) value = 500.0f;

        const uintptr_t h = lp.humanoid_address;
        // Read before write: this is a property the game re-applies on some
        // state changes, and writing every pass at 1kHz is a needless syscall.
        const float cur_p = read<float>(h + Offsets::Humanoid::JumpPower);
        const uint8_t cur_u = read<uint8_t>(h + Offsets::Humanoid::UseJumpPower);
        if (std::fabs(cur_p - value) < 0.01f && cur_u == 1) return;

        if (Offsets::Humanoid::UseJumpPower) {
            write<uint8_t>(h + Offsets::Humanoid::UseJumpPower, 1);
        }
        if (Offsets::Humanoid::JumpPower) {
            write<float>(h + Offsets::Humanoid::JumpPower, value);
        }
        // The reference's ratio: keep JumpHeight in step so a place that reads
        // the height model jumps the same distance.
        if (Offsets::Humanoid::JumpHeight) {
            write<float>(h + Offsets::Humanoid::JumpHeight, value * 0.144f);
        }
        if (!s_said) {
            s_said = true;
            LogLine("jump power: %.1f (was %.1f), height model switched to power", (double)value,
                    (double)s_saved.power);
        }
    }

}  // namespace features
