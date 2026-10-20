#pragma once

// -----------------------------------------------------------------------------
//  features/fpscap/fps_field.h  --  the frame-rate arithmetic, provable here
// -----------------------------------------------------------------------------
//  Two separate jobs, one header, because both are "a frame rate that lives in
//  a 4-byte field" problems:
//
//   1. THE CLIENT'S CAP FIELD. The dump publishes TaskScheduler::MaxFPS as an
//      offset but says nothing about its type, and that type matters: writing a
//      float into an int field (or the reverse) is not "a cap that does not
//      work", it is a garbage value in the scheduler. The two possibilities are
//      unambiguous by value, though - the bit pattern of integer 60 read as a
//      float is 8.4e-44, and the bit pattern of 60.0f read as an int is
//      1,114,636,288 - so the field can be classified instead of guessed, and
//      an unrecognised field is left ALONE rather than written blind.
//
//   2. THE OVERLAY'S PACING. The overlay used to present as fast as the GPU
//      would accept (thousands of frames a second of an empty window while
//      idle). The arithmetic for fixing that is a period, a deadline that drops
//      missed frames instead of accumulating debt, and that is it.
//
//  Free of Windows, ImGui, memory and offsets: the Linux harness runs it.
// -----------------------------------------------------------------------------

#include <cmath>
#include <cstdint>

namespace features {
namespace fpsfield {

    enum Form : int {
        Unknown = 0,
        AsInt = 1,
        AsFloat = 2,
    };

    // The range a real frame-rate value lives in. 1 fps is the floor because 0
    // is "uncapped" in most of these fields and must not read as a cap; 1000 is
    // above any display and well below the value of a bit pattern read as the
    // wrong type.
    inline bool Plausible(float fps) {
        return std::isfinite(fps) && fps >= 1.0f && fps <= 1000.0f;
    }

    // What the 4 bytes actually hold. `as_int` and `as_float` are the SAME
    // bytes read both ways. Exactly one of them can be plausible, as shown
    // above; both or neither means "do not write here".
    inline Form Classify(int as_int, float as_float) {
        const bool i_ok = as_int >= 1 && as_int <= 1000;
        const bool f_ok = Plausible(as_float);
        if (i_ok && !f_ok) return AsInt;
        if (f_ok && !i_ok) return AsFloat;
        return Unknown;
    }

    // The value in the field, in fps. Unknown reads as 0 ("not a frame rate").
    inline float Value(Form form, int as_int, float as_float) {
        if (form == AsInt) return static_cast<float>(as_int);
        if (form == AsFloat) return as_float;
        return 0.0f;
    }

    // The bytes to write for `fps` in a classified field.
    inline bool Encode(float fps, Form form, int& out_int, float& out_float) {
        if (form == AsInt) { out_int = static_cast<int>(fps + 0.5f); out_float = 0.0f; return true; }
        if (form == AsFloat) { out_float = fps; out_int = 0; return true; }
        return false;
    }

    // The cap a slider or a hand-edited config asks for. <= 0 (and NaN) means
    // "no cap at all" - the switch is what disables the feature, and a config
    // that says 0 must never become a write of 0 into the scheduler.
    inline float ClampCap(float fps) {
        if (!(fps >= 1.0f) || !std::isfinite(fps)) return 0.0f;
        if (fps < 10.0f) return 10.0f;
        if (fps > 360.0f) return 360.0f;
        return fps;
    }

    // Nanoseconds per frame for a target rate; 0 means "do not pace".
    inline uint64_t PeriodNs(float fps) {
        if (!(fps >= 1.0f) || !std::isfinite(fps)) return 0;
        const double ns = 1e9 / static_cast<double>(fps);
        if (ns < 1.0e6) return 1000000;        // never above 1000 fps
        if (ns > 1.0e9) return 1000000000;
        return static_cast<uint64_t>(ns + 0.5);
    }

    // The next frame's deadline, from the previous one and the clock.
    //
    // A deadline already in the past is DROPPED, not carried forward: a frame
    // that overran (a resize, a font re-bake, the game hitching) must not be
    // followed by a burst of catch-up frames, which is both a worse stutter and
    // a worse CPU profile than the one slow frame.
    inline uint64_t NextDeadlineNs(uint64_t prev, uint64_t now, uint64_t period_ns) {
        if (period_ns == 0) return now;
        if (prev == 0) return now + period_ns;
        const uint64_t next = prev + period_ns;
        return next < now ? now : next;
    }

}  // namespace fpsfield
}  // namespace features
