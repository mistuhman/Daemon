// Unit test for features/click_teleport/tp_math.h -- the click-to-teleport geometry.
//
//   g++ -std=c++17 -O1 -I"../roblox_external" tools/tp_math_test.cpp -o build/tp_math_test
//   ./build/tp_math_test
//
// No Windows, no Roblox: the header is pure math, which is the whole reason it exists.
// The important case is round-trip against the projection ESP draws with. That
// projection is copied below from features/esp/esp.cpp (GetCamera + WorldToScreen);
// if someone changes one and not the other, this test fails, which is exactly the
// coupling we want -- "click where you want to go" is only correct if the ray under
// the cursor is the inverse of the pixels on screen.
#include "features/click_teleport/tp_math.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_fail = 0;
static int g_n = 0;

static void Check(bool cond, const char* what)
{
    ++g_n;
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++g_fail;
}

static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------------------
//  camera setup
// ---------------------------------------------------------------------------
struct Cam
{
    float pos[3] = { 0, 0, 0 };
    float rot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};

// Fill a Roblox row-major 3x3 from orthonormal axes, per the CFrame convention
// (right = (0,3,6), up = (1,4,7), look = -(2,5,8)).
static void SetBasis(Cam& c, const tpmath::Vec3& right, const tpmath::Vec3& up,
                     const tpmath::Vec3& fwd)
{
    c.rot[0] = right.x; c.rot[3] = right.y; c.rot[6] = right.z;
    c.rot[1] = up.x;    c.rot[4] = up.y;    c.rot[7] = up.z;
    c.rot[2] = -fwd.x;  c.rot[5] = -fwd.y;  c.rot[8] = -fwd.z;
}

