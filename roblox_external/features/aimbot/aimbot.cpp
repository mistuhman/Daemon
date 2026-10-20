#include <cstdint>
#include <cmath>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include "aimbot.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"
#include "imgui/imgui.h"

namespace features {

    struct AimVec2 { float x, y; };
    struct AimVec3 { float x, y, z; };
    struct AimMatrix4 { float data[16]; };

    static bool AimReadRaw(uint64_t address, void* buffer, size_t size) {
        return read_raw(address, buffer, size);
    }

    static instance cached_camera{};
    static uint64_t last_camera_lookup = 0;

    static instance GetCameraInstance() {
        uint64_t now = platform::NowMs();
        if (cached_camera.is_valid() && (now - last_camera_lookup) < 2000)
            return cached_camera;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance workspace = dm.read_service("Workspace");
        if (!workspace.is_valid()) return instance{};
        cached_camera = read<instance>(workspace.address + Offsets::Workspace::CurrentCamera);
        last_camera_lookup = now;
        return cached_camera;
    }

    static bool GetCameraTransform(AimVec3& pos, float rot[9]) {
        instance cam = GetCameraInstance();
        if (!cam.is_valid()) return false;
        if (!AimReadRaw(cam.address + Offsets::Camera::Position, &pos, sizeof(pos))) return false;
        if (!AimReadRaw(cam.address + Offsets::Camera::Rotation, rot, sizeof(float) * 9)) return false;
        return true;
    }

    static bool GetViewData(AimMatrix4& view, AimVec2& viewport) {
        instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
        if (!ve.is_valid()) return false;
        if (!AimReadRaw(ve.address + Offsets::VisualEngine::ViewMatrix, &view, sizeof(view))) return false;
        if (!AimReadRaw(ve.address + Offsets::VisualEngine::Dimensions, &viewport, sizeof(viewport))) return false;
        if (viewport.x <= 0.0f || viewport.y <= 0.0f) return false;
        return true;
    }

    static bool AimWorldToScreen(const AimVec3& world, AimVec2& out, const AimMatrix4& view, const AimVec2& viewport) {
        const float* m = view.data;
        float w_x = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
        if (w_x < 0.01f) return false;
        float screen_x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
        float screen_y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
        float inv_w = 1.0f / w_x;
        out.x = (viewport.x * 0.5f * screen_x * inv_w) + (viewport.x * 0.5f);
        out.y = -(viewport.y * 0.5f * screen_y * inv_w) + (viewport.y * 0.5f);
        if (out.x != out.x || out.y != out.y) return false;
        return true;
    }

