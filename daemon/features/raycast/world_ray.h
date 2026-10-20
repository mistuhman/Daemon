#pragma once
// -----------------------------------------------------------------------------
//  features/raycast/world_ray.h  --  the wall check's geometry, kept pure
// -----------------------------------------------------------------------------
//  The old wall check tested the segment against every world box in a flat loop
//  and did it with an AXIS-ALIGNED box that a rotated part is not. Both halves of
//  that are wrong in ways that matter in a real map:
//
//    * Cost. A big map is tens of thousands of boxes and the ESP raycast runs per
//      entity per frame. Every box was tested, so the check either ate the frame
//      or (worse) the sweeper never finished and the list it tested was empty -
//      and an empty list is "nothing is in the way", which reads as a wall check
//      that does not work.
//    * Rotation. A ramp, a rotated wall, a wedge, a door - all of them are boxes
//      rotated about some axis, and their world-space AABB is bigger and in the
//      wrong place. Testing the AABB reports a block where the part is not and a
//      clear line through the part where it is.
//
//  So: a uniform grid so a ray only visits the cells it passes through, and a
//  real OBB test so rotation is honoured. This header is deliberately free of
//  memory access, Windows and ImGui - `tools/world_ray_test.cpp` drives it with
//  synthetic geometry (a diagonal through a wall, a rotated ramp, a box the ray
//  misses), so the math is proven off the game, and world_ray.cpp is the thin
//  part that fills the grid from the cache snapshot and re-reads live values.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace worldray {

// A wall/building box. `prim` is the live Primitive it came from, so a candidate
// can be re-read at test time (parts move - doors, lifts, platforms - and the
// snapshot is up to a sweep old). 0 means "no live source": test the snapshot.
struct Box {
    float cx, cy, cz;      // centre
    float hx, hy, hz;      // half extents
    uintptr_t prim;
};

inline bool finite1(float v) { return std::isfinite(v); }
inline bool finite3(const float p[3]) { return finite1(p[0]) && finite1(p[1]) && finite1(p[2]); }

// Roblox stores the part basis as 9 floats in COLUMN-major order: columns are
// right / up / back (matching esp.cpp's camera read). Usable = finite and close
// enough to orthonormal that using it cannot be worse than ignoring it.
inline bool rotation_usable(const float r[9]) {
    for (int i = 0; i < 9; ++i) if (!finite1(r[i])) return false;
    // column lengths ~1
    for (int c = 0; c < 3; ++c) {
        const float x = r[c * 3 + 0], y = r[c * 3 + 1], z = r[c * 3 + 2];
        const float len = std::sqrt(x * x + y * y + z * z);
        if (len < 0.9f || len > 1.1f) return false;
    }
    // columns ~orthogonal
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            float d = 0.0f;
            for (int i = 0; i < 3; ++i) d += r[a * 3 + i] * r[b * 3 + i];
            if (d > 0.1f || d < -0.1f) return false;
        }
    }
    return true;
}

