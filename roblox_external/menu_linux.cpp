// ----------------------------------------------------------------------------
//  menu_linux.cpp — Linux glue for the menu (no Windows.h)
// ----------------------------------------------------------------------------
//  Mirrors menu.cpp but for the Linux overlay. Layout still lives in
//  ui/menu_ui.cpp (platform-free). This file provides:
//   * KeyName / PollKey via VK codes + platform::SampleKey
//   * OSName via /etc/os-release
//   * FillSnapshot / FillPlayers / RunTest (same as Windows, memory reads
//     are OS-agnostic)
//   * MakeActions / RenderMenu / TickKeybinds
// ----------------------------------------------------------------------------
#ifdef __linux__

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>

#include "imgui/imgui.h"
#include "obsidian/obsidian.h"

#include "globals.h"
#include "memory.h"
#include "game.h"
#include "cache.h"
#include "features/movement/mover.h"
#include "features/explorer/explorer.h"
#include "offsets.h"
#include "offsets_live.h"
#include "features/skybox_changer/skybox_changer.h"
#include "features/players/friends.h"
#include "features/hitsound/hitsound.h"
#include "features/aimbot/aimbot.h"
#include "features/players/players.h"
#include "features/waypoints/waypoints.h"
#include "features/config/config.h"
#include "features/servers/servers.h"
#include "ui/menu_ui.h"
#include "platform/platform.h"
#include "platform/streamproof_watcher.h"
#include "offsets_dynamic.h"

// overlay_linux.cpp — status line for the debug page (click-through / game rect)
namespace overlay { const char* OverlayStatus(); }

namespace obs = obsidian;

namespace {

// ----------------------------------------------------------------------------
//  key names
// ----------------------------------------------------------------------------
const char* KeyName(int key) {
    if (key == 0) return "unbound";
    if (key == platform::key::LBUTTON) return "mouse 1";
    if (key == platform::key::RBUTTON) return "mouse 2";
    if (key == platform::key::MBUTTON) return "mouse 3";
    if (key == platform::key::MOUSE_X1) return "mouse 4";
    if (key == platform::key::MOUSE_X2) return "mouse 5";
    // Simple static table for common keys
    static char buf[32];
    switch (key) {
        case platform::key::SPACE: return "space";
        case platform::key::ESCAPE: return "escape";
        case platform::key::TAB: return "tab";
        case platform::key::RETURN: return "enter";
        case platform::key::SHIFT: return "shift";
        case platform::key::CONTROL: return "ctrl";
        case platform::key::LSHIFT: return "left shift";
        case platform::key::RSHIFT: return "right shift";
        case platform::key::LCONTROL: return "left ctrl";
        case platform::key::RCONTROL: return "right ctrl";
        case platform::key::LEFT: return "left";
        case platform::key::RIGHT: return "right";
        case platform::key::UP: return "up";
        case platform::key::DOWN: return "down";
        case platform::key::BACK: return "backspace";
        case platform::key::DEL: return "delete";
        default: break;
    }
    if (key >= 'A' && key <= 'Z') {
        buf[0] = (char)key; buf[0] = (char)(buf[0] + 32); buf[1]=0; return buf;
    }
    if (key >= '0' && key <= '9') { buf[0]=(char)key; buf[1]=0; return buf; }
    snprintf(buf, sizeof(buf), "key %d", key);
    return buf;
}

int PollKey() {
    // Linux: poll via platform::SampleKey. De-bounce by waiting for release.
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == platform::key::LBUTTON || vk == platform::key::RBUTTON || vk == platform::key::MBUTTON ||
            vk == platform::key::MOUSE_X1 || vk == platform::key::MOUSE_X2 || vk == platform::key::ESCAPE)
            continue;
        auto ks = platform::SampleKey(vk);
        if (ks.down) {
            // wait for release
            for (int w=0; w<2000; ++w) {
                platform::SleepMs(1);
                if (!platform::SampleKey(vk).down) break;
            }
            return vk;
        }
    }
    static const int buttons[5] = { platform::key::LBUTTON, platform::key::RBUTTON, platform::key::MBUTTON,
                                    platform::key::MOUSE_X1, platform::key::MOUSE_X2 };
    for (int i = 0; i < 5; ++i) {
        if (platform::SampleKey(buttons[i]).down) {
            for (int w=0; w<1000; ++w) {
                platform::SleepMs(1);
                if (!platform::SampleKey(buttons[i]).down) break;
            }
            return buttons[i];
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
//  what we are running on
// ----------------------------------------------------------------------------
const char* OSName()
{
    static char buf[64];
    if (buf[0]) return buf;
    std::ifstream f("/etc/os-release");
    std::string pretty;
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("PRETTY_NAME=",0)==0) {
                size_t q1 = line.find('"');
                size_t q2 = line.rfind('"');
                if (q1!=std::string::npos && q2!=std::string::npos && q2>q1) pretty=line.substr(q1+1,q2-q1-1);
                else pretty=line.substr(12);
                break;
            }
        }
    }
    if (pretty.empty()) {
        // fallback: /etc/lsb-release or uname
        std::ifstream f2("/etc/lsb-release");
        if (f2) pretty="Linux";
        else pretty="Void Linux";
    }
    snprintf(buf, sizeof(buf), "%s", pretty.c_str());
    return buf;
}

