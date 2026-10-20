#include "world_esp.h"
// -----------------------------------------------------------------------------
//  features/world/world_esp.cpp  --  NPC ESP: the bodies that are not players
// -----------------------------------------------------------------------------
//  Started as jatosm/RobloxExternal-base's visual/world (seven categories: ore,
//  plants, bots, animals, tools, crates, keycards). The user's verdict was
//  "why is there a bunch of world objects in world, remove them" - so the five
//  OBJECT categories are gone (a loot crate is not an ESP target), and the two
//  that are CHARACTERS stayed: the AI bodies (MilitaryHitbox / ZombieHitbox /
//  BTR / Heli) and the animals (deer / boar / wolf). They live on the ESP tab
//  now, as NPC ESP, next to the player ESP they belong with.
//
//  How it reads the world without a tree walk every frame:
//
//    * a bounded breadth-first walk of Workspace (4,000 nodes, 6 levels) runs
//      when the cache is empty or every five seconds; each visited instance's
//      NAME is matched against the reference's exact name tables (and its Heli
//      substring rule), and a match stores the instance plus the primitive of
//      the first part under it;
//    * every pass after that re-reads that primitive's Position - three floats
//      per object - and drops an object whose instance lost its parent or whose
//      position stayed zero for five passes.
//
//  That split is the whole cost story: a 12,000-node walk at 1kHz would be
//  hundreds of thousands of reads a second; one walk every five seconds plus a
//  3-float read per object per pass is a few hundred reads.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "game.h"
#include "esp/esp.h"
#include "platform/platform.h"
#include "imgui/imgui.h"

namespace features {
namespace worldesp {

    namespace {

        // NPC ONLY. This started as the reference's seven world-object
        // categories; the user's verdict was "why is there a bunch of world
        // objects in world, remove them" - ore, plants, tools, crates and
        // keycards are gone, and the two categories that are CHARACTERS (the AI
        // bodies and the animals) moved to the ESP tab as NPC ESP. A place's
        // loot is not an ESP target; a body that can walk up to you is.
        enum Category : int {
            Bot = 0, Animal, kCategoryCount
        };

        const char* kCategoryName[kCategoryCount] = { "bots", "animals" };

        // The reference's tables, verbatim (visual/world/core/types.h).
        const char* kBotNames[4] = { "MilitaryHitbox", "ZombieHitbox", "BTR", "Heli" };
        const char* kAnimalNames[3] = { "PREFAB_ANIMAL_DEER", "PREFAB_ANIMAL_WILDBOAR",
                                        "PREFAB_ANIMAL_WOLF" };

        struct Entry {
            uintptr_t inst = 0;
            uintptr_t prim = 0;
            int       category = 0;
            char      name[48] = "";
            float     pos[3] = {};
            int       zero_streak = 0;
        };

        struct Pending {
            uintptr_t inst = 0;
            int       depth = 0;
        };

        struct State {
            uintptr_t workspace = 0;
            uint64_t  resolved_ms = 0;
            uint64_t  last_scan_ms = 0;
            uint64_t  last_fast_ms = 0;
            bool      scanning = false;
            std::vector<Pending> queue;
            std::vector<Entry>   entries;
            int       last_match_count = -1;
            int       last_category_mask = -1;
            bool      said_no_workspace = false;
        };
        State s;

        bool CategoryOn(int c) {
            switch (c) {
            case Bot:    return world_bots_enabled;
            case Animal: return world_animals_enabled;
            default:     return false;
            }
        }
        bool AnyOn() {
            for (int c = 0; c < kCategoryCount; ++c)
                if (CategoryOn(c)) return true;
            return false;
        }

        bool Equals(const std::string& a, const char* b) { return a == b; }

        // The reference's match_node, minus the per-subtype checkboxes.
        bool Match(const std::string& n, int& category, const char*& display) {
            for (int i = 0; i < 4; ++i) {
                bool hit = Equals(n, kBotNames[i]);
                if (!hit && i == 3 &&
                    n.find("Heli") != std::string::npos &&
                    n.find("Rocket") == std::string::npos &&
                    n.find("Crate") == std::string::npos)
                    hit = true;
                if (hit) { category = Bot; display = kBotNames[i]; return true; }
            }
            for (int i = 0; i < 3; ++i)
                if (Equals(n, kAnimalNames[i])) { category = Animal; display = kAnimalNames[i]; return true; }
            return false;
        }

