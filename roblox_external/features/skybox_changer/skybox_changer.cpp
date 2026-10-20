#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <chrono>
#include "skybox_changer.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"

namespace features {

    static bool had_players = false;
    static bool skybox_written = false;
    static int last_skybox_type = -1;
    static uintptr_t last_sky_address = 0;
    static uint64_t last_place_id = 0;
    static std::chrono::steady_clock::time_point place_change_time;
    static bool place_just_changed = false;
    static std::chrono::steady_clock::time_point last_apply_time{};

    // The two validity bytes are the part of this feature no public dump documents, so
    // the feature watches what the renderer does with them: 300ms after invalidating,
    // read them back. True means something consumed the invalidation (the renderer
    // re-uploaded); false means nobody did - either these are not the bytes it watches
    // or it has not drawn since. It speaks on a change, not on every 2.5s re-apply.
    static uintptr_t last_rv_for_invalidate = 0;
    static bool verify_waiting = false;
    static std::chrono::steady_clock::time_point verify_due{};
    static uintptr_t verify_rv = 0;
    static bool verify_have_last = false;
    static int logged_preset = -1;

    struct AllocatedFace {
        uintptr_t string_addr;
        uintptr_t data_addr;
    };
    static std::vector<AllocatedFace> our_allocations;

    struct SkyboxPreset {
        const char* name;
        const char* up;
        const char* rt;
        const char* lf;
        const char* ft;
        const char* bk;
        const char* dn;
    };