static tpmath::Vec3 Cross(const tpmath::Vec3& a, const tpmath::Vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// yaw around +Y (0 = looking down -Z, Roblox's identity view), pitch up positive.
static Cam MakeCam(float px, float py, float pz, float yaw_deg, float pitch_deg)
{
    Cam c;
    c.pos[0] = px; c.pos[1] = py; c.pos[2] = pz;
    const float yaw = yaw_deg * 0.0174532925199432955f;
    const float pit = pitch_deg * 0.0174532925199432955f;
    tpmath::Vec3 fwd{ std::sin(yaw) * std::cos(pit), std::sin(pit),
                      -std::cos(yaw) * std::cos(pit) };
    const tpmath::Vec3 world_up{ 0, 1, 0 };
    const tpmath::Vec3 back{ -fwd.x, -fwd.y, -fwd.z };
    tpmath::Vec3 right = Cross(world_up, back);
    float l = tpmath::Length(right);
    if (l < 1e-6f) { right = { 1, 0, 0 }; } else { right = right * (1.0f / l); }
    tpmath::Vec3 up = Cross(back, right);
    l = tpmath::Length(up);
    up = up * (1.0f / l);
    SetBasis(c, right, up, fwd);
    return c;
}

// ---------------------------------------------------------------------------
//  esp.cpp's projection, verbatim (client-space pixels; no overlay transform)
// ---------------------------------------------------------------------------
static bool ProjectWorldToScreen(const Cam& c, const tpmath::Vec3& world,
                                 float fov_rad, float vp_w, float vp_h,
                                 float out[2])
{
    tpmath::Vec3 right = { c.rot[0], c.rot[3], c.rot[6] };
    tpmath::Vec3 up    = { c.rot[1], c.rot[4], c.rot[7] };
    tpmath::Vec3 fwd   = { -c.rot[2], -c.rot[5], -c.rot[8] };

    const float tan_half = std::tan(fov_rad * 0.5f);
    const float aspect   = vp_w / vp_h;
    const float scale_x  = 1.0f / (tan_half * aspect);
    const float scale_y  = 1.0f / tan_half;

    const tpmath::Vec3 d{ world.x - c.pos[0], world.y - c.pos[1], world.z - c.pos[2] };
    const float dotr = d.x * right.x + d.y * right.y + d.z * right.z;
    const float dotu = d.x * up.x + d.y * up.y + d.z * up.z;
    const float dotf = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;
    if (dotf < 0.01f) return false;                       // behind the camera

    const float screen_x = dotr * scale_x;
    const float screen_y = dotu * scale_y;
    out[0] = (vp_w * 0.5f * screen_x / dotf) + vp_w * 0.5f;
    out[1] = -(vp_h * 0.5f * screen_y / dotf) + vp_h * 0.5f;
    return true;
}

// distance from `p` to the infinite line through ray `r`
static float PointRayDist(const tpmath::Ray& r, const tpmath::Vec3& p)
{
    const tpmath::Vec3 v{ p.x - r.o.x, p.y - r.o.y, p.z - r.o.z };
    const float t = v.x * r.d.x + v.y * r.d.y + v.z * r.d.z;
    const tpmath::Vec3 q{ r.o.x + r.d.x * t, r.o.y + r.d.y * t, r.o.z + r.d.z * t };
    return tpmath::Length({ p.x - q.x, p.y - q.y, p.z - q.z });
}

int main()
{
    using tpmath::Vec3;

    // ---- 1. round trip: project a world point, unproject its pixel ----------
    {
        const float fov = 1.2217f;                     // 70 deg, the ESP fallback
        const float views[][5] = {
            //  cam x  cam y  cam z   yaw   pitch      (third person sits above and
            { 0.0f, 5.0f, 12.0f,   0.0f, -12.0f },     //  behind, pitched down -- the
            { 0.0f, 3.0f,  0.0f,   0.0f,   0.0f },     //  case that used to land in
            { 4.0f, 2.0f, -6.0f,  37.0f, -30.0f },     //  the ground and stop)
            { 0.0f, 30.0f, 40.0f, 180.0f, -45.0f },
            { -9.0f, 1.7f, 3.0f, -71.0f, 5.0f },
        };
        const float vps[][2] = { { 1920.0f, 1080.0f }, { 1280.0f, 1024.0f },
                                 { 800.0f, 600.0f } };
        float worst = 0.0f;
        int cases = 0;
        for (size_t vi = 0; vi < sizeof(views) / sizeof(views[0]); ++vi) {
            for (size_t pi = 0; pi < sizeof(vps) / sizeof(vps[0]); ++pi) {
                const Cam c = MakeCam(views[vi][0], views[vi][1], views[vi][2],
                                      views[vi][3], views[vi][4]);
                const float vw = vps[pi][0], vh = vps[pi][1];
                // a grid of pixels spanning the frame, each with a point at several
                // depths along its own ray
                for (int iy = 0; iy < 5; ++iy) {
                    for (int ix = 0; ix < 7; ++ix) {
                        const float px = (ix + 0.5f) * vw / 7.0f;
                        const float py = (iy + 0.5f) * vh / 5.0f;
                        tpmath::Ray r = tpmath::ScreenRay(c.pos, c.rot, fov, vw, vh, px, py);
                        if (!r.ok) continue;
                        const float dists[3] = { 6.0f, 33.0f, 147.0f };
                        for (int di = 0; di < 3; ++di) {
                            const float dist = dists[di];
                            const Vec3 p{ r.o.x + r.d.x * dist, r.o.y + r.d.y * dist,
                                          r.o.z + r.d.z * dist };
                            float sp[2];
                            if (!ProjectWorldToScreen(c, p, fov, vw, vh, sp)) continue;
                            // the projected pixel, unprojected, must reproduce the ray
                            tpmath::Ray r2 = tpmath::ScreenRay(c.pos, c.rot, fov, vw, vh,
                                                               sp[0], sp[1]);
                            if (!r2.ok) continue;
                            ++cases;
                            const float e = PointRayDist(r2, p);
                            if (e > worst) worst = e;
                        }
                    }
                }
            }
        }
        printf("    round trip: %d cases, worst miss %.4g studs\n", cases, (double)worst);
        Check(cases > 300 && worst < 1e-2f,
              "the cursor ray is the exact inverse of the ESP projection");
    }

    // ---- 2. the centre of the screen is where the camera looks --------------
    {
        const Cam c = MakeCam(1.0f, 2.0f, 3.0f, 25.0f, -10.0f);
        tpmath::Ray r = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1920, 1080, 960, 540);
        Vec3 right, up, fwd;
        tpmath::CameraBasis(c.rot, right, up, fwd);
        const float dot = r.d.x * fwd.x + r.d.y * fwd.y + r.d.z * fwd.z;
        Check(r.ok && Near(dot, 1.0f, 1e-5f), "centre pixel == camera forward");

        const tpmath::Ray hi = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1920, 1080, 960, 120);
        const tpmath::Ray lo = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1920, 1080, 960, 960);
        Check(hi.d.y > 0.0f && lo.d.y < 0.0f, "a cursor above centre looks up, below looks down");
    }

    // ---- 3. the ground plane ------------------------------------------------
    {
        const Cam c = MakeCam(0.0f, 10.0f, 0.0f, 0.0f, -45.0f);
        const tpmath::Ray r = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1000, 1000, 500, 500);
        float t = 0.0f;
        const bool hit = tpmath::HitPlaneY(r, 0.0f, t);
        const Vec3 p{ r.o.x + r.d.x * t, r.o.y + r.d.y * t, r.o.z + r.d.z * t };
        Check(hit && Near(p.y, 0.0f, 1e-3f), "looking 45 deg down hits the plane at eye level distance");
        Check(hit && t > 14.0f && t < 14.2f, "that distance is ~10 * sqrt(2) studs");

        const Cam level = MakeCam(0.0f, 3.0f, 0.0f, 0.0f, 0.0f);
        const tpmath::Ray rl = tpmath::ScreenRay(level.pos, level.rot, 1.2217f, 1000, 1000, 500, 500);
        float tjunk = 12345.0f;
        Check(!tpmath::HitPlaneY(rl, 0.0f, tjunk), "a level ray reports no ground, rather than a fake one");
        Check(tjunk == 12345.0f, "and leaves the caller's distance alone");

        const Cam up = MakeCam(0.0f, 3.0f, 0.0f, 0.0f, 30.0f);
        const tpmath::Ray ru = tpmath::ScreenRay(up.pos, up.rot, 1.2217f, 1000, 1000, 500, 500);
        Check(!tpmath::HitPlaneY(ru, 0.0f, tjunk), "a ray aimed at the sky reports no ground");
    }

    // ---- 4. clicking a player ------------------------------------------------
    {
        const Cam c = MakeCam(0.0f, 4.0f, 10.0f, 0.0f, -6.0f);
        // a character standing at the origin: root at 3 studs, canonical box extents
        const float ctr[3] = { 0.0f, 3.0f, 0.0f }, half[3] = { 1.0f, 2.5f, 1.0f };
        const tpmath::Ray mid = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1600, 900, 800, 470);
        float t = 0.0f;
        Check(tpmath::HitAabb(mid, ctr, half, t), "the centre of the frame hits a player standing ahead");
        const tpmath::Ray corner = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1600, 900, 40.0f, 40.0f);
        Check(!tpmath::HitAabb(corner, ctr, half, t), "and a cursor in the corner does not");
        const float big[3] = { 200.0f, 200.0f, 200.0f };
        const tpmath::Ray inside = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1600, 900, 800, 470);
        float tin = -1.0f;
        Check(tpmath::HitAabb(inside, ctr, big, tin) && Near(tin, 0.0f, 1e-5f),
              "an origin inside the box counts as a hit at zero distance");
    }

    // ---- 5. degenerate input -------------------------------------------------
    {
        const Cam c = MakeCam(0.0f, 3.0f, 0.0f, 0.0f, -20.0f);
        tpmath::Ray bad = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 0.0f, 900.0f, 100, 100);
        Check(!bad.ok && std::strstr(bad.reason, "viewport") != nullptr,
              "a zero viewport explains itself instead of dividing by zero");
        // a stale FOV write (0 or garbage) must fall back to the same value ESP uses
        const tpmath::Ray f0 = tpmath::ScreenRay(c.pos, c.rot, 0.0f, 1600, 900, 400, 300);
        const tpmath::Ray f70 = tpmath::ScreenRay(c.pos, c.rot, 1.2217f, 1600, 900, 400, 300);
        Check(f0.ok && Near(f0.d.x, f70.d.x, 1e-6f) && Near(f0.d.z, f70.d.z, 1e-6f),
              "an unreadable FOV falls back to 70 deg, like the ESP projection");
        Check(Near(tpmath::Length(f70.d), 1.0f, 1e-6f), "the direction comes out normalised");
        Check(tpmath::Clamp(50.0f, 5.0f, 30.0f) == 30.0f && tpmath::Clamp(1.0f, 5.0f, 30.0f) == 5.0f,
              "the range clamp works both ways");
        Check(tpmath::Clamp(7.0f, 20.0f, 3.0f) == 20.0f, "an inverted range cannot return the high bound");
    }

    printf("%s  (%d checks)\n", g_fail ? "FAILURES" : "all ok", g_n);
    return g_fail ? 1 : 0;
}