// ----------------------------------------------------------------------------
//  snapshot plumbing
// ----------------------------------------------------------------------------
menuui::Snapshot g_snap;

void Row(std::vector<menuui::Snapshot::Row>& v, const char* key, int state,
         const char* fmt, ...) {
    if (v.size() >= 14) return;
    menuui::Snapshot::Row r;
    r.key = key;
    r.state = state;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r.value, sizeof(r.value), fmt, ap);
    va_end(ap);
    v.push_back(r);
}

// ----------------------------------------------------------------------------
//  the players tab's list
// ----------------------------------------------------------------------------
void FillPlayers(menuui::Snapshot& s, const cache::LocalPlayerData& lp)
{
    s.player_count = 0;
    s.spectating   = spectate_enabled;
    s.player_note[0] = '\0';

    if (players_status_msg[0])
        snprintf(s.player_note, sizeof(s.player_note), "%s", players_status_msg);

    const cache::EspSnapshot ents = cache::GetEspSnapshot();
    if (!ents || ents->empty()) {
        if (!s.player_note[0])
            snprintf(s.player_note, sizeof(s.player_note), "%s",
                     s.attached && !s.in_game
                         ? "There is no server to list on the Roblox home page. Join an "
                           "experience and the rows appear here."
                         : "No other players resolved yet. The cache fills itself a few "
                           "seconds after a character spawns.");
        return;
    }

    struct Hit { float dist; const cache::EspEntity* e; };
    Hit hits[128];
    int n = 0;
    for (const cache::EspEntity& e : *ents) {
        if (n >= (int)(sizeof(hits) / sizeof(hits[0]))) break;
        const float dx = e.root_x - lp.x, dy = e.root_y - lp.y, dz = e.root_z - lp.z;
        hits[n].dist = lp.valid ? sqrtf(dx * dx + dy * dy + dz * dz) : 0.0f;
        hits[n].e = &e;
        ++n;
    }
    std::sort(hits, hits + n, [](const Hit& a, const Hit& b) { return a.dist < b.dist; });

    for (int i = 0; i < n && s.player_count < menuui::Snapshot::kMaxPlayers; ++i) {
        const cache::EspEntity& e = *hits[i].e;
        menuui::Snapshot::PlayerRow& r = s.players[s.player_count];

        const char* nm = e.display_name[0] ? e.display_name : e.name;
        snprintf(r.name, sizeof(r.name), "%s", (nm && nm[0]) ? nm : "unknown");
        snprintf(r.account, sizeof(r.account), "%s", e.name[0] ? e.name : r.name);
        r.user_id = e.user_id;
        r.friended = features::friends::IsFriend(e.user_id);

        if (e.IsDead()) {
            snprintf(r.detail, sizeof(r.detail), "dead - %.0f studs", hits[i].dist);
            r.state = 1;
        } else {
            snprintf(r.detail, sizeof(r.detail), "%.0f/%.0f hp - %.0f studs",
                     e.health, e.max_health > 0.0f ? e.max_health : 100.0f, hits[i].dist);
            if (e.tool_name[0]) {
                const size_t len = strlen(r.detail);
                snprintf(r.detail + len, sizeof(r.detail) - len, " - %s", e.tool_name);
            }
            r.state = 0;
        }
        if (spectate_enabled && e.user_id == spectate_user_id) r.state = 2;
        ++s.player_count;
    }
}

