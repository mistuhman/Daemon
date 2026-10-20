#pragma once
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <cstring>
#include <string>

// ---- in-gui log (replaces the old console window) ----
inline std::mutex g_log_mutex;
inline std::deque<std::string> g_log_lines;

inline void LogLine(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_lines.emplace_back(buf);
    while (g_log_lines.size() > 200) g_log_lines.pop_front();
}

// The overlay's menu-open state, mirrored once a frame by overlay::render_ui. A
// feature that must not fire while the menu is eating the click reads this instead of
// including overlay.h -- that header pulls d3d11 into every feature translation unit,
// which is exactly what makes those units impossible to parse without a Windows SDK.
// Starts true, because the menu starts open.
inline std::atomic<bool> g_menu_open{ true };

// True only while a menu TEXT FIELD is capturing the keyboard (ImGui
// WantTextInput, sampled on the render thread). THIS - not g_menu_open - is
// what a key-activated feature should gate on. The menu being open is not a
// reason to ignore space: the debug page sits behind the menu, so an
// open-menu gate made infinite jump impossible to test where its own log
// lives ("nothing happens in debug when i press space"). Only actual text
// capture (keybind capture boxes, the config name field) must eat keys.
inline std::atomic<bool> g_menu_text_input{ false };

// set by the menu's X button / tray "exit" so the app can shut down cleanly
inline bool g_request_exit = false;

// SPACE input. THE LESSON OF THREE FAILED FIXES: GetAsyncKeyState cannot
// carry a press count. Its level bit misses presses that happen entirely
// inside a feature-loop stall, and its "pressed since the last call" edge bit
// is DOCUMENTED BY MICROSOFT AS UNRELIABLE - the latch is system-wide and any
// other process polling the key (the game, other overlays, the OS) consumes
// it between our calls, which is exactly what happened live: spam still
// missed. So presses now arrive by QUEUE, not by poll: overlay.h installs a
// WH_KEYBOARD_LL low-level hook (its render loop pumps messages every frame,
// which is what delivers the callbacks), and the hook counts every physical
// space press into g_input_space_hook_presses. mover::Tick drains that
// counter into g_input_space_edge once per iteration. The GAKS edge bit is
// OR'd in as a supplement but ONLY gated on a fresh down transition (the
// sampled level was up at the previous iteration): auto-repeat re-sets the
// bit for as long as the key is held, and an ungated OR turned holding space
// into an endless stream of phantom presses. The level comes from GAKS,
// whose level bit is always truthful about the current instant.
inline bool g_input_space_down = false;      // set by mover::Tick each pass
inline bool g_input_space_edge = false;      // >=1 press since previous pass
inline std::atomic<bool>     g_input_hook_installed{ false };
inline std::atomic<bool>     g_input_space_hook_down{ false };
inline std::atomic<unsigned> g_input_space_hook_presses{ 0 };

// menu toggle key - rebindable from the keybinds tab (VK_HOME by default)
inline int menu_toggle_keybind = 0x24;

inline bool aimbot_enabled = false;
inline int aimbot_aim_type = 1;
inline int aimbot_keybind = 0;
inline int aimbot_part = 0;
inline bool sticky_aim = false;
inline bool prediction_enabled = false;
inline float prediction_x = 10.0f;
inline float prediction_y = 10.0f;
inline float smoothing_x = 4.0f;
inline float smoothing_y = 4.0f;
inline float fov_size = 150.0f;
inline bool show_fov = false;

// humanizer: reaction delay + eased/curved aim + a little decaying jitter so the
// aimbot moves like a person instead of snapping instantly.
inline bool humanizer_enabled = false;
inline float humanizer_strength = 0.5f;   // 0..1

inline bool esp_enabled = false;
inline bool box_esp = false;
inline int box_esp_type = 0;
inline bool healthbar = false;
inline bool health_text = false;
inline bool name = false;
inline bool distance = false;
inline bool rig_type = false;
inline bool tool_esp = false;
inline bool aimviewer = false;
inline bool skeleton_esp = false;
inline bool chinahat = false;
inline bool render_expanded_hitbox = false;
inline bool hitbox_expander_enabled = false;
inline float hitbox_expander_value = 10.0f;
inline float esp_render_distance = 0.0f;
inline bool chams_enabled = false;

inline bool box_fill = false;
inline bool box_fill_gradient = false;
inline bool box_fill_gradient_rotate = false;

inline uintptr_t g_base_address = 0;

