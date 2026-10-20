#include "fling.h"
// -----------------------------------------------------------------------------
//  features/fling/fling.cpp  --  spin the character, throw everyone touching it
// -----------------------------------------------------------------------------
//  How a fling works, stated because \"spin\" alone sounds like a visual gag:
//  the physics solver hands momentum to whatever two bodies' parts overlap, so a
//  character rotating at hundreds of radians per second sweeps its arms and legs
//  through a nearby player many times per second and each overlap transfers
//  energy. The observable result is players near you being launched.
//
//  The spin is ANGULAR velocity, written into both motion blocks from mover's
//  hold thread (mover::HoldSpin). It is deliberately not a rotation write: the
//  motion block's rotation is the engine's own output and fighting it every step
//  is what the old \"spin\" experiments did - the body jitters in place instead of
//  spinning, and the solver sees a snapped orientation rather than a velocity.
//
//  The axis is re-rolled every ~120ms. A fixed Y axis spins on the spot and
//  flings almost nothing (a torso is close to rotationally symmetric about Y);
//  tumbling through X and Z is what puts a limb through a neighbour.
// -----------------------------------------------------------------------------
#include <cmath>

#include "globals.h"
#include "memory.h"
#include "movement/mover.h"
#include "platform/platform.h"

namespace features {

    namespace {
        uintptr_t s_last_prim = 0;
        uint64_t  s_axis_at = 0;
        float     s_axis[3] = { 0.0f, 1.0f, 0.0f };
        bool      s_said = false;

        uint32_t s_rng = 0x9E3779B9u;
        float frand() {
            s_rng ^= s_rng << 13;
            s_rng ^= s_rng >> 17;
            s_rng ^= s_rng << 5;
            return (float)(s_rng & 0xFFFFu) / 65535.0f;
        }
    }

    void ReleaseFling() {
        mover::ReleaseSpin();
        s_last_prim = 0;
        s_said = false;
    }

    void RunFling() {
        if (!fling_enabled) {
            if (s_last_prim) ReleaseFling();
            return;
        }

        const mover::Body& body = mover::Local();
        if (!body.valid || !is_valid_address(body.primitive)) return;

        // New body (respawn) or first run: nothing to restore, just start.
        if (body.primitive != s_last_prim) {
            s_last_prim = body.primitive;
            s_said = false;
        }

        const uint64_t now = platform::NowMs();
        if (s_axis_at == 0 || now - s_axis_at >= 120) {
            s_axis_at = now;
            // Mostly horizontal axes (tumble) with a little Y so the body does
            // not sit in one plane; normalised so the speed setting is rad/s.
            float x = frand() * 2.0f - 1.0f;
            float y = (frand() * 2.0f - 1.0f) * 0.35f;
            float z = frand() * 2.0f - 1.0f;
            const float len = std::sqrt(x * x + y * y + z * z);
            if (len > 1e-4f) { x /= len; y /= len; z /= len; }
            s_axis[0] = x; s_axis[1] = y; s_axis[2] = z;
        }

        float power = fling_power;
        if (!(power >= 1.0f)) power = 1.0f;
        if (power > 2000.0f) power = 2000.0f;

        const float ang[3] = { s_axis[0] * power, s_axis[1] * power, s_axis[2] * power };
        mover::HoldSpin(body.primitive, ang);

        if (!s_said) {
            s_said = true;
            LogLine("fling: spinning the character at %.0f rad/s about a randomized axis "
                    "(angular velocity, both motion blocks, unpaced from the hold thread)",
                    (double)power);
        }
    }

}  // namespace features
