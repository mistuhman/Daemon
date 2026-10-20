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

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_lines.emplace_back(buf);
        while (g_log_lines.size() > 200) g_log_lines.pop_front();
    }
#ifdef __linux__
    // Headless Linux: also print to stdout so `external` in a terminal shows
    // plug-and-play + scanner receipts without a GUI. Windows keeps the in-gui log only.
    fprintf(stdout, "%s\n", buf);
    fflush(stdout);
#endif
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
// How the key means "on". Hold (the default, and what this did before the option
// existed) aims only while the button is down; off makes it a latch - one press
// arms the aimbot, the next disarms it - which is the same switch flight has, for
// the same reason: a hand tired of holding RMB should not have to choose between
// cramping and giving the feature up. The latch lives in the feature, not the menu,
// because the menu is a window that gets closed.
inline bool aimbot_hold_mode = true;
inline int aimbot_part = 0;
inline bool sticky_aim = false;
inline bool prediction_enabled = false;
inline float prediction_x = 10.0f;      // simple: horizontal divisor
inline float prediction_y = 10.0f;      // simple: vertical divisor
inline float smoothing_x = 4.0f;
inline float smoothing_y = 4.0f;
inline float fov_size = 150.0f;
inline bool show_fov = false;

// humanizer: reaction delay + eased/curved aim + a little decaying jitter so the
// ── the visual set, from jatosm/RobloxExternal-base ─────────────────────────
//  headdot: a marker on the head, the reference's visual/headdot. Pixels, not
//  studs: this draws on the overlay, which is screen space, and a dot that shrinks
//  with distance is a second meaning for one slider.
inline bool  headdot_enabled = false;
inline float headdot_color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
inline bool  headdot_fill = true;
inline bool  headdot_outline = true;
inline float headdot_outline_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
inline float headdot_thickness = 3.0f;

// off-screen indicators, the reference's visual/oof: where a player is when the
// game is not showing them at all. `radius` is the ring the arrows sit on, from
// the centre of the view, in pixels.
inline bool  oof_enabled = false;
inline float oof_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
inline bool  oof_outline = true;
inline float oof_outline_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
inline float oof_size = 7.0f;
inline int   oof_arrow_style = 2;      // 0 arrow, 1 chevron, 2 triangle
inline bool  oof_names = false;
inline bool  oof_distance = false;
inline float oof_radius = 30.0f;

// ── the misc set, from jatosm/RobloxExternal-base ───────────────────────────
//  Key semantics are this repo's convention everywhere below: no key bound = the
//  switch is enough (always on), a bound key = hold to use. The reference's
//  three-way always/hold/toggle mode collapses into these two, the way walkspeed
//  and flight already do it here.

// gravity: Workspace.Gravity while on, the original put back on release.
inline bool  gravity_enabled = false;
inline int   gravity_keybind = 0;
inline float gravity_value = 196.2f;

// hip height: Humanoid.HipHeight while on. 2 is a default R15; 0 sits the
// character on the floor and is the reason the slider starts there.
inline bool  hipheight_enabled = false;
inline int   hipheight_keybind = 0;
inline float hipheight_value = 2.0f;

// no-fall: the local root's downward speed is held at this (studs/s), through
// mover's velocity writer and only while nobody else owns the body.
// fling: spin the local character (angular velocity) to launch nearby players.
// `fling_power` is rad/s - a few hundred is already violent.
inline bool  fling_enabled = false;
inline int   fling_keybind = 0;
inline float fling_power = 420.0f;
inline bool  nofall_enabled = false;
inline int   nofall_keybind = 0;
inline float nofall_value = 50.0f;   // retired: no fall is a position hold now; kept so old configs parse
// jump power (reference: misc/jumppower). The key, like no-fall's, is a hold-to-
// run gate; 0 means "the switch is the gate".
inline bool  jumppower_enabled = false;
inline int   jumppower_keybind = 0;
inline float jumppower_value = 50.0f;

// anti-AFK: a spacebar tap every this many minutes, foreground only.
inline bool  antiafk_enabled = false;
inline float antiafk_interval = 5.0f;

