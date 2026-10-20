#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>

#include "imgui/imgui.h"
#include "obsidian/obsidian.h"

#include "globals.h"
#include "memory.h"
#include "game.h"
#include "cache.h"
#include "features/movement/mover.h"
#include "offsets.h"
#include "offsets_live.h"
#include "features/skybox_changer/skybox_changer.h"
#include "features/aimbot/aimbot.h"
#include "features/players/players.h"
#include "features/waypoints/waypoints.h"
#include "features/config/config.h"
#include "features/servers/servers.h"
#include "ui/menu_ui.h"

// -----------------------------------------------------------------------------
//  menu.cpp  --  the platform half of the menu
// -----------------------------------------------------------------------------
//  The layout, every widget and every setting binding live in ui/menu_ui.cpp.
//  What is left here is exactly the code that cannot exist without Win32 or the
//  game's memory:
//
//    * KeyName() / PollKey(): MapVirtualKey + GetAsyncKeyState keybind capture
//    * FillSnapshot():  what the reader sees right now (datamodel, visual engine,
//                       local player), formatted into strings for the UI
//    * the config page's file operations and the debug page's offset write tests
//
//  Splitting it this way is what lets the menu be rendered and screenshotted off
//  Windows entirely (glass-obsidian/tools/menu_shots.cpp drives the same
//  RenderMenu body with stub callbacks), so visual regressions can be caught
//  without the game, a GPU or a Windows box.
// -----------------------------------------------------------------------------
namespace obs = obsidian;

