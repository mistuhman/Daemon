#pragma once

// -----------------------------------------------------------------------------
//  features/flight/stealth.h  --  the part of flight that can be proven here
// -----------------------------------------------------------------------------
//  Flight's problem is not "can we move the character" (it can, thousands of
//  velocity writes a second) - it is that moving it the obvious way looks like
//  nothing the engine would ever produce, and some games notice. Three of those
//  giveaways are arithmetic, and this header is that arithmetic:
//
//    1. THE GRAVITY ZERO. The old flight set Workspace.Gravity to 0 and put it
//       back afterwards. That is a GLOBAL, replicated property: every falling
//       object in the map changed behaviour, and a game script that reads
//       workspace.Gravity sees 0 - which is not a subtle signal. The engine's
//       own gravity is left alone in the stealth profile; instead the pinned
//       vertical velocity carries a bias of 0.5*g*dt, the value that makes one
//       physics step of gravity integrate to zero net displacement. The world
//       is untouched, and the motion is what a body held against gravity
//       actually looks like.
//
//    2. THE INSTANT VELOCITY. 0 to 70 studs/s in one write is an acceleration
//       no character reaches. `Approach` ramps each axis at a bounded rate, so
//       every take-off and turn is a curve with a slope, not a step.
//
//    3. THE UNBOUNDED SPEED. A vertical-only component used to be whatever the
//       speed slider said, so "up" could be 300 studs/s. The caps bound the
//       horizontal speed and the climb/descent separately, because climbing is
//       the axis a game is most likely to find implausible.
//
//  Plus two helpers the caller needs to keep itself honest: a hover trim (the
//  bias assumes a 60 Hz step; the trim measures the actual drift and corrects
//  it) and a write-verification test (if the assembly stops holding what we
//  pin, somebody else owns the body and continuing to write is both pointless
//  and loud).
//
//  Free of Windows, ImGui, memory and offsets: the Linux harness runs it.
// -----------------------------------------------------------------------------

#include <cmath>

namespace features {
namespace flights {

    inline float HorizontalSpeed(const float v[3]) {
        return std::sqrt(v[0] * v[0] + v[2] * v[2]);
    }

    // Move each component of `v` toward `target` by at most `accel * dt`.
    // No overshoot: a component that would pass its target lands exactly on it.
    // A non-finite or non-positive `dt`, or a non-positive `accel`, changes
    // nothing - a caller with a broken clock must not teleport the character.
    inline void Approach(float v[3], const float target[3], float accel, float dt) {
        if (!(accel > 0.0f) || !(dt > 0.0f) || !std::isfinite(dt)) return;
        const float step = accel * dt;
        for (int i = 0; i < 3; ++i) {
            const float d = target[i] - v[i];
            if (d > step) v[i] += step;
            else if (d < -step) v[i] -= step;
            else v[i] = target[i];
        }
    }

    // Scale the horizontal component down to `max_speed`, leaving the vertical
    // component alone. `max_speed <= 0` means "no cap" rather than "stop": a
    // zeroed setting is a bad config, not an instruction to freeze the player.
    inline void ClampHorizontal(float v[3], float max_speed) {
        if (!(max_speed > 0.0f)) return;
        const float h = HorizontalSpeed(v);
        if (!(h > max_speed) || !std::isfinite(h)) return;
        const float k = max_speed / h;
        v[0] *= k;
        v[2] *= k;
    }

    // Bound the vertical component: up to +max_up, down to -max_down.
    // Negative or non-finite caps are ignored (same rule as ClampHorizontal).
    inline void ClampVertical(float v[3], float max_up, float max_down) {
        if (max_up > 0.0f && v[1] > max_up) v[1] = max_up;
        if (max_down > 0.0f && v[1] < -max_down) v[1] = -max_down;
    }

    // The upward velocity whose one-step gravity decay integrates to zero
    // displacement. With the velocity pinned at v at the top of a step, the
    // engine integrates v - g*t over the step and moves the body by
    // v*dt - 0.5*g*dt^2; choosing v = 0.5*g*dt cancels it exactly.
    // A zero, negative or non-finite gravity or step is no bias at all.
    inline float HoverBias(float gravity, float step_seconds) {
        if (!(gravity > 0.0f) || !(step_seconds > 0.0f)) return 0.0f;
        const float b = 0.5f * gravity * step_seconds;
        return std::isfinite(b) ? b : 0.0f;
    }

    // Integral trim for the hover bias. `drift_per_s` is the measured vertical
    // speed while hovering (positive = creeping up); a drift inside `deadband`
    // is left alone, because chasing measurement noise turns a hover into a
    // slow bounce. The correction SUBTRACTS: a body creeping up is over-lifted
    // and needs less bias, a body sinking is under-lifted and needs more. (The
    // first version added it, which doubled the error on both sides - the Linux
    // cases caught it, and this comment is the reason they exist.)
    // The result is clamped to [lo, hi] so a bad measurement (a teleport, a
    // respawn) cannot wind the bias up without bound.
    inline float TrimBias(float bias, float drift_per_s, float gain, float deadband,
                          float lo, float hi) {
        if (!std::isfinite(bias) || !std::isfinite(drift_per_s) || !(gain > 0.0f)) return bias;
        if (std::fabs(drift_per_s) <= deadband) return bias;
        float next = bias - drift_per_s * gain;
        if (next < lo) next = lo;
        if (next > hi) next = hi;
        return next;
    }

    // Did the body keep the velocity we pinned? `abs_tol` covers the gravity
    // term and sampling; `frac_tol` covers a fast mover whose write lands a
    // step late. Deliberately tighter than it looks: at 50 studs/s a body that
    // stopped dead must MISS (a 50 % fraction said "held", which the cases
    // caught), while the vertical oscillation the physics step produces
    // (g*dt = 3.3 studs/s) must pass comfortably. A NaN read is never "held".
    inline bool VelocityHeld(const float written[3], const float read[3],
                             float abs_tol, float frac_tol) {
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(read[i])) return false;
            const float want = written[i] < 0.0f ? -written[i] : written[i];
            float tol = abs_tol + want * frac_tol;
            if (tol < abs_tol) tol = abs_tol;
            const float d = read[i] - written[i];
            if ((d < 0.0f ? -d : d) > tol) return false;
        }
        return true;
    }

}  // namespace flights
}  // namespace features