// ── triggerbot ──────────────────────────────────────────────────────────────
//  Fires a click when a target is under the crosshair. Every gate below is one
//  the aimbot/ESP already agree on (team_check, wall_check are shared); these are
//  the ones only a triggerbot needs.
// hitsound (reference: misc/hitsound): a tone when the triggerbot's 400ms
// read-back proves a shot landed.
inline bool  hitsound_enabled = false;
inline float hitsound_volume = 70.0f;
inline int   hitsound_selected = 0;
inline bool triggerbot_enabled = false;
inline int triggerbot_keybind = 0;
inline bool triggerbot_hold_mode = true;   // see aimbot_hold_mode
inline int triggerbot_part = 0;            // 0 head, 1 nearest-of-the-managed-bones
inline float triggerbot_fov = 6.0f;        // px around the crosshair, in addition to the body box
inline float triggerbot_delay_ms = 120.0f; // how long a target must STAY under the crosshair
inline float triggerbot_cooldown_ms = 150.0f;  // minimum gap between shots
inline float triggerbot_max_dist = 0.0f;   // studs, 0 = infinite (same rule as the ESP's draw distance)
inline bool triggerbot_visible_check = true;
inline float triggerbot_chance = 100.0f;   // 0..100, rolled per shot

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
// Draw distance in studs. 0 = INFINITE (the slider's own label says so when it is
// there), which is the value the ESP's InRenderRange() treats as "no gate at all".
inline float esp_render_distance = 0.0f;

// The draw-distance ceiling, in studs, as ONE constant: the draw-distance slider,
// the tracer's cap and the triggerbot's own cap all refer to it, and
// glass-obsidian's menu harness asserts it is still 25k, so walking the limit back
// is a failing check instead of a quiet edit nobody notices. 25,000 because most
// public places are ~10k studs across - the old 2,000 ceiling meant "you cannot
// see the map", and 0 (infinite) was a special case only people who read the
// source knew about. It is also the range the far-range label fallback exists for
// (LabelPoint in esp.cpp): out there is no box to draw, only a name and a distance.
inline constexpr float esp_draw_distance_max = 25000.0f;
inline bool chams_enabled = false;
// Solid, backface-culled chams of the character's parts ("mesh chams").
// Separate switch and colour from the box chams above: they answer different
// questions ("where is every part" vs "what is the silhouette") and a user who
// wants one does not automatically want the other.
inline bool mesh_chams = false;

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
inline float mesh_chams_color[4] = { 1.0f, 0.25f, 0.25f, 0.85f };
inline float box_fill_top[4] = { 0.0f, 0.0f, 0.0f, 0.4f };
inline float box_fill_bottom[4] = { 0.5f, 0.0f, 0.5f, 0.4f };
inline float healthbar_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

// ---- ESP strokes: one width, one glow, one outline --------------------------
//  These are shared by every ESP element (box, corners, skeleton, tracers, health
//  bar) rather than being a per-feature knob, because the thing that makes an ESP
//  look *drawn* instead of *printed* is that all its lines agree with each other.
//  A 2px box with a 1px skeleton and a 3px tracer looks like three plugins.
inline float esp_line_width = 1.0f;     // px; every stroke multiplies this
inline float esp_glow = 0.35f;          // 0..1 soft colour halo behind a stroke
inline float esp_corner_frac = 0.25f;   // corner-bracket length as a fraction of the shorter side

// ---- Tracers: a line from an anchor on the screen to the player -------------
//  A tracer answers "which way and how far" without needing the box to be on
//  screen, so the origin is a screen anchor and the attach point is the FEET:
//  bottom-of-box is where a Roblox character actually stands, and a tracer to the
//  centre of the box reads as pointing at a floating head.
inline bool  esp_tracer = false;
inline int   esp_tracer_origin = 0;     // 0 bottom centre, 1 bottom left, 2 bottom right, 3 screen centre
inline int   esp_tracer_attach = 0;     // 0 feet, 1 box centre, 2 head (top of box)
inline float esp_tracer_width = 1.5f;   // px, independent of esp_line_width (a tracer wants to be thinner than it looks)
inline float esp_tracer_max_dist = 0.0f;// studs; 0 = draw for everyone on the list
inline bool  esp_tracer_box_color = true;  // take the entity's tint (team/whitelist) rather than the picked colour
inline float esp_tracer_color[4] = { 1.0f, 1.0f, 1.0f, 0.85f };