namespace {

// -----------------------------------------------------------------------------
//  key names
// -----------------------------------------------------------------------------
const char* KeyName(int key) {
    if (key == 0) return "unbound";
    if (key == VK_LBUTTON) return "mouse 1";
    if (key == VK_RBUTTON) return "mouse 2";
    if (key == VK_MBUTTON) return "mouse 3";
    if (key == VK_XBUTTON1) return "mouse 4";
    if (key == VK_XBUTTON2) return "mouse 5";
    LONG lp = (MapVirtualKeyA(key, 0) << 16) | 1;
    static char buf[64]{};
    if (GetKeyNameTextA(lp, buf, sizeof(buf)) > 0) {
        // GetKeyNameText gives "Left Shift"/"Backspace"-style names; the menu
        // prints these in a tiny keycap, so lower-case and shorten them.
        for (char* p = buf; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
            if (*p == ' ') *p = '\0';
        }
        if (!buf[0]) return "key";
        return buf;
    }
    return "key";
}

// The UI owns "which slot is capturing"; this is the polling half. The wait for
// the key to be RELEASED is not politeness: without it the same physical press
// that set the bind would also trigger the feature (and, for the menu key
// itself, close the menu on the frame it was captured).
int PollKey() {
    // Skipping LMB/RMB outright was the old "you cannot bind a mouse button" bug: the
    // buttons were excluded from the scan, so Mouse 1 and Mouse 2 -- the two most
    // useful binds for a click feature -- could never be captured. The reason they were
    // excluded is real though: capture STARTS with a click on the card. So the rule is
    // about the hand, not the key: accept nothing while any mouse button is still
    // down, and the click that opened capture can never be the click that binds.
    // Escape stays out of the accepted set: the UI owns it as the cancel key (see
    // menuui::TickKeybinds), and returning it here would let one press both cancel
    // capture and bind it. The settle window that keeps the opening click from
    // binding its own slot lives there, where the capture state is.
    constexpr unsigned down = 0x8000u;

    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
            vk == VK_XBUTTON1 || vk == VK_XBUTTON2 || vk == VK_ESCAPE)
            continue;
        if (GetAsyncKeyState(vk) & down) {
            while (GetAsyncKeyState(vk) & down) Sleep(1);
            return vk;
        }
    }
    // The buttons themselves, in the order a hand is likely to press them. The side
    // buttons used to be the only ones reachable here, via a separate branch.
    static const int buttons[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON,
                                    VK_XBUTTON1, VK_XBUTTON2 };
    for (int i = 0; i < 5; ++i) {
        if (GetAsyncKeyState(buttons[i]) & down) {
            while (GetAsyncKeyState(buttons[i]) & down) Sleep(1);
            return buttons[i];
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
//  what we are running on
// -----------------------------------------------------------------------------
// One registry query, cached for the process lifetime: the rail card wants the
// OS next to the player and the pid, and nothing about it changes while we run.
//
// ProductName is used for the edition string but NOT for the version: it says
// "Windows 10 Pro" on 11 as well (a compatibility lie every consumer of it has
// carried since 8.1), so Windows 11 is decided by the build number, which is what
// the OS itself agrees on.
const char* OSName()
{
    static char buf[48];
    if (buf[0]) return buf;

    char prod[64] = "Windows";
    char build[24] = "";
    DWORD sz, type = 0;
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        sz = sizeof(prod) - 1;
        if (RegQueryValueExA(key, "ProductName", nullptr, &type,
                             reinterpret_cast<LPBYTE>(prod), &sz) != ERROR_SUCCESS)
            snprintf(prod, sizeof(prod), "Windows");
        sz = sizeof(build) - 1;
        if (RegQueryValueExA(key, "CurrentBuildNumber", nullptr, &type,
                             reinterpret_cast<LPBYTE>(build), &sz) != ERROR_SUCCESS)
            build[0] = '\0';
        RegCloseKey(key);
    }

    // "Windows 10 Pro" -> the edition token, "Pro". Absent for Home, which stores
    // just "Windows 10 Home"... which is the token after the number, so one split.
    const char* edition = nullptr;
    for (const char* p = prod; *p; ++p)
        if (*p == ' ') edition = p + 1;
    if (edition && (*edition < '0' || *edition > '9')) {
        while (edition[1] == ' ' || (*edition >= '0' && *edition <= '9')) ++edition;
    } else {
        edition = nullptr;
    }

    char ver[16];
    snprintf(ver, sizeof(ver), "%s", build[0] ? build : "?");
    // The registry's ProductName still says "Windows 10" on every 11 install, so the
    // build number is what actually decides the label. It is not printed afterwards:
    // the rail card gives the value about fifteen monospace cells, and the build was
    // the part that got clipped.
    const bool win11 = atoi(ver) >= 22000;
    snprintf(buf, sizeof(buf), "Windows %s%s%.*s", win11 ? "11" : "10",
             edition ? " " : "", edition ? 12 : 0, edition ? edition : "");
    return buf;
}

// -----------------------------------------------------------------------------
//  snapshot plumbing
// -----------------------------------------------------------------------------
menuui::Snapshot g_snap;

void Row(std::vector<menuui::Snapshot::Row>& v, const char* key, int state,
         const char* fmt, ...) {
    if (v.size() >= 14) return;         // matches the UI's per-card capacity
    menuui::Snapshot::Row r;
    r.key = key;
    r.state = state;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r.value, sizeof(r.value), fmt, ap);
    va_end(ap);
    v.push_back(r);
}

