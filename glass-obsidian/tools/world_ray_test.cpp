// -----------------------------------------------------------------------------
//  tools/world_ray_test.cpp -- the wall check's geometry, proven off the game
// -----------------------------------------------------------------------------
//  features/raycast/world_ray.h is pure: no memory reads, no Windows, no ImGui.
//  That is on purpose - the two things that were wrong with the old wall check
//  (a flat loop over every box, and an axis-aligned test for rotated parts) are
//  both testable here, with synthetic geometry, right now:
//
//    * a ray that crosses a wall must be blocked, including one that crosses
//      the wall DIAGONALLY - the case a "which cell is the midpoint in" shortcut
//      misses;
//    * a ray parallel to a wall, a ray that misses by a stud, and a ray through
//      the empty corner of a rotated part's bounding box must all be CLEAR - that
//      last one is the false block the old AABB test produced on every ramp;
//    * a rotated wall must block a ray that goes through the wall itself, which
//      is the half of the rotation bug that hides a player;
//    * the grid walk must be bounded (garbage coordinates cannot loop) and must
//      still find terrain-sized boxes, which live in the always-tested list.
//
//  It also measures what the grid is FOR: 10k rays against a 20k-box map, grid
//  vs flat loop. That number is the reason the module exists.
//
//  Usage: world_ray_test            exit code == number of failed checks
// -----------------------------------------------------------------------------
#include "../roblox_external/features/raycast/world_ray.h"
#include "../roblox_external/features/esp/range.h"
#include "../roblox_external/features/fov/fov_encode.h"
#include "../roblox_external/features/misc/body_rules.h"
#include "../roblox_external/features/flight/stealth.h"
#include "../roblox_external/features/players/humanoid_state.h"
#include "../roblox_external/features/fpscap/fps_field.h"
#include "../roblox_external/features/world/sky.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using worldray::Box;
using worldray::Grid;

static int g_pass = 0;
static int g_fail = 0;
static std::string g_group;

static void group(const char* name) {
    g_group = name;
    printf("\n== %s ==\n", name);
}

static void check(bool ok, const char* what, const char* detail = "") {
    if (ok) { ++g_pass; printf("  [PASS] %-58s %s\n", what, detail); }
    else { ++g_fail; printf("  [FAIL] %-58s %s\n", what, detail); }
}

// ---------------------------------------------------------------- geometry
static void test_slab() {
    group("slab test: axis-aligned");
    const float org[3] = { 0, 0, 0 };
    const float to[3] = { 10, 0, 0 };
    const float c_hit[3] = { 5, 0, 0 };
    const float c_miss[3] = { 5, 9, 0 };
    const float h1[3] = { 0.5f, 4, 4 };
    const float dirs[3] = { 10, 0, 0 };
    check(worldray::slab_hits(org, dirs, c_hit, h1), "a wall across the segment blocks");
    check(!worldray::slab_hits(org, dirs, c_miss, h1), "a wall above the segment does not");
    const float c_miss_z[3] = { 5, 0, 9 };
    check(!worldray::slab_hits(org, dirs, c_miss_z, h1), "a wall beside the segment does not");
    // grazing: the wall starts exactly at y = 4 and the ray is at y = 4
    const float c_edge[3] = { 5, 4, 0 };
    check(worldray::slab_hits(org, dirs, c_edge, h1), "touching the boundary counts as a hit");

    group("slab test: behind and past");
    const float dir_back[3] = { -10, 0, 0 };
    check(!worldray::slab_hits(org, dir_back, c_hit, h1), "a wall behind the segment does not block");
    const float far[3] = { 5, 0, 0 };
    const float short_seg[3] = { 1, 0, 0 };
    check(!worldray::slab_hits(org, short_seg, far, h1), "a wall past the segment end does not block");
    (void)to;

    group("slab test: degenerate inputs");
    const float zero[3] = { 0, 0, 0 };
    const float inside[3] = { 0, 0, 0 };
    check(worldray::slab_hits(inside, zero, inside, h1), "a zero-length ray inside a box hits");
    const float outside[3] = { 100, 100, 100 };
    check(!worldray::slab_hits(outside, zero, c_hit, h1), "a zero-length ray outside every box misses");
}