    static const SkyboxPreset k_skybox_presets[] = {

        {"Piss", "rbxassetid://2651437350", "rbxassetid://2651436979", "rbxassetid://2651436494", "rbxassetid://2651435990", "rbxassetid://2651432901", "rbxassetid://2651434974"},
        {"Peach", "rbxassetid://566616187", "rbxassetid://566616082", "rbxassetid://566616044", "rbxassetid://566616141", "rbxassetid://566616113", "rbxassetid://566616232"},
        {"Saku", "http://www.roblox.com/asset/?id=271077958", "http://www.roblox.com/asset/?id=271042467", "http://www.roblox.com/asset/?id=271042310", "http://www.roblox.com/asset/?id=271042556", "http://www.roblox.com/asset/?id=271042516", "http://www.roblox.com/asset/?id=271077243"},
        {"Purple", "http://www.roblox.com/asset/?id=570557727", "http://www.roblox.com/asset/?id=570557672", "http://www.roblox.com/asset/?id=570557620", "http://www.roblox.com/asset/?id=570557559", "http://www.roblox.com/asset/?id=570557514", "http://www.roblox.com/asset/?id=570557775"},
        {"Retro", "rbxassetid://18164890128", "rbxassetid://18164873920", "rbxassetid://18164877945", "rbxassetid://18164870251", "rbxassetid://18164881924", "rbxassetid://18166113875"},
        {"Space", "rbxassetid://15983964246", "rbxassetid://15983966246", "rbxassetid://15983967420", "rbxassetid://15983965025", "rbxassetid://15983968922", "rbxassetid://15983966825"},
        {"Sea", "http://www.roblox.com/asset/?id=321846070", "http://www.roblox.com/asset/?id=321846207", "http://www.roblox.com/asset/?id=321846162", "http://www.roblox.com/asset/?id=321845951", "http://www.roblox.com/asset/?id=321846018", "http://www.roblox.com/asset/?id=321846104"},
        {"Night V2", "http://www.roblox.com/asset/?id=12064131", "http://www.roblox.com/asset/?id=12064115", "http://www.roblox.com/asset/?id=12063984", "http://www.roblox.com/asset/?id=12064121", "http://www.roblox.com/asset/?id=12064107", "http://www.roblox.com/asset/?id=12064152"},
        {"Dark", "rbxassetid://15470160563", "rbxassetid://15470158022", "rbxassetid://15470155938", "rbxassetid://15470153860", "rbxassetid://15470149279", "rbxassetid://15470151245"},
        {"Anime", "http://www.roblox.com/asset/?id=104038404823203", "http://www.roblox.com/asset/?id=99961685452126", "http://www.roblox.com/asset/?id=84924000207295", "http://www.roblox.com/asset/?id=95687237979398", "http://www.roblox.com/asset/?id=81858382098344", "http://www.roblox.com/asset/?id=138472117789684"},
        {"Beach", "http://www.roblox.com/asset/?id=151165227", "http://www.roblox.com/asset/?id=151165206", "http://www.roblox.com/asset/?id=151165191", "http://www.roblox.com/asset/?id=151165224", "http://www.roblox.com/asset/?id=151165214", "http://www.roblox.com/asset/?id=151165197"},
        {"Space V2", "http://www.roblox.com/asset/?id=16262366016", "http://www.roblox.com/asset/?id=16262363873", "http://www.roblox.com/asset/?id=16262362003", "http://www.roblox.com/asset/?id=16262360469", "http://www.roblox.com/asset/?id=16262356578", "http://www.roblox.com/asset/?id=16262358026"},
        {"Pink", "rbxassetid://12635316856", "rbxassetid://12635315817", "rbxassetid://12635313718", "rbxassetid://12635312870", "rbxassetid://12635309703", "rbxassetid://12635311686"},
        {"Rainbow", "rbxassetid://12877083856", "rbxassetid://12877085497", "rbxassetid://12877085497", "rbxassetid://12877085497", "rbxassetid://12877085497", "rbxassetid://12877086914"},
        {"Forest", "http://www.roblox.com/asset/?id=237593929", "http://www.roblox.com/asset/?id=237593835", "http://www.roblox.com/asset/?id=237593861", "http://www.roblox.com/asset/?id=237593922", "http://www.roblox.com/asset/?id=237593887", "http://www.roblox.com/asset/?id=237593849"},
        {"Night", "http://www.roblox.com/asset/?id=154185031", "http://www.roblox.com/asset/?id=154184972", "http://www.roblox.com/asset/?id=154184943", "http://www.roblox.com/asset/?id=154185021", "http://www.roblox.com/asset/?id=154185004", "http://www.roblox.com/asset/?id=154184960"},
        {"Lava", "http://www.roblox.com/asset/?id=4776130793", "http://www.roblox.com/asset/?id=4776133150", "http://www.roblox.com/asset/?id=4776128425", "http://www.roblox.com/asset/?id=4776131365", "http://www.roblox.com/asset/?id=4776124334", "http://www.roblox.com/asset/?id=4776125375"},
        {"Rainy", "http://www.roblox.com/asset/?id=4495867486", "http://www.roblox.com/asset/?id=4495866584", "http://www.roblox.com/asset/?id=4495866035", "http://www.roblox.com/asset/?id=4495865458", "http://www.roblox.com/asset/?id=4495864450", "http://www.roblox.com/asset/?id=4495864887"},
        {"Green", "rbxassetid://566611218", "rbxassetid://566611300", "rbxassetid://566611266", "rbxassetid://566611142", "rbxassetid://566611187", "rbxassetid://566613198"},
        {"Volcanic", "http://www.roblox.com/asset/?id=150281471", "http://www.roblox.com/asset/?id=150281426", "http://www.roblox.com/asset/?id=150281400", "http://www.roblox.com/asset/?id=150281461", "http://www.roblox.com/asset/?id=150281446", "http://www.roblox.com/asset/?id=150281418"},
        {"Minecraft", "http://www.roblox.com/asset/?id=8735166729", "http://www.roblox.com/asset/?id=8735166751", "http://www.roblox.com/asset/?id=8735166755", "http://www.roblox.com/asset/?id=8735231668", "rbxassetid://8735166756", "http://www.roblox.com/asset/?id=8735166707"},
        {"Lucid", "rbxassetid://8508112781", "rbxassetid://8508111092", "rbxassetid://8508107681", "rbxassetid://8508104949", "rbxassetid://8508098796", "rbxassetid://8508103588"},
        {"Nebulous", "rbxassetid://131036626982613", "rbxassetid://103716549795832", "rbxassetid://126542804346203", "rbxassetid://107665368823185", "rbxassetid://95020137072033", "rbxassetid://92862258103959"}
    };

