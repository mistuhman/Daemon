// -----------------------------------------------------------------------------
//  ui/menu_ui.h  --  the menu, as pure UI
// -----------------------------------------------------------------------------
//  WHY THIS FILE IS SEPARATE FROM menu.cpp
//  ---------------------------------------
//  The layout lives here; the Win32 / memory-reading glue lives in menu.cpp.
//  Nothing in this file includes <Windows.h>, so the *same* code the overlay
//  draws is also what the headless harness (glass-obsidian/tests) feeds a
//  synthetic frame and rasterises to PNG. The screenshots in glass-obsidian/out
//  are therefore the real menu, not a mock-up of it -- which is the only reason
//  a UI change here can be verified without a Windows box, a GPU or the game.
//
//  Settings themselves are NOT mirrored into a state struct: this file reads and
//  writes the globals in globals.h directly, so there is exactly one copy of
//  every setting and no sync step to get wrong.
//
//  Everything platform-shaped comes in through `Actions` (key capture, config
//  files, the debug write tests). Everything memory-shaped comes in through
//  `Snapshot`, already formatted by the app -- this file never dereferences a
//  pointer into Roblox.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/obsidian.h"
#include "obsidian/axiom.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace menuui {

// -----------------------------------------------------------------------------
//  read-only data the app refreshes every frame
// -----------------------------------------------------------------------------
struct Snapshot
{
    // attach / identity
    bool     attached   = false;
    char     user[64]   = "";      // roblox username, "" until resolved
    unsigned pid        = 0;
    char     status[80] = "";      // header chip + rail status line
    // The experience's name, from games.roblox.com (see features/servers). It is not
    // readable from memory at all: the datamodel's instance name is the DataModel
    // *kind* ("Ugc", "Game"), which is what this field used to show. "" until the API
    // answers, and the rail drops its GAME row rather than showing a wrong name.
    char     game[128]  = "";
    bool     in_game    = false;
    char     place[24]  = "";      // place id, formatted
    char     version[40] = "";     // client version string
    char     os[48] = "";          // "Windows 11 Pro", for the rail card

    // world page
    const char* const* skybox_names = nullptr;
    int          skybox_count       = 0;
    char     skybox_note[256] = "";  // the feature's own status string, copied out

    // players page: everybody in the server except you (the cache skips your own
    // character). The app has already read the memory and formatted the strings --
    // fixed buffers again, so the UI never owns or frees anything.
    struct PlayerRow
    {
        char     name[32];
        char     account[32];   // username used for blacklist/whitelist
        char     detail[48];    // "100/100 hp - 142 studs - Sword"
        uint32_t user_id = 0;   // the id the actions take: stable across re-sorts
        int      state = 0;     // 0 normal, 1 dead, 2 spectated right now
    };
    enum { kMaxPlayers = 48 };   // an enum, not a static const: no out-of-line definition to miss
    PlayerRow players[kMaxPlayers];
    int       player_count = 0;
    bool      spectating   = false;
    char      player_note[256] = "";   // empty, or why the list is empty / last result

    // world page: the server list. Rows come from games.roblox.com's public servers
    // endpoint, formatted by the app; `state` is the only judgement the UI makes.
    struct ServerRow
    {
        char id[20];        // the guid, cut down: enough to tell two apart
        char detail[24];    // "12/40"
        int  state = 0;     // 0 another server, 1 this one, 2 full, 3 full and ours
    };
    enum { kMaxServers = 24 };
    ServerRow servers[kMaxServers];
    int  server_count       = 0;
    int  server_current     = -1;   // which row is ours, -1 = not known
    bool servers_busy       = false;
    bool servers_can_rejoin = false;
    int  server_age_sec     = -1;   // -1 = never fetched
    char servers_msg[256]   = "";   // what the last request or link did, always set

    // debug page: the app has already read and formatted these.
    struct Row
    {
        const char* key;
        char        value[96];
        int         state = 0;   // 0 dim, 1 ok, 2 warn, 3 danger
    };
    std::vector<Row> runtime;      // pointers / validity
    std::vector<Row> camera;       // viewport, view matrix, local player
    char     test_result[200] = "";
    bool     can_test         = false;   // a live HRP primitive to write to
};

// -----------------------------------------------------------------------------
//  the platform layer's callbacks
// -----------------------------------------------------------------------------
struct Actions
{
    // keybinds: poll_key() returns a VK the moment one is pressed (mouse buttons
    // included), 0 otherwise. `waiting` state is owned by menu_ui.
    std::function<int()>        poll_key;
    const char* (*key_name)(int vk) = nullptr;

    // configs
    std::function<std::vector<std::string>()> list_configs;
    std::function<bool(const char*)>        config_save;
    std::function<bool(const char*)>        config_load;
    std::function<bool(const char*)>        config_delete;
    std::function<bool(const char*, const char*)> config_rename;
    std::function<void()>                   open_config_folder;

    // debug page. id: 0 = position write, 1 = velocity write, 2 = float probe
    std::function<void(int)>                run_test;

    // players page. All three take a PlayerRow::user_id, because a row index is
    // only meaningful until the cache rebuilds the list.
    // world page: server hop / rejoin. Everything is fire-and-report: the feature
    // owns the request and the outcome lands in Snapshot::servers_msg, because a hop
    // waits on a browser hand-off and must never wait on the render thread.
    std::function<void()>                   rescan;
    std::function<void()>                   servers_refresh;
    std::function<void(int)>                servers_join;
    std::function<void()>                   servers_hop_openest;
    std::function<void()>                   servers_rejoin;

    std::function<bool(uint32_t)>           tp_to_player;
    std::function<void(uint32_t)>           spectate_player;
    std::function<void()>                   spectate_stop;

    std::function<void(const char*)>        waypoint_add;
    std::function<void()>                    waypoint_fill;
    std::function<void(const char*, float, float, float)> waypoint_add_xyz;
    std::function<void(int)>                waypoint_goto;
    std::function<void(int)>                waypoint_remove;
};

void SetActions(const Actions& actions);

// Builds the palette from the ui_* globals (transparency / corners / accent,
// including the rainbow cycle). Called once per frame by Render(), and by the
// app at startup so the very first frame is already themed.
obsidian::Palette BuildPalette();

// Advances any pending keybind capture. Call every frame before Render().
void TickKeybinds();

// Draws the whole menu window. Sets g_request_exit when closed from the header.
void Render(Snapshot& snap);

// Which page Render() last drew (0..7). Useful for screenshot tooling.
int  CurrentPage();
// Screen rect of the menu window after the last Render() (0,0 / 0,0 before it).
void LastRect(ImVec2* pos, ImVec2* size);
void SetPage(int page);
int  PageCount();
const char* PageName(int page);

} // namespace menuui