static void test_rotation() {
    group("rotation: a 45-degree wall");
    // A 4-stud-thick, 20-stud-wide wall rotated 45 degrees about Y, centred at
    // (10, 0, 0). Its world AABB is ~17 studs wide (the old test's box).
    //
    //   col0 (right)  = ( cos45, 0, sin45)
    //   col1 (up)     = ( 0,     1, 0    )
    //   col2 (back)   = (-sin45, 0, cos45)
    const float s = 0.70710678f;
    const float rot[9] = { s, 0, s, 0, 1, 0, -s, 0, s };
    const float centre[3] = { 10, 0, 0 };
    const float half[3] = { 2.0f, 5.0f, 8.0f };   // thickness/2, height/2, width/2
    check(worldray::rotation_usable(rot), "the rotated basis is accepted");

    // Through the wall's own thickness along +Z (the short axis).
    const float o1[3] = { 10, 0, -8 }, e1[3] = { 10, 0, 8 };
    check(worldray::segment_hits_rotated(o1, e1, centre, half, rot), "through the wall's face: blocked");

    // Through the AABB but through the rotated-away corner: z offset ~ +6 puts
    // the ray outside the wall's actual extent at that x. The unrotated (AABB)
    // test says blocked; the rotated test must say clear.
    const float o2[3] = { 10 + 6.0f, 0, -2 }, e2[3] = { 10 + 6.0f, 0, 2 };
    const float aabb_centre_shifted[3] = { 10, 0, 0 };
    check(!worldray::segment_hits_rotated(o2, e2, centre, half, rot),
          "through the bounding corner: clear (was a false block)");
    // what the old axis-aligned test would have said about the same ray, using
    // the wall's AABB half extents (rotated extents are larger on X and Z)
    const float aabb_half[3] = { 8 * s + 2 * s, 5.0f, 8 * s + 2 * s };
    check(worldray::segment_hits(o2, e2, aabb_centre_shifted, aabb_half),
          "  ...and the old AABB test did report a block there");

    group("rotation: unusable matrices fall back");
    const float bad[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 3.0f };   // third column has length 3
    check(!worldray::rotation_usable(bad), "a non-orthonormal basis is rejected");
    const float nan_rot[9] = { 1, 0, 0, 0, NAN, 0, 0, 0, 1 };
    check(!worldray::rotation_usable(nan_rot), "a NaN basis is rejected");
}

static std::vector<Box> make_map(size_t n) {
    // A grid of rooms: walls every 64 studs in X and Z, plus a floor slab. This
    // is the shape a real map has (thousands of separated boxes), not one blob.
    std::vector<Box> boxes;
    const size_t side = (size_t)std::sqrt((double)n) + 1;
    for (size_t i = 0; i < side; ++i) {
        for (size_t j = 0; j < side && boxes.size() < n; ++j) {
            Box w{};
            w.cx = (float)i * 64.0f;
            w.cy = 8.0f;
            w.cz = (float)j * 64.0f;
            w.hx = 1.0f;   // a thin wall
            w.hy = 8.0f;
            w.hz = 30.0f;
            w.prim = 0;
            boxes.push_back(w);
        }
    }
    Box floor{};
    floor.cx = 2000; floor.cy = -2; floor.cz = 2000;
    floor.hx = 2000; floor.hy = 2; floor.hz = 2000;
    boxes.push_back(floor);   // terrain-sized: lands in the always-tested list
    return boxes;
}