inline float box_esp_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float name_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float distance_color[4] = { 0.7f, 0.7f, 0.7f, 1.0f };
inline float health_text_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float rig_type_color[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
inline float tool_color[4] = { 0.9f, 0.9f, 0.5f, 1.0f };
inline float skeleton_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
inline float chinahat_color[4] = { 0.0f, 0.8f, 1.0f, 1.0f };
inline float chams_color[4] = { 1.0f, 0.0f, 0.0f, 0.6f };
inline float box_fill_top[4] = { 0.0f, 0.0f, 0.0f, 0.4f };
inline float box_fill_bottom[4] = { 0.5f, 0.0f, 0.5f, 0.4f };
inline float healthbar_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

inline bool walkspeed_enabled = false;
inline int walkspeed_keybind = 0;
inline float walkspeed_value = 30.0f;

inline bool flight_enabled = false;
inline int flight_keybind = 0;
inline float flight_value = 70.0f;
inline bool flight_hold_mode = false;   // false = toggle (like the lua script)

inline bool fov_changer_enabled = false;
inline float fov_value = 70.0f;

// ---- ui customisation (ui page) ----
inline float ui_transparency = 6.0f;
inline bool  ui_rounded_corners = true;
inline float ui_corner_radius = 16.0f;
inline bool  ui_rainbow = false;
inline float ui_accent_color[3] = { 0.78f, 0.08f, 0.08f };

inline bool infinite_jump_enabled = false;
// 100 by default, not 50: that is the value the debug hammered-velocity test
// pins, and the one the user watched climb +30-34 studs per click. "Almost
// exactly how the debug test works" includes the number.
inline float infinite_jump_power = 100.0f;

inline bool inventory_checker_enabled = false;
inline int inventory_checker_keybind = 0;

inline bool click_teleport_enabled = false;
inline int click_teleport_keybind = 0;
inline float click_teleport_distance = 30.0f;

// players tab: jump to somebody, or park the camera on them and watch.
// spectate_user_id is a ROBLOX id and not an index into the player list: the
// cache re-builds (and re-sorts) that list every pass, so an index would end up
// following a different person. 0 means nobody, which is also "not spectating".
inline bool     spectate_enabled  = false;
inline uint32_t spectate_user_id  = 0;
inline float    spectate_distance = 14.0f;  // studs behind their root part
inline float    spectate_height   = 3.0f;   // studs above it
inline float    tp_land_height    = 2.0f;   // above the target, so you do not fuse
// what the last players-tab action did (or why it refused). Same shape as
// skybox_debug_msg: the feature writes it, the menu copies it into a note card.
inline char     players_status_msg[256]{};

inline bool noclip_enabled = false;
inline int noclip_keybind = 0;



inline bool skybox_changer_enabled = false;
inline int skybox_type = 0;
inline char skybox_debug_msg[256]{};



inline bool  show_fps = true;
inline float fps_pos_x = 12.0f;
inline float fps_pos_y = 10.0f;

inline bool  freecam_enabled = false;
inline int   freecam_keybind = 0;
inline float freecam_speed   = 60.0f;

enum { kMaxWaypoints = 12, kWaypointNameLen = 28 };
inline char  waypoint_names[kMaxWaypoints][kWaypointNameLen]{};
inline float waypoint_x[kMaxWaypoints]{};
inline float waypoint_y[kMaxWaypoints]{};
inline float waypoint_z[kMaxWaypoints]{};
inline int   waypoint_n = 0;
inline bool  show_waypoints = true;

// ── now-playing (Spotify / YouTube Music / Monochrome) ──────────────────────
inline bool  show_now_playing = true;
inline float media_pos_x = 12.0f;
inline float media_pos_y = 40.0f;   // below the FPS counter
inline char  waypoint_name_buf[kWaypointNameLen]{};
inline float waypoint_edit_x = 0.0f;
inline float waypoint_edit_y = 0.0f;
inline float waypoint_edit_z = 0.0f;

// Player tags: blacklist is ignored by aimbot and ESP. Whitelist still draws
// on ESP but uses whitelist_esp_color (box/name/skeleton). Blacklist wins.
enum { kPlayerTagMax = 24, kPlayerTagLen = 32 };
inline char player_blacklist[kPlayerTagMax][kPlayerTagLen]{};
inline int  player_blacklist_n = 0;
inline char player_whitelist[kPlayerTagMax][kPlayerTagLen]{};
inline int  player_whitelist_n = 0;
inline float whitelist_esp_color[4] = { 0.25f, 0.95f, 0.45f, 1.0f };
// Teammates when team_check is on: they are NOT hidden anymore - box, name,
// skeleton and chams render in this colour instead, and the aimbot skips them.
inline float team_esp_color[4] = { 0.20f, 0.60f, 1.00f, 1.0f };

inline bool NamesEqual(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return false;
    }
    return *a == *b;
}

inline bool NameInTags(const char* name, const char tags[][kPlayerTagLen], int n)
{
    if (!name || !name[0]) return false;
    for (int i = 0; i < n; ++i)
        if (NamesEqual(name, tags[i])) return true;
    return false;
}

inline bool PlayerIsBlacklisted(const char* name)
{
    return NameInTags(name, player_blacklist, player_blacklist_n);
}

inline bool PlayerIsWhitelisted(const char* name)
{
    return NameInTags(name, player_whitelist, player_whitelist_n);
}

inline bool AddPlayerTag(char tags[][kPlayerTagLen], int& n, const char* name)
{
    if (!name || !name[0] || n >= kPlayerTagMax) return false;
    if (NameInTags(name, tags, n)) return false;
    snprintf(tags[n], kPlayerTagLen, "%s", name);
    ++n;
    return true;
}

inline void RemovePlayerTagAt(char tags[][kPlayerTagLen], int& n, int i)
{
    if (i < 0 || i >= n) return;
    for (int k = i; k < n - 1; ++k)
        memcpy(tags[k], tags[k + 1], kPlayerTagLen);
    tags[n - 1][0] = '\0';
    --n;
}

inline bool team_check = false;
// line-of-sight check: aimbot won't lock / ESP won't draw targets blocked by
// another player's body (true world-geometry walls need the primitives list)
inline bool wall_check = false;