// ---- ESP text ---------------------------------------------------------------
inline float esp_text_size = 12.0f;     // name / health / distance / rig / tool, in px
inline int   esp_name_plate = 1;        // 0 none, 1 solid plate, 2 plate that fades downward

inline bool walkspeed_enabled = false;
inline int walkspeed_keybind = 0;
inline float walkspeed_value = 30.0f;

inline bool flight_enabled = false;
inline int flight_keybind = 0;
inline float flight_value = 70.0f;
inline bool flight_hold_mode = false;   // false = toggle (like the lua script)
// ---- flight, the stealth profile (features/flight/stealth.h) ----------------
// The three things a game can notice about the old flight, removed one at a time:
// it zeroed Workspace.Gravity (a global, replicated property), it stepped the
// velocity from 0 to full in one write, and it let the vertical component be
// whatever the speed slider said. Off = the old behaviour, kept because a game
// that notices none of it likes the exactly-still hover.
inline bool  flight_stealth = true;
inline float flight_max_speed = 80.0f;    // studs/s, horizontal cap
inline float flight_max_climb = 45.0f;    // studs/s, upward cap
inline float flight_max_descent = 60.0f;  // studs/s, downward cap
inline float flight_accel = 180.0f;       // studs/s^2, the ramp

inline bool fov_changer_enabled = false;
inline float fov_value = 70.0f;

// ═══════════════════════════════════════════════════════════════════════════
//  UI customisation -- theme, material, layout.  One global per setting, same
//  rule as every other feature: the menu edits these directly, the config file
//  reads and writes them, and BuildPalette()/ApplyLayout() in ui/menu_ui.cpp are
//  the only place they are turned into an obsidian style. Nothing here is a
//  cached copy of anything, so there is no sync step to get wrong.
//
//  The block is split in three, because the three questions a user asks are
//  different: "what COLOUR" (theme), "what MATERIAL" (glass or card), and "how
//  BIG / how much is VISIBLE" (layout). Everything in the last group is a size the
//  user can drag, and everything in the middle is a strength that can go to zero.
// ═══════════════════════════════════════════════════════════════════════════

// ---- theme identity ---------------------------------------------------------
// Index into menuui's kThemes table (Obsidian, Liquid, Ink, Frost, Paper, Ember,
// Aurora, Verdant, Crimson). The first three are dark, Frost/Paper are the light
// pair -- Frost is translucent glass, Paper is opaque and maximally legible.
inline int   ui_theme = 1;
// 0..1 "very high contrast": pushes the ink to the extremes of its polarity and
// flattens the glass. Doubles as the accessibility knob and the OLED knob.
inline float ui_contrast = 0.20f;
// A colour mixed into the SURFACES only (never the text), so "smoky violet glass"
// is a slider next to a picker instead of a tenth hard-coded palette.
inline float ui_tint[3] = { 0.55f, 0.65f, 1.00f };
inline float ui_tint_amount = 0.00f;

// ---- accent ----------------------------------------------------------------
inline float ui_accent_color[3] = { 0.78f, 0.08f, 0.08f };
// The far end of every gradient. `auto` derives it from the accent; off means the
// user picked both, which is the difference between "themed" and "designed".
inline bool  ui_accent2_auto = true;
inline float ui_accent2_color[3] = { 0.45f, 0.20f, 0.90f };
// 0 off, 1 cycle, 2 sweep (the pair rotates, keeping its interval), 3 breathe
// (saturation pulses around the picked colour), 4 duotone (accent fixed, the far
// stop of every gradient cycles). Rainbow was a bool and could only do 1.
inline int   ui_rainbow = 0;
inline float ui_rainbow_speed = 0.22f;
inline float ui_rainbow_sat = 0.80f;
inline float ui_rainbow_span = 0.22f;    // hue interval between accent and accent_2