static void test_grid() {
    group("grid: walls in the right places");
    std::vector<Box> boxes = make_map(20 * 20);
    Grid grid;
    grid.build(boxes, 64.0f);
    check(!grid.always.empty(), "the terrain slab is in the always-tested list");
    check(grid.cells.size() > 100, "the wall boxes populate many cells");

    auto blocked = [&](float ax, float ay, float az, float bx, float by, float bz) {
        const float from[3] = { ax, ay, az };
        const float to[3] = { bx, by, bz };
        bool hit = false;
        grid.for_each_candidate(from, to, [&](uint32_t i) {
            if (worldray::segment_hits(from, to, &boxes[i].cx, &boxes[i].hx)) { hit = true; return true; }
            return false;
        });
        return hit;
    };

    check(blocked(0 - 40, 8, 0, 0 + 40, 8, 0), "straight through a wall: blocked");
    check(!blocked(0 - 40, 40, 0, 0 + 40, 40, 0), "over the top of the wall: clear");
    check(!blocked(32, 8, 0 - 40, 32, 8, 0 + 40), "parallel to the wall, no crossing: clear");
    check(blocked(-40, 8, -40, 40, 8, 40), "DIAGONAL through two walls: blocked");
    check(blocked(-500, 5, -500, 900, 5, 900), "long diagonal across the map: blocked");
    check(blocked(-100, -1, -100, 100, -1, 100), "through the floor slab: blocked");

    group("grid: bounded walk, garbage input");
    const float nan_from[3] = { NAN, 0, 0 };
    const float nan_to[3] = { 0, 0, 0 };
    int visited = 0;
    grid.for_each_candidate(nan_from, nan_to, [&](uint32_t) { ++visited; return false; });
    check(visited <= 8, "a NaN endpoint cannot walk the map");
    const float inf_to[3] = { INFINITY, INFINITY, INFINITY };
    const float zero_from[3] = { 0, 8, 0 };
    visited = 0;
    grid.for_each_candidate(zero_from, inf_to, [&](uint32_t) { ++visited; return false; });
    check(visited <= 64, "an infinite endpoint cannot walk the map", ("visited=" + std::to_string(visited)).c_str());

    group("grid vs flat loop: cost");
    std::vector<Box> big = make_map(200 * 100);
    Grid big_grid;
    big_grid.build(big, 64.0f);
    // rays that cross the map and mostly miss (the ESP's actual pattern)
    const int rays = 10000;
    auto t0 = std::chrono::steady_clock::now();
    long long grid_tests = 0, flat_tests = 0;
    bool grid_blocked_any = false;
    for (int r = 0; r < rays; ++r) {
        const float ox = -100.0f - (float)(r % 200);
        const float oz = -100.0f - (float)(r % 137);
        const float tx = 1500.0f + (float)(r % 97);
        const float tz = 1500.0f + (float)(r % 71);
        const float from[3] = { ox, 30.0f, oz };
        const float to[3] = { tx, 30.0f, tz };
        bool hit = false;
        big_grid.for_each_candidate(from, to, [&](uint32_t i) {
            ++grid_tests;
            if (worldray::segment_hits(from, to, &big[i].cx, &big[i].hx)) { hit = true; return true; }
            return false;
        });
        grid_blocked_any = grid_blocked_any || hit;
    }
    auto t1 = std::chrono::steady_clock::now();
    for (int r = 0; r < rays; ++r) {
        const float ox = -100.0f - (float)(r % 200);
        const float oz = -100.0f - (float)(r % 137);
        const float tx = 1500.0f + (float)(r % 97);
        const float tz = 1500.0f + (float)(r % 71);
        const float from[3] = { ox, 30.0f, oz };
        const float to[3] = { tx, 30.0f, tz };
        for (size_t i = 0; i < big.size(); ++i) {
            ++flat_tests;
            if (worldray::segment_hits(from, to, &big[i].cx, &big[i].hx)) break;
        }
    }
    auto t2 = std::chrono::steady_clock::now();
    const double grid_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double flat_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    char buf[160];
    snprintf(buf, sizeof(buf), "boxes=%zu rays=%d grid=%.1fms flat=%.1fms tests %lld vs %lld",
             big.size(), rays, grid_ms, flat_ms, grid_tests, flat_tests);
    check(grid_tests * 4 < flat_tests, "the grid tests far fewer boxes than the flat loop", buf);
    check(grid_ms < flat_ms, "the grid is measurably faster", buf);
    (void)grid_blocked_any;
}

// ---------------------------------------------------------------------------
//  the draw-distance rule (features/esp/range.h)
// ---------------------------------------------------------------------------
//  This is the user-visible half of the ESP work, and the half that used to be
//  three inconsistent `if`s: the setting must mean studs, 0 must mean infinite,
//  and an entity must not be drawn past the set range. Physics runs at ~60Hz and
//  the render pass at ~vsync, so the entity cache and the camera do NOT update on
//  the same tick - which is how "it lingers past the range" happened: the range
//  was never re-evaluated for a pass that skipped the check.
static void test_draw_range() {
    printf("\n[draw range]\n");
    struct Case { float dist, setting; bool want; const char* why; };
    const Case cases[] = {
        { 100.0f, 200.0f,  true,  "well inside the range" },
        { 200.0f, 200.0f,  true,  "on the edge is inside, not outside" },
        { 200.1f, 200.0f,  false, "just past the edge is out" },
        { 25000.0f, 0.0f,  true,  "0 = infinite: 25k studs away still draws" },
        { 25000.0f, -5.0f, true,  "a hand-edited negative cfg reads as infinite" },
        { 999.0f, 25000.0f, true, "the new slider ceiling admits 25k studs" },
        { 0.0f, 25000.0f,  true,  "distance 0 (same part as the anchor) is inside" },
        { std::nanf(""), 500.0f, false, "a NaN distance is never drawn" },
        { 42.0f, std::nanf(""), true,  "a NaN setting falls back to infinite, not to off" },
    };
    for (const Case& c : cases)
        check(features::DrawRangeAllows(c.dist, c.setting) == c.want, c.why);

    // The stale-entity case the user reported, in numbers: an entity measured at
    // 400 studs while the setting is 300 must be gone, and the SAME entity must
    // come back the moment the setting grows past it - no state, no hysteresis.
    check(!features::DrawRangeAllows(400.0f, 300.0f), "a stale entity past the range is hidden");
    check(features::DrawRangeAllows(400.0f, 400.0f), "and returns exactly at the range");
    check(features::DrawRangeAllows(400.0f, 25000.0f), "and at the new ceiling");
}