// Slab test. `org` is the segment start, `delta` the FULL segment vector (so a hit
// is t in [0,1]), `c`/`h` the box centre and half extents. Touching counts as a
// hit: a wall exactly on the boundary must block, or the check flickers.
//
// Callers almost always have two POINTS, not a point and a delta, and passing an
// absolute endpoint here silently tests a segment from the origin - it compiles,
// it returns an answer, and the answer is wrong. That mistake was made once while
// writing this file, which is why segment_hits() below exists and why every call
// site in the app goes through it.
inline bool slab_hits(const float org[3], const float delta[3], const float c[3], const float h[3]) {
    float tmin = 0.0f, tmax = 1.0f;
    for (int i = 0; i < 3; ++i) {
        const float e = org[i];
        const float f = delta[i];
        const float mn = c[i] - h[i];
        const float mx = c[i] + h[i];
        if (f > -1e-6f && f < 1e-6f) {
            if (e < mn || e > mx) return false;
        } else {
            float t1 = (mn - e) / f;
            float t2 = (mx - e) / f;
            if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    return true;
}

// Segment vs oriented box. The segment is rotated into the box's local frame
// (local = R^T * (p - c)), where R's columns are r[0..2], r[3..5], r[6..8].
inline bool slab_hits_rotated(const float org[3], const float delta[3],
                              const float c[3], const float h[3], const float r[9]) {
    float o[3] = { org[0] - c[0], org[1] - c[1], org[2] - c[2] };
    float d[3] = { delta[0], delta[1], delta[2] };
    float lo[3], ld[3];
    for (int row = 0; row < 3; ++row) {
        lo[row] = r[0 * 3 + row] * o[0] + r[1 * 3 + row] * o[1] + r[2 * 3 + row] * o[2];
        ld[row] = r[0 * 3 + row] * d[0] + r[1 * 3 + row] * d[1] + r[2 * 3 + row] * d[2];
    }
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    return slab_hits(lo, ld, zero, h);
}

// ------------------------------------------------- the shape callers have
// Two points in, a boolean out. This is the API the app uses; slab_hits and
// slab_hits_rotated stay public for the tests that exercise the primitive.
inline bool segment_hits(const float from[3], const float to[3], const float c[3], const float h[3]) {
    if (!finite3(from) || !finite3(to) || !finite1(c[0]) || !finite1(c[1]) || !finite1(c[2])) return false;
    const float delta[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    return slab_hits(from, delta, c, h);
}

inline bool segment_hits_rotated(const float from[3], const float to[3],
                                 const float c[3], const float h[3], const float r[9]) {
    if (!finite3(from) || !finite3(to) || !finite1(c[0]) || !finite1(c[1]) || !finite1(c[2])) return false;
    const float delta[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    return slab_hits_rotated(from, delta, c, h, r);
}

// ------------------------------------------------------------ the app surface
// Implemented in world_ray.cpp (it needs the cache and the memory layer, which
// this header deliberately does not). Callers pass two world points and the
// player whose own body must not block the ray (0 = nobody).
//
//   Blocked      -- world geometry AND the other players' bodies. This is the
//                   wall check: ESP, aimbot and triggerbot all ask this one.
//   WorldBlocked -- world geometry only (the world-visualisation path).
//
// Both return FALSE when there is nothing to test (no sweep yet, no boxes) --
// "unknown" must never read as "blocked", because a wall check that hides every
// player is worse than one that does not hide any.
bool Blocked(const float from[3], const float to[3], uintptr_t skip_player);
bool WorldBlocked(const float from[3], const float to[3]);

// How many times the grid was rebuilt (diagnostics: one per published sweep).
uint64_t builds();

// ------------------------------------------------------------------ the grid
// A dense array is wrong here: maps run to ~20k studs a side, which at a 64-stud
// cell is 30M cells. Sparse hash cells are exact and cost only what the map uses.
struct Grid {
    float cell = 64.0f;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
    std::vector<uint32_t> always;   // boxes spanning too many cells to list

    static uint64_t key(int x, int y, int z) {
        // 21 bits per axis, biased so negatives pack cleanly
        const uint64_t ux = (uint64_t)(uint32_t)(x + (1 << 20)) & 0x1FFFFF;
        const uint64_t uy = (uint64_t)(uint32_t)(y + (1 << 20)) & 0x1FFFFF;
        const uint64_t uz = (uint64_t)(uint32_t)(z + (1 << 20)) & 0x1FFFFF;
        return (ux << 42) | (uy << 21) | uz;
    }

    void clear() { cells.clear(); always.clear(); }

    void build(const std::vector<Box>& boxes, float cell_size) {
        clear();
        cell = cell_size > 1.0f ? cell_size : 1.0f;
        cells.reserve(boxes.size() * 2);
        always.reserve(boxes.size() / 8 + 8);
        for (uint32_t i = 0; i < (uint32_t)boxes.size(); ++i) {
            const Box& b = boxes[i];
            const int x0 = (int)std::floor((b.cx - b.hx) / cell);
            const int x1 = (int)std::floor((b.cx + b.hx) / cell);
            const int y0 = (int)std::floor((b.cy - b.hy) / cell);
            const int y1 = (int)std::floor((b.cy + b.hy) / cell);
            const int z0 = (int)std::floor((b.cz - b.hz) / cell);
            const int z1 = (int)std::floor((b.cz + b.hz) / cell);
            const long long nx = (long long)x1 - x0, ny = (long long)y1 - y0, nz = (long long)z1 - z0;
            // Terrain-sized boxes: listing every cell would be the dense array
            // again, so they go in the always-tested list (rare, and a hit on one
            // is nearly always the answer anyway).
            if (nx > 8 || ny > 8 || nz > 8) { always.push_back(i); continue; }
            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                        cells[key(x, y, z)].push_back(i);
        }
    }

    // Amanatides-Woo: walk the cells the segment crosses. `fn` takes a box index
    // and returns true to stop the walk (a hit was found).
    template <class F>
    void for_each_candidate(const float from[3], const float to[3], F&& fn) const {
        for (uint32_t i : always) if (fn(i)) return;

        float dir[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
        const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (!(len > 1e-6f) || !finite3(from) || !finite3(to)) {
            // a zero-length "ray" is a point: one cell
            const int cx = (int)std::floor(from[0] / cell);
            const int cy = (int)std::floor(from[1] / cell);
            const int cz = (int)std::floor(from[2] / cell);
            auto it = cells.find(key(cx, cy, cz));
            if (it != cells.end()) for (uint32_t i : it->second) if (fn(i)) return;
            return;
        }

        int cell_x = (int)std::floor(from[0] / cell);
        int cell_y = (int)std::floor(from[1] / cell);
        int cell_z = (int)std::floor(from[2] / cell);
        const int end_x = (int)std::floor(to[0] / cell);
        const int end_y = (int)std::floor(to[1] / cell);
        const int end_z = (int)std::floor(to[2] / cell);

        int step_x = dir[0] > 0 ? 1 : (dir[0] < 0 ? -1 : 0);
        int step_y = dir[1] > 0 ? 1 : (dir[1] < 0 ? -1 : 0);
        int step_z = dir[2] > 0 ? 1 : (dir[2] < 0 ? -1 : 0);

        float t_max[3] = { 1e30f, 1e30f, 1e30f };
        float t_delta[3] = { 1e30f, 1e30f, 1e30f };
        const float o[3] = { from[0], from[1], from[2] };
        for (int i = 0; i < 3; ++i) {
            if (dir[i] > 1e-9f || dir[i] < -1e-9f) {
                const float inv = 1.0f / dir[i];
                const int c = (i == 0 ? cell_x : (i == 1 ? cell_y : cell_z));
                const int st = (i == 0 ? step_x : (i == 1 ? step_y : step_z));
                const float next_edge = (float)((st > 0 ? c + 1 : c)) * cell;
                t_max[i] = (next_edge - o[i]) * inv;
                t_delta[i] = (float)st * cell * inv;
            }
        }

        // the walk is bounded: a segment cannot cross more cells than its extent
        // allows, and this is the guard that keeps a garbage ray from looping.
        const long long axis_cells =
            (long long)std::abs(end_x - cell_x) + std::abs(end_y - cell_y) + std::abs(end_z - cell_z);
        const long long max_steps = 3 * (axis_cells + 3);
        for (long long step = 0; step <= max_steps; ++step) {
            auto it = cells.find(key(cell_x, cell_y, cell_z));
            if (it != cells.end()) for (uint32_t i : it->second) if (fn(i)) return;
            if (cell_x == end_x && cell_y == end_y && cell_z == end_z) return;
            // advance along whichever axis has the smallest t_max
            if (t_max[0] < t_max[1] && t_max[0] < t_max[2]) { cell_x += step_x; t_max[0] += t_delta[0]; }
            else if (t_max[1] < t_max[2]) { cell_y += step_y; t_max[1] += t_delta[1]; }
            else { cell_z += step_z; t_max[2] += t_delta[2]; }
            if (t_max[0] > 1.0f && t_max[1] > 1.0f && t_max[2] > 1.0f) {
                auto last = cells.find(key(cell_x, cell_y, cell_z));
                if (last != cells.end()) for (uint32_t i : last->second) if (fn(i)) return;
                return;
            }
        }
    }
};

}  // namespace worldray