        uintptr_t PartPrimitive(uintptr_t inst) {
            if (!is_valid_address(inst)) return 0;
            const uintptr_t prim = read<uintptr_t>(inst + Offsets::BasePart::Primitive);
            return is_valid_address(prim) ? prim : 0;
        }

        bool PositionOf(uintptr_t prim, float out[3]) {
            if (!is_valid_address(prim)) return false;
            if (!read_raw(prim + Offsets::Primitive::Position, out, sizeof(float) * 3)) return false;
            if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2]))
                return false;
            return !(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
        }

        // First primitive under `inst` (the instance itself, a model's primary
        // part, or a descendant within three levels).
        uintptr_t ResolvePrimitive(instance inst, int depth) {
            if (!inst.is_valid()) return 0;
            const uintptr_t own = PartPrimitive(inst.address);
            if (own) return own;
            const uintptr_t pp = read<uintptr_t>(inst.address + Offsets::Model::PrimaryPart);
            if (is_valid_address(pp)) {
                const uintptr_t prim = PartPrimitive(pp);
                if (prim) return prim;
            }
            if (depth >= 3) return 0;
            for (const instance& c : inst.get_children()) {
                const uintptr_t prim = PartPrimitive(c.address);
                if (prim) return prim;
                for (const instance& cc : c.get_children()) {
                    const uintptr_t p2 = PartPrimitive(cc.address);
                    if (p2) return p2;
                }
                if (depth + 1 < 3) {
                    const uintptr_t deep = ResolvePrimitive(c, depth + 1);
                    if (deep) return deep;
                }
            }
            return 0;
        }

        void ResolveWorkspace() {
            const uint64_t now = platform::NowMs();
            if (s.workspace && is_valid_address(s.workspace)) return;
            if (now - s.resolved_ms < 2000) return;
            s.resolved_ms = now;
            if (!g_base_address) return;
            instance dm = game::ReadDatamodel(g_base_address);
            if (!dm.is_valid()) return;
            instance ws = dm.read_service("Workspace");
            s.workspace = ws.address;
            if (s.workspace) {
                s.queue.clear();
                s.last_scan_ms = 0;      // force a full walk
            }
        }

        void ForgetAll() {
            s.entries.clear();
            s.queue.clear();
            s.workspace = 0;
            s.last_scan_ms = 0;
        }

        // One bounded walk. When the queue empties, the scan published its
        // matches and every entry starts being refreshed from its primitive.
        void ScanSlice() {
            if (!is_valid_address(s.workspace)) return;
            if (s.queue.empty()) {
                if (s.scanning) {
                    s.scanning = false;
                    s.last_scan_ms = platform::NowMs();
                    if (s.last_match_count != (int)s.entries.size()) {
                        s.last_match_count = (int)s.entries.size();
                        LogLine("world: %d named objects found; they refresh from their parts "
                                "from here on", (int)s.entries.size());
                    }
                }
                return;
            }

            const uint64_t start = platform::NowNs();
            while (!s.queue.empty()) {
                // ~1ms of work per pass keeps the feature loop responsive. The
                // queue survives to the next pass, so the walk is sliced.
                if (platform::NowNs() - start > 1000000ull) break;
                Pending p = s.queue.back();
                s.queue.pop_back();
                if (!is_valid_address(p.inst)) continue;

                instance inst{ p.inst };
                const std::string name = inst.get_name();
                int cat = -1;
                const char* display = nullptr;
                if (Match(name, cat, display) && CategoryOn(cat)) {
                    const uintptr_t prim = ResolvePrimitive(inst, 0);
                    if (prim) {
                        Entry e{};
                        e.inst = p.inst;
                        e.prim = prim;
                        e.category = cat;
                        snprintf(e.name, sizeof(e.name), "%s", name.c_str());
                        if (PositionOf(prim, e.pos)) {
                            s.entries.push_back(e);
                        } else {
                            // A model whose parts are not loaded yet is worth
                            // keeping: its primitive will resolve next pass.
                            s.entries.push_back(e);
                        }
                    }
                }

                if (p.depth < 6) {
                    for (const instance& c : inst.get_children()) {
                        if (s.queue.size() >= 8000) break;
                        s.queue.push_back(Pending{ c.address, p.depth + 1 });
                    }
                }
            }
        }