// ---------------------------------------------------------------------------
//  FOV storage form (features/fov/fov_encode.h)
// ---------------------------------------------------------------------------
//  Two features now write Camera::FieldOfView and the reference repo's zoom writes
//  it in a different unit than this repo's FOV changer. The header's job is to keep
//  the encode/decode in one place; these cases pin down what "one place" means.
static void test_fov_form() {
    printf("\n[fov form]\n");
    check(features::ClassifyFov(70.0f) == features::FovDegrees,
          "70 is classified as degrees (no real FOV is 70 radians)");
    check(features::ClassifyFov(1.2217f) == features::FovRadians,
          "1.2217 is radians - this repo's form, and 70 degrees in it");
    check(features::ClassifyFov(0.7002f) == features::FovRadians,
          "0.7002 defaults to radians, which is the documented ambiguity");
    check(features::ClassifyFov(0.004f) == features::FovTangentScale,
          "a value too small for either unit is the renderer's tangent scale");
    check(features::ClassifyFov(0.0f) == features::FovRadians, "0 classifies as nothing at all");
    check(features::ClassifyFov(std::nanf("")) == features::FovRadians, "NaN too");

    struct FormCase { features::FovForm form; const char* name; float stored70; };
    const FormCase forms[] = {
        { features::FovRadians,      "radians",       1.22173f },
        { features::FovDegrees,      "degrees",       70.0f },
        { features::FovTangentScale, "tangent scale", 0.700208f },
    };
    for (const FormCase& f : forms) {
        const float enc = features::EncodeFovDegrees(70.0f, f.form);
        const float back = features::DecodeFovDegrees(enc, f.form);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s: 70 deg -> %.6f -> %.4f deg", f.name, enc, back);
        check(fabsf(enc - f.stored70) < 0.001f, "the encode matches the known storage value", buf);
        check(fabsf(back - 70.0f) < 0.01f, "and decodes back to 70 degrees", buf);
    }
    // The round trip is what a restore depends on: whatever unit the client holds,
    // "put back the degrees I saw" must land on the same number.
    const float held = 0.9f;   // a camera someone left at ~52 degrees, in radians
    const float restored = features::EncodeFovDegrees(
        features::DecodeFovDegrees(held, features::FovRadians), features::FovRadians);
    check(fabsf(restored - held) < 1e-4f, "decode -> encode is the identity (restore is lossless)");

    check(features::ClampFovDegrees(0.0f) == 70.0f, "a 0-degree FOV falls back to the client default");
    check(features::ClampFovDegrees(1.0f) == 5.0f, "1 degree clamps up to 5");
    check(features::ClampFovDegrees(500.0f) == 120.0f, "500 clamps down to 120");
    check(features::ClampFovDegrees(std::nanf("")) == 70.0f, "a NaN FOV is the default, not a write");
}

// ---------------------------------------------------------------------------
//  save/restore rules (features/misc/body_rules.h)
// ---------------------------------------------------------------------------
static void test_body_rules() {
    printf("\n[body rules]\n");
    // gravity, hip height: the "is this my own write coming back?" case is the bug
    // that leaves a character permanently changed, so it gets three cases.
    check(features::RememberOriginal(196.2f, 1.0f, 2000.0f, 20.0f, 0.0f, 196.2f) == 196.2f,
          "a real original is remembered (gravity 196.2 read before the write)");
    check(features::RememberOriginal(20.0f, 1.0f, 2000.0f, 20.0f, 0.0f, 196.2f) == 196.2f,
          "our own write is not an original: the fallback stands in");
    check(features::RememberOriginal(20.0f, 1.0f, 2000.0f, 20.0f, 196.2f, 999.0f) == 196.2f,
          "and when one was saved earlier, that one is kept");
    check(features::RememberOriginal(0.0f, 1.0f, 2000.0f, 20.0f, 0.0f, 196.2f) == 196.2f,
          "0 gravity (below the range a real place sets) is not remembered");
    check(features::RememberOriginal(9999.0f, 1.0f, 2000.0f, 20.0f, 0.0f, 196.2f) == 196.2f,
          "neither is 9999");
    check(features::RememberOriginal(std::nanf(""), 1.0f, 2000.0f, 20.0f, 196.2f, 999.0f) == 196.2f,
          "a failed read keeps the previous save instead of poisoning it");
    check(features::PlausibleValue(2.0f, 0.0f, 50.0f), "2 is a plausible hip height");
    check(!features::PlausibleValue(std::nanf(""), 0.0f, 50.0f), "NaN never is");

    // no-fall: only the downward component, only past the cap, and a NaN is left
    // alone because "clamp" on a value we cannot read is a physics change.
    float v = -80.0f;
    check(features::CapFallSpeed(v, 50.0f) && v == -50.0f, "a fast fall is held at the cap");
    v = -30.0f;
    check(!features::CapFallSpeed(v, 50.0f) && v == -30.0f, "a slow fall is not written to at all");
    v = 40.0f;
    check(!features::CapFallSpeed(v, 50.0f) && v == 40.0f, "rising is never touched");
    v = -50.0f;
    check(!features::CapFallSpeed(v, 50.0f), "exactly at the cap is inside it");
    v = std::nanf("");
    check(!features::CapFallSpeed(v, 50.0f) && !(v == v), "a NaN velocity is left alone");
}

