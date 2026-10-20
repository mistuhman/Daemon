// -----------------------------------------------------------------------------
//  config.cpp — Daemon config save/load (Linux only)
// -----------------------------------------------------------------------------
#include "config.h"
#include "../../globals.h"
#include "../players/friends.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <cstdio>

namespace config {

    std::string GetConfigDir() {
        // XDG_CONFIG_HOME/daemon or ~/.config/daemon
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0]) {
            return std::string(xdg) + "/daemon";
        }
        const char* home = std::getenv("HOME");
        if (home && home[0]) {
            return std::string(home) + "/.config/daemon";
        }
        return "./config";  // fallback
    }

    void EnsureConfigDir() {
        std::string dir = GetConfigDir();
        std::filesystem::create_directories(dir);
    }

    static std::string BoolStr(bool v) { return v ? "1" : "0"; }
    static std::string FloatStr(float v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%f", v);
        return buf;
    }
    static std::string IntStr(int v) { return std::to_string(v); }

    static bool ParseBool(const std::string& s) { return s == "1" || s == "true"; }
    static float ParseFloat(const std::string& s) {
        try { return std::stof(s); } catch (...) { return 0.0f; }
    }
    static int ParseInt(const std::string& s) {
        try { return std::stoi(s); } catch (...) { return 0; }
    }

    static void WriteEntry(std::ofstream& f, const char* key, bool val) {
        f << key << "=" << BoolStr(val) << "\n";
    }
    static void WriteEntry(std::ofstream& f, const char* key, int val) {
        f << key << "=" << IntStr(val) << "\n";
    }
    static void WriteEntry(std::ofstream& f, const char* key, float val) {
        f << key << "=" << FloatStr(val) << "\n";
    }
    static void WriteEntry(std::ofstream& f, const char* key, const float val[4]) {
        f << key << "=" << FloatStr(val[0]) << "," << FloatStr(val[1]) << "," << FloatStr(val[2]) << "," << FloatStr(val[3]) << "\n";
    }
    // Three-float colours: the globals that the menu's pickers write are RGB
    static void WriteRGB(std::ofstream& f, const char* key, const float v[3]) {
        f << key << "=" << FloatStr(v[0]) << "," << FloatStr(v[1]) << "," << FloatStr(v[2]) << "\n";
    }
    static bool ReadRGB(const std::string& line, const char* key, float v[3]) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        std::stringstream ss(line.substr(prefix.size()));
        std::string token;
        for (int i = 0; i < 3 && std::getline(ss, token, ','); ++i) v[i] = ParseFloat(token);
        return true;
    }
    static void WriteStr(std::ofstream& f, const char* key, const char* v) {
        f << key << "=" << (v ? v : "") << "\n";
    }
    static bool ReadStr(const std::string& line, const char* key, char* out, size_t cap) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        std::snprintf(out, cap, "%s", line.substr(prefix.size()).c_str());
        return true;
    }

    static void WriteTags(std::ofstream& f, const char* key, const char tags[][kPlayerTagLen], int n) {
        f << key << "=";
        for (int i = 0; i < n; ++i) {
            if (i) f << ",";
            f << tags[i];
        }
        f << "\n";
    }
    static bool ReadTags(const std::string& line, const char* key, char tags[][kPlayerTagLen], int& n) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        n = 0;
        std::stringstream ss(line.substr(prefix.size()));
        std::string token;
        while (std::getline(ss, token, ',') && n < kPlayerTagMax) {
            if (token.empty()) continue;
            std::snprintf(tags[n], kPlayerTagLen, "%s", token.c_str());
            ++n;
        }
        return true;
    }

    static bool ReadLine(const std::string& line, const char* key, bool& out) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        out = ParseBool(line.substr(prefix.size()));
        return true;
    }
    static bool ReadLine(const std::string& line, const char* key, int& out) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        out = ParseInt(line.substr(prefix.size()));
        return true;
    }
    static bool ReadLine(const std::string& line, const char* key, float& out) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        out = ParseFloat(line.substr(prefix.size()));
        return true;
    }

    // All the globals we persist — one list, one source of truth.
    void SaveToFile(const std::string& path) {
        std::ofstream f(path);
        if (!f) return;

        // aimbot
        WriteEntry(f, "aimbot_enabled", aimbot_enabled);
        WriteEntry(f, "aimbot_aim_type", aimbot_aim_type);
        WriteEntry(f, "aimbot_keybind", aimbot_keybind);
        WriteEntry(f, "aimbot_hold_mode", aimbot_hold_mode);
        WriteEntry(f, "aimbot_part", aimbot_part);
        WriteEntry(f, "sticky_aim", sticky_aim);
        WriteEntry(f, "prediction_enabled", prediction_enabled);
        WriteEntry(f, "prediction_x", prediction_x);
        WriteEntry(f, "prediction_y", prediction_y);
        WriteEntry(f, "smoothing_x", smoothing_x);
        WriteEntry(f, "smoothing_y", smoothing_y);
        WriteEntry(f, "fov_size", fov_size);
        WriteEntry(f, "show_fov", show_fov);
        WriteEntry(f, "humanizer_enabled", humanizer_enabled);
        WriteEntry(f, "humanizer_strength", humanizer_strength);

        // triggerbot
        WriteEntry(f, "triggerbot_enabled", triggerbot_enabled);
        WriteEntry(f, "triggerbot_keybind", triggerbot_keybind);
        WriteEntry(f, "triggerbot_hold_mode", triggerbot_hold_mode);
        WriteEntry(f, "triggerbot_part", triggerbot_part);
        WriteEntry(f, "triggerbot_fov", triggerbot_fov);
        WriteEntry(f, "triggerbot_delay_ms", triggerbot_delay_ms);
        WriteEntry(f, "triggerbot_cooldown_ms", triggerbot_cooldown_ms);
        WriteEntry(f, "triggerbot_max_dist", triggerbot_max_dist);
        WriteEntry(f, "triggerbot_visible_check", triggerbot_visible_check);
        WriteEntry(f, "triggerbot_chance", triggerbot_chance);

        // hitsound
        WriteEntry(f, "hitsound_enabled", hitsound_enabled);
        WriteEntry(f, "hitsound_volume", hitsound_volume);
        WriteEntry(f, "hitsound_selected", hitsound_selected);

        // esp
        WriteEntry(f, "esp_enabled", esp_enabled);
        WriteEntry(f, "box_esp", box_esp);
        WriteEntry(f, "box_esp_type", box_esp_type);
        WriteEntry(f, "healthbar", healthbar);
        WriteEntry(f, "health_text", health_text);
        WriteEntry(f, "name", name);
        WriteEntry(f, "distance", distance);
        WriteEntry(f, "rig_type", rig_type);
        WriteEntry(f, "tool_esp", tool_esp);
        WriteEntry(f, "aimviewer", aimviewer);
        WriteEntry(f, "skeleton_esp", skeleton_esp);
        WriteEntry(f, "chinahat", chinahat);
        WriteEntry(f, "render_expanded_hitbox", render_expanded_hitbox);
        WriteEntry(f, "hitbox_expander_enabled", hitbox_expander_enabled);
        WriteEntry(f, "hitbox_expander_value", hitbox_expander_value);
        WriteEntry(f, "esp_render_distance", esp_render_distance);
        WriteEntry(f, "chams_enabled", chams_enabled);
        WriteEntry(f, "mesh_chams", mesh_chams);
        WriteEntry(f, "box_fill", box_fill);
        WriteEntry(f, "box_fill_gradient", box_fill_gradient);
        WriteEntry(f, "box_fill_gradient_rotate", box_fill_gradient_rotate);
        WriteRGB(f, "box_esp_color", box_esp_color);
        WriteRGB(f, "name_color", name_color);
        WriteRGB(f, "distance_color", distance_color);
        WriteRGB(f, "health_text_color", health_text_color);
        WriteRGB(f, "rig_type_color", rig_type_color);
        WriteRGB(f, "tool_color", tool_color);
        WriteRGB(f, "skeleton_color", skeleton_color);
        WriteRGB(f, "chinahat_color", chinahat_color);
        WriteRGB(f, "chams_color", chams_color);
        WriteRGB(f, "mesh_chams_color", mesh_chams_color);
        WriteRGB(f, "box_fill_top", box_fill_top);
        WriteRGB(f, "box_fill_bottom", box_fill_bottom);
        WriteRGB(f, "healthbar_color", healthbar_color);
        WriteEntry(f, "esp_line_width", esp_line_width);
        WriteEntry(f, "esp_glow", esp_glow);
        WriteEntry(f, "esp_corner_frac", esp_corner_frac);
        WriteEntry(f, "esp_tracer", esp_tracer);
        WriteEntry(f, "esp_tracer_origin", esp_tracer_origin);
        WriteEntry(f, "esp_tracer_attach", esp_tracer_attach);
        WriteEntry(f, "esp_tracer_width", esp_tracer_width);
        WriteEntry(f, "esp_tracer_max_dist", esp_tracer_max_dist);
        WriteEntry(f, "esp_tracer_box_color", esp_tracer_box_color);
        WriteRGB(f, "esp_tracer_color", esp_tracer_color);
        WriteEntry(f, "esp_text_size", esp_text_size);
        WriteEntry(f, "esp_name_plate", esp_name_plate);

        // movement
        WriteEntry(f, "walkspeed_enabled", walkspeed_enabled);
        WriteEntry(f, "walkspeed_keybind", walkspeed_keybind);
        WriteEntry(f, "walkspeed_value", walkspeed_value);
        WriteEntry(f, "flight_enabled", flight_enabled);
        WriteEntry(f, "flight_keybind", flight_keybind);
        WriteEntry(f, "flight_value", flight_value);
        WriteEntry(f, "flight_hold_mode", flight_hold_mode);
        WriteEntry(f, "flight_stealth", flight_stealth);
        WriteEntry(f, "flight_max_speed", flight_max_speed);
        WriteEntry(f, "flight_max_climb", flight_max_climb);
        WriteEntry(f, "flight_max_descent", flight_max_descent);
        WriteEntry(f, "flight_accel", flight_accel);
        WriteEntry(f, "infinite_jump_enabled", infinite_jump_enabled);
        WriteEntry(f, "infinite_jump_power", infinite_jump_power);
        WriteEntry(f, "noclip_enabled", noclip_enabled);
        WriteEntry(f, "noclip_keybind", noclip_keybind);
        WriteEntry(f, "click_teleport_enabled", click_teleport_enabled);
        WriteEntry(f, "click_teleport_keybind", click_teleport_keybind);
        WriteEntry(f, "click_teleport_distance", click_teleport_distance);

        // physics
        WriteEntry(f, "gravity_enabled", gravity_enabled);
        WriteEntry(f, "gravity_keybind", gravity_keybind);
        WriteEntry(f, "gravity_value", gravity_value);
        WriteEntry(f, "hipheight_enabled", hipheight_enabled);
        WriteEntry(f, "hipheight_keybind", hipheight_keybind);
        WriteEntry(f, "hipheight_value", hipheight_value);
        WriteEntry(f, "nofall_enabled", nofall_enabled);
        WriteEntry(f, "nofall_keybind", nofall_keybind);
        WriteEntry(f, "nofall_value", nofall_value);
        WriteEntry(f, "fling_enabled", fling_enabled);
        WriteEntry(f, "fling_keybind", fling_keybind);
        WriteEntry(f, "fling_power", fling_power);
        WriteEntry(f, "jumppower_enabled", jumppower_enabled);
        WriteEntry(f, "jumppower_keybind", jumppower_keybind);
        WriteEntry(f, "jumppower_value", jumppower_value);
        WriteEntry(f, "antiafk_enabled", antiafk_enabled);
        WriteEntry(f, "antiafk_interval", antiafk_interval);

        // camera
        WriteEntry(f, "fov_changer_enabled", fov_changer_enabled);
        WriteEntry(f, "fov_value", fov_value);

        // visuals extras
        WriteEntry(f, "headdot_enabled", headdot_enabled);
        WriteRGB(f, "headdot_color", headdot_color);
        WriteEntry(f, "headdot_fill", headdot_fill);
        WriteEntry(f, "headdot_outline", headdot_outline);
        WriteRGB(f, "headdot_outline_color", headdot_outline_color);
        WriteEntry(f, "headdot_thickness", headdot_thickness);
        WriteEntry(f, "oof_enabled", oof_enabled);
        WriteRGB(f, "oof_color", oof_color);
        WriteEntry(f, "oof_outline", oof_outline);
        WriteRGB(f, "oof_outline_color", oof_outline_color);
        WriteEntry(f, "oof_size", oof_size);
        WriteEntry(f, "oof_arrow_style", oof_arrow_style);
        WriteEntry(f, "oof_names", oof_names);
        WriteEntry(f, "oof_distance", oof_distance);
        WriteEntry(f, "oof_radius", oof_radius);

        // world
        WriteEntry(f, "fullbright_enabled", fullbright_enabled);
        WriteEntry(f, "fullbright_value", fullbright_value);
        WriteEntry(f, "world_ambient_enabled", world_ambient_enabled);
        WriteRGB(f, "world_ambient_color", world_ambient_color);
        WriteEntry(f, "world_fog_enabled", world_fog_enabled);
        WriteEntry(f, "world_fog_start", world_fog_start);
        WriteEntry(f, "world_fog_end", world_fog_end);
        WriteRGB(f, "world_fog_color", world_fog_color);
        WriteEntry(f, "world_exposure_enabled", world_exposure_enabled);
        WriteEntry(f, "world_exposure_value", world_exposure_value);
        WriteEntry(f, "noshadow_enabled", noshadow_enabled);
        WriteEntry(f, "nograss_enabled", nograss_enabled);
        WriteEntry(f, "night_enabled", night_enabled);
        WriteEntry(f, "night_level", night_level);
        WriteEntry(f, "world_bots_enabled", world_bots_enabled);
        WriteEntry(f, "world_animals_enabled", world_animals_enabled);
        WriteEntry(f, "world_esp_distance", world_esp_distance);
        WriteEntry(f, "world_esp_labels", world_esp_labels);
        WriteEntry(f, "world_time_enabled", world_time_enabled);
        WriteEntry(f, "world_time_value", world_time_value);
        WriteEntry(f, "skybox_changer_enabled", skybox_changer_enabled);
        WriteEntry(f, "skybox_type", skybox_type);

        // players / social
        WriteEntry(f, "blacklist_hides_esp", blacklist_hides_esp);
        WriteTags(f, "player_blacklist", player_blacklist, player_blacklist_n);
        WriteTags(f, "player_whitelist", player_whitelist, player_whitelist_n);
        WriteRGB(f, "whitelist_esp_color", whitelist_esp_color);
        WriteRGB(f, "team_esp_color", team_esp_color);
        WriteRGB(f, "friends_esp_color", friends_esp_color);

        // misc
        WriteEntry(f, "show_fps", show_fps);
        WriteEntry(f, "fps_cap_enabled", fps_cap_enabled);
        WriteEntry(f, "fps_cap_value", fps_cap_value);
        WriteEntry(f, "overlay_fps_cap", overlay_fps_cap);
        WriteEntry(f, "overlay_idle_fps", overlay_idle_fps);
        WriteEntry(f, "freecam_enabled", freecam_enabled);
        WriteEntry(f, "freecam_keybind", freecam_keybind);
        WriteEntry(f, "freecam_speed", freecam_speed);
        WriteEntry(f, "show_waypoints", show_waypoints);
        WriteEntry(f, "show_now_playing", show_now_playing);
        WriteEntry(f, "menu_toggle_keybind", menu_toggle_keybind);
        WriteEntry(f, "dead_check", dead_check);
        WriteEntry(f, "team_check", team_check);
        WriteEntry(f, "wall_check", wall_check);
        WriteEntry(f, "streamproof_enabled", streamproof_enabled);
        WriteStr(f, "streamproof_apps", streamproof_apps);

        // theme / UI
        WriteEntry(f, "ui_theme", ui_theme);
        WriteEntry(f, "ui_contrast", ui_contrast);
        WriteRGB(f, "ui_tint", ui_tint);
        WriteEntry(f, "ui_tint_amount", ui_tint_amount);
        WriteRGB(f, "ui_accent_color", ui_accent_color);
        WriteEntry(f, "ui_accent2_auto", ui_accent2_auto);
        WriteRGB(f, "ui_accent2_color", ui_accent2_color);
        WriteEntry(f, "ui_rainbow", ui_rainbow);
        WriteEntry(f, "ui_rainbow_speed", ui_rainbow_speed);
        WriteEntry(f, "ui_rainbow_sat", ui_rainbow_sat);
        WriteEntry(f, "ui_rainbow_span", ui_rainbow_span);
        WriteEntry(f, "ui_polish", ui_polish);
        WriteEntry(f, "ui_mat_sheen", ui_mat_sheen);
        WriteEntry(f, "ui_mat_edge", ui_mat_edge);
        WriteEntry(f, "ui_mat_bloom", ui_mat_bloom);
        WriteEntry(f, "ui_mat_chroma", ui_mat_chroma);
        WriteEntry(f, "ui_mat_frost", ui_mat_frost);
        WriteEntry(f, "ui_mat_sweep", ui_mat_sweep);
        WriteEntry(f, "ui_mat_aurora", ui_mat_aurora);
        WriteEntry(f, "ui_mat_shimmer", ui_mat_shimmer);
        WriteEntry(f, "ui_mat_lift", ui_mat_lift);
        WriteEntry(f, "ui_mat_gradient", ui_mat_gradient);
        WriteEntry(f, "ui_anim", ui_anim);
        WriteEntry(f, "ui_anim_speed", ui_anim_speed);
        WriteEntry(f, "ui_transparency", ui_transparency);
        WriteEntry(f, "ui_rounded_corners", ui_rounded_corners);
        WriteEntry(f, "ui_corner_radius", ui_corner_radius);
        WriteEntry(f, "ui_window_shadow", ui_window_shadow);
        WriteEntry(f, "ui_glow", ui_glow);
        WriteEntry(f, "ui_win_w", ui_win_w);
        WriteEntry(f, "ui_win_h", ui_win_h);
        WriteEntry(f, "ui_min_w", ui_min_w);
        WriteEntry(f, "ui_min_h", ui_min_h);
        WriteEntry(f, "ui_max_w", ui_max_w);
        WriteEntry(f, "ui_max_h", ui_max_h);
        WriteEntry(f, "ui_scale", ui_scale);
        WriteEntry(f, "ui_font_size", ui_font_size);
        WriteEntry(f, "ui_title_size", ui_title_size);
        WriteEntry(f, "ui_columns", ui_columns);
        WriteEntry(f, "ui_card_h", ui_card_h);
        WriteEntry(f, "ui_card_pad", ui_card_pad);
        WriteEntry(f, "ui_gutter", ui_gutter);
        WriteEntry(f, "ui_track_h", ui_track_h);
        WriteEntry(f, "ui_control_h", ui_control_h);
        WriteEntry(f, "ui_nav_w", ui_nav_w);
        WriteEntry(f, "ui_nav_row_h", ui_nav_row_h);
        WriteEntry(f, "ui_title_h", ui_title_h);
        WriteEntry(f, "ui_page_pad", ui_page_pad);
        WriteEntry(f, "ui_rail_mode", ui_rail_mode);
        WriteEntry(f, "ui_session_card", ui_session_card);
        WriteEntry(f, "ui_hero_cards", ui_hero_cards);
        WriteEntry(f, "ui_hints", ui_hints);
        WriteEntry(f, "ui_collapse_groups", ui_collapse_groups);
        WriteEntry(f, "ui_row_space", ui_row_space);
        WriteStr(f, "ui_closed_groups", ui_closed_groups);

        // waypoints
        for (int i = 0; i < kMaxWaypoints; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), "waypoint_%d", i);
            WriteEntry(f, key, waypoint_names[i]);
            std::snprintf(key, sizeof(key), "waypoint_%d_x", i);
            WriteEntry(f, key, waypoint_x[i]);
            std::snprintf(key, sizeof(key), "waypoint_%d_y", i);
            WriteEntry(f, key, waypoint_y[i]);
            std::snprintf(key, sizeof(key), "waypoint_%d_z", i);
            WriteEntry(f, key, waypoint_z[i]);
        }
        WriteEntry(f, "waypoint_n", waypoint_n);
    }

    void LoadFromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;

            // aimbot
            ReadLine(line, "aimbot_enabled", aimbot_enabled);
            ReadLine(line, "aimbot_aim_type", aimbot_aim_type);
            ReadLine(line, "aimbot_keybind", aimbot_keybind);
            ReadLine(line, "aimbot_hold_mode", aimbot_hold_mode);
            ReadLine(line, "aimbot_part", aimbot_part);
            ReadLine(line, "sticky_aim", sticky_aim);
            ReadLine(line, "prediction_enabled", prediction_enabled);
            ReadLine(line, "prediction_x", prediction_x);
            ReadLine(line, "prediction_y", prediction_y);
            ReadLine(line, "smoothing_x", smoothing_x);
            ReadLine(line, "smoothing_y", smoothing_y);
            ReadLine(line, "fov_size", fov_size);
            ReadLine(line, "show_fov", show_fov);
            ReadLine(line, "humanizer_enabled", humanizer_enabled);
            ReadLine(line, "humanizer_strength", humanizer_strength);

            // triggerbot
            ReadLine(line, "triggerbot_enabled", triggerbot_enabled);
            ReadLine(line, "triggerbot_keybind", triggerbot_keybind);
            ReadLine(line, "triggerbot_hold_mode", triggerbot_hold_mode);
            ReadLine(line, "triggerbot_part", triggerbot_part);
            ReadLine(line, "triggerbot_fov", triggerbot_fov);
            ReadLine(line, "triggerbot_delay_ms", triggerbot_delay_ms);
            ReadLine(line, "triggerbot_cooldown_ms", triggerbot_cooldown_ms);
            ReadLine(line, "triggerbot_max_dist", triggerbot_max_dist);
            ReadLine(line, "triggerbot_visible_check", triggerbot_visible_check);
            ReadLine(line, "triggerbot_chance", triggerbot_chance);

            // hitsound
            ReadLine(line, "hitsound_enabled", hitsound_enabled);
            ReadLine(line, "hitsound_volume", hitsound_volume);
            ReadLine(line, "hitsound_selected", hitsound_selected);

            // esp
            ReadLine(line, "esp_enabled", esp_enabled);
            ReadLine(line, "box_esp", box_esp);
            ReadLine(line, "box_esp_type", box_esp_type);
            ReadLine(line, "healthbar", healthbar);
            ReadLine(line, "health_text", health_text);
            ReadLine(line, "name", name);
            ReadLine(line, "distance", distance);
            ReadLine(line, "rig_type", rig_type);
            ReadLine(line, "tool_esp", tool_esp);
            ReadLine(line, "aimviewer", aimviewer);
            ReadLine(line, "skeleton_esp", skeleton_esp);
            ReadLine(line, "chinahat", chinahat);
            ReadLine(line, "render_expanded_hitbox", render_expanded_hitbox);
            ReadLine(line, "hitbox_expander_enabled", hitbox_expander_enabled);
            ReadLine(line, "hitbox_expander_value", hitbox_expander_value);
            ReadLine(line, "esp_render_distance", esp_render_distance);
            ReadLine(line, "chams_enabled", chams_enabled);
            ReadLine(line, "mesh_chams", mesh_chams);
            ReadLine(line, "box_fill", box_fill);
            ReadLine(line, "box_fill_gradient", box_fill_gradient);
            ReadLine(line, "box_fill_gradient_rotate", box_fill_gradient_rotate);
            ReadRGB(line, "box_esp_color", box_esp_color);
            ReadRGB(line, "name_color", name_color);
            ReadRGB(line, "distance_color", distance_color);
            ReadRGB(line, "health_text_color", health_text_color);
            ReadRGB(line, "rig_type_color", rig_type_color);
            ReadRGB(line, "tool_color", tool_color);
            ReadRGB(line, "skeleton_color", skeleton_color);
            ReadRGB(line, "chinahat_color", chinahat_color);
            ReadRGB(line, "chams_color", chams_color);
            ReadRGB(line, "mesh_chams_color", mesh_chams_color);
            ReadRGB(line, "box_fill_top", box_fill_top);
            ReadRGB(line, "box_fill_bottom", box_fill_bottom);
            ReadRGB(line, "healthbar_color", healthbar_color);
            ReadLine(line, "esp_line_width", esp_line_width);
            ReadLine(line, "esp_glow", esp_glow);
            ReadLine(line, "esp_corner_frac", esp_corner_frac);
            ReadLine(line, "esp_tracer", esp_tracer);
            ReadLine(line, "esp_tracer_origin", esp_tracer_origin);
            ReadLine(line, "esp_tracer_attach", esp_tracer_attach);
            ReadLine(line, "esp_tracer_width", esp_tracer_width);
            ReadLine(line, "esp_tracer_max_dist", esp_tracer_max_dist);
            ReadLine(line, "esp_tracer_box_color", esp_tracer_box_color);
            ReadRGB(line, "esp_tracer_color", esp_tracer_color);
            ReadLine(line, "esp_text_size", esp_text_size);
            ReadLine(line, "esp_name_plate", esp_name_plate);

            // movement
            ReadLine(line, "walkspeed_enabled", walkspeed_enabled);
            ReadLine(line, "walkspeed_keybind", walkspeed_keybind);
            ReadLine(line, "walkspeed_value", walkspeed_value);
            ReadLine(line, "flight_enabled", flight_enabled);
            ReadLine(line, "flight_keybind", flight_keybind);
            ReadLine(line, "flight_value", flight_value);
            ReadLine(line, "flight_hold_mode", flight_hold_mode);
            ReadLine(line, "flight_stealth", flight_stealth);
            ReadLine(line, "flight_max_speed", flight_max_speed);
            ReadLine(line, "flight_max_climb", flight_max_climb);
            ReadLine(line, "flight_max_descent", flight_max_descent);
            ReadLine(line, "flight_accel", flight_accel);
            ReadLine(line, "infinite_jump_enabled", infinite_jump_enabled);
            ReadLine(line, "infinite_jump_power", infinite_jump_power);
            ReadLine(line, "noclip_enabled", noclip_enabled);
            ReadLine(line, "noclip_keybind", noclip_keybind);
            ReadLine(line, "click_teleport_enabled", click_teleport_enabled);
            ReadLine(line, "click_teleport_keybind", click_teleport_keybind);
            ReadLine(line, "click_teleport_distance", click_teleport_distance);

            // physics
            ReadLine(line, "gravity_enabled", gravity_enabled);
            ReadLine(line, "gravity_keybind", gravity_keybind);
            ReadLine(line, "gravity_value", gravity_value);
            ReadLine(line, "hipheight_enabled", hipheight_enabled);
            ReadLine(line, "hipheight_keybind", hipheight_keybind);
            ReadLine(line, "hipheight_value", hipheight_value);
            ReadLine(line, "nofall_enabled", nofall_enabled);
            ReadLine(line, "nofall_keybind", nofall_keybind);
            ReadLine(line, "nofall_value", nofall_value);
            ReadLine(line, "fling_enabled", fling_enabled);
            ReadLine(line, "fling_keybind", fling_keybind);
            ReadLine(line, "fling_power", fling_power);
            ReadLine(line, "jumppower_enabled", jumppower_enabled);
            ReadLine(line, "jumppower_keybind", jumppower_keybind);
            ReadLine(line, "jumppower_value", jumppower_value);
            ReadLine(line, "antiafk_enabled", antiafk_enabled);
            ReadLine(line, "antiafk_interval", antiafk_interval);

            // camera
            ReadLine(line, "fov_changer_enabled", fov_changer_enabled);
            ReadLine(line, "fov_value", fov_value);

            // visuals extras
            ReadLine(line, "headdot_enabled", headdot_enabled);
            ReadRGB(line, "headdot_color", headdot_color);
            ReadLine(line, "headdot_fill", headdot_fill);
            ReadLine(line, "headdot_outline", headdot_outline);
            ReadRGB(line, "headdot_outline_color", headdot_outline_color);
            ReadLine(line, "headdot_thickness", headdot_thickness);
            ReadLine(line, "oof_enabled", oof_enabled);
            ReadRGB(line, "oof_color", oof_color);
            ReadLine(line, "oof_outline", oof_outline);
            ReadRGB(line, "oof_outline_color", oof_outline_color);
            ReadLine(line, "oof_size", oof_size);
            ReadLine(line, "oof_arrow_style", oof_arrow_style);
            ReadLine(line, "oof_names", oof_names);
            ReadLine(line, "oof_distance", oof_distance);
            ReadLine(line, "oof_radius", oof_radius);

            // world
            ReadLine(line, "fullbright_enabled", fullbright_enabled);
            ReadLine(line, "fullbright_value", fullbright_value);
            ReadLine(line, "world_ambient_enabled", world_ambient_enabled);
            ReadRGB(line, "world_ambient_color", world_ambient_color);
            ReadLine(line, "world_fog_enabled", world_fog_enabled);
            ReadLine(line, "world_fog_start", world_fog_start);
            ReadLine(line, "world_fog_end", world_fog_end);
            ReadRGB(line, "world_fog_color", world_fog_color);
            ReadLine(line, "world_exposure_enabled", world_exposure_enabled);
            ReadLine(line, "world_exposure_value", world_exposure_value);
            ReadLine(line, "noshadow_enabled", noshadow_enabled);
            ReadLine(line, "nograss_enabled", nograss_enabled);
            ReadLine(line, "night_enabled", night_enabled);
            ReadLine(line, "night_level", night_level);
            ReadLine(line, "world_bots_enabled", world_bots_enabled);
            ReadLine(line, "world_animals_enabled", world_animals_enabled);
            ReadLine(line, "world_esp_distance", world_esp_distance);
            ReadLine(line, "world_esp_labels", world_esp_labels);
            ReadLine(line, "world_time_enabled", world_time_enabled);
            ReadLine(line, "world_time_value", world_time_value);
            ReadLine(line, "skybox_changer_enabled", skybox_changer_enabled);
            ReadLine(line, "skybox_type", skybox_type);

            // players / social
            ReadLine(line, "blacklist_hides_esp", blacklist_hides_esp);
            ReadTags(line, "player_blacklist", player_blacklist, player_blacklist_n);
            ReadTags(line, "player_whitelist", player_whitelist, player_whitelist_n);
            ReadRGB(line, "whitelist_esp_color", whitelist_esp_color);
            ReadRGB(line, "team_esp_color", team_esp_color);
            ReadRGB(line, "friends_esp_color", friends_esp_color);

            // misc
            ReadLine(line, "show_fps", show_fps);
            ReadLine(line, "fps_cap_enabled", fps_cap_enabled);
            ReadLine(line, "fps_cap_value", fps_cap_value);
            ReadLine(line, "overlay_fps_cap", overlay_fps_cap);
            ReadLine(line, "overlay_idle_fps", overlay_idle_fps);
            ReadLine(line, "freecam_enabled", freecam_enabled);
            ReadLine(line, "freecam_keybind", freecam_keybind);
            ReadLine(line, "freecam_speed", freecam_speed);
            ReadLine(line, "show_waypoints", show_waypoints);
            ReadLine(line, "show_now_playing", show_now_playing);
            ReadLine(line, "menu_toggle_keybind", menu_toggle_keybind);
            ReadLine(line, "dead_check", dead_check);
            ReadLine(line, "team_check", team_check);
            ReadLine(line, "wall_check", wall_check);
            ReadLine(line, "streamproof_enabled", streamproof_enabled);
            ReadStr(line, "streamproof_apps", streamproof_apps, sizeof(streamproof_apps));

            // theme / UI
            ReadLine(line, "ui_theme", ui_theme);
            ReadLine(line, "ui_contrast", ui_contrast);
            ReadRGB(line, "ui_tint", ui_tint);
            ReadLine(line, "ui_tint_amount", ui_tint_amount);
            ReadRGB(line, "ui_accent_color", ui_accent_color);
            ReadLine(line, "ui_accent2_auto", ui_accent2_auto);
            ReadRGB(line, "ui_accent2_color", ui_accent2_color);
            ReadLine(line, "ui_rainbow", ui_rainbow);
            ReadLine(line, "ui_rainbow_speed", ui_rainbow_speed);
            ReadLine(line, "ui_rainbow_sat", ui_rainbow_sat);
            ReadLine(line, "ui_rainbow_span", ui_rainbow_span);
            ReadLine(line, "ui_polish", ui_polish);
            ReadLine(line, "ui_mat_sheen", ui_mat_sheen);
            ReadLine(line, "ui_mat_edge", ui_mat_edge);
            ReadLine(line, "ui_mat_bloom", ui_mat_bloom);
            ReadLine(line, "ui_mat_chroma", ui_mat_chroma);
            ReadLine(line, "ui_mat_frost", ui_mat_frost);
            ReadLine(line, "ui_mat_sweep", ui_mat_sweep);
            ReadLine(line, "ui_mat_aurora", ui_mat_aurora);
            ReadLine(line, "ui_mat_shimmer", ui_mat_shimmer);
            ReadLine(line, "ui_mat_lift", ui_mat_lift);
            ReadLine(line, "ui_mat_gradient", ui_mat_gradient);
            ReadLine(line, "ui_anim", ui_anim);
            ReadLine(line, "ui_anim_speed", ui_anim_speed);
            ReadLine(line, "ui_transparency", ui_transparency);
            ReadLine(line, "ui_rounded_corners", ui_rounded_corners);
            ReadLine(line, "ui_corner_radius", ui_corner_radius);
            ReadLine(line, "ui_window_shadow", ui_window_shadow);
            ReadLine(line, "ui_glow", ui_glow);
            ReadLine(line, "ui_win_w", ui_win_w);
            ReadLine(line, "ui_win_h", ui_win_h);
            ReadLine(line, "ui_min_w", ui_min_w);
            ReadLine(line, "ui_min_h", ui_min_h);
            ReadLine(line, "ui_max_w", ui_max_w);
            ReadLine(line, "ui_max_h", ui_max_h);
            ReadLine(line, "ui_scale", ui_scale);
            ReadLine(line, "ui_font_size", ui_font_size);
            ReadLine(line, "ui_title_size", ui_title_size);
            ReadLine(line, "ui_columns", ui_columns);
            ReadLine(line, "ui_card_h", ui_card_h);
            ReadLine(line, "ui_card_pad", ui_card_pad);
            ReadLine(line, "ui_gutter", ui_gutter);
            ReadLine(line, "ui_track_h", ui_track_h);
            ReadLine(line, "ui_control_h", ui_control_h);
            ReadLine(line, "ui_nav_w", ui_nav_w);
            ReadLine(line, "ui_nav_row_h", ui_nav_row_h);
            ReadLine(line, "ui_title_h", ui_title_h);
            ReadLine(line, "ui_page_pad", ui_page_pad);
            ReadLine(line, "ui_rail_mode", ui_rail_mode);
            ReadLine(line, "ui_session_card", ui_session_card);
            ReadLine(line, "ui_hero_cards", ui_hero_cards);
            ReadLine(line, "ui_hints", ui_hints);
            ReadLine(line, "ui_collapse_groups", ui_collapse_groups);
            ReadLine(line, "ui_row_space", ui_row_space);
            ReadStr(line, "ui_closed_groups", ui_closed_groups, sizeof(ui_closed_groups));

            // waypoints
            for (int i = 0; i < kMaxWaypoints; ++i) {
                char key[32];
                std::snprintf(key, sizeof(key), "waypoint_%d", i);
                ReadStr(line, key, waypoint_names[i], kWaypointNameLen);
                std::snprintf(key, sizeof(key), "waypoint_%d_x", i);
                ReadLine(line, key, waypoint_x[i]);
                std::snprintf(key, sizeof(key), "waypoint_%d_y", i);
                ReadLine(line, key, waypoint_y[i]);
                std::snprintf(key, sizeof(key), "waypoint_%d_z", i);
                ReadLine(line, key, waypoint_z[i]);
            }
            ReadLine(line, "waypoint_n", waypoint_n);
        }
    }

    std::vector<std::string> GetConfigList() {
        std::vector<std::string> out;
        std::string dir = GetConfigDir();
        std::filesystem::create_directories(dir);
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".cfg") {
                std::string name = entry.path().stem().string();
                if (!name.empty()) out.push_back(name);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool Save(const char* name) {
        if (!name || !name[0]) return false;
        std::string path = GetConfigDir() + "/" + name + ".cfg";
        SaveToFile(path);
        return true;
    }

    bool Load(const char* name) {
        if (!name || !name[0]) return false;
        std::string path = GetConfigDir() + "/" + name + ".cfg";
        LoadFromFile(path);
        return true;
    }

    bool Delete(const char* name) {
        if (!name || !name[0]) return false;
        std::string path = GetConfigDir() + "/" + name + ".cfg";
        return std::filesystem::remove(path);
    }

    bool Rename(const char* from, const char* to) {
        if (!from || !from[0] || !to || !to[0]) return false;
        std::string old_path = GetConfigDir() + "/" + from + ".cfg";
        std::string new_path = GetConfigDir() + "/" + to + ".cfg";
        if (!std::filesystem::exists(old_path)) return false;
        std::error_code ec;
        std::filesystem::rename(old_path, new_path, ec);
        return !ec;
    }

    void OpenConfigFolder() {
        std::string dir = GetConfigDir();
        std::filesystem::create_directories(dir);
        std::string cmd = "xdg-open '" + dir + "' >/dev/null 2>&1 &";
        std::system(cmd.c_str());
    }

} // namespace config