    static_assert(sizeof(k_skybox_presets) / sizeof(k_skybox_presets[0]) == k_skybox_count,
                  "k_skybox_names (skybox_changer.h) is out of sync with k_skybox_presets");

    // k_skybox_count / k_skybox_names now live in the header (inline) - the
    // menu's combo reads them directly.

    // Roblox's asset service still accepts the legacy
    // "http://www.roblox.com/asset/?id=N" form in the DataModel, but the modern
    // content pipeline resolves everything through the rbxassetid scheme - and
    // half of the preset table above is in the legacy form (it comes from the
    // reference, which was written years ago). Rewriting every face to the scheme
    // the client actually uses is free and removes one whole class of "the string
    // is set and nothing happens": a url the loader refuses is a sky that does
    // not change, with a perfect read-back.
    static std::string normalise_asset_url(const char* url) {
        if (!url) return std::string();
        const char* p = std::strstr(url, "id=");
        if (!p) return std::string(url);
        const char* digits = p + 3;
        const char* end = digits;
        while (*end >= '0' && *end <= '9') ++end;
        if (end == digits) return std::string(url);   // "id=" with no number: leave it
        std::string out = "rbxassetid://";
        out.append(digits, (size_t)(end - digits));
        return out;
    }

    // Invalidation is a STATE, not an event. Writing false once and then writing
    // true again in the same pass means the renderer, which samples its dirty
    // flags when it builds a frame, could never observe the false. Keeping them
    // false for a short window (below) guarantees at least one rendered frame
    // sees it; the writes stop on their own and the hold expires.
    static bool     invalidate_hold = false;
    static uint64_t invalidate_until = 0;

    // ── THE DIRTY-FLAG PROBLEM, AND WHY THIS SCANS FOR IT ────────────────────
    //
    // Writing the six face strings is not enough: the renderer keeps the sky
    // cubemap it uploaded and only rebuilds it when its validity byte is false.
    // The two offsets this file used to poke (RenderView::SkyValid 0x28d /
    // LightingValid 0x150) are GUESSES - no dump of this build publishes them -
    // and the +0x10 relationship that holds for the dump's Lighting fields does
    // not hold for RenderView at all (the reference's two bytes are 0x2dc and
    // 0x278, which is neither our value nor its neighbour), so they cannot be
    // derived. Guessing is why the sky "didn't work" twice.
    //
    // So this does not guess. It finds the renderer's validity bytes by
    // BEHAVIOUR: a byte that the renderer resets to true on its own after we
    // clear it is a byte it consumes while building frames. The scan walks a
    // window of RenderView, only considers bytes that are exactly 1 and isolated
    // on both sides (a float's 0x01 byte is almost never surrounded by zeros;
    // a bool flag almost always is), clears one, waits ~three frames, reads it
    // back, and keeps the ones that came back true. Everything touched is put
    // back to 1 either way, and the scan is paced one candidate at a time - it
    // never sleeps inside the feature loop.
    //
    // The discovered set is then cleared on every apply, which is precisely the
    // "reset the cache every time" the user asked for: whatever the sky's own
    // validity byte is, it is in the set.
    static constexpr uintptr_t kProbeBase = 0x100;
    static constexpr size_t    kProbeLen = 0x300;

    struct DirtyFlagProbe {
        int      state = 0;        // 0 = not started, 1 = walking candidates, 2 = done
        uintptr_t rv = 0;
        std::vector<uintptr_t> candidates;
        size_t   index = 0;
        int      phase = 0;        // 0 = clear the byte, 1 = read it back
        uint64_t due = 0;
        std::vector<uintptr_t> flags;   // confirmed renderer-consumed, absolute
        bool     logged = false;
    };
    static DirtyFlagProbe s_probe;

    static void ResetProbe() {
        s_probe = DirtyFlagProbe{};
    }

