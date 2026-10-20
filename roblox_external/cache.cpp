#include "cache.h"
#include "memory.h"
#include "offsets.h"
#include "globals.h"
#include "game.h"
#include "world_detect.h"
#include "media_tracker.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

struct vec3_t { float x, y, z; };

namespace cache {
    static std::mutex s_mutex;
    static std::vector<EspEntity> s_entities;
    static EspSnapshot s_entity_snapshot;
    static SkelSnapshot s_skel_snapshot;
    static std::vector<SkeletonEntity> s_skeletons;
    static LocalPlayerData s_local{};
    static bool s_running = false;
    static void* s_thread = nullptr;

    std::mutex& GetMutex() { return s_mutex; }

    static void scpy(char* dst, const char* src, size_t max) {
        if (!src) { dst[0] = '\0'; return; }
        if (max == 0) return;
        // strncpy_s(dst, max, src, _TRUNCATE) semantics: copy at most max-1
        // bytes and always NUL-terminate -- but portable, so this TU parses
        // with gcc too (strncpy_s is MSVC-only and hid here for months
        // because cache.cpp was in no check list)
        snprintf(dst, max, "%s", src);
    }

    static uintptr_t get_prim(instance part) {
        if (!part.is_valid()) return 0;
        return read<uintptr_t>(part.address + Offsets::BasePart::Primitive);
    }

    static bool find_part(instance ch, const char* name, uintptr_t& out) {
        for (auto& c : ch.get_children()) {
            if (!c.is_valid()) continue;
            if (c.get_name() == name) { out = get_prim(c); return true; }
        }
        return false;
    }

    static bool is_r15(instance ch) {
        for (auto& c : ch.get_children()) {
            if (!c.is_valid()) continue;
            auto n = c.get_name();
            if (n == "UpperTorso" || n == "LowerTorso" ||
                n == "LeftUpperArm" || n == "RightUpperArm" ||
                n == "LeftUpperLeg" || n == "RightUpperLeg")
                return true;
        }
        return false;
    }

    static bool build_skel(instance ch, SkeletonEntity& s) {
        if (!ch.is_valid()) return false;
        s.is_r15 = is_r15(ch);
        find_part(ch, "Head", s.head);
        find_part(ch, "UpperTorso", s.upper_torso);
        find_part(ch, "LowerTorso", s.lower_torso);
        find_part(ch, "LeftHand", s.left_hand);
        find_part(ch, "RightHand", s.right_hand);
        find_part(ch, "LeftFoot", s.left_foot);
        find_part(ch, "RightFoot", s.right_foot);
        if (s.is_r15) {
            find_part(ch, "LeftUpperArm", s.left_upper_arm);
            find_part(ch, "LeftLowerArm", s.left_lower_arm);
            find_part(ch, "RightUpperArm", s.right_upper_arm);
            find_part(ch, "RightLowerArm", s.right_lower_arm);
            find_part(ch, "LeftUpperLeg", s.left_upper_leg);
            find_part(ch, "LeftLowerLeg", s.left_lower_leg);
            find_part(ch, "RightUpperLeg", s.right_upper_leg);
            find_part(ch, "RightLowerLeg", s.right_lower_leg);
        }
        return s.head != 0 || s.upper_torso != 0;
    }

    static const char* body_parts[] = {
        "Head", "HumanoidRootPart", "UpperTorso", "LowerTorso",
        "LeftUpperArm", "LeftLowerArm", "LeftHand",
        "RightUpperArm", "RightLowerArm", "RightHand",
        "LeftUpperLeg", "LeftLowerLeg", "LeftFoot",
        "RightUpperLeg", "RightLowerLeg", "RightFoot",
        "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg"
    };

    static bool is_body_part(const char* n) {
        for (auto bp : body_parts)
            if (strcmp(n, bp) == 0) return true;
        return false;
    }

    static uintptr_t get_player_team(instance player) {
        if (!player.is_valid()) return 0;
        return read<uintptr_t>(player.address + Offsets::Player::Team);
    }

