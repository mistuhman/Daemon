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
#include <utility>

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
        // 0 = undiscovered on Linux: reading +0 would return the vtable and
        // every ESP position read downstream would be garbage.
        if (!part.is_valid() || !Offsets::BasePart::Primitive) return 0;
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
        if (!player.is_valid() || !Offsets::Player::Team) return 0;
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
            e.user_id = Offsets::Player::UserId ? read<uint32_t>(p.address + Offsets::Player::UserId) : 0;
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
                    if (Offsets::Humanoid::Health) {
                        e.health = read<float>(c.address + Offsets::Humanoid::Health);
                        e.max_health = read<float>(c.address + Offsets::Humanoid::MaxHealth);
                    }
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
            s.user_id = Offsets::Player::UserId ? read<uint32_t>(p.address + Offsets::Player::UserId) : 0;
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
        if (is_valid_address(lp.humanoid_address) && Offsets::Humanoid::HumanoidRootPart) {
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

    // ---------------------------------------------------------------- the fallback
    // The fast path above reads the engine's primitive CONTAINER, whose layout is
    // undocumented: worlddet validates its guess at runtime, and when the guess is
    // wrong the box list is empty - which the wall check reads as "nothing is in
    // the way". That is the reported "wall check doesn't work".
    //
    // This is the method that does not depend on a guessed layout: WALK THE INSTANCE
    // TREE. Every BasePart in Workspace has a Primitive pointer at a documented
    // offset, so "is this instance a part" is ONE read, and the class name (a
    // string read, cached per class descriptor) is only needed for parts that pass
    // that read. It is slower - tens of thousands of instance reads - so it runs on
    // the sweep thread with a wall-clock budget and publishes whatever it reached;
    // the sweep's cadence is stretched so a big map is walked once every ~10s, not
    // every second. Geometry is static; the boxes are re-centred from the live
    // primitives by world_ray when a ray comes near them.
    // ── the size field, MEASURED instead of trusted ─────────────────────────
    // Primitive::Size is what every external uses for a part's box, but this repo
    // already has a note (esp.cpp) that it reads ~0 on the live client. A wall
    // check built on it then drops every part - the box lists are empty, and the
    // ESP's wall check can only ever agree with itself. That is what "wall check
    // is still broken" looks like from the outside.
    //
    // So the offset is measured, not trusted: the sweep samples primitives and
    // scores every 4-byte-aligned triple outside the motion blocks for "looks
    // like a size" (three finite non-negative numbers, at least one axis >= 0.02,
    // not the position triple itself). The best-scoring offset becomes the size
    // field, and on a build where 0x1c4 really is Size, the measurement picks
    // 0x1c4 and nothing changes.
    static uintptr_t s_size_off = Offsets::Primitive::Size;
    static bool      s_size_off_measured = false;
    static unsigned  s_size_missing = 0;

    // A part whose size we cannot read is still a box: a one-stud cube at its
    // centre blocks a ray that goes through it instead of vanishing from the map
    // entirely. Over-blocking a point is a far smaller lie than an empty world.
    static const float kUnknownHalf = 0.5f;

    static bool HoldsSizeTriple(uintptr_t prim, uintptr_t off, float s[3]) {
        if (off == 0) return false;
        if (!read_raw(prim + off, s, sizeof(float) * 3)) return false;
        for (int k = 0; k < 3; ++k) {
            if (!(s[k] == s[k]) || s[k] < 0.0f || s[k] > 20000.0f) return false;
        }
        return true;
    }

    static bool LooksLikeSize(uintptr_t prim, uintptr_t off, const float pos[3]) {
        float s[3] = { 0, 0, 0 };
        if (!HoldsSizeTriple(prim, off, s)) return false;
        if (s[0] == pos[0] && s[1] == pos[1] && s[2] == pos[2]) return false;  // the position
        if (!(s[0] >= 0.02f) && !(s[1] >= 0.02f) && !(s[2] >= 0.02f)) return false;
        return true;
    }

    static uintptr_t MeasureSizeOffset(const std::vector<uintptr_t>& sample) {
        const size_t n = sample.size() < 24 ? sample.size() : 24;
        if (n < 6) return Offsets::Primitive::Size;
        int best_score = -1;
        uintptr_t best = Offsets::Primitive::Size;
        for (uintptr_t off = 0x80; off <= 0x240; off += 4) {
            // The motion blocks (rotation/position/velocity, 0xb0..0x140) are
            // floats too and would score by accident; a size is never in there.
            if (off > 0xac && off < 0x140) continue;
            int score = 0;
            for (size_t i = 0; i < n; ++i) {
                const uintptr_t p = sample[i];
                float pos[3] = { 0, 0, 0 };
                if (!read_raw(p + Offsets::Primitive::Position, pos, sizeof(pos))) continue;
                if (LooksLikeSize(p, off, pos)) ++score;
            }
            if (off == Offsets::Primitive::Size) ++score;   // a tie goes to the dump
            if (score > best_score) { best_score = score; best = off; }
        }
        // Fewer than half the sample looking like a size means the measurement
        // found nothing convincing: keep the dump's offset and let the 1-stud
        // fallback keep the world listed.
        return best_score > (int)n / 2 ? best : Offsets::Primitive::Size;
    }

    static void ReadHalfExtents(uintptr_t prim, float half[3]) {
        float s[3] = { 0, 0, 0 };
        if (HoldsSizeTriple(prim, s_size_off, s) &&
            ((s[0] >= 0.02f) || (s[1] >= 0.02f) || (s[2] >= 0.02f))) {
            for (int k = 0; k < 3; ++k) {
                half[k] = (s[k] >= 0.01f ? s[k] : 0.02f) * 0.5f;
            }
            return;
        }
        ++s_size_missing;
        half[0] = half[1] = half[2] = kUnknownHalf;
    }

    struct WalkPart {
        uintptr_t prim;
        float     pos[3];
    };

    struct WalkGather {
        std::vector<WalkPart>* out;
        const std::vector<uintptr_t>* skip;
        uint64_t deadline;
        size_t   seen;
        bool     ran_out;
    };

    static void walk_parts(instance node, int depth, WalkGather& g) {
        if (!node.is_valid() || depth > 12) return;
        if (g.seen >= 200000) { g.ran_out = true; return; }
        if ((g.seen & 63u) == 0 && platform::NowMs() > g.deadline) {
            g.ran_out = true;
            return;
        }
        for (const instance& child : node.get_children()) {
            ++g.seen;
            // The budget must be checked INSIDE the child loop too: Workspace in a
            // big place has thousands of direct children, and a recursive-only
            // check would walk that whole list before noticing the deadline.
            if ((g.seen & 63u) == 0 && platform::NowMs() > g.deadline) {
                g.ran_out = true;
                return;
            }
            if (!child.is_valid()) continue;
            // A BasePart always carries a Primitive pointer. This is the one read
            // that separates "wall" from "Script" without a string compare.
            const uintptr_t prim = read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
            if (is_valid_address(prim) &&
                !std::binary_search(g.skip->begin(), g.skip->end(), prim)) {
                float pos[3] = { 0, 0, 0 };
                if (read_raw(prim + Offsets::Primitive::Position, pos, sizeof(pos))) {
                    // No CanQuery gate here any more. The flag word lives next to
                    // the primitive's rotation, which this repo already documents
                    // as unreliable on this client; a misread flag silently removed
                    // the whole map from the wall check. The gate is a
                    // nice-to-have (foliage), never worth losing walls over.
                    g.out->push_back(WalkPart{ prim, { pos[0], pos[1], pos[2] } });
                }
                // A part cannot have part children that matter for LOS (welds are
                // not parented), so stop here - it cuts the walk by an order of
                // magnitude on maps whose geometry is all parts.
                continue;
            }
            // The players' characters live under Workspace too; skipping their
            // models by CLASS is cheaper than relying only on the primitive skip
            // list, which is a sweep behind a respawn.
            const std::string cls = child.get_class_name();
            if (cls == "Player" || cls == "Players" || cls == "Terrain") continue;
            walk_parts(child, depth + 1, g);
        }
    }

    static void WorldSweep() {
        if (!g_base_address) return;
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return;
        instance ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return;
        if (!Offsets::Workspace::World) return;   // undiscovered: no world reads
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
            // The size field is the one plausibility check a wrong dump offset can
            // kill on its own. Retry on positions alone; the size offset is then
            // measured from the entries this finds (MeasureSizeOffset).
            det = worlddet::detect(world, Offsets::World::Primitives, prim_count_hint,
                                   world_reader, nullptr, Offsets::Primitive::Position, 0);
            if (det.layout != worlddet::LAYOUT_NONE && !s_size_off_measured) {
                LogLine("wall check: the primitive container was accepted on positions alone - "
                        "the size field is not where the dump says; measuring it");
            }
        }
        const bool have_container = det.layout != worlddet::LAYOUT_NONE;
        if (!have_container) {
            // No container: fall through to the instance walk below. The skip list
            // is built there from the same snapshots, so the two paths publish the
            // same kind of box list and world_ray needs no knowledge of which ran.
        }
        s_world_undetected_logged = !have_container;

        // pull the primitive pointers in 32KB chunks (one syscall per 4096
        // entries; a corrupt count cannot make us read forever -- hard cap)
        size_t n = 0;
        std::vector<uintptr_t> prims;
        if (have_container) {
            n = det.count > 200000 ? 200000 : det.count;
            prims.resize(n);
            for (size_t i = 0; i < n; i += 4096) {
                size_t k = (n - i) < 4096 ? (n - i) : 4096;
                if (!read_raw(det.data + i * 8, &prims[i], k * 8)) return;   // keep last snapshot
            }
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

        std::vector<WorldBox> boxes;
        if (!have_container) {
            // Budget: 250ms of tree walking. A 20k-instance map reads in well
            // under that on a warm cache; the budget only bounds the pathological
            // case (a 200k-instance showcase) so the sweep thread cannot starve.
            std::vector<WalkPart> parts;
            WalkGather g{ &parts, &skip, platform::NowMs() + 250, 0, false };
            instance ws2 = dm.read_service("Workspace");
            walk_parts(ws2, 0, g);

            if (!s_size_off_measured && parts.size() >= 6) {
                std::vector<uintptr_t> sample;
                sample.reserve(24);
                for (const WalkPart& p : parts) {
                    sample.push_back(p.prim);
                    if (sample.size() >= 24) break;
                }
                s_size_off = MeasureSizeOffset(sample);
                s_size_off_measured = true;
                LogLine("wall check: the part size field measures at Primitive+0x%llX %s",
                        (unsigned long long)s_size_off,
                        s_size_off == Offsets::Primitive::Size
                            ? "(the dump's offset - sizes are real)"
                            : "(the dump's Primitive::Size does not hold a size on this build)");
            }

            s_size_missing = 0;
            boxes.reserve(parts.size());
            for (const WalkPart& p : parts) {
                float half[3];
                ReadHalfExtents(p.prim, half);
                boxes.push_back(WorldBox{ p.pos[0], p.pos[1], p.pos[2],
                                          half[0], half[1], half[2], p.prim });
            }
            publish_world_boxes(std::make_shared<const std::vector<WorldBox>>(std::move(boxes)));
            // The walk is the expensive path, and its staleness has a bound that
            // matters: a newly spawned character's parts are in the box list until
            // the next sweep, which would make rays at that character report
            // "blocked" by its own body. 4s keeps that window short; a truncated
            // walk retries sooner. world_ray re-reads live positions per ray, so
            // the 4s only bounds the box LIST, not the geometry's position.
            s_world_interval_ms = g.ran_out ? 6000 : 4000;
            static size_t s_walk_logged = 0;
            if (parts.size() != s_walk_logged) {
                s_walk_logged = parts.size();
                LogLine("wall check: instance-tree sweep found %zu parts in %zu instances%s",
                        parts.size(), g.seen, g.ran_out ? " (time budget hit)" : "");
            }
            return;
        }

        if (!s_size_off_measured && prims.size() >= 6) {
            std::vector<uintptr_t> sample;
            sample.reserve(24);
            for (uintptr_t p : prims) {
                sample.push_back(p);
                if (sample.size() >= 24) break;
            }
            s_size_off = MeasureSizeOffset(sample);
            s_size_off_measured = true;
            LogLine("wall check: the part size field measures at Primitive+0x%llX %s",
                    (unsigned long long)s_size_off,
                    s_size_off == Offsets::Primitive::Size
                        ? "(the dump's offset - sizes are real)"
                        : "(the dump's Primitive::Size does not hold a size on this build)");
        }

        s_size_missing = 0;
        boxes.reserve(prims.size());
        for (uintptr_t p : prims) {
            if (!is_valid_address(p)) continue;
            if (std::binary_search(skip.begin(), skip.end(), p)) continue;
            float pos[3] = { 0, 0, 0 };
            if (!read_raw(p + Offsets::Primitive::Position, pos, sizeof(pos))) continue;
            // No CanQuery gate: see walk_parts. The flag word sits next to the
            // primitive's rotation, which is documented as garbage on this build,
            // and a misread flag removed the whole map from the wall check.
            //
            // The box is an AABB from the measured size field, with a 1-stud
            // fallback for a part whose size cannot be read - dropping it is how
            // the wall check ended up with no world.
            float half[3];
            ReadHalfExtents(p, half);
            boxes.push_back(WorldBox{ pos[0], pos[1], pos[2], half[0], half[1], half[2], p });
        }

        // If most of the map fell back, the field moved under us (place change or a
        // new build): measure again on the next sweep instead of living with 1-stud
        // boxes forever.
        if (s_size_missing > n / 2) s_size_off_measured = false;
        static unsigned s_missing_logged = 0;
        if (s_size_missing > 0 && s_size_missing != s_missing_logged && s_size_missing > n / 4) {
            s_missing_logged = s_size_missing;
            LogLine("wall check: %u of %zu parts had no readable size (1-stud fallback boxes) - "
                    "the measured size field is Primitive+0x%llX",
                    s_size_missing, boxes.size(), (unsigned long long)s_size_off);
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
            // Same idle backoff as the feature loop: all three fetches return
            // empty until a game is attached, so 62.5 Hz wakeups buy nothing
            // pre-attach and only burn the CPU. Attached cadence (16 ms) is
            // unchanged.
            platform::SleepMs(g_base_address ? 16 : 100);
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