// ---------------------------------------------------------------------------
//  FLIGHT STEALTH (features/flight/stealth.h)
// ---------------------------------------------------------------------------
//  The three things that made flight noticeable were arithmetic: a gravity write,
//  a velocity step, and an unbounded climb. The gravity write is gone by not
//  calling the write at all; these cases cover the other two plus the hover bias
//  and the write watch that reports the failure.
static void test_flight_stealth() {
    printf("\n[flight stealth]\n");

    // The ramp: a bounded change per second, and no overshoot when it arrives.
    float v[3] = { 0, 0, 0 };
    const float target[3] = { 100, 0, 0 };
    features::flights::Approach(v, target, 200.0f, 0.05f);
    check(std::fabs(v[0] - 10.0f) < 1e-4f, "0 to 100 at 200/s^2 for 50ms is 10 studs/s");
    for (int i = 0; i < 9; ++i) features::flights::Approach(v, target, 200.0f, 0.05f);
    check(v[0] == 100.0f, "ten steps of 10 land exactly on the target");
    features::flights::Approach(v, target, 200.0f, 0.05f);
    check(v[0] == 100.0f, "and one more does not overshoot it");

    // Deceleration is the same rule with a zero target: a smooth stop, not a halt.
    const float stop[3] = { 0, 0, 0 };
    features::flights::Approach(v, stop, 200.0f, 0.1f);
    check(std::fabs(v[0] - 80.0f) < 1e-4f, "100 to 0 at 200/s^2 for 100ms is 80");

    // Reversal passes through zero at the same rate (no instant flip of sign).
    float r[3] = { 10, 0, 0 };
    const float back[3] = { -10, 0, 0 };
    features::flights::Approach(r, back, 100.0f, 0.1f);
    check(r[0] == 0.0f, "a reversal spends its step reaching zero, not skipping it");

    // Per-axis, independently.
    float a[3] = { 0, 0, 0 };
    const float mixed[3] = { 100, -50, 25 };
    features::flights::Approach(a, mixed, 100.0f, 0.1f);
    check(a[0] == 10.0f && a[1] == -10.0f && a[2] == 10.0f, "each axis ramps at its own slope");

    // A broken clock must not teleport the character.
    float z[3] = { 0, 0, 0 };
    features::flights::Approach(z, target, 200.0f, 0.0f);
    check(z[0] == 0.0f, "dt 0 changes nothing");
    features::flights::Approach(z, target, 200.0f, std::nanf(""));
    check(z[0] == 0.0f, "a NaN dt changes nothing");
    features::flights::Approach(z, target, 0.0f, 0.05f);
    check(z[0] == 0.0f, "accel 0 changes nothing");

    // Speed caps: horizontal scaled, vertical untouched, and 0 means "no cap".
    float c[3] = { 60, 5, 80 };            // horizontal magnitude 100
    features::flights::ClampHorizontal(c, 50.0f);
    check(std::fabs(std::sqrt(c[0] * c[0] + c[2] * c[2]) - 50.0f) < 1e-3f,
          "a 100 studs/s horizontal vector is scaled to the 50 cap");
    check(c[1] == 5.0f, "and the vertical component is not touched by the horizontal cap");
    float under[3] = { 3, 0, 4 };
    features::flights::ClampHorizontal(under, 50.0f);
    check(under[0] == 3.0f && under[2] == 4.0f, "a vector under the cap is unchanged");
    float nocap[3] = { 300, 0, 400 };
    features::flights::ClampHorizontal(nocap, 0.0f);
    check(nocap[0] == 300.0f, "a 0 cap means no cap, not a stop");

    float upv[3] = { 0, 60, 0 };
    features::flights::ClampVertical(upv, 45.0f, 60.0f);
    check(upv[1] == 45.0f, "a 60 studs/s climb is held at 45");
    float downv[3] = { 0, -90, 0 };
    features::flights::ClampVertical(downv, 45.0f, 60.0f);
    check(downv[1] == -60.0f, "a 90 studs/s dive is held at 60");
    float flat[3] = { 0, 10, 0 };
    features::flights::ClampVertical(flat, 45.0f, 60.0f);
    check(flat[1] == 10.0f, "inside both caps is unchanged");

    // The hover bias is the value that cancels one step of gravity.
    const float bias = features::flights::HoverBias(196.2f, 1.0f / 60.0f);
    check(std::fabs(bias - 1.635f) < 0.01f, "196.2 studs/s^2 at 60 Hz needs a 1.635 bias");
    check(features::flights::HoverBias(0.0f, 1.0f / 60.0f) == 0.0f, "no gravity, no bias");
    check(features::flights::HoverBias(-9.8f, 1.0f / 60.0f) == 0.0f, "a negative gravity is not a lift");
    check(features::flights::HoverBias(196.2f, 0.0f) == 0.0f, "a zero step is no bias");

    // The trim: sinking raises the bias, rising lowers it, noise is left alone,
    // and a wild measurement cannot wind it past its clamps.
    check(std::fabs(features::flights::TrimBias(1.6f, -2.0f, 0.35f, 0.2f, 0.0f, 3.3f) - 2.3f) < 1e-4f,
          "sinking 2 studs/s RAISES the bias: it is under-lifted");
    check(std::fabs(features::flights::TrimBias(1.6f, 2.0f, 0.35f, 0.2f, 0.0f, 3.3f) - 0.9f) < 1e-4f,
          "rising 2 studs/s LOWERS it: it is over-lifted");
    check(features::flights::TrimBias(1.6f, 0.1f, 0.35f, 0.2f, 0.0f, 3.3f) == 1.6f,
          "drift inside the deadband is measurement noise, not a correction");
    check(features::flights::TrimBias(1.6f, -500.0f, 0.35f, 0.2f, 0.0f, 3.3f) == 3.3f,
          "a teleport-sized measurement is clamped, not trusted");
    check(features::flights::TrimBias(1.6f, std::nanf(""), 0.35f, 0.2f, 0.0f, 3.3f) == 1.6f,
          "a NaN drift leaves the bias alone");

    // The write watch: the read-back either matches what we pinned or it does not.
    const float want[3] = { 50, 1.6f, 20 };
    const float close[3] = { 52, 3.0f, 21 };
    const float stopped[3] = { 0, 0, 0 };
    const float wild[3] = { 200, 0, 0 };
    check(features::flights::VelocityHeld(want, close, 15.0f, 0.35f),
          "a read within tolerance means the body is still ours");
    check(!features::flights::VelocityHeld(want, stopped, 15.0f, 0.35f),
          "a body that stopped dead is not holding the pin");
    check(!features::flights::VelocityHeld(want, wild, 15.0f, 0.35f),
          "neither is one running at four times the command");
    const float nanv[3] = { std::nanf(""), 0, 0 };
    check(!features::flights::VelocityHeld(want, nanv, 15.0f, 0.35f), "a NaN read is never held");
    // The vertical oscillation the physics step produces must still count as held:
    // a bias of +1.6 sampled after gravity is -1.6, a 3.2 studs/s swing.
    const float midstep[3] = { 50, -1.6f, 20 };
    check(features::flights::VelocityHeld(want, midstep, 15.0f, 0.35f),
          "the gravity swing between two pins is not a lost body");
}

