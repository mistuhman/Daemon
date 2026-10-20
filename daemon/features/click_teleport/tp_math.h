#pragma once

// The geometry behind click-to-teleport, with no Win32 and no process reads in it,
// because "I clicked and nothing happened" is a question about these six numbers
// more often than about memory: the ray, the plane it lands on, and how far along
// it goes. Keeping them in a header means a host can unit-test them (see
// glass-obsidian/tools/tp_math_test.cpp) and -- more usefully -- pin ScreenRay to be
// the exact inverse of the projection ESP already draws with, which is the only
// projection in this codebase that is confirmed to agree with what the player sees.
//
// Conventions, all Roblox world units (studs), right-handed, Y up:
//   camera position  cam[3]   -- Camera::Position
//   camera rotation  rot[9]   -- Camera::Rotation, the 3x3 of a CFrame, row-major
//   fov              fov_rad   -- Camera::FieldOfView, RADIANS on this client
//   viewport         vp_w/vp_h -- VisualEngine::Dimensions, in render-target pixels
//   cursor           px/py     -- pixels in the SAME space as the viewport, origin
//                                top-left (client area, not the desktop: see the
//                                ScreenToClient note in click_teleport.cpp)

#include <cmath>

namespace tpmath {

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s)        const { return { x * s, y * s, z * s }; }
};

inline float Length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

struct Ray
{
    Vec3 o;   // origin: the camera position
    Vec3 d;   // direction: normalised, unless ok == false
    bool ok = false;
    // Why a ray could not be built. Set so the caller can *say* what went wrong
    // instead of silently returning; a cheat that refuses to act and never explains
    // is indistinguishable from one that is switched off.
    const char* reason = "not attempted";
};

// Basis vectors out of a Roblox rotation matrix. The rows of the 3x3 are the axes
// (column-major read, exactly as esp.cpp's GetCamera does it): right = (0,3,6),
// up = (1,4,7), and the camera looks down -Z, so forward = -(2,5,8).
inline void CameraBasis(const float rot[9], Vec3& right, Vec3& up, Vec3& fwd)
{
    right = { rot[0], rot[3], rot[6] };
    up    = { rot[1], rot[4], rot[7] };
    fwd   = { -rot[2], -rot[5], -rot[8] };
}

// The ray through a pixel. Inverse of WorldToScreen by construction: the ESP code
// computes ndc = (0.5 * dot(right,d)*scale_x / w + 0.5) * vp_w with
// scale_x = 1/(tan(half_fov)*aspect), so unravelling it leaves
//     dir = fwd + right * (ndc_x * tan * aspect) + up * (ndc_y * tan)
// with ndc_y flipped, because screen y grows down while the projection's y grows up.
// The two scale factors cancel: this is why no focal length / ImagePlaneDepth read
// is needed.
inline Ray ScreenRay(const float cam[3], const float rot[9], float fov_rad,
                     float vp_w, float vp_h, float px, float py)
{
    Ray r;
    if (vp_w < 1.0f || vp_h < 1.0f) { r.reason = "viewport dimensions unread"; return r; }

    // Same fallback as the ESP projection, so a stale FOV write cannot make the two
    // disagree about where the cursor is pointing.
    if (!(fov_rad > 0.05f && fov_rad < 3.0f)) fov_rad = 1.2217f;   // ~70 deg

    Vec3 right, up, fwd;
    CameraBasis(rot, right, up, fwd);

    const float tan_half = std::tan(fov_rad * 0.5f);
    const float aspect   = vp_w / vp_h;
    const float ndc_x    = (2.0f * px / vp_w) - 1.0f;
    const float ndc_y    = 1.0f - (2.0f * py / vp_h);

    r.o = { cam[0], cam[1], cam[2] };
    r.d = fwd + right * (ndc_x * tan_half * aspect) + up * (ndc_y * tan_half);
    const float len = Length(r.d);
    if (!(len > 1e-5f)) { r.reason = "degenerate camera basis"; return r; }
    r.d = r.d * (1.0f / len);
    r.ok = true;
    r.reason = "ok";
    return r;
}

// Intersection with the horizontal plane y = plane_y, in front of the origin.
// Returns false when the ray is level or pointing up -- which is not an error, it
// just means the cursor is not on the ground, and the caller has to pick something
// else rather than teleport at the cap and hope.
inline bool HitPlaneY(const Ray& r, float plane_y, float& t)
{
    if (!r.ok) return false;
    const float dy = r.d.y;
    if (dy > -1e-4f) return false;
    t = (plane_y - r.o.y) / dy;
    return t > 0.25f;
}

// Ray vs axis-aligned box (centre + half extents), slab test. `t` is the distance to
// the first surface, or 0 when the origin is already inside.
inline bool HitAabb(const Ray& r, const float c[3], const float h[3], float& t)
{
    if (!r.ok) return false;
    const float o[3] = { r.o.x, r.o.y, r.o.z };
    const float d[3] = { r.d.x, r.d.y, r.d.z };
    float enter = 0.0f, leave = 1e30f;
    for (int a = 0; a < 3; ++a) {
        const float lo = c[a] - h[a], hi = c[a] + h[a];
        if (std::fabs(d[a]) < 1e-7f) {
            if (o[a] < lo || o[a] > hi) return false;      // parallel and outside
            continue;
        }
        float t0 = (lo - o[a]) / d[a];
        float t1 = (hi - o[a]) / d[a];
        if (t0 > t1) { const float s = t0; t0 = t1; t1 = s; }
        if (t0 > enter) enter = t0;
        if (t1 < leave) leave = t1;
        if (enter > leave) return false;
    }
    t = enter;
    return true;
}

inline float Clamp(float v, float lo, float hi)
{
    if (hi < lo) hi = lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace tpmath