        void StartScan() {
            s.queue.clear();
            s.queue.push_back(Pending{ s.workspace, 0 });
            s.scanning = true;
            // The entries survive until the new walk has an answer; dropping
            // them here is what made the reference's full scan blink the ESP off.
        }

        void Refresh() {
            const uint64_t now = platform::NowMs();
            if (s.last_fast_ms != 0 && now - s.last_fast_ms < 100) return;
            s.last_fast_ms = now;

            for (size_t i = 0; i < s.entries.size();) {
                Entry& e = s.entries[i];
                // A parentless instance is a destroyed one.
                const uintptr_t parent = read<uintptr_t>(e.inst + Offsets::Instance::Parent);
                if (!parent) {
                    if (++e.zero_streak >= 5) {
                        e = s.entries.back();
                        s.entries.pop_back();
                        continue;
                    }
                    ++i;
                    continue;
                }
                float p[3];
                if (PositionOf(e.prim, p)) {
                    e.pos[0] = p[0]; e.pos[1] = p[1]; e.pos[2] = p[2];
                    e.zero_streak = 0;
                } else if (++e.zero_streak >= 5) {
                    // The primitive went away (a part was re-created). Re-resolve
                    // once before dropping, because a crate that respawns is the
                    // same object to the player.
                    const uintptr_t prim = ResolvePrimitive(instance{ e.inst }, 0);
                    if (prim) {
                        e.prim = prim;
                        e.zero_streak = 0;
                        ++i;
                        continue;
                    }
                    e = s.entries.back();
                    s.entries.pop_back();
                    continue;
                }
                ++i;
            }
        }

        ImU32 ColorFor(int category) {
            switch (category) {
            case Bot:    return IM_COL32(255, 90, 90, 235);
            case Animal: return IM_COL32(255, 170, 80, 235);
            default:     return IM_COL32(255, 255, 255, 235);
            }
        }

    }  // namespace

    void ReleaseWorldEsp() { ForgetAll(); }

    void RunWorldEsp() {
        if (!AnyOn()) {
            if (!s.entries.empty() || s.workspace) ForgetAll();
            return;
        }
        ResolveWorkspace();
        if (!is_valid_address(s.workspace)) {
            if (!s.said_no_workspace) {
                s.said_no_workspace = true;
                LogLine("world: Workspace did not resolve - the world scan cannot run");
            }
            return;
        }
        s.said_no_workspace = false;

        int mask = 0;
        for (int c = 0; c < kCategoryCount; ++c)
            if (CategoryOn(c)) mask |= (1 << c);

        const uint64_t now = platform::NowMs();
        // A category switched on after the last walk must not wait five seconds
        // (the feature reads as broken until it appears).
        if (mask != s.last_category_mask) {
            s.last_category_mask = mask;
            s.last_scan_ms = 0;
        }
        if (!s.scanning && (s.last_scan_ms == 0 || now - s.last_scan_ms > 5000))
            StartScan();
        ScanSlice();
        Refresh();
    }

    void RenderWorldEsp() {
        if (!AnyOn() || s.entries.empty()) return;
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        float cam[3] = { 0.0f, 0.0f, 0.0f };
        const bool have_cam = CameraPosition(cam[0], cam[1], cam[2]);
        const float max_d = world_esp_distance;

        for (const Entry& e : s.entries) {
            if (!CategoryOn(e.category)) continue;
            const float dx = e.pos[0] - cam[0];
            const float dy = e.pos[1] - cam[1];
            const float dz = e.pos[2] - cam[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (have_cam && max_d > 0.0f && dist > max_d) continue;

            float sx = 0.0f, sy = 0.0f;
            if (!ProjectToOverlay(e.pos[0], e.pos[1], e.pos[2], sx, sy)) continue;

            const ImU32 col = ColorFor(e.category);
            const ImVec2 c(sx, sy);
            draw->AddRect(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f),
                          col, 0.0f, 0, 1.5f);
            if (world_esp_labels) {
                char label[96];
                snprintf(label, sizeof(label), "%s  %.0f", e.name, dist);
                draw->AddText(ImVec2(c.x + 7.0f, c.y - 6.0f), col, label);
            }
        }
    }

}  // namespace worldesp
}  // namespace features