// -----------------------------------------------------------------------------
//  the players tab's list
// -----------------------------------------------------------------------------
// Everything here comes out of the cache thread's snapshot -- this function does
// no memory reads of its own -- because that list is rebuilt every pass and a
// second reader would only race with it. Rows are sorted by distance: the people
// you could actually jump to are the ones you want to see first.
void FillPlayers(menuui::Snapshot& s, const cache::LocalPlayerData& lp)
{
    s.player_count = 0;
    s.spectating   = spectate_enabled;
    s.player_note[0] = '\0';

    // last thing the page's buttons did, or why they refused
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

        if (e.IsDead()) {
            snprintf(r.detail, sizeof(r.detail), "dead - %.0f studs", hits[i].dist);
            r.state = 1;
        } else {
            // Note the `else` covers "0 hp with no readable max_health" too: a
            // humanoid read that failed is unknown, not dead, and the row says so the
            // way the aimbot's dead check does rather than calling them a corpse.
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

// Reading the datamodel/visual engine every frame is pointless work while the
// menu is closed or sitting on a page that does not show it, so the heavier
// fields are filled only for the debug page, the only one that displays them.
void FillSnapshot() {
    menuui::Snapshot& s = g_snap;

    s.runtime.clear();
    s.camera.clear();

    const bool attached = (g_base_address != 0);
    s.attached = attached;

    // The aimbot's latch lives in the feature (that is where the key is read), so it
    // reaches the menu the way every other piece of runtime state does.
    s.aimbot_latched = features::AimbotKeyLatched();

    const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
    s.user[0] = '\0';
    if (lp.name[0]) snprintf(s.user, sizeof(s.user), "%s", lp.name);
    s.pid = (unsigned)mem::process_id.load();
    snprintf(s.os, sizeof(s.os), "%s", OSName());

    if (!attached) {
        snprintf(s.status, sizeof(s.status), "waiting for roblox");
        // The world page's server cards read these, so say here why they are empty -
        // otherwise "not fetched yet" is the only clue and it does not explain that
        // nothing will ever be fetched while there is no client to ask about.
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

    s.skybox_names = features::k_skybox_names;
    s.skybox_count = features::k_skybox_count;
    snprintf(s.skybox_note, sizeof(s.skybox_note), "%s",
             skybox_debug_msg[0] ? skybox_debug_msg : "Nothing applied yet");

    // The in-game flag and the place id drive the rail, the world page and the
    // players page, so they are refreshed every frame the menu is open. (They used
    // to live behind the reader gate below, which is why the rail kept saying
    // "usermode overlay" after you joined: nothing visited that branch until you
    // opened the world or debug tab.) FillSnapshot only runs while the menu is
    // open, so "every frame" is a few reads, not a hot loop.
    //
    // The experience NAME is not read from memory at all, and that is the fix for the
    // rail saying "Ugc": the datamodel's instance name is the DataModel *kind*
    // ("Ugc", "Game"), never the name on the website. It comes from games.roblox.com
    // through features/servers, which asks once per place and is empty until it
    // answers - and the rail drops its GAME row on empty rather than lying.
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
            snprintf(s.game, sizeof(s.game), "roblox home");   // a real state, not a name
    } else {
        s.game[0] = '\0';
        s.in_game = false;
    }

    // the servers block, copied out of the feature under its own lock. The copy itself
    // lives in menuui (FillServers) so the host harness can pin its bounds: with a full
    // page of rows it used to write one row past `servers[]`, onto `server_count`, and
    // the next frame's draw loop then ran to that number.
    menuui::FillServers(s, sv);

    FillPlayers(s, lp);

    const int page = menuui::CurrentPage();
    if (page != 8) return;                   // 8 == debug, the only real reader

    instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);

    // Which offset set every read on this page is trusting, and what version the client
    // is actually running - first row, because it changes how all the others are read: a
    // null engine pointer is a broken feature when the offsets match and a dump for
    // another version when they do not. RefreshOffsets() runs in WinMain, before any of
    // this, and it is what fills both strings.
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
    // The hidden variable behind every "it works sometimes" report. Holding a
    // position is a race against the engine's per-step restore; below ~8 writes
    // per 16ms step we start losing it visibly.
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

// -----------------------------------------------------------------------------
//  debug page: the offset write tests
// -----------------------------------------------------------------------------
void RunTest(int id) {
    const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
    const uintptr_t prim = lp.hrp_primitive;
    if (!is_valid_address(prim)) {
        snprintf(g_snap.test_result, sizeof(g_snap.test_result), "No root part to write to");
        return;
    }

    if (id == 0) {
        // The primitive holds TWO 0x48 motion blocks and the engine re-syncs from
        // the second, so the test writes BOTH whole blocks - the same thing the
        // features do. The old version wrote the two positions but left block B's
        // VELOCITY alone, which is why it alternated "dY 10.00 / dY 0.00": the
        // engine integrated the stale velocity back over the write about half the
        // time, and the test reported the race rather than the offsets.
        const uintptr_t a = prim + Offsets::Primitive::MotionBlock;
        const uintptr_t b = a + Offsets::Primitive::MotionStride;
        float before[18] = {};
        read_raw(a, before, sizeof(float) * 18);
        float blk[18] = {};
        read_raw(a, blk, sizeof(float) * 18);
        blk[10] += 10.0f;                       // position.y  (block offset 0x24)
        blk[12] = blk[13] = blk[14] = 0.0f;     // linear velocity
        blk[15] = blk[16] = blk[17] = 0.0f;     // angular velocity
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
        // Velocity in BOTH blocks. Block B's linear velocity at 0x140 is live -
        // the probe caught it holding the character's exact walkspeed - so a
        // write to 0xF8 alone is undone by the engine's next sync.
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
        // ---------------------------------------------------------------------
        //  THE TEST THAT ACTUALLY SETTLES IT: does a write SURVIVE?
        // ---------------------------------------------------------------------
        //  Every other test reads back immediately, which only proves
        //  the write syscall succeeded - it cannot tell "the engine accepted
        //  this" from "the engine overwrote it 3ms later". Both of my previous
        //  diagnoses were built on that blind spot. This one writes ONCE, then
        //  samples for 500ms and reports the timeline, so the failure names
        //  itself instead of being inferred.
        //
        //  Reading the result:
        //    held to the end            -> the write path is fine; the bug is in
        //                                  the feature's logic, not the offsets
        //    reverted in 1-2 samples    -> the engine/solver is overwriting us;
        //                                  a different field is the real input
        //    reverted after ~100-300ms  -> the SERVER rejected it and corrected
        //                                  the client. That is replication, and
        //                                  no local write rate will beat it
        //    drifts smoothly back       -> we are fighting the humanoid controller
        const uintptr_t a = prim + Offsets::Primitive::Rotation;
        const uintptr_t b = a + (Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation);
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));

        float blk[18] = {};
        read_raw(a, blk, sizeof(blk));
        const float want_y = p0[1] + 25.0f;
        blk[10] = want_y;
        blk[12] = blk[13] = blk[14] = 0.0f;   // linear
        blk[15] = blk[16] = blk[17] = 0.0f;   // angular
        write_raw(a, blk, sizeof(blk));
        write_raw(b, blk, sizeof(blk));

        LogLine("--- survival test: +25 studs, one write, sampled 500ms ---");
        LogLine("  target y = %.2f (from %.2f)", want_y, p0[1]);
        char line[160];
        int held = 0, samples = 0;
        for (int t = 0; t <= 500; t += 50) {
            if (t) Sleep(50);
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
        // ---------------------------------------------------------------------
        //  ANCHORED survival test - the measurement never actually taken
        // ---------------------------------------------------------------------
        //  Test 4 proved an UNANCHORED write is recomputed away within 50ms, and
        //  I concluded from that "so anchor it" and shipped anchoring in flight
        //  and teleport without ever measuring the anchored case. Both of those
        //  features now bury the character in the ground, which is the symptom of
        //  an anchor that is set but whose position write is NOT surviving - a
        //  combination my reasoning said was impossible. So measure it.
        //
        //  This does the same +25 studs, but sets Anchored first and reports the
        //  flags byte at every sample, so we can see three separate things that
        //  the previous test could not distinguish:
        //    - did the anchor bit actually STAY set (or does something clear it)?
        //    - does the position hold WHILE anchored?
        //    - what happens in the 500ms after it is released?
        //  It restores the original flags byte at the end, unconditionally.
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

        // Anchor FIRST, then write - the order the features use.
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
            if (t) Sleep(50);
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

        // Release and watch the handoff: this is where "buried in the ground"
        // would show up if the fall happens on unanchor.
        write<uint8_t>(fa, orig);
        LogLine("  --- anchor released, flags restored to 0x%02X ---", (unsigned)orig);
        for (int t = 50; t <= 300; t += 50) {
            Sleep(50);
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
    } else if (id == 6) {
        // ---------------------------------------------------------------------
        //  WHO OWNS THE POSITION? A value scan.
        // ---------------------------------------------------------------------
        //  The anchored test settled that my whole model was wrong. The anchor
        //  bit STAYED SET (0x3A) for all 500ms and the position still returned to
        //  exactly 32.60 with vel.y 0.00. An anchored part cannot be simulated,
        //  and nothing fell (a 25-stud drop is ~505ms at ~99 studs/s). So the
        //  value is being COPIED BACK from somewhere else that still holds 32.60.
        //
        //  Both motion blocks are therefore caches, not the source of truth. This
        //  scans a wide window around the primitive AND the BasePart instance for
        //  every float that currently equals the character's y, so the authority
        //  names itself instead of being guessed at. Whatever still reads 32.60
        //  after we have written 57.60 into both blocks is the thing restoring us.
        const uintptr_t part = lp.hrp_address;
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));
        const float y = p0[1], x = p0[0], z = p0[2];

        LogLine("--- position authority scan ---");
        LogLine("  hrp part 0x%llX  primitive 0x%llX",
                (unsigned long long)part, (unsigned long long)prim);
        LogLine("  looking for x %.2f  y %.2f  z %.2f", x, y, z);

        struct Hit { const char* base; uintptr_t off; };
        Hit hits[64]; int nhits = 0;

        auto scan = [&](const char* nm, uintptr_t root, uintptr_t lo, uintptr_t hi) {
            if (!is_valid_address(root)) return;
            const size_t span = (size_t)(hi - lo);
            char* buf = (char*)malloc(span);
            if (!buf) return;
            if (read_raw(root + lo, buf, span)) {
                for (size_t o = 0; o + 12 <= span; o += 4) {
                    float v[3];
                    memcpy(v, buf + o, 12);
                    // match the full XYZ triple, not just y: a lone float equal to
                    // y is coincidence, three in a row is a position.
                    if (fabsf(v[0] - x) < 0.02f && fabsf(v[1] - y) < 0.02f &&
                        fabsf(v[2] - z) < 0.02f) {
                        LogLine("  %s +0x%03llX  = %.2f %.2f %.2f",
                                nm, (unsigned long long)(lo + o), v[0], v[1], v[2]);
                        if (nhits < 64) { hits[nhits].base = nm; hits[nhits].off = lo + o; ++nhits; }
                    }
                }
            }
            free(buf);
        };

        scan("primitive", prim, 0x00, 0x400);
        scan("basepart",  part, 0x00, 0x400);

        // Follow one pointer level out of the primitive: the assembly / rigid body
        // that an anchored part still belongs to is reached this way, and that is
        // the most likely holder of the authoritative CFrame.
        for (uintptr_t o = 0x150; o <= 0x260; o += 8) {
            const uintptr_t pv = read<uintptr_t>(prim + o);
            if (!is_valid_address(pv)) continue;
            char nm[48];
            snprintf(nm, sizeof(nm), "prim->[+0x%03llX]", (unsigned long long)o);
            // named per-pointer so the log says which field to follow
            scan(nm, pv, 0x00, 0x200);
        }

        LogLine("  %d matching XYZ triples found", nhits);
        LogLine("--- scan end ---");
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Authority scan: %d XYZ triples equal to the current position. "
                 "The ones that are NOT the two motion blocks (0xC8/0x110) are the "
                 "candidates for what restores us. See the log.", nhits);

    } else if (id == 7) {
        // ---------------------------------------------------------------------
        //  Does CONTINUOUS writing hold the character up?
        // ---------------------------------------------------------------------
        //  The single-write tests cannot distinguish "the engine restores from a
        //  master copy once" from "the engine restores every step". If hammering
        //  both blocks as fast as possible keeps the character at +25, then the
        //  features are simply not writing often enough or early enough, and the
        //  fix is in the feature loop. If it does NOT, the write target is wrong
        //  and no amount of rate will help - which is the answer that matters,
        //  because it is the one my last three commits assumed away.
        const uintptr_t a = prim + Offsets::Primitive::Rotation;
        const uintptr_t b = a + (Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation);
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));
        const float want_y = p0[1] + 25.0f;

        LogLine("--- continuous write test: +25 studs, hammered for 500ms ---");
        float blk[18] = {};
        read_raw(a, blk, sizeof(blk));
        blk[10] = want_y;
        blk[12] = blk[13] = blk[14] = 0.0f;
        blk[15] = blk[16] = blk[17] = 0.0f;

        const DWORD t0 = GetTickCount();
        int writes = 0, next_report = 0;
        while ((int)(GetTickCount() - t0) < 500) {
            write_raw(a, blk, sizeof(blk));
            write_raw(b, blk, sizeof(blk));
            ++writes;
            const int el = (int)(GetTickCount() - t0);
            if (el >= next_report) {
                float na[3] = {}, lin[3] = {};
                read_raw(prim + Offsets::Primitive::Position, na, sizeof(na));
                read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, lin, sizeof(lin));
                LogLine("  t+%3dms  A.y %8.2f (off %+7.2f)  vel.y %7.2f  writes %d",
                        el, na[1], na[1] - want_y, lin[1], writes);
                next_report = el + 50;
            }
        }
        // and where it ends up once we stop
        for (int t = 50; t <= 200; t += 50) {
            Sleep(50);
            float na[3] = {};
            read_raw(prim + Offsets::Primitive::Position, na, sizeof(na));
            LogLine("  after+%3dms  A.y %8.2f (off %+7.2f)", t, na[1], na[1] - want_y);
        }
        LogLine("--- continuous write test end (%d writes) ---", writes);
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Hammered %d writes over 500ms. If A.y sat at the target the whole "
                 "time, rate/timing is the problem. If it stayed at the old value, "
                 "we are writing to a cache and the real position is elsewhere.", writes);

    } else if (id == 8) {
        // ---------------------------------------------------------------------
        //  CameraType: does Scriptable stick?
        // ---------------------------------------------------------------------
        //  The probe printed type 0x138 = 5. Scriptable is 6. Freecam "rotates
        //  around your player but will not move" because the default camera
        //  module still owns position - which is exactly what a non-Scriptable
        //  camera does. This writes 6 and watches whether the game puts it back,
        //  and how fast, which separates "wrong offset/enum" from "CameraScript
        //  re-asserts it every frame".
        instance dm = game::ReadDatamodel(g_base_address);
        instance ws = dm.is_valid() ? dm.read_service("Workspace") : instance{};
        instance cam = ws.is_valid()
                     ? read<instance>(ws.address + Offsets::Workspace::CurrentCamera)
                     : instance{};
        if (!cam.is_valid()) {
            snprintf(g_snap.test_result, sizeof(g_snap.test_result), "No CurrentCamera");
            LogLine("No CurrentCamera to test");
            return;
        }
        const uintptr_t ct = cam.address + Offsets::Camera::CameraType;
        const int was = read<int>(ct);
        LogLine("--- CameraType test (cam 0x%llX) ---", (unsigned long long)cam.address);
        LogLine("  before = %d   (0=Fixed 1=Attach 2=Watch 3=Track 4=Follow 5=Custom 6=Scriptable)", was);
        write<int>(ct, 6);
        LogLine("  wrote 6, immediate read-back = %d", read<int>(ct));
        int reverted_at = -1;
        for (int t = 0; t <= 500; t += 50) {
            if (t) Sleep(50);
            const int now = read<int>(ct);
            LogLine("  t+%3dms  CameraType = %d%s", t, now,
                    now == 6 ? "" : "  <- NOT Scriptable");
            if (now != 6 && reverted_at < 0) reverted_at = t;
        }

        // Now try holding it AND moving the camera, which is what freecam does.
        LogLine("  --- now hammering type=6 and pushing the camera +30 studs ---");
        float cf[12] = {};
        read_raw(cam.address + Offsets::Camera::Rotation, cf, sizeof(cf));
        const float cam_y = cf[10];
        cf[10] = cam_y + 30.0f;
        const DWORD t0 = GetTickCount();
        while ((int)(GetTickCount() - t0) < 300) {
            write<int>(ct, 6);
            write_raw(cam.address + Offsets::Camera::Rotation, cf, sizeof(cf));
        }
        float chk[12] = {};
        read_raw(cam.address + Offsets::Camera::Rotation, chk, sizeof(chk));
        LogLine("  after 300ms of hammering: cam.y %.2f (wanted %.2f, started %.2f) type %d",
                chk[10], cam_y + 30.0f, cam_y, read<int>(ct));

        write<int>(ct, was);
        LogLine("  restored CameraType to %d", was);
        LogLine("--- CameraType test end ---");
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "CameraType was %d. %s  Camera y after hammering: %.2f (wanted %.2f).",
                 was,
                 reverted_at < 0 ? "Scriptable STUCK for 500ms."
                                 : "Something put it back - the game owns it.",
                 chk[10], cam_y + 30.0f);

    } else if (id == 9) {
        // ---------------------------------------------------------------------
        //  Does a VELOCITY write SURVIVE? (the test never run)
        // ---------------------------------------------------------------------
        //  Test 1 writes y=100 and reads it straight back: "LANDED". That only
        //  proves the write syscall returned true - the exact blind spot that
        //  produced four wrong diagnoses in a row for position. Infinite jump is
        //  built entirely on velocity and does nothing at all, even with
        //  JumpPower set, so the obvious question is whether the impulse is still
        //  there a few milliseconds later. Sample it.
        const uintptr_t a = prim + Offsets::Primitive::Rotation;
        const uintptr_t b = a + (Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation);
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));

        float blk[18] = {};
        read_raw(a, blk, sizeof(blk));
        blk[12] = 0.0f; blk[13] = 100.0f; blk[14] = 0.0f;   // linear vel y = 100
        write_raw(a, blk, sizeof(blk));
        write_raw(b, blk, sizeof(blk));

        LogLine("--- velocity survival: y=100 once, sampled 400ms ---");
        LogLine("  start y %.2f. A real 100 studs/s launch should climb ~25 studs "
                "in the first 300ms and decay by ~19.6 studs/s per 100ms.", p0[1]);
        for (int t = 0; t <= 400; t += 25) {
            if (t) Sleep(25);
            float v[3] = {}, np[3] = {};
            read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, v, sizeof(v));
            read_raw(prim + Offsets::Primitive::Position, np, sizeof(np));
            LogLine("  t+%3dms  vel.y %8.2f   y %8.2f (climbed %+6.2f)",
                    t, v[1], np[1], np[1] - p0[1]);
        }
        LogLine("--- velocity survival end ---");
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Velocity survival: if vel.y is ~0 by the first sample and y never "
                 "climbed, the impulse is being eaten and infinite jump cannot work "
                 "through velocity at all. See the log.");

    } else if (id == 10) {
        // ---------------------------------------------------------------------
        //  HAMMERED velocity: can we hold an upward speed the way we hold a
        //  position?
        // ---------------------------------------------------------------------
        //  Hammering position beat the engine outright. If hammering velocity
        //  also holds, infinite jump can stay a velocity feature. If it does not,
        //  the jump has to be driven the same way flight now is - by integrating
        //  an arc and hammering POSITION - and that is the answer that decides
        //  the implementation.
        const uintptr_t a = prim + Offsets::Primitive::Rotation;
        const uintptr_t b = a + (Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation);
        float p0[3] = {};
        read_raw(prim + Offsets::Primitive::Position, p0, sizeof(p0));
        float blk[18] = {};
        read_raw(a, blk, sizeof(blk));
        blk[12] = 0.0f; blk[13] = 100.0f; blk[14] = 0.0f;

        LogLine("--- hammered velocity: y=100 held for 400ms ---");
        const DWORD t0 = GetTickCount();
        int writes = 0, next = 0;
        while ((int)(GetTickCount() - t0) < 400) {
            write_raw(a + 0x30, blk + 12, 12);   // linear velocity only
            write_raw(b + 0x30, blk + 12, 12);
            ++writes;
            const int el = (int)(GetTickCount() - t0);
            if (el >= next) {
                float v[3] = {}, np[3] = {};
                read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, v, sizeof(v));
                read_raw(prim + Offsets::Primitive::Position, np, sizeof(np));
                LogLine("  t+%3dms  vel.y %8.2f   y %8.2f (climbed %+6.2f)  writes %d",
                        el, v[1], np[1], np[1] - p0[1], writes);
                next = el + 50;
            }
        }
        LogLine("--- hammered velocity end (%d writes) ---", writes);
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Hammered velocity: %d writes. If y climbed, infinite jump can use "
                 "velocity. If y sat still while vel.y read 100, the velocity field "
                 "is decorative and the jump must be driven by position.", writes);

    } else if (id == 3) {
        // The camera equivalent of the primitive probe. Freecam writes
        // Camera::Rotation(0xd8)+Position(0xfc) as one 48-byte CFrame, and that
        // adjacency is an assumption the live offset service can invalidate on a
        // client update - so this prints the range and states whether the two are
        // still adjacent, instead of leaving "the camera bugs out" to guesswork.
        instance dm = game::ReadDatamodel(g_base_address);
        instance ws = dm.is_valid() ? dm.read_service("Workspace") : instance{};
        instance cam = ws.is_valid()
                     ? read<instance>(ws.address + Offsets::Workspace::CurrentCamera)
                     : instance{};
        if (!cam.is_valid()) {
            snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                     "No CurrentCamera to probe - load into a game first");
            LogLine("%s", g_snap.test_result);
            return;
        }
        LogLine("--- camera float probe (cam 0x%llX) ---", (unsigned long long)cam.address);
        char line[128];
        for (int off = 0xC0; off <= 0x160; off += 0x10) {
            float vals[4] = {};
            read_raw(cam.address + off, vals, sizeof(vals));
            snprintf(line, sizeof(line), "  +0x%03X: %9.3f %9.3f %9.3f %9.3f",
                     off, vals[0], vals[1], vals[2], vals[3]);
            LogLine("%s", line);
        }
        const bool adjacent =
            Offsets::Camera::Rotation + 36 == Offsets::Camera::Position;
        LogLine("  rot 0x%llx + 36 %s pos 0x%llx | type 0x%llx = %d | fov 0x%llx = %.4f rad",
                (unsigned long long)Offsets::Camera::Rotation,
                adjacent ? "==" : "!=",
                (unsigned long long)Offsets::Camera::Position,
                (unsigned long long)Offsets::Camera::CameraType,
                read<int>(cam.address + Offsets::Camera::CameraType),
                (unsigned long long)Offsets::Camera::FieldOfView,
                read<float>(cam.address + Offsets::Camera::FieldOfView));
        LogLine("--- camera probe end ---");
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "Camera probe in the log (0xC0..0x160). Rotation+36 %s Position; "
                 "a rotation matrix should read as 9 values in [-1,1] and the "
                 "position as your view point.",
                 adjacent ? "==" : "!= (freecam will use two writes)");
    } else if (id == 11) {
        // The wall check's world raycast needs the physics world's primitive
        // array, whose container layout no public source documents. The sweep
        // auto-detects it; this probe dumps the raw container + the verdict
        // so a failed detection can be pinned by hand from real data.
        cache::WorldProbeLog();
        snprintf(g_snap.test_result, sizeof(g_snap.test_result),
                 "World probe in the log: container qwords, detection verdict, "
                 "sample primitives. If detection says 'not recognised', send "
                 "that log - walls need it.");
    } else {
        // Offset discovery for a new client version: one line per 0x10, formatted
        // so it can be copied out of the log and read as a table.
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

// -----------------------------------------------------------------------------
//  the actions the UI calls into
// -----------------------------------------------------------------------------
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

    // world page: the server list and the two links. No return values on purpose - a
    // hop is a request that finishes later, and its outcome comes back through the
    // snapshot like every other feature's status does.
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
//  entry points (declared in main.cpp, called from overlay::render_ui)
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