    // One paced step of the scan. Returns immediately unless the next phase is due.
    static void ProbeDirtyFlags(uintptr_t rv) {
        if (!is_valid_address(rv)) return;
        if (s_probe.state == 2) return;
        if (s_probe.rv != rv) {
            // The RenderView was re-created (a rejoin); start over on the new one.
            ResetProbe();
            s_probe.rv = rv;
            s_probe.state = 1;
        }
        if (s_probe.state == 0) {
            s_probe.rv = rv;
            s_probe.state = 1;
        }

        const uint64_t now = platform::NowMs();
        if (s_probe.due != 0 && (int)(now - s_probe.due) < 0) return;
        s_probe.due = now + 55;   // ~3 frames at 60fps; still just 18 probes/s

        if (s_probe.candidates.empty() && s_probe.index == 0 && s_probe.phase == 0) {
            std::vector<uint8_t> buf(kProbeLen);
            if (!read_raw(rv + kProbeBase, buf.data(), kProbeLen)) {
                s_probe.state = 2;
                LogLine("skybox: the RenderView window is not readable - keeping the "
                        "guessed validity bytes");
                return;
            }
            for (size_t i = 1; i + 2 < kProbeLen; ++i) {
                if (buf[i] != 1) continue;
                if (buf[i - 1] != 0 || buf[i + 1] != 0 || buf[i + 2] != 0) continue;
                s_probe.candidates.push_back(rv + kProbeBase + i);
                if (s_probe.candidates.size() >= 40) break;
            }
            if (s_probe.candidates.empty()) {
                s_probe.state = 2;
                LogLine("skybox: no isolated flag bytes in RenderView+0x%llX..+0x%llX - "
                        "the sky validity byte may be a bitfield, not a bool",
                        (unsigned long long)kProbeBase,
                        (unsigned long long)(kProbeBase + kProbeLen));
                return;
            }
            LogLine("skybox: scanning %lu candidate validity bytes in RenderView "
                    "(this is paced, ~%lu ms)", (unsigned long)s_probe.candidates.size(),
                    (unsigned long)(s_probe.candidates.size() * 110));
            return;
        }

        if (s_probe.phase == 0) {
            if (s_probe.index >= s_probe.candidates.size()) {
                s_probe.state = 2;
                if (!s_probe.flags.empty()) {
                    LogLine("skybox: %lu renderer-consumed validity byte(s) found - the sky "
                            "cache is now reset by behaviour, not by a guessed offset",
                            (unsigned long)s_probe.flags.size());
                } else {
                    LogLine("skybox: no renderer-consumed validity byte found in "
                            "RenderView+0x%llX.. - the sky cannot be refreshed by "
                            "invalidation, so the face writes alone will not show",
                            (unsigned long long)kProbeBase);
                }
                return;
            }
            write<uint8_t>(s_probe.candidates[s_probe.index], 0);
            s_probe.phase = 1;
            return;
        }

        // phase 1: did the renderer put it back?
        const uintptr_t a = s_probe.candidates[s_probe.index];
        const uint8_t back = read<uint8_t>(a);
        if (back == 1) s_probe.flags.push_back(a);
        else if (back == 0) write<uint8_t>(a, 1);   // ours to undo, not the renderer's
        ++s_probe.index;
        s_probe.phase = 0;
    }

    // Clear every discovered validity byte. Called on apply and during the hold;
    // falls back to the two historical guesses only until the scan finishes.
    static void InvalidateSky(uintptr_t rv) {
        if (!is_valid_address(rv)) return;
        if (!s_probe.flags.empty()) {
            for (uintptr_t a : s_probe.flags) write<uint8_t>(a, 0);
            return;
        }
        if (Offsets::RenderView::SkyValid)
            write<uint8_t>(rv + Offsets::RenderView::SkyValid, 0);
        if (Offsets::RenderView::LightingValid)
            write<uint8_t>(rv + Offsets::RenderView::LightingValid, 0);
    }

    static void cleanup_allocations() {
        our_allocations.clear();
    }

