#pragma once

// -----------------------------------------------------------------------------
//  features/world/sky.h  --  the lighting arithmetic, provable off Windows
// -----------------------------------------------------------------------------
//  Ported from jatosm/RobloxExternal-base's visual/world/env: the reference does
//  not merely show a time-of-day slider, it RECOMPUTES the sky for that hour -
//  gradient top and bottom, light colour, sun and moon positions, which of the
//  two is the light source. Those tables and the solar-position maths are pure
//  functions of (hours, latitude), so they live here and the Linux harness runs
//  them; env.cpp only does the reads and the writes.
//
//  The one thing this header deliberately does NOT do is write: an hour that is
//  out of range, a latitude that failed to read, a NaN - all of that is decided
//  by the caller, because "produce a wrong sky" and "write a wrong sky" are
//  different mistakes.
// -----------------------------------------------------------------------------

#include <cmath>

namespace features {
namespace sky {

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    inline Vec3 Add(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3 Sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3 Mul(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline Vec3 Cross(const Vec3& a, const Vec3& b) {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Length(const Vec3& a) {
        return std::sqrt(Dot(a, a));
    }
    inline Vec3 Normalised(const Vec3& a) {
        const float l = Length(a);
        return l > 1e-6f ? Mul(a, 1.0f / l) : Vec3{};
    }

    // Piecewise-linear colour ramp over seconds-of-day. Out of range clamps to
    // the end colours, exactly as the reference's spline does - an hour past
    // midnight must not extrapolate into negative light.
    inline Vec3 Spline(float t, const float* times, const Vec3* colors, int count) {
        if (count <= 0) return { 1.0f, 1.0f, 1.0f };
        if (t <= times[0]) return colors[0];
        if (t >= times[count - 1]) return colors[count - 1];
        for (int i = 0; i < count - 1; ++i) {
            if (t < times[i] || t > times[i + 1]) continue;
            const float span = times[i + 1] - times[i];
            const float a = span > 0.0f ? (t - times[i]) / span : 0.0f;
            return Add(Mul(colors[i], 1.0f - a), Mul(colors[i + 1], a));
        }
        return colors[0];
    }

    constexpr float kPi      = 3.1415927f;
    constexpr float kHour    = 3600.0f;
    constexpr float kDay     = 86400.0f;
    constexpr float kSunrise = 6.0f * kHour;
    constexpr float kSunset  = 18.0f * kHour;
    constexpr float kRiseSet = 1.0f * kHour;
    constexpr float kSolarYear  = 365.2564f * kDay;
    constexpr float kHalfSolar  = 182.6282f;

    // The ambient / gradient troughs the reference draws: near-black at night,
    // a dawn ramp, full white in the day, dusk. Grad bottom is the warmer half.
    inline Vec3 SkyAmbient(float t) {
        static const float times[] = {
            0.0f, kSunrise - 2.0f * kHour, kSunrise - kHour, kSunrise - kHour * 0.5f,
            kSunrise, kSunrise + kRiseSet, kSunset - kRiseSet, kSunset,
            kSunset + kHour / 3.0f, kDay
        };
        static const Vec3 colors[] = {
            { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.07f, 0.07f, 0.1f },
            { 0.2f, 0.15f, 0.01f }, { 0.2f, 0.15f, 0.01f }, { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f }, { 0.4f, 0.2f, 0.05f }, { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f }
        };
        return Spline(t, times, colors, 10);
    }

    inline Vec3 SkyAmbient2(float t) {
        static const float times[] = {
            0.0f, kSunrise - 3.0f * kHour, kSunrise - 2.0f * kHour, kSunrise - kHour * 0.5f,
            kSunrise, kSunrise + kRiseSet, kSunset - kRiseSet, kSunset,
            kSunset + kHour / 3.0f, kSunset + 2.0f * kHour, kSunset + 3.0f * kHour, kDay
        };
        static const Vec3 colors[] = {
            { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.21f, 0.21f, 0.28f },
            { 0.4f, 0.3f, 0.3f }, { 0.3f, 0.2f, 0.3f }, { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f }, { 0.4f, 0.3f, 0.2f }, { 0.3f, 0.2f, 0.3f },
            { 0.3f, 0.2f, 0.3f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }
        };
        return Spline(t, times, colors, 12);
    }

    inline Vec3 LightColor(float t) {
        const Vec3 day{ 0.75f, 0.75f, 0.75f };
        static const float times[] = {
            0.0f, kSunrise - kHour, kSunrise, kSunrise + kRiseSet * 0.25f,
            kSunrise + kRiseSet, kSunset - kRiseSet, kSunset - kRiseSet * 0.5f,
            kSunset, kSunset + kHour * 0.5f, kDay
        };
        static const Vec3 colors[] = {
            { 0.2f, 0.2f, 0.2f }, { 0.1f, 0.1f, 0.1f }, { 0.0f, 0.0f, 0.0f },
            { 0.6f, 0.6f, 0.0f }, day, day, { 0.1f, 0.1f, 0.075f },
            { 0.1f, 0.05f, 0.05f }, { 0.1f, 0.1f, 0.1f }, { 0.2f, 0.2f, 0.2f }
        };
        return Spline(t, times, colors, 10);
    }

    // Rotate v about `axis` by `angle` (Rodrigues). Axis is normalised first so
    // a degenerate cross product cannot turn the sun into a NaN vector.
    inline Vec3 RotateAxisAngle(const Vec3& v, const Vec3& axis, float angle) {
        const Vec3 a = Normalised(axis);
        const float c = std::cos(angle), s = std::sin(angle);
        return Add(Add(Mul(v, c), Mul(Cross(a, v), s)), Mul(a, Dot(a, v) * (1.0f - c)));
    }

    // The sun's direction for an absolute time and a latitude: the reference's
    // seasonal tilt, which is what makes a 6am sun rise in the right place
    // instead of straight overhead.
    inline Vec3 SunPosition(float source_angle, float abs_seconds, float latitude_deg) {
        const Vec3 sun{ std::sin(source_angle), -std::cos(source_angle), 0.0f };
        const float day_of_year =
            (abs_seconds - std::floor(abs_seconds / kSolarYear) * kSolarYear) / kDay;
        const float to_rad = kPi / 180.0f;
        const float sun_offset =
            -23.5f * to_rad * std::cos(kPi * (day_of_year - kHalfSolar) / kHalfSolar) -
            latitude_deg * to_rad;
        return RotateAxisAngle(sun, Cross(Vec3{ 0.0f, 0.0f, 1.0f }, sun), sun_offset);
    }

    // Everything one clock write needs, computed without touching the process.
    struct Clock {
        float clock_time = 12.0f;
        Vec3  gradient_top{};
        Vec3  gradient_bottom{};
        Vec3  light_color{};
        Vec3  sun{};
        Vec3  moon{};
        Vec3  light_direction{};
        int   source = 0;          // 0 = sun, 1 = moon
        bool  valid = false;
    };

    inline Clock ClockAt(float hours, float latitude_deg) {
        Clock c{};
        // NaN-safe: a bad slider or config value must not become a write.
        if (!(hours >= 0.0f) || !(hours <= 24.0f)) return c;
        if (!(latitude_deg >= -90.0f) || !(latitude_deg <= 90.0f)) latitude_deg = 0.0f;

        const float seconds = hours * kHour;
        // 24.0 is midnight of the next day, not an unrepresentable hour.
        const float time_of_day = seconds - std::floor(seconds / kDay) * kDay;
        const float source_angle = (time_of_day * 2.0f * kPi) / kDay;

        c.clock_time = hours;
        c.gradient_top = SkyAmbient(time_of_day);
        c.gradient_bottom = SkyAmbient2(time_of_day);
        c.light_color = LightColor(time_of_day);
        c.sun = SunPosition(source_angle, seconds, latitude_deg);
        c.moon = Mul(c.sun, -1.0f);
        const bool use_sun = c.sun.y > -0.3f;
        c.light_direction = use_sun ? c.sun : c.moon;
        c.source = use_sun ? 0 : 1;
        c.valid = true;
        return c;
    }

}  // namespace sky
}  // namespace features
