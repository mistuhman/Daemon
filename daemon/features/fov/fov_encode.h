#pragma once

// -----------------------------------------------------------------------------
//  features/fov/fov_encode.h  --  how the client stores Camera::FieldOfView
// -----------------------------------------------------------------------------
//  There are three forms in the wild and they are NOT interchangeable:
//
//    * DEGREES      -- 70.0 for a 70-degree view. What the API property means.
//    * RADIANS      -- 1.2217 for 70 degrees. What this repo writes, with a
//                      comment in fov_changer.cpp recording the day it was
//                      learned: writing 70 into a radians field is 70 radians
//                      (~4010 degrees), which wraps the projection and shows the
//                      world upside down.
//    * TANGENT SCALE-- tan(fov/2) = 0.7002 for 70 degrees: what a renderer wants
//                      when it builds the projection itself. This is the form
//                      the reference repo's zoom writes (jatosm/RobloxExternal-base,
//                      misc/zoom: `tan(deg * pi/180 * 0.5)`).
//
//  Two of the three overlap in value space and cannot be told apart by a single
//  read: 1.2217 is "70 degrees in radians" AND "101 degrees as a tangent scale",
//  and 0.7002 is "40 degrees in radians" AND "70 degrees as a tangent scale".
//  So this header does not pretend to guess. It classifies only where the value
//  is unambiguous, defaults to the form this repo has evidence for, and keeps the
//  encode/decode pair in ONE place so a client that changes form is one function
//  to fix instead of four scattered writes.
//
//  Free of Windows, ImGui and the offsets table: the Linux harness runs cases
//  over it (glass-obsidian/tools/world_ray_test.cpp).
// -----------------------------------------------------------------------------

#include <cmath>   // tanf / atanf - the header is MSVC-built, so no GCC builtins

namespace features {

    enum FovForm {
        FovRadians = 0,       // 1.2217 == 70 degrees  (this repo's form)
        FovDegrees = 1,       // 70.0    == 70 degrees
        FovTangentScale = 2,  // 0.7002  == 70 degrees
    };

    inline float FovPi() { return 3.14159265358979323846f; }

    // What form a value read from the field is most likely to be in.
    //
    //   > 2.3        degrees: no sane FOV is 2.3 radians (132 degrees) or more,
    //                and no tangent scale in that range is a real view.
    //   <= 0.02      tangent scale: 0.02 radians is 1.1 degrees (a tunnel) and
    //                0.02 as a tangent scale is a 2.3-degree view, so a value
    //                this small only arrives from a renderer-side write.
    //   otherwise    radians, the form this repo has evidence for. A tangent
    //                scale at a normal FOV is indistinguishable from radians by
    //                value alone (see the header comment) - if a live client ever
    //                stores that form, this is the line that changes.
    inline FovForm ClassifyFov(float stored) {
        if (!(stored > 0.0f)) return FovRadians;   // 0 / negative / NaN: nothing to classify
        if (stored > 2.3f) return FovDegrees;
        if (stored <= 0.02f) return FovTangentScale;
        return FovRadians;
    }

    // Degrees -> the value the field holds, in the given form.
    inline float EncodeFovDegrees(float deg, FovForm form) {
        const float pi = FovPi();
        switch (form) {
            case FovDegrees:      return deg;
            case FovTangentScale: return tanf(deg * (pi / 180.0f) * 0.5f);
            case FovRadians:
            default:              return deg * (pi / 180.0f);
        }
    }

    // The field's value -> degrees. The inverse of EncodeFovDegrees, and the
    // half that a "restore what was there before" path needs.
    inline float DecodeFovDegrees(float stored, FovForm form) {
        const float pi = FovPi();
        switch (form) {
            case FovDegrees:      return stored;
            case FovTangentScale: return 2.0f * atanf(stored) * (180.0f / pi);
            case FovRadians:
            default:              return stored * (180.0f / pi);
        }
    }

    // A FOV in degrees that the projection can actually use.
    inline float ClampFovDegrees(float deg) {
        if (!(deg > 0.0f)) return 70.0f;   // 0 / NaN: the client's own default
        if (deg < 5.0f) return 5.0f;
        if (deg > 120.0f) return 120.0f;
        return deg;
    }

}  // namespace features