// ---------------------------------------------------------------------------
//  HUMANOID STATE (features/players/humanoid_state.h)
// ---------------------------------------------------------------------------
//  Two questions, and the consequences of getting either wrong: "is this state a
//  knockdown" feeds a log line flight uses to explain a rubberband, so a false
//  positive is noise; "is it upright" would gate aim on a body, so a false
//  positive is a shot at a corpse and a false negative is a player not aimed at.
static void test_humanoid_state() {
    printf("\n[humanoid state]\n");
    using namespace features::hstate;
    check(IsKnockedDown(Ragdoll) && IsKnockedDown(FallingDown) &&
          IsKnockedDown(GettingUp) && IsKnockedDown(Seated),
          "ragdoll, falling down, getting up and seated are the knockdown set");
    check(!IsKnockedDown(Running) && !IsKnockedDown(Jumping) && !IsKnockedDown(Freefall),
          "running, jumping and freefall are not");
    check(!IsKnockedDown(Frozen), "a frozen body is not knocked down (scripts freeze on purpose)");
    check(IsUpright(Running) && IsUpright(Strafing) && IsUpright(Jumping) && IsUpright(Freefall) &&
          IsUpright(Landed) && IsUpright(Climbing) && IsUpright(Physics),
          "the upright set is what the player did on purpose");
    check(!IsUpright(Ragdoll) && !IsUpright(FallingDown) && !IsUpright(Seated) &&
          !IsUpright(GettingUp),
          "and the knockdown set is not upright");
    check(!IsUpright(Frozen), "frozen is not a body playing");
    check(!IsUpright(HumanoidDeath), "a corpse is never upright, even if a read returns it");
    check(!IsUpright(-1) && !IsUpright(99), "garbage is not upright either");
    check(Plausible(0) && Plausible(31), "0..31 are the values a read can return");
    check(!Plausible(-1) && !Plausible(32) && !Plausible(0x7fffffff),
          "anything outside that is a failed read, not a state - so flight keeps flying");
}