void FillSnapshot() {
    menuui::Snapshot& s = g_snap;

    s.runtime.clear();
    s.camera.clear();

    const bool attached = (g_base_address != 0);
    s.attached = attached;
    s.aimbot_latched = features::AimbotKeyLatched();

    const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
    s.user[0] = '\0';
    if (lp.name[0]) snprintf(s.user, sizeof(s.user), "%s", lp.name);
    s.pid = (unsigned)mem::process_id.load();
    snprintf(s.os, sizeof(s.os), "%s", OSName());

    // The two things a Linux report needs first: what the hook side (PID
    // finder + dynamic offsets) resolved, and what the overlay side (game
    // rect, click-through) is doing. Shown even while detached, because
    // "waiting for Sober" is exactly when the scan diag matters.
    {
        const bool scanned = offsets_dynamic::GetScanResult().ok();
        Row(s.runtime, "sober scan", scanned ? 1 : (attached ? 2 : 0),
            "%.*s", 88, offsets_dynamic::diag().c_str());
        Row(s.runtime, "overlay", 0, "%.*s", 88, overlay::OverlayStatus());
        Row(s.runtime, "streamproof", streamproof_enabled ? 1 : 2,
            streamproof_enabled ? "on — overlay hides while a capture app runs (now: %s)" : "off",
            streamproof_enabled
                ? (streamproof::CapturerName()[0] ? streamproof::CapturerName() : "none")
                : "");
        Row(s.runtime, "lock", g_locked_to_sober ? 1 : 0,
            g_locked_to_sober ? "locked to Sober (bootstrapper-managed)" : "standalone launch");
    }

    if (!attached) {
        snprintf(s.status, sizeof(s.status), "waiting for roblox");
        s.servers_busy = false;
        s.servers_can_rejoin = false;
        s.server_count = 0;
        s.server_current = -1;
        s.server_age_sec = -1;
        s.game[0] = '\0';
        s.place[0] = '\0';
        s.servers_msg[0] = '\0';
        s.in_game = false;
        s.can_test = false;
        return;
    }
    snprintf(s.status, sizeof(s.status), "%s", s.user[0] ? "attached" : "attaching");

    s.hitsound_names = features::hitsound::SoundNames();
    s.hitsound_count = features::hitsound::SoundCount();

    s.skybox_names = features::k_skybox_names;
    s.skybox_count = features::k_skybox_count;
    snprintf(s.skybox_note, sizeof(s.skybox_note), "%s",
             skybox_debug_msg[0] ? skybox_debug_msg : "Nothing applied yet");

    features::servers::View sv;
    features::servers::SnapshotInto(sv);

    instance dm = game::ReadDatamodel(g_base_address);
    if (dm.is_valid()) {
        snprintf(s.game, sizeof(s.game), "%s", sv.place_name);
        const uint64_t place_id = read<uint64_t>(dm.address + Offsets::DataModel::PlaceId);
        const bool loaded = read<bool>(dm.address + Offsets::DataModel::GameLoaded);
        snprintf(s.place, sizeof(s.place), "%llu", (unsigned long long)place_id);
        s.in_game = loaded && place_id != 0;
        if (!s.in_game)
            snprintf(s.game, sizeof(s.game), "roblox home");
    } else {
        s.game[0] = '\0';
        s.in_game = false;
    }

    menuui::FillServers(s, sv);
    FillPlayers(s, lp);

    const int page = menuui::CurrentPage();
    if (page != 8) return;

    instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
    {
        const char* loaded = *OffsetsLoadedVersion() ? OffsetsLoadedVersion()
                                                      : Offsets::ClientVersion.c_str();
        const char* client = OffsetsClientVersion();
        const bool  known  = client[0] != '\0';
        const bool  match  = known && std::strcmp(loaded, client) == 0;
        char otext[128];
        if (!known) snprintf(otext, sizeof(otext), "%s (client unknown)", loaded);
        else if (match) snprintf(otext, sizeof(otext), "%s", loaded);
        else snprintf(otext, sizeof(otext), "%s - CLIENT IS %s", loaded, client);
        Row(s.runtime, "offsets", match ? 1 : (known ? 3 : 2), "%s", otext);
    }

    Row(s.runtime, "base",     0, "0x%llX", (unsigned long long)g_base_address);
    Row(s.runtime, "engine",   ve.is_valid() ? 1 : 3, "0x%llX", (unsigned long long)ve.address);
    Row(s.runtime, "datamodel", dm.is_valid() ? 1 : 3, "0x%llX", (unsigned long long)dm.address);
    const auto ents_snap = cache::GetEspSnapshot();
    Row(s.runtime, "players",  (*ents_snap).empty() ? 2 : 1, "%d cached", (int)(*ents_snap).size());
    {
        const float wpm = features::mover::WritesPerMs();
        const float per_step = wpm * 16.0f;
        Row(s.runtime, "pos writes", per_step >= 8.0f ? 1 : (per_step > 0.0f ? 2 : 0),
            "%.1f/ms  (%.0f per step)", wpm, per_step);
    }

    if (ve.is_valid()) {
        float view[16]{};
        float dims[2]{};
        read_raw(ve.address + Offsets::VisualEngine::ViewMatrix, view, sizeof(view));
        read_raw(ve.address + Offsets::VisualEngine::Dimensions, dims, sizeof(dims));
        const bool dims_ok = (dims[0] > 0.0f && dims[1] > 0.0f);
        Row(s.camera, "viewport", dims_ok ? 1 : 3, "%.0f x %.0f", dims[0], dims[1]);
        bool all_zero = true;
        for (int i = 0; i < 16; ++i) if (view[i] != 0.0f) { all_zero = false; break; }
        Row(s.camera, "view fwd", all_zero ? 3 : 0, "%.2f, %.2f, %.2f",
            view[2], view[6], view[10]);
    }

    Row(s.camera, "local player", lp.valid ? 1 : 3, "%s", lp.valid ? lp.name[0] ? lp.name : "ok" : "INVALID");
    Row(s.camera, "humanoid", lp.humanoid_address ? 0 : 3, "0x%llX",
        (unsigned long long)lp.humanoid_address);
    Row(s.camera, "hrp primitive", is_valid_address(lp.hrp_primitive) ? 1 : 3, "0x%llX",
        (unsigned long long)lp.hrp_primitive);
    if (lp.valid)
        Row(s.camera, "position", 0, "%.1f, %.1f, %.1f", lp.x, lp.y, lp.z);

    s.can_test = is_valid_address(lp.hrp_primitive);
    if (!s.can_test)
        snprintf(s.test_result, sizeof(s.test_result),
                 "No HRP primitive - spawn in (or names are failing) before running a write test");

}