    static std::vector<EspEntity> fetch_entities() {
        std::vector<EspEntity> result;
        if (!g_base_address) return result;
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid() || dm.get_name().empty()) return result;
        instance plrs = dm.read_service("Players");
        if (!plrs.is_valid()) return result;
        instance local = plrs.local_player();

        uintptr_t local_team = 0;
        if (local.is_valid()) {
            local_team = get_player_team(local);
        }

        for (auto& p : plrs.get_children()) {
            if (!p.is_valid()) continue;
            if (local.is_valid() && p.address == local.address) continue;
            instance ch = p.model_instance();
            if (!ch.is_valid()) continue;

            EspEntity e{};
            e.player_address = p.address;
            scpy(e.name, p.get_name().c_str(), sizeof(e.name));
            e.user_id = read<uint32_t>(p.address + Offsets::Player::UserId);
            e.team_address = get_player_team(p);
            e.is_teammate = (local_team != 0 && e.team_address == local_team);

            // fetch the child list ONCE and reuse it for humanoid, tool and parts
            // (this used to walk the same list three times per player)
            std::vector<instance> ch_children = ch.get_children();

            bool found_humanoid = false;
            bool found_tool = false;
            for (auto& c : ch_children) {
                if (!c.is_valid()) continue;
                if (found_humanoid && found_tool) break;
                auto cn = c.get_class_name();
                if (!found_humanoid && cn == "Humanoid") {
                    e.health = read<float>(c.address + Offsets::Humanoid::Health);
                    e.max_health = read<float>(c.address + Offsets::Humanoid::MaxHealth);
                    found_humanoid = true;
                } else if (!found_tool && (cn == "Tool" || cn == "BackpackItem")) {
                    scpy(e.tool_name, c.get_name().c_str(), sizeof(e.tool_name));
                    found_tool = true;
                }
            }
            e.is_r15 = is_r15(ch);
            e.character_address = ch.address;

            for (auto& pt : ch_children) {
                if (!pt.is_valid() || e.primitive_count >= 64) continue;
                auto pn = pt.get_name();
                if (!is_body_part(pn.c_str())) continue;
                uintptr_t pr = get_prim(pt);
                if (!is_valid_address(pr)) continue;
                size_t i = e.primitive_count;
                scpy(e.part_names[i], pn.c_str(), sizeof(e.part_names[i]));
                e.primitives[i] = pr;
                e.part_addresses[i] = pt.address;
                e.primitive_count++;
                if (pn == "HumanoidRootPart") {
                    vec3_t pos{};
                    if (read_raw(pr + Offsets::Primitive::Position, &pos, sizeof(pos))) {
                        e.root_x = pos.x; e.root_y = pos.y; e.root_z = pos.z;
                    }
                }
            }
            if (e.primitive_count > 0) result.push_back(e);
        }