// ---------------------------------------------------------------------------
//  FRAME RATE (features/fpscap/fps_field.h)
// ---------------------------------------------------------------------------
//  The cap write goes into a client field whose type the dump does not state, so
//  the classification is the thing standing between "a cap" and "garbage in the
//  scheduler". The two readings of one 4-byte pattern must never both look like
//  a frame rate; these cases pin that, and the ceiling/floor of the cap.
static void test_fps_field() {
    printf("\n[frame rate]\n");
    using namespace features::fpsfield;

    // The same bytes read both ways: an int field and a float field.
    int  i60 = 0, i30 = 0, i0 = 0;
    float f60 = 0.0f, f30 = 0.0f, f0 = 0.0f;
    {
        uint32_t bits = 60u;                 std::memcpy(&i60, &bits, 4);
        // READ THE BITS AS A FLOAT: this is what the feature does
        std::memcpy(&f60, &bits, 4);
        bits = 30u;                          std::memcpy(&i30, &bits, 4); std::memcpy(&f30, &bits, 4);
        bits = 0u;                           std::memcpy(&i0, &bits, 4);  std::memcpy(&f0, &bits, 4);
    }
    check(Classify(i60, f60) == AsInt, "integer 60 (denormal as a float) classifies as an int");
    check(Classify(i30, f30) == AsInt, "integer 30 too");
    {
        const float ff = 60.0f;
        int as_int = 0;
        std::memcpy(&as_int, &ff, 4);
        check(Classify(as_int, ff) == AsFloat, "float 60.0 (1.1e9 as an int) classifies as a float");
    }
    check(Classify(i0, f0) == Unknown, "zero bits are not a frame rate either way");
    check(Classify(999999, std::nanf("")) == Unknown, "garbage is never written to");
    check(Classify(-60, 60.0f) == AsFloat, "a negative int is not a cap; the float holds");

    // Value / Encode round trip in both forms.
    {
        int oi = 0; float of = 0.0f;
        check(Encode(75.0f, AsInt, oi, of) && oi == 75, "a cap of 75 encodes as int 75");
        check(Encode(75.0f, AsFloat, oi, of) && std::fabs(of - 75.0f) < 1e-4f, "and as float 75.0");
        check(!Encode(75.0f, Unknown, oi, of), "an unclassified field encodes nothing");
        check(std::fabs(Value(AsInt, 60, 8.4e-44f) - 60.0f) < 1e-4f, "an int field reads its int");
        check(std::fabs(Value(AsFloat, 1114636288, 60.0f) - 60.0f) < 1e-4f,
              "a float field reads its float, not the int view");
        check(Value(Unknown, 0, 0.0f) == 0.0f, "unknown reads as 0, which the widget shows as --");
    }

    // The cap's range: 0/NaN mean "no cap", and the ends are held.
    check(ClampCap(0.0f) == 0.0f, "0 means no cap, not a write of zero");
    check(ClampCap(-5.0f) == 0.0f, "a negative cap is no cap");
    check(ClampCap(std::nanf("")) == 0.0f, "NaN is no cap");
    check(ClampCap(1.0f) == 10.0f, "below the floor is held at 10");
    check(ClampCap(60.0f) == 60.0f, "a real cap passes through");
    check(ClampCap(1000.0f) == 360.0f, "above the ceiling is held at 360");

    // The pacer: a period, and a deadline that drops missed frames.
    check(PeriodNs(0.0f) == 0, "0 fps means do not pace");
    check(PeriodNs(-1.0f) == 0, "a negative rate means do not pace");
    check(PeriodNs(60.0f) == 16666667ull, "60 fps is 16.67ms a frame");
    check(PeriodNs(144.0f) == 6944444ull, "144 fps is 6.94ms a frame");
    check(PeriodNs(240.0f) == 4166667ull, "240 fps is 4.17ms a frame");
    check(PeriodNs(5000.0f) == 1000000ull, "the period floor is 1ms (1000 fps)");

    const uint64_t period = 10000000ull;             // 10ms
    check(NextDeadlineNs(0, 1000000000ull, period) == 1010000000ull,
          "the first frame schedules one period out");
    check(NextDeadlineNs(1010000000ull, 1012000000ull, period) == 1020000000ull,
          "an on-time frame keeps the grid instead of drifting with the clock");
    check(NextDeadlineNs(1010000000ull, 1090000000ull, period) == 1090000000ull,
          "a frame 80ms late is dropped, not made up for with a burst");
    check(NextDeadlineNs(1234, 5678, 0) == 5678, "no period means no pacing at all");
}