// ----------------------------------------------------------------------------
//  debug page: the offset write tests (same logic as Windows, SleepMs)
// ----------------------------------------------------------------------------
void RunTest(int id) {
    const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
    const uintptr_t prim = lp.hrp_primitive;
    if (!is_valid_address(prim)) {
        snprintf(g_snap.test_result, sizeof(g_snap.test_result), "No root part to write to");
        return;
    }

    if (id == 0) {
        const uintptr_t a = prim + Offsets::Primitive::MotionBlock;
        const uintptr_t b = a + Offsets::Primitive::MotionStride;
        float before[18] = {};
        read_raw(a, before, sizeof(float) * 18);
        float blk[18] = {};
        read_raw(a, blk, sizeof(float) * 18);
        blk[10] += 10.0f;
        blk[12] = blk[13] = blk[14] = 0.0f;
        blk[15] = blk[16] = blk[17] = 0.0f;
        const bool w1 = write_raw(a, blk, sizeof(float) * 18);
        const bool w2 = write_raw(b, blk, sizeof(float) * 18);
        float a1[3] = {}, a2[3] = {};
        read_raw(prim + Offsets::Primitive::Position,  a1, sizeof(a1));
        read_raw(prim + Offsets::Primitive::Position2, a2, sizeof(a2));
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "BLOCK A: %s dY %.2f  |  BLOCK B: %s dY %.2f  (both blocks, whole)",
                 w1 ? "ok" : "FAIL", a1[1] - before[10],
                 w2 ? "ok" : "FAIL", a2[1] - before[10]);
        LogLine("%s", g_snap.test_result);
    } else if (id == 1) {
        const uintptr_t a = prim + Offsets::Primitive::MotionBlock;
        const uintptr_t b = a + Offsets::Primitive::MotionStride;
        float blk[18] = {};
        read_raw(a, blk, sizeof(float) * 18);
        blk[12] = 0.0f; blk[13] = 100.0f; blk[14] = 0.0f;
        const bool wrote = write_raw(a, blk, sizeof(float) * 18) &&
                           write_raw(b, blk, sizeof(float) * 18);
        float v1[3] = {}, v2[3] = {};
        read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity,  v1, sizeof(v1));
        read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity2, v2, sizeof(v2));
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "VEL: %s  wrote y=100  A reads %.2f, B reads %.2f  %s",
                 wrote ? "ok" : "FAILED", v1[1], v2[1],
                 (v1[1] > 50.0f && v2[1] > 50.0f) ? "LANDED" : "did NOT stick");
        LogLine("%s", g_snap.test_result);
    } else if (id == 4) {
        const uintptr_t a = prim + Offsets::Primitive::Rotation;
        const uintptr_t b = a + (Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation);
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));
        float blk[18] = {};
        read_raw(a, blk, sizeof(blk));
        const float want_y = p0[1] + 25.0f;
        blk[10] = want_y;
        blk[12] = blk[13] = blk[14] = 0.0f;
        blk[15] = blk[16] = blk[17] = 0.0f;
        write_raw(a, blk, sizeof(blk));
        write_raw(b, blk, sizeof(blk));
        LogLine("--- survival test: +25 studs, one write, sampled 500ms ---");
        LogLine("  target y = %.2f (from %.2f)", want_y, p0[1]);
        char line[160];
        int held = 0, samples = 0;
        for (int t = 0; t <= 500; t += 50) {
            if (t) platform::SleepMs(50);
            float now_a[3] = {}, now_b[3] = {};
            float lin[3] = {};
            read_raw(prim + Offsets::Primitive::Position,  now_a, sizeof(now_a));
            read_raw(prim + Offsets::Primitive::Position2, now_b, sizeof(now_b));
            read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, lin, sizeof(lin));
            const float dy = now_a[1] - want_y;
            ++samples;
            if (fabsf(dy) < 1.0f) ++held;
            snprintf(line, sizeof(line),
                     "  t+%3dms  A.y %8.2f (off %+7.2f)  B.y %8.2f  vel.y %7.2f",
                     t, now_a[1], dy, now_b[1], lin[1]);
            LogLine("%s", line);
        }
        LogLine("--- survival test end ---");
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Survival: held %d/%d samples over 500ms. See the log - WHEN it "
                 "reverts is the diagnosis (instantly = solver, ~100-300ms = the "
                 "server correcting us, never = the write path is fine).",
                 held, samples);
    } else if (id == 5) {
        const uintptr_t a = prim + Offsets::Primitive::Rotation;
        const uintptr_t b = a + (Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation);
        const uintptr_t fa = prim + Offsets::Primitive::Flags;
        const uint8_t bit = (uint8_t)Offsets::PrimitiveFlags::Anchored;
        const uint8_t orig = read<uint8_t>(fa);
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));
        LogLine("--- ANCHORED survival test: +25 studs, sampled 500ms ---");
        LogLine("  flags before = 0x%02X (anchored bit %s)",
                (unsigned)orig, (orig & bit) ? "ALREADY SET" : "clear");
        write<uint8_t>(fa, (uint8_t)(orig | bit));
        const uint8_t after_set = read<uint8_t>(fa);
        LogLine("  flags after set = 0x%02X  -> anchor %s",
                (unsigned)after_set,
                (after_set & bit) ? "took" : "DID NOT TAKE (write rejected/wrong byte)");
        float blk[18] = {};
        read_raw(a, blk, sizeof(blk));
        const float want_y = p0[1] + 25.0f;
        blk[10] = want_y;
        blk[12] = blk[13] = blk[14] = 0.0f;
        blk[15] = blk[16] = blk[17] = 0.0f;
        write_raw(a, blk, sizeof(blk));
        write_raw(b, blk, sizeof(blk));
        char line[192];
        int held = 0, samples = 0;
        for (int t = 0; t <= 500; t += 50) {
            if (t) platform::SleepMs(50);
            float na[3] = {}, nb[3] = {}, lin[3] = {};
            read_raw(prim + Offsets::Primitive::Position,  na, sizeof(na));
            read_raw(prim + Offsets::Primitive::Position2, nb, sizeof(nb));
            read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, lin, sizeof(lin));
            const uint8_t f = read<uint8_t>(fa);
            const float dy = na[1] - want_y;
            ++samples;
            if (fabsf(dy) < 1.0f) ++held;
            snprintf(line, sizeof(line),
                     "  t+%3dms  A.y %8.2f (off %+7.2f)  B.y %8.2f  vel.y %7.2f  flags 0x%02X%s",
                     t, na[1], dy, nb[1], lin[1], (unsigned)f,
                     (f & bit) ? "" : "  <- ANCHOR CLEARED");
            LogLine("%s", line);
        }
        write<uint8_t>(fa, orig);
        LogLine("  --- anchor released, flags restored to 0x%02X ---", (unsigned)orig);
        for (int t = 50; t <= 300; t += 50) {
            platform::SleepMs(50);
            float na[3] = {}, lin[3] = {};
            read_raw(prim + Offsets::Primitive::Position, na, sizeof(na));
            read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, lin, sizeof(lin));
            snprintf(line, sizeof(line),
                     "  post+%3dms  A.y %8.2f (rel start %+7.2f)  vel.y %7.2f",
                     t, na[1], na[1] - want_y, lin[1]);
            LogLine("%s", line);
        }
        LogLine("--- anchored survival test end ---");
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Anchored: held %d/%d over 500ms. If the anchor bit shows CLEARED, "
                 "the flags write is being undone. If it held but y still fell, the "
                 "anchor does not own position here. See the log.",
                 held, samples);
    } else {
        LogLine("--- primitive float probe (hrp 0x%llX) ---", (unsigned long long)prim);
        char line[128];
        for (int off = 0xA0; off <= 0x1C0; off += 0x10) {
            float vals[4] = {};
            read_raw(prim + off, vals, sizeof(vals));
            snprintf(line, sizeof(line), "  +0x%03X: %9.2f %9.2f %9.2f %9.2f",
                     off, vals[0], vals[1], vals[2], vals[3]);
            LogLine("%s", line);
        }
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Probe written to the log below (0xA0..0x1C0)");
        LogLine("--- probe end ---");
    }
}

