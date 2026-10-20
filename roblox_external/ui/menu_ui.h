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

namespace features { namespace servers { struct View; } }   // servers.cpp's published
                                                            // state; only FillServers
                                                            // below needs to see it

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

    // aimbot page: whether the aimbot key is a latch that is currently ON. State
    // comes through this struct rather than a call into the feature, which is the
    // whole reason the menu can be rendered headless at all.
    bool     aimbot_latched = false;

    // the sounds/ folder, for the hit-sound combo. Owned by the app's hitsound
    // feature; nullptr/0 until it has scanned.
    const char* const* hitsound_names = nullptr;
    int          hitsound_count       = 0;

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
        bool     friended = false;   // in features/players/friends (paints the ESP green)
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

    // the dex tab. The browser lives in the app (it reads memory); the page
    // gives it a rectangle inside the menu and paints nothing itself.
    std::function<void()>                   draw_dex;

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

    std::function<void()>                   hitsound_test;

    std::function<void(uint32_t)>           toggle_friend;

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

// Re-applies the font/atlas-dependent half of the theme (ui_scale, font sizes).
// LoadFonts() must run OUTSIDE a frame, so this is called from the render loop
// between Render() and the next NewFrame(); it is a no-op until a size setting
// moves. Returns true when it rebuilt the atlas, which is when the app may want to
// nudge its backend. Also call it once at startup so the first frame is themed.
bool SyncTheme();

// Which settings windows are OPEN, as a ';'-joined list of feature ids
// ("aimbot;esp;debug"). The config file owns the string; the menu owns the
// state. Storing the OPEN set (and not a closed set) is what makes the default —
// every window closed, the plain column — free, and it lets a config file written
// by the old grouped menu load as "nothing open" instead of swinging every
// window onto the screen. Persisted under the historic key ui_closed_groups.
void SetClosedGroups(const char* list);
void GetClosedGroups(char* out, size_t n);

// Draws the whole menu window. Sets g_request_exit when closed from the header.
void Render(Snapshot& snap);

// Style stack for the plain-ImGui float windows (the fps counter and the media
// badge). They live outside the obsidian window, so without this a light theme
// would leave white-on-white text hovering over the game. Pushes Text/WindowBg/
// Border/rounding in the palette's tokens; `bg_alpha` is how opaque the plate is
// over the 3D view, which is a legibility call, not a style one. Pop in pairs.
void PushFloatWindowStyle(float bg_alpha = 0.55f);
void PopFloatWindowStyle();

// Which page Render() last drew (0..7). Useful for screenshot tooling.
int  CurrentPage();
// Screen rect of the main column after the last Render() (0,0 / 0,0 before it).
void LastRect(ImVec2* pos, ImVec2* size);
// How many feature rows exist, and the screen rect of each feature's settings
// window after the last Render(). FeatureRect returns false while that window
// is closed (or before its first draw) — the harness bleed-checks an open
// dropdown against its own rect with these.
int  FeatureCount();
bool FeatureRect(int idx, ImVec2* pos, ImVec2* size);
void SetPage(int page);
int  PageCount();
const char* PageName(int page);
// Whether a "page/group" id exists, once the pages have been drawn. Tooling only:
// it lets a test assert that a setting is filed on a given tab (and stopped being
// filed on the old one), which is the part of a menu refile that a picture of the
// new page cannot prove.
bool HasGroup(const char* id);

// Copies the server list out of the feature's View into the snapshot, and the message
// that explains why it is empty when it is. Split out of FillSnapshot because it is the
// one place where the UI copies an array whose size the *producer* does not know: 24
// rows here (kMaxServers), `kMaxRows` there, and `server_count` / `server_current` /
// `servers_msg` live in the struct immediately behind it. A copy that overshoots is
// therefore not a visual glitch but a rewrite of the fields that bound the next frame's
// loop - which is exactly what it did once (the row index was the count), so the clamp
// is here, and glass-obsidian/tools/menu_shots.cpp asserts a producer with too many
// rows, too big a message, or an out-of-range `current` cannot reach past the array.
// Also cheap: the row copy is skipped when nothing changed since last frame, because
// this runs on the render thread every frame and a row is two snprintfs and a strlen.
void FillServers(Snapshot& s, const ::features::servers::View& sv);

} // namespace menuui