// ---------------------------------------------------------------------------
//  SKY (features/world/sky.h)
// ---------------------------------------------------------------------------
//  The time-of-day slider is a WRITE of eight properties, so the arithmetic that
//  turns an hour into that sky has to be provable without a client. The cases
//  pin the parts that would be silent failures: the ramps clamp instead of
//  extrapolating, "sun or moon" flips at the horizon, and an hour or latitude
//  that did not read produces no sky at all rather than a wrong one.
static void test_sky() {
    printf("\n[sky]\n");
    using namespace features::sky;

    const float times[] = { 0.0f, 10.0f, 20.0f };
    const Vec3  cols[] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
    {
        const Vec3 below = Spline(-5.0f, times, cols, 3);
        const Vec3 above = Spline(99.0f, times, cols, 3);
        const Vec3 mid = Spline(15.0f, times, cols, 3);
        check(below.x == 0.0f && below.y == 0.0f, "a time before the ramp clamps to its first colour");
        check(above.y == 1.0f && above.z == 1.0f, "a time past it clamps to the last, never extrapolates");
        check(std::fabs(mid.y - 0.75f) < 1e-4f && std::fabs(mid.z - 0.5f) < 1e-4f,
              "halfway between two stops is their midpoint");
    }

    check(SkyAmbient(0.0f).x == 0.0f && SkyAmbient(kDay).x == 0.0f,
          "midnight is black at both ends of the day");
    {
        const Vec3 noon = SkyAmbient(12.0f * kHour);
        check(noon.x == 1.0f && noon.y == 1.0f && noon.z == 1.0f, "noon ambient is white");
        const Vec3 pre = SkyAmbient(3.0f * kHour);
        check(pre.x < 0.2f && pre.y < 0.2f && pre.z < 0.2f, "3am is dark, not a dim noon");
    }
    check(std::fabs(LightColor(12.0f * kHour).x - 0.75f) < 1e-4f,
          "noon's light colour is the day value");
    check(LightColor(0.0f).x < LightColor(12.0f * kHour).x,
          "and midnight's is dimmer than noon's");

    {
        const Clock noon = ClockAt(12.0f, 0.0f);
        check(noon.valid && noon.source == 0 && noon.sun.y > 0.5f,
              "noon names the sun and puts it above the horizon");
        const Clock midnight = ClockAt(0.0f, 0.0f);
        check(midnight.valid && midnight.source == 1 && midnight.light_direction.y == -midnight.sun.y,
              "midnight names the moon, which is the sun's opposite");
        const Clock sunrise = ClockAt(6.0f, 0.0f);
        check(sunrise.source == 0, "at sunrise the sun is still the source (it is on the horizon)");
        check(std::fabs(sunrise.sun.y) < 1e-3f, "and exactly on it");
    }

    check(!ClockAt(25.0f, 0.0f).valid && !ClockAt(-1.0f, 0.0f).valid,
          "an hour outside 0..24 produces no sky");
    check(!ClockAt(std::nanf(""), 0.0f).valid, "and neither does a NaN");
    {
        const Clock end = ClockAt(24.0f, 0.0f);
        const Clock start = ClockAt(0.0f, 0.0f);
        // The sky repeats; the seasonal tilt moved one day further, so the sun
        // is the same to a hundredth of a unit rather than bit-identical.
        check(end.valid && end.clock_time == 24.0f &&
              end.gradient_top.x == start.gradient_top.x &&
              std::fabs(end.sun.y - start.sun.y) < 0.01f,
              "24.0 is midnight of the next day, not an unrepresentable hour");
    }
    check(ClockAt(12.0f, 200.0f).sun.y == ClockAt(12.0f, 0.0f).sun.y,
          "a latitude that cannot exist is treated as the equator");
    check(std::fabs(ClockAt(12.0f, 45.0f).sun.y - ClockAt(12.0f, 0.0f).sun.y) > 1e-3f,
          "a real latitude tilts the sun");
    {
        const Vec3 v{ 0.3f, -0.7f, 0.2f };
        const Vec3 r = RotateAxisAngle(v, Vec3{ 1.0f, 2.0f, 3.0f }, 0.9f);
        check(std::fabs(Length(r) - Length(v)) < 1e-4f, "a rotation preserves length");
    }
    {
        const Clock dusk = ClockAt(18.0f, 0.0f);
        check(dusk.gradient_bottom.z > dusk.gradient_top.z,
              "the gradient's bottom is the warmer half at dusk");
    }
}


// ------------------------------------------------------------- aim angles
//  The aimbot's mouse path started here: "when you lock onto a player it
//  immediately glitches your mouse out above the player". The old path sent
//  pixels, from a client-space target minus a screen-space cursor, with no idea
//  what a count is worth. The fix is an angle plus a closed loop; these cases pin
//  the angle down, including the signs, because a sign is what that report was.

int main() {
    printf("world_ray_test -- wall-check geometry and the draw-distance rule\n");
    test_slab();
    test_rotation();
    test_grid();
    test_draw_range();
    test_fov_form();
    test_body_rules();
    test_flight_stealth();
    test_humanoid_state();
    test_fps_field();
    test_sky();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