// ----------------------------------------------------------------------------
//  the actions the UI calls into
// ----------------------------------------------------------------------------
menuui::Actions MakeActions() {
    menuui::Actions a;
    a.poll_key = [] { return PollKey(); };
    a.key_name = &KeyName;

    a.list_configs = [] { return config::GetConfigList(); };
    a.config_save = [](const char* n) { return config::Save(n); };
    a.config_load = [](const char* n) { return config::Load(n); };
    a.config_delete = [](const char* n) { return config::Delete(n); };
    a.config_rename = [](const char* from, const char* to) { return config::Rename(from, to); };
    a.open_config_folder = [] { config::OpenConfigFolder(); };

    a.run_test = &RunTest;
    a.draw_dex = [] { features::explorer::DrawPanel(); };
    a.hitsound_test = [] { features::hitsound::Test(); };
    a.toggle_friend = [](uint32_t id) { features::friends::ToggleFriend(id); };

    a.rescan              = [] { clear_instance_caches(); };
    a.servers_refresh     = [] { features::servers::Refresh(); };
    a.servers_join        = [](int i) { features::servers::JoinRow(i); };
    a.servers_hop_openest = [] { features::servers::HopToOpenest(); };
    a.servers_rejoin      = [] { features::servers::Rejoin(); };

    a.tp_to_player     = [](uint32_t id) { return features::TeleportToPlayer(id); };
    a.spectate_player  = [](uint32_t id) { features::StartSpectate(id); };
    a.spectate_stop    = [] { features::StopSpectate(); };
    a.waypoint_add     = [](const char* n) { features::AddWaypointHere(n); };
    a.waypoint_fill    = [] { features::FillWaypointEdit(); };
    a.waypoint_add_xyz = [](const char* n, float x, float y, float z) {
        features::AddWaypointAt(n, x, y, z);
    };
    a.waypoint_goto    = [](int i) { features::GotoWaypoint(i); };
    a.waypoint_remove  = [](int i) { features::RemoveWaypoint(i); };
    return a;
}

} // namespace

// =============================================================================
//  entry points (called from overlay::render_ui)
// =============================================================================
void TickKeybinds() {
    menuui::TickKeybinds();
}

void RenderMenu() {
    static bool s_actions_installed = false;
    if (!s_actions_installed) {
        s_actions_installed = true;
        config::EnsureConfigDir();
        menuui::SetActions(MakeActions());
    }

    FillSnapshot();
    menuui::Render(g_snap);
}

#endif
