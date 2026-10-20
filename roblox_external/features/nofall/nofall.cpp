#include "nofall.h"
// -----------------------------------------------------------------------------
//  features/nofall/nofall.cpp  --  a terminal velocity you can survive
// -----------------------------------------------------------------------------
//  Started from jatosm/RobloxExternal-base's misc/nofall (a velocity clamp),
//  then from our own velocity clamp, and the user's verdict on both was the
//  same: "no fall is still broken". The reason is in mover::HoldVertical: a
//  velocity clamp cannot stop a fall, because the solver integrates gravity
//  into the POSITION every step; clamping velocity only changes how fast the
//  position falls. This version pins the POSITION's Y at the hold thread's
//  ~66-writes-per-physics-step rate - the mechanism that makes teleports and
//  flight stick - and moves the anchor down with you while you walk, so slopes
//  and stairs still work and only a real fall (below 15 studs/s downward) is
//  frozen.
//
//  WHAT WAS STILL WRONG ("no fall still won't work", twice): the clamp could
//  never hold anything but a FALL SPEED. The slider's 0 released the feature
//  instead of being its strongest setting, and every other value let the body
//  keep descending - at 50 studs/s by default, which is a fast drop, not no
//  fall. 0 now means "hold the downward component at zero", and mover's hold
//  thread re-asserts it ~66 times per physics step, so gravity's per-step
//  velocity never survives to a position integration: the body hovers.
//
//  IT DID NOT WORK. One write from the feature loop loses a race the humanoid
//  damper runs every physics step: a lone velocity write survives only if it
//  lands just after a step boundary. That is the same measurement that made
//  infinite jump and flight use mover's hammered writes (mover.h: "a single
//  velocity write is a coin flip"), and no-fall was the one feature still
//  writing once.
//
//  The old clamp lived in mover's hold thread (mover::HoldFallSpeed):
//
//    * the hold thread re-reads the linear velocity ~4000 times a second and
//      rewrites the DOWNWARD component when it is past the cap - ~66 attempts
//      per physics step, which wins the race the feature loop could not;
//    * only the downward component is touched. x/z and every upward value are
//      left alone, so walking, jumping and flying are not clamped by this;
//    * a velocity burst (flight, infinite jump) owns the whole vector, and the
//      hold skips while one is live rather than fighting a feature the user
//      turned on more recently;
//    * the cap is a floor, not a freeze: a body slower than the cap is never
//      written to at all.
//
//  The feature keeps its own evidence: the first clamp it sees prints what the
//  body was doing, and once a second a read-back says whether the held value
//  survived. Silence on that second line is the proof; the line is the bug
//  report.
// -----------------------------------------------------------------------------
#include <cstdint>

#include "globals.h"
#include "memory.h"
#include "movement/mover.h"
#include "features/misc/body_rules.h"
#include "platform/platform.h"

namespace features {

    namespace {
        uintptr_t s_prim = 0;
        float     s_anchor = 0.0f;      // the world Y the body may not go below
        uint64_t  s_last_valid_ms = 0;
        uint64_t  s_watch_ms = 0;
        bool      s_said_hold = false;
        bool      s_said_drop = false;

        // A real fall, in studs per second downward. Anything slower is walking
        // down a slope or a stair, where the anchor must FOLLOW the body or the
        // feature would freeze you on every ramp.
        constexpr float kFallSpeed = 15.0f;
    }  // namespace

    void ReleaseNoFall() {
        mover::ReleaseVertical();
        s_prim = 0;
        s_last_valid_ms = 0;
        s_said_hold = false;
        s_said_drop = false;
    }

    void RunNoFall() {
        const bool held_key = nofall_keybind == 0 || platform::IsKeyDown(nofall_keybind);
        if (!nofall_enabled || !held_key) {
            if (s_prim) ReleaseNoFall();
            return;
        }

        const mover::Body& body = mover::Local();
        if (!body.valid || !is_valid_address(body.primitive)) {
            const uint64_t now = platform::NowMs();
            if (s_last_valid_ms != 0 && now - s_last_valid_ms > 2000) ReleaseNoFall();
            return;
        }
        s_last_valid_ms = platform::NowMs();

        // New body (respawn): the anchor is that body's own height, never the old
        // one's - carrying it across a respawn would drop you at the old floor.
        if (body.primitive != s_prim) {
            s_prim = body.primitive;
            s_anchor = body.pos[1];
            s_said_hold = false;
        }

        float vel[3] = { 0.0f, 0.0f, 0.0f };
        const bool have_vel = mover::ReadVelocity(vel);

        // The anchor follows the body whenever the body is not FALLING: walking,
        // standing, climbing and jumping all move it. Only a real descent freezes
        // it, and that freeze is what the hold thread enforces.
        const bool falling = have_vel && vel[1] < -kFallSpeed;
        if (!falling || body.pos[1] > s_anchor) s_anchor = body.pos[1];

        mover::HoldVertical(body.primitive, s_anchor);

        if (falling && !s_said_hold) {
            s_said_hold = true;
            LogLine("no fall: a fall started at %.0f studs/s - the position is now held at "
                    "y %.1f from mover's hold thread (%lu writes so far)", (double)vel[1],
                    (double)s_anchor, mover::VerticalHoldWrites());
        }

        // A read-back once a second. If the body is BELOW the anchor by more than a
        // stud, the hold is losing - and the line says so instead of leaving "it
        // still falls" unexplained.
        const uint64_t now = platform::NowMs();
        if (have_vel && (s_watch_ms == 0 || now - s_watch_ms >= 1000)) {
            s_watch_ms = now;
            if (body.pos[1] < s_anchor - 1.0f) {
                if (!s_said_drop) {
                    s_said_drop = true;
                    LogLine("no fall: the body is %.1f studs below the held level (v %.0f) - "
                            "the position hold is being overwritten; send this line",
                            (double)(s_anchor - body.pos[1]), (double)vel[1]);
                }
            } else {
                s_said_drop = false;
            }
        }
    }

}  // namespace features