// ---- material: how polished the glass is. 0 = off, 1 = the designer's intent --
// The whole layer on one switch. Off means every strength below reads as 0 while
// keeping the value you dialled in, so "flat card paper, please" is one click
// instead of ten sliders -- and turning it back on returns the glass you tuned.
inline bool  ui_polish = true;
inline float ui_mat_sheen = 1.00f;       // top-edge specular
inline float ui_mat_edge = 1.00f;        // hairline weight
inline float ui_mat_bloom = 1.00f;       // accent glow on state
inline float ui_mat_chroma = 0.40f;      // rim dispersion (RGB split on the edge)
inline float ui_mat_frost = 0.35f;       // inner frost ring
inline float ui_mat_sweep = 0.55f;       // travelling specular band
inline float ui_mat_aurora = 0.45f;      // accent blooms drifting in the window
inline float ui_mat_shimmer = 0.60f;     // highlight travelling along filled tracks
inline float ui_mat_lift = 1.00f;        // hover emphasis
inline float ui_mat_gradient = 1.00f;    // surface top->bottom ramp
inline bool  ui_anim = true;             // master switch: freezes every layer
inline float ui_anim_speed = 1.00f;

// ---- surface ---------------------------------------------------------------
inline float ui_transparency = 6.0f;
inline bool  ui_rounded_corners = true;
inline float ui_corner_radius = 16.0f;
inline bool  ui_window_shadow = false;   // off for an overlay: see window.h
inline float ui_glow = 0.0f;             // extra accent wash inside the window

// ---- layout: sizes, in SCREEN pixels (they are absolute, not ui_scale'd) ----
inline float ui_win_w = 900.0f;
inline float ui_win_h = 620.0f;
inline float ui_min_w = 580.0f;
inline float ui_min_h = 380.0f;
inline float ui_max_w = 0.0f;            // 0 = the work area
inline float ui_max_h = 0.0f;
inline float ui_scale = 1.00f;           // font + widget scale (re-bakes the atlas)
inline float ui_font_size = 15.0f;
inline float ui_title_size = 19.0f;
inline int   ui_columns = 2;             // card grid columns, 1..3
inline float ui_card_h = 50.0f;
inline float ui_card_pad = 11.0f;
inline float ui_gutter = 8.0f;
inline float ui_track_h = 8.0f;
inline float ui_control_h = 26.0f;
inline float ui_nav_w = 220.0f;
inline float ui_nav_row_h = 36.0f;
inline float ui_title_h = 40.0f;
inline float ui_page_pad = 20.0f;

// ---- layout: what is visible -----------------------------------------------
// 0 = labels + hints, 1 = icons only, 2 = rail hidden (the page goes full width).
inline int   ui_rail_mode = 0;
inline bool  ui_session_card = true;     // the rail's pinned player/process card
inline bool  ui_hero_cards = true;       // the big master switch at the top of a page
inline bool  ui_hints = true;            // one-line hints under a label
inline bool  ui_collapse_groups = true;  // group rules get a chevron and can close
inline float ui_row_space = 8.0f;        // vertical gap between rows
// Groups the user closed, as a ';'-joined list of their ids. It is a string rather
// than a bitfield so a config survives a page being renamed, and so "which groups
// were open when I last looked" travels with the config like any other preference.
inline char  ui_closed_groups[420]{};

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