        // Teammates are flagged, not removed: the ESP tints them in
        // team_esp_color and the aimbot skips them. (They used to be erased
        // here, which made the ESP half of team check look broken -- the
        // boxes just vanished.)
        return result;
    }

    static std::vector<SkeletonEntity> fetch_skeletons() {
        std::vector<SkeletonEntity> result;
        if (!g_base_address) return result;
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid() || dm.get_name().empty()) return result;
        instance plrs = dm.read_service("Players");
        if (!plrs.is_valid()) return result;

        instance local = plrs.local_player();
        uintptr_t local_team = 0;
        if (local.is_valid()) {
            local_team = get_player_team(local);
        }

        for (auto& p : plrs.get_children()) {
            if (!p.is_valid()) continue;
            if (local.is_valid() && p.address == local.address) continue;
            instance ch = p.model_instance();
            if (!ch.is_valid()) continue;
            SkeletonEntity s{};
            s.player_address = p.address;
            s.team_address = get_player_team(p);
            s.is_teammate = (local_team != 0 && s.team_address == local_team);
            scpy(s.name, p.get_name().c_str(), sizeof(s.name));
            if (build_skel(ch, s)) result.push_back(s);
        }

        // Flagged, not erased -- see fetch_entities().
        return result;
    }

    static LocalPlayerData fetch_local() {
        LocalPlayerData lp{};
        if (!g_base_address) return lp;
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid() || dm.get_name().empty()) return lp;
        instance plrs = dm.read_service("Players");
        if (!plrs.is_valid()) return lp;
        instance local = plrs.local_player();
        if (!local.is_valid()) return lp;
        // the LocalPlayer instance is named after the account - the same source
        // the esp uses for other players - so the menu can show the username
        // even before the character resolves
        scpy(lp.name, local.get_name().c_str(), sizeof(lp.name));
        instance ch = local.model_instance();
        if (!ch.is_valid()) return lp;
        lp.valid = true;
        // single pass over the character's children for both HRP and Humanoid
        std::vector<instance> parts = ch.get_children();
        for (auto& pt : parts) {
            if (!pt.is_valid()) continue;
            if (pt.get_name() == "HumanoidRootPart") {
                lp.hrp_primitive = get_prim(pt);
                lp.hrp_address = pt.address;
                break;
            }
        }
        if (is_valid_address(lp.hrp_primitive)) {
            vec3_t pos{};
            if (read_raw(lp.hrp_primitive + Offsets::Primitive::Position, &pos, sizeof(pos))) {
                lp.x = pos.x; lp.y = pos.y; lp.z = pos.z;
            }
        }
        for (auto& c : parts) {
            if (!c.is_valid()) continue;
            if (c.get_class_name() == "Humanoid") { lp.humanoid_address = c.address; break; }
        }

        // Prefer resolving the root part straight off the humanoid instead of by
        // name. Humanoid::HumanoidRootPart is a direct pointer, so this works even
        // if a name lookup fails - flight / infinite jump / teleport all depend on
        // this pointer being valid.
        if (is_valid_address(lp.humanoid_address)) {
            uintptr_t hrp_part = read<uintptr_t>(lp.humanoid_address + Offsets::Humanoid::HumanoidRootPart);
            if (is_valid_address(hrp_part)) {
                uintptr_t prim = read<uintptr_t>(hrp_part + Offsets::BasePart::Primitive);
                if (is_valid_address(prim)) {
                    lp.hrp_primitive = prim;
                    lp.hrp_address = hrp_part;
                    vec3_t pos{};
                    if (read_raw(prim + Offsets::Primitive::Position, &pos, sizeof(pos))) {
                        lp.x = pos.x; lp.y = pos.y; lp.z = pos.z;
                    }
                }
            }
        }
        return lp;
    }

    // ---------------------------------------------------------------------
    //  World geometry sweep (wall check). The aimbot/ESP raycasts used to test
    //  line-of-sight against other players' body parts only -- the comment
    //  there said "we don't have the world primitives list offset", but the
    //  live offset service has carried World::Primitives for a while. The
    //  layout of the container at that offset is undocumented, so
    //  world_detect.h validates its interpretation at runtime and this sweep
    //  simply publishes nothing when detection fails (the wall check then
    //  stays player-body-only, as it always was).
    //
    //  Character parts are EXCLUDED from the box list: the target's own body
    //  would eat every ray aimed at it, and other players' bodies are checked
    //  live (fresh positions) by the callers. What is left is walls, floors,
    //  ramps -- the geometry a wall check exists for. Parts whose CanQuery
    //  flag is clear are skipped too (default RaycastParams semantics: they
    //  are invisible to raycasts by design -- foliage, effect meshes).
    // ---------------------------------------------------------------------
    static WorldSnapshot s_world_snapshot;
    static std::atomic<uint64_t> s_last_world_sweep_ms{ 0 };
    static uint64_t s_world_interval_ms = 1500;   // grows with world size
    static void* s_world_thread = nullptr;
    static std::atomic<bool> s_world_running{ false };
    static bool s_world_undetected_logged = false;

    static bool world_reader(void* /*ctx*/, uintptr_t addr, void* out, size_t size) {
        return read_raw(addr, out, size);
    }

    static void publish_world_boxes(std::shared_ptr<const std::vector<WorldBox>> snap) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_world_snapshot = std::move(snap);
    }

    static void WorldSweep() {
        if (!g_base_address) return;
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return;
        instance ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return;
        uintptr_t world = read<uintptr_t>(ws.address + Offsets::Workspace::World);
        if (!is_valid_address(world)) return;
        // uint32 on purpose: if the field is 4 bytes, reading 8 would drag an
        // adjacent field into the high half and explode the sanity ranges.
        uintptr_t prim_count_hint = read<uint32_t>(dm.address + Offsets::DataModel::PrimitiveCount);

        worlddet::Result det = worlddet::detect(world, Offsets::World::Primitives,
                                                prim_count_hint, world_reader, nullptr,
                                                Offsets::Primitive::Position,
                                                Offsets::Primitive::Size);
        if (det.layout == worlddet::LAYOUT_NONE) {
            if (!s_world_undetected_logged) {
                s_world_undetected_logged = true;
                LogLine("wall check: world primitive container not recognised - "
                        "walls do not block, only other players' bodies do. "
                        "Run the world probe on the debug page and report it.");
                publish_world_boxes(std::make_shared<const std::vector<WorldBox>>());
            }
            return;
        }
        s_world_undetected_logged = false;

        // pull the primitive pointers in 32KB chunks (one syscall per 4096
        // entries; a corrupt count cannot make us read forever -- hard cap)
        size_t n = det.count > 200000 ? 200000 : det.count;
        std::vector<uintptr_t> prims(n);
        for (size_t i = 0; i < n; i += 4096) {
            size_t k = (n - i) < 4096 ? (n - i) : 4096;
            if (!read_raw(det.data + i * 8, &prims[i], k * 8)) return;   // keep last snapshot
        }

        // every character primitive we know of: ESP entities, skeletons, and
        // the local player's own character (fetch_entities skips it)
        std::vector<uintptr_t> skip;
        {
            auto es = GetEspSnapshot();
            for (const auto& e : *es)
                for (size_t i = 0; i < e.primitive_count; ++i) skip.push_back(e.primitives[i]);
            auto ss = GetSkeletonSnapshot();
            for (const auto& s : *ss) {
                const uintptr_t parts[] = {
                    s.head, s.upper_torso, s.lower_torso,
                    s.left_hand, s.right_hand, s.left_foot, s.right_foot,
                    s.left_upper_arm, s.left_lower_arm,
                    s.right_upper_arm, s.right_lower_arm,
                    s.left_upper_leg, s.left_lower_leg,
                    s.right_upper_leg, s.right_lower_leg,
                };
                for (uintptr_t p : parts) if (p) skip.push_back(p);
            }
            instance plrs = dm.read_service("Players");
            instance local = plrs.is_valid() ? plrs.local_player() : instance{};
            if (local.is_valid()) {
                instance ch = local.model_instance();
                if (ch.is_valid()) {
                    for (const auto& c : ch.get_children()) {
                        if (!c.is_valid()) continue;
                        uintptr_t pr = read<uintptr_t>(c.address + Offsets::BasePart::Primitive);
                        if (is_valid_address(pr)) skip.push_back(pr);
                    }
                }
            }
            std::sort(skip.begin(), skip.end());
        }

        const uint32_t can_query_mask = (uint32_t)Offsets::PrimitiveFlags::CanQuery;
        std::vector<WorldBox> boxes;
        boxes.reserve(prims.size());
        for (uintptr_t p : prims) {
            if (!is_valid_address(p)) continue;
            if (std::binary_search(skip.begin(), skip.end(), p)) continue;
            float pos[3] = { 0, 0, 0 }, size[3] = { 0, 0, 0 };
            if (!read_raw(p + Offsets::Primitive::Position, pos, sizeof(pos))) continue;
            if (!read_raw(p + Offsets::Primitive::Size, size, sizeof(size))) continue;
            // CanQuery gate (default RaycastParams semantics: parts that are
            // invisible to raycasts -- foliage, effect meshes -- do not
            // block). flags==0 reads as "the flag word is not where we
            // think" - keep the part, because a wall check that misses walls
            // is worse than one that over-blocks.
            uint32_t flags = 0;
            if (read_raw(p + Offsets::Primitive::Flags, &flags, sizeof(flags)) &&
                flags != 0 && (flags & can_query_mask) == 0) {
                continue;
            }
            // Axis-aligned approximation: rotated parts get their bounding
            // box, which over-blocks by a little and never under-blocks.
            WorldBox b{ pos[0], pos[1], pos[2],
                        size[0] * 0.5f, size[1] * 0.5f, size[2] * 0.5f };
            if (b.hx < 0.01f && b.hy < 0.01f && b.hz < 0.01f) continue;   // degenerate
            boxes.push_back(b);
        }

        publish_world_boxes(std::make_shared<const std::vector<WorldBox>>(std::move(boxes)));
        // adaptive cadence: 20k+ part maps get ~4s between sweeps; static
        // geometry does not care, and the sweep thread must not hog the bus
        s_world_interval_ms = 1500 + (uint64_t)n * 2;
    }

    static void WorldThread(void*) {
        while (s_world_running) {
            if (wall_check && g_base_address) {
                uint64_t now = platform::NowMs();
                if (now - s_last_world_sweep_ms.load(std::memory_order_relaxed) >= s_world_interval_ms) {
                    s_last_world_sweep_ms.store(now, std::memory_order_relaxed);
                    WorldSweep();
                }
            }
            platform::SleepMs(100);
        }
    }

    static void CacheThread(void*) {
        while (s_running) {
            auto new_entities = fetch_entities();
            auto new_skeletons = fetch_skeletons();
            auto new_local = fetch_local();

            auto ents = std::make_shared<const std::vector<EspEntity>>(std::move(new_entities));
            auto skel = std::make_shared<const std::vector<SkeletonEntity>>(std::move(new_skeletons));
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_entity_snapshot = ents;
                s_skel_snapshot = skel;
                s_local = new_local;
            }
            media::Tick();
            platform::SleepMs(16);
        }
    }

    void StartThread() {
        s_running = true;
        s_thread = platform::StartJoinableThread(CacheThread, nullptr);
        s_world_running = true;
        s_world_thread = platform::StartJoinableThread(WorldThread, nullptr);
    }

    void StopThread() {
        s_running = false;
        // joins (up to 2s) and always releases the thread handle
        if (s_thread) { platform::JoinThread(s_thread, 2000); s_thread = nullptr; }
        s_world_running = false;
        if (s_world_thread) { platform::JoinThread(s_world_thread, 2000); s_world_thread = nullptr; }
    }

    EspSnapshot GetEspSnapshot() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_entity_snapshot) s_entity_snapshot = std::make_shared<const std::vector<EspEntity>>();
        return s_entity_snapshot;
    }

    SkelSnapshot GetSkeletonSnapshot() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_skel_snapshot) s_skel_snapshot = std::make_shared<const std::vector<SkeletonEntity>>();
        return s_skel_snapshot;
    }

    WorldSnapshot GetWorldBoxes() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_world_snapshot) {
            static const auto empty = std::make_shared<const std::vector<WorldBox>>();
            return empty;
        }
        return s_world_snapshot;
    }

    std::vector<EspEntity> GetEspEntities() { return *GetEspSnapshot(); }
    std::vector<SkeletonEntity> GetSkeletonEntities() { return *GetSkeletonSnapshot(); }

    LocalPlayerData GetLocalPlayer() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_local;
    }

    void WorldProbeLog() {
        LogLine("--- world probe ---");
        if (!g_base_address) {
            LogLine("not attached - load into a game first");
        } else {
            instance dm = game::ReadDatamodel(g_base_address);
            instance ws = dm.is_valid() ? dm.read_service("Workspace") : instance{};
            uintptr_t world = ws.is_valid()
                ? read<uintptr_t>(ws.address + Offsets::Workspace::World) : 0;
            if (!is_valid_address(world)) {
                LogLine("no physics world (datamodel %s, workspace %s)",
                        dm.is_valid() ? "ok" : "missing", ws.is_valid() ? "ok" : "missing");
            } else {
                uintptr_t hint = read<uint32_t>(dm.address + Offsets::DataModel::PrimitiveCount);
                LogLine("workspace 0x%llX  world 0x%llX  datamodel primitive count %llu",
                        (unsigned long long)ws.address, (unsigned long long)world,
                        (unsigned long long)hint);
                // the raw container region, so a failed auto-detection can be
                // pinned by hand from this dump
                for (int i = 0; i < 8; ++i) {
                    uintptr_t at = world + Offsets::World::Primitives + (uintptr_t)i * 8;
                    LogLine("  world+0x%03llX: 0x%016llX",
                            (unsigned long long)(Offsets::World::Primitives + (uintptr_t)i * 8),
                            (unsigned long long)read<uintptr_t>(at));
                }
                worlddet::Result det = worlddet::detect(world, Offsets::World::Primitives,
                                                        hint, world_reader, nullptr,
                                                        Offsets::Primitive::Position,
                                                        Offsets::Primitive::Size);
                LogLine("detection: %s", worlddet::layout_name(det.layout));
                if (det.layout != worlddet::LAYOUT_NONE) {
                    size_t shown = det.count < 4 ? det.count : 4;
                    LogLine("  %llu entries at 0x%llX (first %llu sampled)",
                            (unsigned long long)det.count, (unsigned long long)det.data,
                            (unsigned long long)shown);
                    for (size_t i = 0; i < shown; ++i) {
                        uintptr_t p = read<uintptr_t>(det.data + i * 8);
                        if (!is_valid_address(p)) { LogLine("  [0x%llX] unreadable", (unsigned long long)p); continue; }
                        float pos[3] = { 0, 0, 0 }, size[3] = { 0, 0, 0 };
                        read_raw(p + Offsets::Primitive::Position, pos, sizeof(pos));
                        read_raw(p + Offsets::Primitive::Size, size, sizeof(size));
                        uint32_t flags = 0;
                        read_raw(p + Offsets::Primitive::Flags, &flags, sizeof(flags));
                        LogLine("  prim[%llu]: 0x%llX  pos (%8.1f %8.1f %8.1f)  size (%6.1f %6.1f %6.1f)  flags 0x%08X",
                                (unsigned long long)i, (unsigned long long)p, pos[0], pos[1], pos[2],
                                size[0], size[1], size[2], flags);
                    }
                }
                // a character part for comparison: its flags tell us whether
                // the CanQuery bit is where we think it is
                LocalPlayerData lp = GetLocalPlayer();
                if (is_valid_address(lp.hrp_primitive)) {
                    uint32_t flags = 0;
                    read_raw(lp.hrp_primitive + Offsets::Primitive::Flags, &flags, sizeof(flags));
                    LogLine("local HRP (character, for comparison): 0x%llX  flags 0x%08X",
                            (unsigned long long)lp.hrp_primitive, flags);
                    LogLine("PrimitiveFlags masks: Anchored 0x%X  CanCollide 0x%X  CanTouch 0x%X  CanQuery 0x%X",
                            (unsigned)Offsets::PrimitiveFlags::Anchored,
                            (unsigned)Offsets::PrimitiveFlags::CanCollide,
                            (unsigned)Offsets::PrimitiveFlags::CanTouch,
                            (unsigned)Offsets::PrimitiveFlags::CanQuery);
                }
            }
        }
        LogLine("--- world probe end ---");
    }

}