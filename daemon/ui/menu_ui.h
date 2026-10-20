// -----------------------------------------------------------------------------
//  ui/menu_ui.h  --  Daemon menu UI (Linux only)
// -----------------------------------------------------------------------------
//  This is the Linux-only menu UI. All visual design decisions are deferred
//  to the user — this file provides only the structural API.
//  Nothing in this file includes platform-specific headers.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/obsidian.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace features { namespace servers { struct View; } }

namespace menuui {

// -----------------------------------------------------------------------------
//  read-only data the app refreshes every frame
// -----------------------------------------------------------------------------
struct Snapshot
{
    // attach / identity
    bool     attached   = false;
    char     user[64]   = "";
    unsigned pid        = 0;
    char     status[80] = "";
    char     game[128]  = "";
    bool     in_game    = false;
    char     place[24]  = "";
    char     version[40] = "";
    char     os[48] = "";

    // world page
    const char* const* skybox_names = nullptr;
    int          skybox_count       = 0;
    char     skybox_note[256] = "";

    // aimbot page
    bool     aimbot_latched = false;

    // hit sounds
    const char* const* hitsound_names = nullptr;
    int          hitsound_count       = 0;

    // players page
    struct PlayerRow
    {
        char     name[32];
        char     account[32];
        char     detail[48];
        uint32_t user_id = 0;
        int      state = 0;
        bool     friended = false;
    };
    enum { kMaxPlayers = 48 };
    PlayerRow players[kMaxPlayers];
    int       player_count = 0;
    bool      spectating   = false;
    char      player_note[256] = "";

    // servers page
    struct ServerRow
    {
        char id[20];
        char detail[24];
        int  state = 0;
    };
    enum { kMaxServers = 24 };
    ServerRow servers[kMaxServers];
    int  server_count       = 0;
    int  server_current     = -1;
    bool servers_busy       = false;
    bool servers_can_rejoin = false;
    int  server_age_sec     = -1;
    char servers_msg[256]   = "";

    // debug page
    struct Row
    {
        const char* key;
        char        value[96];
        int         state = 0;
    };
    std::vector<Row> runtime;
    std::vector<Row> camera;
    char     test_result[200] = "";
    bool     can_test         = false;
};

// -----------------------------------------------------------------------------
//  the platform layer's callbacks
// -----------------------------------------------------------------------------
struct Actions
{
    std::function<int()>        poll_key;
    const char* (*key_name)(int vk) = nullptr;

    std::function<std::vector<std::string>()> list_configs;
    std::function<bool(const char*)>        config_save;
    std::function<bool(const char*)>        config_load;
    std::function<bool(const char*)>        config_delete;
    std::function<bool(const char*, const char*)> config_rename;
    std::function<void()>                   open_config_folder;

    std::function<void(int)>                run_test;
    std::function<void()>                   draw_dex;
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
    std::function<void()>                   waypoint_fill;
    std::function<void(const char*, float, float, float)> waypoint_add_xyz;
    std::function<void(int)>                waypoint_goto;
    std::function<void(int)>                waypoint_remove;
};

void SetActions(const Actions& actions);

// Builds the palette from the ui_* globals. Called once per frame by Render(),
// and by the app at startup so the very first frame is already themed.
obsidian::Palette BuildPalette();

// Advances any pending keybind capture. Call every frame before Render().
void TickKeybinds();

// Re-applies the font/atlas-dependent half of the theme.
bool SyncTheme();

// Persistence for open settings windows (stored as ';'-joined feature ids in ui_closed_groups).
void SetClosedGroups(const char* list);
void GetClosedGroups(char* out, size_t n);

// Draws the whole menu window. Sets g_request_exit when closed.
void Render(Snapshot& snap);

// Style stack for plain-ImGui float windows (fps counter, media badge).
void PushFloatWindowStyle(float bg_alpha = 0.55f);
void PopFloatWindowStyle();

// Tooling accessors
int  CurrentPage();
void LastRect(float* pos_x, float* pos_y, float* size_x, float* size_y);
int  FeatureCount();
bool FeatureRect(int idx, float* pos_x, float* pos_y, float* size_x, float* size_y);
void SetPage(int page);
int  PageCount();
const char* PageName(int page);
bool HasGroup(const char* id);

// Copies server list into snapshot.
void FillServers(Snapshot& s, const ::features::servers::View& sv);

} // namespace menuui