// ---- world lighting (features/world) ----------------------------------------
// The reference repo's visual/world/env set: the sky, fog, ambient light,
// exposure, shadows and grass are all plain Lighting/Terrain properties, and
// each one is saved and put back exactly like gravity and hip height.
inline bool  fullbright_enabled = false;
inline float fullbright_value = 1.6f;      // brightness, 1..5
inline bool  world_ambient_enabled = false;
inline float world_ambient_color[4] = { 0.45f, 0.45f, 0.45f, 1.0f };
inline bool  world_fog_enabled = false;
inline float world_fog_start = 260.0f;
inline float world_fog_end = 900.0f;
inline float world_fog_color[4] = { 0.62f, 0.66f, 0.74f, 1.0f };
inline bool  world_exposure_enabled = false;
inline float world_exposure_value = 0.2f;  // -3..3
inline bool  noshadow_enabled = false;     // GlobalShadows = false
inline bool  nograss_enabled = false;      // Terrain.GrassLength = 0
inline bool  night_enabled = false;
inline float night_level = 0.75f;          // 0..1, darkens ambient + brightness
// ---- world objects (features/world/world_esp.cpp) ---------------------------
// The reference's visual/world categories: the named objects a survival place
// puts on the ground. One switch per category; the name tables live in the
// feature.
inline bool  world_bots_enabled = false;
inline bool  world_animals_enabled = false;
inline float world_esp_distance = 2000.0f;     // 0 = no distance gate
inline bool  world_esp_labels = true;
inline bool  world_time_enabled = false;
inline float world_time_value = 14.0f;     // hours, 0..24
inline bool  skybox_changer_enabled = false;
inline int skybox_type = 0;
inline char skybox_debug_msg[256]{};



inline bool  show_fps = true;
// ---- frame rate -------------------------------------------------------------
// The client's own cap (TaskScheduler::MaxFPS, written by features/fpscap) and
// the overlay's pacing (overlay.cpp). Three separate numbers because they are
// three separate frame rates, and conflating them is why "the FPS widget" was
// only ever the menu's: the overlay draws in thousands of fps, the client runs
// at its own cap, and the tool's cost is the overlay's rate.
inline bool  fps_cap_enabled = false;
inline float fps_cap_value = 60.0f;      // frames per second, 10..360
inline float overlay_fps_cap = 144.0f;   // 0 = uncapped; the overlay's own rate
inline float overlay_idle_fps = 20.0f;   // when nothing is being drawn at all
// Written by render_ui() and read by the overlay's pacer: true when the frame has
// something to draw (the menu, any ESP pass, any float window with content).
inline std::atomic<bool> g_overlay_busy{ false };
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

// Player tags: the blacklist steers the AIMBOT - that is its job, and the Players
// page labels it that way. It hides an entity from ESP too only if you ask it to
// (`blacklist_hides_esp`), because "do not aim at them" and "do not draw them" are
// two decisions a user may want separately: a friend you never want to lock onto is
// still someone you may want to see through a wall.
// Whitelist still draws on ESP but uses whitelist_esp_color (box/name/skeleton).
// Blacklist wins.
inline bool blacklist_hides_esp = false;
enum { kPlayerTagMax = 24, kPlayerTagLen = 32 };
inline char player_blacklist[kPlayerTagMax][kPlayerTagLen]{};
inline int  player_blacklist_n = 0;
inline char player_whitelist[kPlayerTagMax][kPlayerTagLen]{};
inline int  player_whitelist_n = 0;
inline float whitelist_esp_color[4] = { 0.25f, 0.95f, 0.45f, 1.0f };
// Teammates when team_check is on: they are NOT hidden anymore - box, name,
// skeleton and chams render in this colour instead, and the aimbot skips them.
inline float team_esp_color[4] = { 0.20f, 0.60f, 1.00f, 1.0f };
// The reference paints a friend's box green (visual/core/visual.cpp). Kept
// distinct from the whitelist's green by being brighter, and it applies to the
// skeleton, chams and tracer too because all four ask EspTint.
inline float friends_esp_color[4] = { 0.30f, 1.00f, 0.30f, 1.0f };

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

// Dead check: never lock onto a player whose Humanoid says health 0, and drop a
// sticky lock the moment the target dies (you killed them - the corpse keeps its
// head part readable, so without this the aim happily aims at a body). Default off
// because it is a filter over the same health read the players page labels with: if
// the humanoid read is unavailable, IsDead() says unknown, not dead.
inline bool dead_check = false;
inline bool team_check = false;
// line-of-sight check: aimbot won't lock / ESP won't draw targets blocked by
// another player's body (true world-geometry walls need the primitives list)
inline bool wall_check = false;