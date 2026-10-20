#include "world_ray.h"
// -----------------------------------------------------------------------------
//  features/raycast/world_ray.cpp  --  the wall check's memory-backed half
// -----------------------------------------------------------------------------
//  world_ray.h does the geometry (and tools/world_ray_test.cpp proves it off the
//  game). This file is the thin part: fill the grid from the cache's world sweep,
//  then answer "is this line blocked" for the ESP, the aimbot and the triggerbot.
//
//  Two things it deliberately does that the old per-caller loops did not:
//
//    * Re-reads the LIVE centre (and rotation) of every box a ray comes near. The
//      snapshot is up to a sweep old, and a door, lift or moving platform at a
//      stale position is a raycast that lies in both directions at once.
//    * Uses the part's real basis when the read is sane. A rotated wall's world
//      AABB covers corners the wall does not, which is a permanent false "wall"
//      in front of anyone standing near a ramp.
//
//  The player-body layer stays here too, because "can I see him" is one question:
//  world geometry AND the other players' bodies. Both callers used to ask it with
//  their own copy of this code and their own copy of the size table.
// -----------------------------------------------------------------------------
#include "cache.h"
#include "memory.h"
#include "offsets.h"
#include "globals.h"   // LogLine -- the grid announces itself once, see below
#include "esp/esp.h"   // features::CanonicalPartSize -- ONE size table for the tool

namespace worldray {

    // ------------------------------------------------------------ the world grid
    // Rebuilt only when the cache publishes a NEW snapshot pointer - that is
    // exactly when the box list changed, so a rebuild per sweep rather than per
    // ray. The boxes vector is kept because the grid stores indices into it.
    static Grid s_grid;
    static std::vector<Box> s_boxes;
    static const void* s_stamp = nullptr;
    static uint64_t s_builds = 0;
    static size_t s_last_logged_boxes = 0;

    static void rebuild_if_needed(const cache::WorldSnapshot& snap) {
        if (snap.get() == s_stamp) return;
        s_boxes.clear();
        s_boxes.reserve(snap->size());
        for (const cache::WorldBox& b : *snap) {
            s_boxes.push_back(Box{ b.cx, b.cy, b.cz, b.hx, b.hy, b.hz, b.primitive });
        }
        s_grid.build(s_boxes, 64.0f);
        s_stamp = snap.get();
        ++s_builds;

        // The wall check's README row is "does the world sweep find the primitives at
        // all". That is answered by the grid existing: announce it the first time it is
        // non-empty and again when the map's box count moves by more than a quarter
        // (a place change), but not on every sweep - the map is static by nature and
        // this log is read by a human.
        const size_t n = s_boxes.size();
        const bool first = s_last_logged_boxes == 0 && n > 0;
        const bool moved_a_lot = s_last_logged_boxes > 0 &&
                                 (n > s_last_logged_boxes + s_last_logged_boxes / 4 ||
                                  n + n / 4 < s_last_logged_boxes);
        if (first || moved_a_lot) {
            s_last_logged_boxes = n;
            LogLine("wall check: world grid live - %zu boxes in %zu cells (%zu always-tested), "
                    "build %llu. Line of sight now uses the map, not just player bodies",
                    n, s_grid.cells.size(), s_grid.always.size(), (unsigned long long)s_builds);
        }
    }

    static bool world_blocked(const float from[3], const float to[3]) {
        cache::WorldSnapshot snap = cache::GetWorldBoxes();
        if (!snap || snap->empty()) return false;
        rebuild_if_needed(snap);

        bool hit = false;
        s_grid.for_each_candidate(from, to, [&](uint32_t i) {
            const Box& b = s_boxes[i];
            if (b.prim && is_valid_address(b.prim)) {
                float live[3] = { 0, 0, 0 };
                if (read_raw(b.prim + Offsets::Primitive::Position, live, sizeof(live))) {
                    const float half[3] = { b.hx, b.hy, b.hz };
                    float rot[9] = { 0 };
                    if (read_raw(b.prim + Offsets::Primitive::Rotation, rot, sizeof(rot)) &&
                        rotation_usable(rot)) {
                        if (segment_hits_rotated(from, to, live, half, rot)) { hit = true; return true; }
                        return false;
                    }
                    if (segment_hits(from, to, live, half)) { hit = true; return true; }
                    return false;
                }
            }
            // no live source (or an unreadable one): the snapshot's own numbers
            if (segment_hits(from, to, &b.cx, &b.hx)) { hit = true; return true; }
            return false;
        });
        return hit;
    }

    // ----------------------------------------------------------- player bodies
    // Same canonical-size rule the ESP draws with, so "the box I see" and "the box
    // that blocks" are the same box. Sizes come from esp.cpp (one table, exposed
    // through esp.h) rather than a second copy here.
    static bool bodies_blocked(const float from[3], const float to[3], uintptr_t skip_player) {
        cache::EspSnapshot snap = cache::GetEspSnapshot();
        if (!snap) return false;
        for (const cache::EspEntity& ent : *snap) {
            if (ent.player_address == skip_player) continue;
            for (size_t i = 0; i < ent.primitive_count; ++i) {
                const uintptr_t p = ent.primitives[i];
                if (!is_valid_address(p)) continue;
                float c[3] = { 0, 0, 0 };
                if (!read_raw(p + Offsets::Primitive::Position, c, sizeof(c))) continue;
                float sx = 1.0f, sy = 1.0f, sz = 1.0f;
                features::CanonicalPartSize(ent.part_names[i], sx, sy, sz);
                const float half[3] = { sx * 0.5f, sy * 0.5f, sz * 0.5f };
                if (segment_hits(from, to, c, half)) return true;
            }
        }
        return false;
    }

    bool Blocked(const float from[3], const float to[3], uintptr_t skip_player) {
        if (!finite3(from) || !finite3(to)) return false;
        if (world_blocked(from, to)) return true;
        return bodies_blocked(from, to, skip_player);
    }

    bool WorldBlocked(const float from[3], const float to[3]) {
        if (!finite3(from) || !finite3(to)) return false;
        return world_blocked(from, to);
    }

    uint64_t builds() { return s_builds; }

}  // namespace worldray