    static bool write_string_safe(uintptr_t string_addr, const std::string& str) {
        if (!string_addr || !is_valid_address(string_addr)) return false;

        int str_len = static_cast<int>(str.length());
        if (str_len == 0) return false;

        if (!is_valid_address(string_addr + 0x18)) return false;
        if (!is_valid_address(string_addr + Offsets::Misc::StringLength)) return false;

        int current_capacity = read<int>(string_addr + 0x18);

        char* buffer = new (std::nothrow) char[str_len + 1];
        if (!buffer) return false;

        std::memset(buffer, 0, str_len + 1);
        std::memcpy(buffer, str.c_str(), static_cast<size_t>(str_len));

        bool success = false;

        if (str_len < 16) {
            write_raw(string_addr, buffer, static_cast<size_t>(str_len) + 1);
            write<int>(string_addr + Offsets::Misc::StringLength, str_len);
            write<int>(string_addr + 0x18, 15);
            success = true;
        }
        else {
            uintptr_t data_addr = 0;

            if (current_capacity >= str_len) {
                data_addr = read<uintptr_t>(string_addr);
            }
            else {
                for (const auto& alloc : our_allocations) {
                    if (alloc.string_addr == string_addr && alloc.data_addr) {
                        data_addr = alloc.data_addr;
                        break;
                    }
                }

                if (!data_addr) {
                    // Remote allocation through the platform seam: on Windows
                    // this is NtAllocateVirtualMemory inside the game; a Linux
                    // port has no remote allocator over /proc and refuses
                    // here (skybox strings then stay Windows-only).
                    void* process = mem::roblox_h.load();
                    if (process) {
                        uintptr_t alloc_base = 0;
                        if (platform::AllocMem(process, (size_t)(str_len + 1), &alloc_base)) {
                            data_addr = alloc_base;
                            our_allocations.push_back({ string_addr, data_addr });
                        }
                    }
                }
            }

            if (data_addr && is_valid_address(data_addr)) {
                write_raw(data_addr, buffer, static_cast<size_t>(str_len) + 1);
                write<uintptr_t>(string_addr, data_addr);
                write<int>(string_addr + Offsets::Misc::StringLength, str_len);
                write<int>(string_addr + 0x18, str_len > current_capacity ? str_len : current_capacity);
                success = true;
            }
        }

        delete[] buffer;
        return success;
    }