    static AimVec3 AimNormalize(AimVec3 v) {
        float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 0.0001f) return v;
        return { v.x / len, v.y / len, v.z / len };
    }

    static AimVec3 AimCross(AimVec3 a, AimVec3 b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    static void ComputeLookAt(const AimVec3& from, const AimVec3& to, float out[9]) {
        AimVec3 forward = AimNormalize({ to.x - from.x, to.y - from.y, to.z - from.z });
        AimVec3 right = AimNormalize(AimCross({ 0, 1, 0 }, forward));
        AimVec3 up = AimCross(forward, right);

        out[0] = -right.x;  out[1] = up.x;  out[2] = -forward.x;
        out[3] = right.y;   out[4] = up.y;  out[5] = -forward.y;
        out[6] = -right.z;  out[7] = up.z;  out[8] = -forward.z;
    }

    static uintptr_t locked_head = 0;
    static uintptr_t locked_player = 0;
    static bool was_key_held = false;
    // The key as a latch (aimbot_hold_mode == false): the value the press edge
    // flips, and the previous physical level it is edge-detected against.
    static bool s_latched = false;
    static bool s_key_was_down = false;
    static uint64_t last_target_search_time = 0;
    static uint64_t last_aim_update_time = 0;
    static constexpr uint64_t k_target_search_interval_ms = 100;
    static constexpr uint64_t k_aim_update_interval_ms = 8;

    // wall-check helper is defined below FindPartForPlayer(), but target search
    // needs to call it, so declare it here first.
    static bool LineOfSightClear(const AimVec3& from, const AimVec3& to, uintptr_t skip_player);

    static uintptr_t GetPartFromSkeleton(const cache::SkeletonEntity& skel, int part_index) {
        switch (part_index) {
        case 0: return skel.head;
        case 1: return skel.upper_torso;
        case 2: return skel.lower_torso;
        case 3: return skel.left_hand;
        case 4: return skel.right_hand;
        case 5: return skel.left_foot;
        case 6: return skel.right_foot;
        default: return skel.head;
        }
    }

    // Teammate lookup by player address, for dropping a sticky lock when team
    // check gets toggled mid-lock (the search skips teammates at find time, so
    // the only way to hold one is to have acquired it as an enemy).
    static bool PlayerIsTeammate(uintptr_t player_address) {
        {
            auto snap = cache::GetSkeletonSnapshot();
            for (const auto& s : *snap)
                if (s.player_address == player_address) return s.is_teammate;
        }
        auto snap = cache::GetEspSnapshot();
        for (const auto& e : *snap)
            if (e.player_address == player_address) return e.is_teammate;
        return false;
    }

    // Same shape, and the same cost, as PlayerIsTeammate above: the health the dead
    // check needs lives on the ESP snapshot (the skeleton one carries part addresses
    // only), so this is a lookup in the list the cache thread already publishes. It is
    // also deliberately conservative: a player who is NOT in the snapshot is "unknown",
    // not dead - the part validation below is what ends a lock on somebody who has
    // actually left the game, and this function should not compete with it.
    static bool PlayerIsDead(uintptr_t player_address) {
        auto snap = cache::GetEspSnapshot();
        for (const auto& e : *snap)
            if (e.player_address == player_address) return e.IsDead();
        return false;
    }

    static bool FindClosestTarget(const AimMatrix4& view, const AimVec2& viewport, const AimVec2& fov_center, uintptr_t& out_part, uintptr_t& out_player) {
        float best_dist_sq = fov_size * fov_size;
        bool found = false;

        AimVec3 cam_pos{};
        bool have_cam = false;
        if (wall_check) {
            float rot[9]{};
            have_cam = GetCameraTransform(cam_pos, rot);
        }

        auto skeletons_snap = cache::GetSkeletonSnapshot();
        const auto& skeletons = *skeletons_snap;
        for (const auto& skel : skeletons) {
            if (PlayerIsBlacklisted(skel.name)) continue;
            if (team_check && skel.is_teammate) continue;
            if (dead_check && PlayerIsDead(skel.player_address)) continue;
            uintptr_t part = GetPartFromSkeleton(skel, aimbot_part);
            if (!part) part = skel.head;
            if (!part) continue;

            AimVec3 part_pos{};
            if (!AimReadRaw(part + Offsets::Primitive::Position, &part_pos, sizeof(part_pos))) continue;

            if (wall_check && have_cam && !LineOfSightClear(cam_pos, part_pos, skel.player_address)) continue;

            AimVec2 screen_pos{};
            if (!AimWorldToScreen(part_pos, screen_pos, view, viewport)) continue;

            float dx = screen_pos.x - fov_center.x;
            float dy = screen_pos.y - fov_center.y;
            float dist_sq = dx * dx + dy * dy;

            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                out_part = part;
                out_player = skel.player_address;
                found = true;
            }
        }

        if (!found) {
            auto entities_snap = cache::GetEspSnapshot();
            const auto& entities = *entities_snap;
            for (const auto& entity : entities) {
                if (PlayerIsBlacklisted(entity.name)) continue;
                if (team_check && entity.is_teammate) continue;
                if (dead_check && entity.IsDead()) continue;
                if (entity.root_x == 0.0f && entity.root_y == 0.0f && entity.root_z == 0.0f) continue;
                if (entity.primitive_count == 0) continue;

                AimVec3 root_pos = { entity.root_x, entity.root_y, entity.root_z };
                if (wall_check && have_cam && !LineOfSightClear(cam_pos, root_pos, entity.player_address)) continue;

                AimVec2 screen_pos{};
                if (!AimWorldToScreen(root_pos, screen_pos, view, viewport)) continue;

                float dx = screen_pos.x - fov_center.x;
                float dy = screen_pos.y - fov_center.y;
                float dist_sq = dx * dx + dy * dy;

                if (dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    out_part = entity.primitives[0];
                    out_player = entity.player_address;
                    found = true;
                }
            }
        }

        return found;
    }

    static uintptr_t FindPartForPlayer(uintptr_t player_address) {
        auto skeletons_snap = cache::GetSkeletonSnapshot();
        const auto& skeletons = *skeletons_snap;
        for (const auto& skel : skeletons) {
            if (skel.player_address != player_address) continue;
            if (PlayerIsBlacklisted(skel.name)) return 0;
            // The refresh that re-derives the part address for a held lock honours the
            // same filters, or the drop two lines up would be undone by the re-lock.
            if (dead_check && PlayerIsDead(player_address)) return 0;
            uintptr_t part = GetPartFromSkeleton(skel, aimbot_part);
            if (part) return part;
            if (skel.head) return skel.head;
        }
        return 0;
    }

    // humanizer helpers (defined just before RunAimbot) — declared here so the
    // aim functions below can call them
    static void  HumanizerJitter(AimVec3& target);
    static float HumanizerProgress();

    // ---------------------------------------------------------------------
    // Wall check (line-of-sight). Two layers:
    //   1. World geometry from cache::GetWorldBoxes() -- walls, floors,
    //      ramps, the thing a wall check exists for. Published by the cache's
    //      slow world sweep (World::Primitives, layout auto-detected; empty
    //      until detection succeeds, so on a container we cannot read this
    //      layer simply is not there).
    //   2. Other players' cached body parts, read live: a player standing
    //      between the camera and the target still blocks, with fresh
    //      positions.
    // Boxes that CONTAIN the aim point do not block (layer 1): the target is
    // hugging that wall or standing in that doorway, and a wall at the
    // endpoint would otherwise make such targets unaimable forever.
    // ---------------------------------------------------------------------
    static void AimPartHalfExtents(const char* name, AimVec3& half) {
        if (!name) { half = { 1.0f, 1.0f, 1.0f }; return; }
        if (!strcmp(name, "Head"))                              { half = { 1.0f, 0.5f, 0.5f }; return; }
        if (!strcmp(name, "HumanoidRootPart"))                  { half = { 1.0f, 1.0f, 0.5f }; return; }
        if (!strcmp(name, "Torso") || !strcmp(name, "UpperTorso")) { half = { 1.0f, 1.0f, 0.5f }; return; }
        if (!strcmp(name, "LowerTorso"))                        { half = { 1.0f, 0.5f, 0.5f }; return; }
        if (!strcmp(name, "Left Arm")  || !strcmp(name, "Right Arm")  ||
            !strcmp(name, "LeftUpperArm")  || !strcmp(name, "RightUpperArm")  ||
            !strcmp(name, "LeftLowerArm")  || !strcmp(name, "RightLowerArm") ||
            !strcmp(name, "Left Leg")  || !strcmp(name, "Right Leg")  ||
            !strcmp(name, "LeftUpperLeg")  || !strcmp(name, "RightUpperLeg")  ||
            !strcmp(name, "LeftLowerLeg")  || !strcmp(name, "RightLowerLeg")) { half = { 0.5f, 1.0f, 0.5f }; return; }
        half = { 0.5f, 0.5f, 0.5f };
    }

    static bool SegmentHitsAABB(const AimVec3& from, const AimVec3& to,
                                const AimVec3& center, const AimVec3& half) {
        float org[3] = { from.x, from.y, from.z };
        float dir[3] = { to.x - from.x, to.y - from.y, to.z - from.z };
        float c[3]   = { center.x, center.y, center.z };
        float h[3]   = { half.x, half.y, half.z };
        float tmin = 0.0f, tmax = 1.0f;
        for (int i = 0; i < 3; ++i) {
            float e = org[i], f = dir[i], mn = c[i] - h[i], mx = c[i] + h[i];
            if (f > -1e-6f && f < 1e-6f) {
                if (e < mn || e > mx) return false;
            } else {
                float t1 = (mn - e) / f;
                float t2 = (mx - e) / f;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            }
        }
        return true;
    }

    static bool LineOfSightClear(const AimVec3& from, const AimVec3& to, uintptr_t skip_player) {
        if (!wall_check) return true;

        // layer 1: world geometry (walls). Character parts are excluded at
        // sweep time; a box that contains the aim point does not block.
        auto world_snap = cache::GetWorldBoxes();
        for (const auto& b : *world_snap) {
            bool endpoint_inside =
                to.x > b.cx - b.hx && to.x < b.cx + b.hx &&
                to.y > b.cy - b.hy && to.y < b.cy + b.hy &&
                to.z > b.cz - b.hz && to.z < b.cz + b.hz;
            if (endpoint_inside) continue;
            AimVec3 c{ b.cx, b.cy, b.cz };
            AimVec3 h{ b.hx, b.hy, b.hz };
            if (SegmentHitsAABB(from, to, c, h)) return false;
        }

        // layer 2: other players' bodies, live positions
        auto ents_snap = cache::GetEspSnapshot();
        const auto& ents = *ents_snap;
        for (const auto& ent : ents) {
            if (ent.player_address == skip_player) continue;
            for (size_t i = 0; i < ent.primitive_count; ++i) {
                uintptr_t p = ent.primitives[i];
                if (!is_valid_address(p)) continue;
                AimVec3 center{};
                if (!AimReadRaw(p + Offsets::Primitive::Position, &center, sizeof(center))) continue;
                AimVec3 half{};
                AimPartHalfExtents(ent.part_names[i], half);
                if (SegmentHitsAABB(from, to, center, half)) return false;
            }
        }
        return true;
    }

    static void AimAtPrimitive(uintptr_t head_prim) {
        AimVec3 cam_pos{};
        float cam_rot[9]{};
        if (!GetCameraTransform(cam_pos, cam_rot)) return;

        AimVec3 target_pos{};
        if (!AimReadRaw(head_prim + Offsets::Primitive::Position, &target_pos, sizeof(target_pos))) return;

        if (prediction_enabled) {
            AimVec3 velocity{};
            if (AimReadRaw(head_prim + Offsets::Primitive::AssemblyLinearVelocity, &velocity, sizeof(velocity))) {
                target_pos.x += velocity.x / prediction_x;
                target_pos.y += velocity.y / prediction_y;
                target_pos.z += velocity.z / prediction_x;
            }
        }

        HumanizerJitter(target_pos);

        float target_rot[9]{};
        ComputeLookAt(cam_pos, target_pos, target_rot);

        float progress = HumanizerProgress();
        float factor_x = progress / smoothing_x;
        float factor_y = progress / smoothing_y;
        if (factor_x > 1.0f) factor_x = 1.0f;
        if (factor_y > 1.0f) factor_y = 1.0f;

        float new_rot[9]{};
        for (int i = 0; i < 9; ++i) {
            float factor = (i % 3 == 0) ? factor_x : factor_y;
            new_rot[i] = cam_rot[i] + (target_rot[i] - cam_rot[i]) * factor;
        }

        instance cam = GetCameraInstance();
        if (!cam.is_valid()) return;
        write_raw(cam.address + Offsets::Camera::Rotation, new_rot, sizeof(new_rot));
    }

    static void AimAtPrimitiveMouse(uintptr_t head_prim) {
        AimMatrix4 view{};
        AimVec2 viewport{};
        if (!GetViewData(view, viewport)) return;

        AimVec3 target_pos{};
        if (!AimReadRaw(head_prim + Offsets::Primitive::Position, &target_pos, sizeof(target_pos))) return;

        if (prediction_enabled) {
            AimVec3 velocity{};
            if (AimReadRaw(head_prim + Offsets::Primitive::AssemblyLinearVelocity, &velocity, sizeof(velocity))) {
                target_pos.x += velocity.x / prediction_x;
                target_pos.y += velocity.y / prediction_y;
                target_pos.z += velocity.z / prediction_x;
            }
        }

        HumanizerJitter(target_pos);

        AimVec2 target_screen{};
        if (!AimWorldToScreen(target_pos, target_screen, view, viewport)) return;

        // DPI correction for the humanizer's pixel math (96 = no scaling).
        // platform::ScreenDpi caches it; the value cannot change while we run.
        const int dpi = platform::ScreenDpi();

        int cur_x = 0, cur_y = 0;
        platform::CursorScreen(cur_x, cur_y);

        float dx = target_screen.x - (float)cur_x;
        float dy = target_screen.y - (float)cur_y;

        float progress = HumanizerProgress();
        float factor_x = progress / smoothing_x;
        float factor_y = progress / smoothing_y;
        if (factor_x > 1.0f) factor_x = 1.0f;
        if (factor_y > 1.0f) factor_y = 1.0f;

        dx *= factor_x;
        dy *= factor_y;

        static uint64_t last_mouse_aim_time = 0;
        const uint64_t now = platform::NowMs();
        if (now - last_mouse_aim_time < 2) return;
        last_mouse_aim_time = now;

        // Relative move: not disturbed by the game's cursor re-centering.
        platform::MoveMouseRelative((int)dx, (int)dy);
    }

    // ---------------------------------------------------------------------
    // Humanizer: makes the aim look human instead of instant.
    //   - reaction delay after a target is (re)acquired (nothing moves at first)
    //   - eased acceleration into the aim (slow -> fast -> settle)
    //   - small decaying jitter so the crosshair never sits perfectly still
    // Strength scales the reaction time and jitter; off = the old instant aim.
    // ---------------------------------------------------------------------
    static uintptr_t s_hum_target      = 0;
    static uint64_t     s_hum_acquire_ms  = 0;
    static uint64_t     s_hum_reaction_ms = 0;
    static float     s_hum_jitter_x = 0.0f;
    static float     s_hum_jitter_y = 0.0f;

    static void HumanizerOnTarget(uintptr_t target) {
        if (target == s_hum_target) return;
        s_hum_target = target;
        s_hum_acquire_ms = platform::NowMs();
        // reaction time grows with strength (a "more human" setting is slower to react)
        float strength = humanizer_enabled ? humanizer_strength : 0.0f;
        s_hum_reaction_ms = 60 + (uint64_t)((rand() % 160) * (0.4f + 0.6f * strength));
        s_hum_jitter_x = ((rand() % 100) / 50.0f - 1.0f) * 0.5f * strength;
        s_hum_jitter_y = ((rand() % 100) / 50.0f - 1.0f) * 0.5f * strength;
    }

    // 0 = still reacting (don't move), 1 = full speed. Eases in over ~220ms.
    static float HumanizerProgress() {
        if (!humanizer_enabled) return 1.0f;
        uint64_t now = platform::NowMs();
        uint64_t elapsed = (now >= s_hum_acquire_ms) ? (now - s_hum_acquire_ms) : 0;
        if (elapsed < s_hum_reaction_ms) return 0.0f;
        float t = (float)(elapsed - s_hum_reaction_ms) / 220.0f;
        if (t > 1.0f) t = 1.0f;
        float e = t * t * (3.0f - 2.0f * t);      // smoothstep
        return 0.25f + 0.75f * e;                  // keep a floor so it always tracks
    }

    static void HumanizerJitter(AimVec3& target) {
        if (!humanizer_enabled) return;
        target.x += s_hum_jitter_x;
        target.y += s_hum_jitter_y;
        s_hum_jitter_x *= 0.86f;   // decay the wobble so it settles
        s_hum_jitter_y *= 0.86f;
    }

    // Whether the aimbot key is currently a latch that is ON. The menu shows this so
    // an armed aimbot is visible with the menu closed and the hand off the mouse -
    // a toggle whose state you cannot read is a footgun, not a convenience.
    bool AimbotKeyLatched() { return !aimbot_hold_mode && s_latched; }

    void RunAimbot() {
        if (!aimbot_enabled) {
            locked_head = 0;
            locked_player = 0;
            was_key_held = false;
            // A latch that survives its own switch being turned off re-arms silently
            // the moment the switch comes back on. It does not.
            s_latched = false;
            s_key_was_down = false;
            return;
        }

        // Anything that changes what the key MEANS - a rebind, or flipping hold/toggle
        // - starts from disarmed. Otherwise "I rebound the key and the aimbot is
        // somehow still on" is a state nobody can explain.
        static int s_bind_seen = aimbot_keybind;
        static bool s_mode_seen = aimbot_hold_mode;
        if (s_bind_seen != aimbot_keybind || s_mode_seen != aimbot_hold_mode) {
            s_bind_seen = aimbot_keybind;
            s_mode_seen = aimbot_hold_mode;
            s_latched = false;
            s_key_was_down = false;
        }

        // No keybind set -> always active while enabled. Otherwise the key is either
        // held to aim, or it flips a latch. Edge-detected here and not in the menu
        // because the menu is a window that gets closed, and a toggle that only works
        // while it is open is not a toggle.
        const bool key_down = (aimbot_keybind == 0) || (platform::IsKeyDown(aimbot_keybind));
        bool key_held = key_down;
        if (aimbot_keybind != 0 && !aimbot_hold_mode) {
            if (key_down && !s_key_was_down) s_latched = !s_latched;
            s_key_was_down = key_down;
            key_held = s_latched;
        } else {
            s_key_was_down = false;
        }

        if (!key_held) {
            locked_head = 0;
            locked_player = 0;
            was_key_held = false;
            return;
        }

        uint64_t now_aim = platform::NowMs();

        if (!sticky_aim) {
            AimMatrix4 view{};
            AimVec2 viewport{};
            if (!GetViewData(view, viewport)) return;

            AimVec2 fov_center = { viewport.x * 0.5f, viewport.y * 0.5f };

            uintptr_t found_part = 0;
            uintptr_t found_player = 0;
            if (!FindClosestTarget(view, viewport, fov_center, found_part, found_player)) return;

            HumanizerOnTarget(found_part);

            if (aimbot_aim_type == 0)
                AimAtPrimitive(found_part);
            else
                AimAtPrimitiveMouse(found_part);
            return;
        }

        if (!was_key_held) {
            AimMatrix4 view{};
            AimVec2 viewport{};
            if (!GetViewData(view, viewport)) return;

            AimVec2 fov_center = { viewport.x * 0.5f, viewport.y * 0.5f };

            uintptr_t found_part = 0;
            uintptr_t found_player = 0;
            if (!FindClosestTarget(view, viewport, fov_center, found_part, found_player)) {
                was_key_held = true;
                return;
            }

            locked_head = found_part;
            locked_player = found_player;
            last_aim_update_time = now_aim;
        }

        if (locked_player != 0) {
            // A lock does not outlive the filters that were allowed to find it. Team
            // check can be toggled while a lock is held, and a target can die under
            // it - and a corpse keeps its head part readable, so the part validation
            // below would happily keep aiming at a body. This is the only place both
            // get enforced for a held lock.
            if ((team_check && PlayerIsTeammate(locked_player)) ||
                (dead_check && PlayerIsDead(locked_player))) {
                locked_head = 0;
                locked_player = 0;
            }
        }

        if (locked_player != 0) {
            uintptr_t refreshed = FindPartForPlayer(locked_player);
            if (refreshed) {
                locked_head = refreshed;
            }

            if (is_valid_address(locked_head)) {
                AimVec3 test{};
                if (AimReadRaw(locked_head + Offsets::Primitive::Position, &test, sizeof(test))) {
                    if (wall_check) {
                        AimVec3 cam_pos{}; float rot[9]{};
                        if (GetCameraTransform(cam_pos, rot) &&
                            !LineOfSightClear(cam_pos, test, locked_player)) {
                            locked_head = 0;
                            locked_player = 0;
                            was_key_held = true;
                            return;
                        }
                    }
                    bool do_aim = (now_aim - last_aim_update_time) >= k_aim_update_interval_ms;
                    if (do_aim) {
                        last_aim_update_time = now_aim;
                        HumanizerOnTarget(locked_head);
                        if (aimbot_aim_type == 0)
                            AimAtPrimitive(locked_head);
                        else
                            AimAtPrimitiveMouse(locked_head);
                    }
                    was_key_held = true;
                    return;
                }
            }

            locked_head = 0;
        }

        was_key_held = true;
    }

    void RenderFOV() {
        if (!show_fov) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        // fixed at the centre of the screen (the crosshair), not the mouse
        ImVec2 center = ImGui::GetIO().DisplaySize;
        center.x *= 0.5f;
        center.y *= 0.5f;
        draw->AddCircle(center, fov_size, IM_COL32(255, 255, 255, 180), 64, 1.0f);
    }
}