    void RunSkyboxChanger() {
        if (!skybox_changer_enabled) {
            cleanup_allocations();
            skybox_written = false;
            had_players = false;
            last_skybox_type = -1;
            last_sky_address = 0;
            last_place_id = 0;
            place_just_changed = false;
            return;
        }

        // Keep the dirty flags down for the hold window (see invalidate_hold).
        if (invalidate_hold) {
            const uint64_t now = platform::NowMs();
            if ((int)(now - invalidate_until) >= 0) {
                invalidate_hold = false;
            } else if (is_valid_address(last_rv_for_invalidate)) {
                InvalidateSky(last_rv_for_invalidate);
            }
        }

        if (verify_waiting && std::chrono::steady_clock::now() >= verify_due) {
            verify_waiting = false;
            if (is_valid_address(verify_rv)) {
                int restored = 0;
                for (uintptr_t a : s_probe.flags) {
                    if (read<uint8_t>(a) == 1) ++restored;
                }
                const int total = (int)s_probe.flags.size();
                if (total > 0 && restored == total && !verify_have_last) {
                    verify_have_last = true;
                    LogLine("skybox: all %d discovered validity bytes were set back to true by "
                            "the renderer - the invalidation is being consumed. If the sky still "
                            "did not change, the face offsets are wrong for this build (the "
                            "read-back cannot tell: it reads the same address it wrote)",
                            total);
                } else if (total > 0 && restored == 0) {
                    LogLine("skybox: none of the %d validity bytes were consumed within 300 ms "
                            "- the renderer is not drawing the sky (minimised?) or these are "
                            "not its bytes", total);
                }
            }
        }

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "FAIL: DataModel invalid");
            had_players = false;
            skybox_written = false;
            last_sky_address = 0;
            return;
        }

        uint64_t current_place_id = game::GetPlaceId(g_base_address);
        if (current_place_id != last_place_id) {
            if (last_place_id != 0 && current_place_id != 0) {
                std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Place changed: %llu -> %llu",
                              (unsigned long long)last_place_id, (unsigned long long)current_place_id);
                cleanup_allocations();
                place_change_time = std::chrono::steady_clock::now();
                place_just_changed = true;
                skybox_written = false;
                last_skybox_type = -1;
                last_sky_address = 0;
            }
            last_place_id = current_place_id;
        }

        if (place_just_changed) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - place_change_time
            ).count();

            if (elapsed < 2) {
                std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Waiting after place change... (%lld/2s)", (long long)elapsed);
                return;
            }
            place_just_changed = false;
        }

        instance players_service = dm.read_service("Players");
        if (!players_service.is_valid()) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Waiting for Players...");
            had_players = false;
            skybox_written = false;
            last_sky_address = 0;
            return;
        }

        auto players = players_service.get_children();
        bool has_players = !players.empty();

        if (!has_players) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Waiting for players to join...");
            had_players = false;
            skybox_written = false;
            last_sky_address = 0;
            return;
        }

        bool type_changed = (skybox_type != last_skybox_type);
        bool players_returned = (has_players && !had_players);

        if (skybox_written && !type_changed && !players_returned && last_sky_address != 0) {
            auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_apply_time).count();
            if (since < 2500) {
                std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Skybox active (%s)", k_skybox_names[skybox_type]);
                had_players = has_players;
                return;
            }
            // periodic refresh: fall through and rewrite, so a core script or the
            // renderer reverting the sky can never permanently undo us
        }

        had_players = has_players;

        std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "DM ok");
        instance lighting = dm.read_service("Lighting");
        if (!lighting.is_valid()) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "FAIL: Lighting not found");
            skybox_written = false;
            last_sky_address = 0;
            return;
        }

        std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Lighting ok");
        instance sky{};
        {
            auto lighting_children = lighting.get_children();
            for (const instance& child : lighting_children) {
                if (!child.is_valid()) continue;
                if (child.get_class_name() == "Sky") {
                    sky = child;
                    break;
                }
            }
        }
        if (!sky.is_valid()) {
            sky = read<instance>(lighting.address + Offsets::Lighting::Sky);
        }
        if (!sky.is_valid()) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "No Sky in game");
            skybox_written = false;
            last_sky_address = 0;
            return;
        }

        if (!sky.is_valid() || !is_valid_address(sky.address)) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Sky address invalid");
            skybox_written = false;
            last_sky_address = 0;
            return;
        }

        if (last_sky_address != 0 && last_sky_address != sky.address) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "New Sky detected, reapplying...");
            skybox_written = false;
            last_skybox_type = -1;
        }
        last_sky_address = sky.address;

        std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Sky ok 0x%zX", (size_t)sky.address);
        uintptr_t ve = read<uintptr_t>(g_base_address + Offsets::VisualEngine::Pointer);
        if (!ve || !is_valid_address(ve)) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "FAIL: VisualEngine invalid (ve=0x%zX)", (size_t)ve);
            skybox_written = false;
            return;
        }

        std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "VE 0x%zX", (size_t)ve);
        uintptr_t render_view = read<uintptr_t>(ve + Offsets::VisualEngine::RenderView);
        if (!render_view || !is_valid_address(render_view)) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "FAIL: RenderView invalid (rv=0x%zX)", (size_t)render_view);
            skybox_written = false;
            return;
        }

        std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "RV 0x%zX", (size_t)render_view);

        int preset_idx = skybox_type;
        if (preset_idx < 0 || preset_idx >= k_skybox_count) preset_idx = 0;
        const SkyboxPreset& preset = k_skybox_presets[preset_idx];

        uintptr_t face_addrs[6] = {
            sky.address + Offsets::Sky::SkyboxUp,
            sky.address + Offsets::Sky::SkyboxDn,
            sky.address + Offsets::Sky::SkyboxFt,
            sky.address + Offsets::Sky::SkyboxBk,
            sky.address + Offsets::Sky::SkyboxLf,
            sky.address + Offsets::Sky::SkyboxRt
        };

        for (int i = 0; i < 6; i++) {
            if (!is_valid_address(face_addrs[i])) {
                std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "FAIL: Invalid face address %d", i);
                skybox_written = false;
                return;
            }
        }

        // The read-back below cannot prove the offset is really the Sky's face - it
        // reads back the address it just wrote. The ORIGINAL value can: Roblox stores
        // a face as an asset url, so a face that does not look like one is not a face,
        // and writing over it would be corrupting somebody else's string. This is the
        // check that turns "the skybox silently does nothing" into a line that names
        // the problem.
        static bool said_bad_offsets = false;
        {
            int plausible = 0;
            std::string sample;
            for (int i = 0; i < 6; ++i) {
                const std::string cur = fetchstring(face_addrs[i]);
                if (sample.empty()) sample = cur;
                if (cur.empty() || cur.find("rbxasset") != std::string::npos ||
                    cur.find("http") != std::string::npos ||
                    cur.find("asset/?id=") != std::string::npos)
                    ++plausible;
            }
            if (plausible < 4) {
                if (!said_bad_offsets) {
                    said_bad_offsets = true;
                    LogLine("skybox: the Sky face offsets do not match this build - the strings "
                            "at Sky+0x%llX.. do not look like texture urls (first reads \"%.48s\"). "
                            "Nothing was written; these offsets need a fresh dump of the build.",
                            (unsigned long long)Offsets::Sky::SkyboxUp, sample.c_str());
                }
                std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg),
                              "FAIL: Sky offsets do not match this build");
                return;
            }
            said_bad_offsets = false;
        }

        // Learn the renderer's validity bytes on this RenderView, one candidate per
        // pass, and clear whatever has been found so far.
        ProbeDirtyFlags(render_view);
        InvalidateSky(render_view);

        static std::string face_storage[6];
        const char* legacy[6] = { preset.up, preset.dn, preset.ft, preset.bk, preset.lf, preset.rt };
        const char* face_values[6] = {};
        for (int i = 0; i < 6; i++) {
            face_storage[i] = normalise_asset_url(legacy[i]);
            face_values[i] = face_storage[i].c_str();
        }

        int failed_count = 0;
        for (int i = 0; i < 6; i++)
            if (!write_string_safe(face_addrs[i], face_values[i])) failed_count++;

        if (failed_count > 0) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "FAIL: %d faces failed to write", failed_count);
            skybox_written = false;
            return;
        }

        // verify the writes actually landed by reading each face straight back
        int bad_readback = 0;
        for (int i = 0; i < 6; i++) {
            std::string rb = fetchstring(face_addrs[i]);
            if (rb != face_values[i]) bad_readback++;
        }
        skybox_written = true;
        last_apply_time = std::chrono::steady_clock::now();
        if (logged_preset != preset_idx) {
            logged_preset = preset_idx;
            LogLine("skybox: applied %s - %d/6 faces read back as written, %lu discovered "
                    "validity byte(s) cleared",
                    k_skybox_names[skybox_type], bad_readback,
                    (unsigned long)s_probe.flags.size());
        }
        verify_waiting = true;
        verify_rv = render_view;
        verify_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        if (bad_readback > 0) {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg),
                          "applied %s (%d/6 readback mismatch)", k_skybox_names[skybox_type], bad_readback);
        } else {
            std::snprintf(skybox_debug_msg, sizeof(skybox_debug_msg), "Skybox applied (%s)", k_skybox_names[skybox_type]);
        }

        // Invalidate AFTER writing, not before-and-then-revalidate. Setting these
        // back to true immediately told the renderer the cached sky was still good,
        // so it never re-uploaded the new textures.
        ProbeDirtyFlags(render_view);
        InvalidateSky(render_view);

        // Arm the multi-frame invalidation hold: the renderer gets several frames
        // to notice, which is what "reset the cache on every change" asks for.
        last_rv_for_invalidate = render_view;
        invalidate_until = platform::NowMs() + 250;
        invalidate_hold = true;

        last_skybox_type = skybox_type;
    }
}