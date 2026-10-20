#include "config.h"
#include "../../globals.h"
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace config {

    std::string GetConfigDir() {
        return "C:\\config";
    }

    void EnsureConfigDir() {
        std::string dir = GetConfigDir();
        CreateDirectoryA(dir.c_str(), nullptr);
    }

    static std::string BoolStr(bool v) { return v ? "1" : "0"; }
    static std::string FloatStr(float v) {
        char buf[64];
        sprintf_s(buf, "%f", v);
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
    // Three-float colours: the globals that the menu's pickers write are RGB (the
    // alpha of an accent has no meaning), so the file keeps three components. Going
    // through the float[4] helper instead would have written a bogus fourth value
    // that ReadLine would then happily shift back into alpha-less memory.
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
    // One free-text value (the closed-group list). The reader copies at most
    // capacity-1 bytes and always terminates, so a hand-written 4MB line in a cfg
    // cannot walk past the buffer on load.
    static void WriteStr(std::ofstream& f, const char* key, const char* v) {
        f << key << "=" << (v ? v : "") << "\n";
    }
    static bool ReadStr(const std::string& line, const char* key, char* out, size_t cap) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        snprintf(out, cap, "%s", line.substr(prefix.size()).c_str());
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
            snprintf(tags[n], kPlayerTagLen, "%s", token.c_str());
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
    static bool ReadLine(const std::string& line, const char* key, float out[4]) {
        std::string prefix = std::string(key) + "=";
        if (line.find(prefix) != 0) return false;
        std::string val = line.substr(prefix.size());
        std::stringstream ss(val);
        std::string token;
        for (int i = 0; i < 4 && std::getline(ss, token, ','); ++i) {
            out[i] = ParseFloat(token);
        }
        return true;
    }

    bool Save(const std::string& config_name) {
        if (config_name.empty()) return false;
        EnsureConfigDir();
        std::string path = GetConfigDir() + "\\" + config_name + ".cfg";
        std::ofstream f(path);
        if (!f.is_open()) return false;

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
        WriteEntry(f, "humanizer_enabled", humanizer_enabled);
        WriteEntry(f, "humanizer_strength", humanizer_strength);
        WriteEntry(f, "fov_size", fov_size);
        WriteEntry(f, "show_fov", show_fov);

        // esp
        WriteEntry(f, "esp_enabled", esp_enabled);
        WriteEntry(f, "box_esp", box_esp);
        WriteEntry(f, "box_esp_type", box_esp_type);
        WriteEntry(f, "healthbar", healthbar);
        WriteEntry(f, "health_text", health_text);
        WriteEntry(f, "name_esp", name);
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
        WriteEntry(f, "box_fill", box_fill);
        WriteEntry(f, "box_fill_gradient", box_fill_gradient);
        WriteEntry(f, "box_fill_gradient_rotate", box_fill_gradient_rotate);
        WriteEntry(f, "team_check", team_check);
        WriteEntry(f, "wall_check", wall_check);
        WriteEntry(f, "dead_check", dead_check);

        // colors
        WriteEntry(f, "box_esp_color", box_esp_color);
        WriteEntry(f, "name_color", name_color);
        WriteEntry(f, "distance_color", distance_color);
        WriteEntry(f, "health_text_color", health_text_color);
        WriteEntry(f, "rig_type_color", rig_type_color);
        WriteEntry(f, "tool_color", tool_color);
        WriteEntry(f, "skeleton_color", skeleton_color);
        WriteEntry(f, "chinahat_color", chinahat_color);
        WriteEntry(f, "chams_color", chams_color);
        WriteEntry(f, "box_fill_top", box_fill_top);
        WriteEntry(f, "box_fill_bottom", box_fill_bottom);
        WriteEntry(f, "healthbar_color", healthbar_color);
        // ESP strokes and tracers. The width/glow/outline trio is saved as three
        // numbers rather than one "style" index because they are dialled in
        // separately on real maps - the outline is on for a bright skybox and off for
        // a dark cave, and the glow is the one thing people turn down at high
        // player counts.
        WriteEntry(f, "esp_line_width", esp_line_width);
        WriteEntry(f, "esp_glow", esp_glow);
        WriteEntry(f, "esp_outline", esp_outline);
        WriteEntry(f, "esp_corner_frac", esp_corner_frac);
        WriteEntry(f, "esp_tracer", esp_tracer);
        WriteEntry(f, "esp_tracer_origin", esp_tracer_origin);
        WriteEntry(f, "esp_tracer_attach", esp_tracer_attach);
        WriteEntry(f, "esp_tracer_width", esp_tracer_width);
        WriteEntry(f, "esp_tracer_max_dist", esp_tracer_max_dist);
        WriteEntry(f, "esp_tracer_box_color", esp_tracer_box_color);
        WriteEntry(f, "esp_tracer_color", esp_tracer_color);
        WriteEntry(f, "esp_text_size", esp_text_size);
        WriteEntry(f, "esp_name_plate", esp_name_plate);
        WriteEntry(f, "whitelist_esp_color", whitelist_esp_color);
        WriteEntry(f, "team_esp_color", team_esp_color);
        WriteEntry(f, "show_fps", show_fps);
        WriteEntry(f, "fps_pos_x", fps_pos_x);
        WriteEntry(f, "fps_pos_y", fps_pos_y);
        WriteEntry(f, "freecam_enabled", freecam_enabled);
        WriteEntry(f, "freecam_keybind", freecam_keybind);
        WriteEntry(f, "freecam_speed", freecam_speed);
        WriteEntry(f, "show_waypoints", show_waypoints);
        WriteEntry(f, "blacklist_hides_esp", blacklist_hides_esp);
        WriteTags(f, "player_blacklist", player_blacklist, player_blacklist_n);
        WriteTags(f, "player_whitelist", player_whitelist, player_whitelist_n);

        // misc
        WriteEntry(f, "walkspeed_enabled", walkspeed_enabled);
        WriteEntry(f, "walkspeed_keybind", walkspeed_keybind);
        WriteEntry(f, "walkspeed_value", walkspeed_value);
        WriteEntry(f, "flight_enabled", flight_enabled);
        WriteEntry(f, "flight_keybind", flight_keybind);
        WriteEntry(f, "flight_value", flight_value);
        WriteEntry(f, "click_teleport_enabled", click_teleport_enabled);
        WriteEntry(f, "click_teleport_keybind", click_teleport_keybind);
        WriteEntry(f, "click_teleport_distance", click_teleport_distance);
        WriteEntry(f, "infinite_jump_enabled", infinite_jump_enabled);
        WriteEntry(f, "infinite_jump_power", infinite_jump_power);
        WriteEntry(f, "fov_changer_enabled", fov_changer_enabled);
        WriteEntry(f, "fov_value", fov_value);
        // ---- UI theme, material and layout -------------------------------------
        // The whole ui_* block round-trips, because a customised menu you have to
        // re-customise after every restart is a menu you stop customising. The list is
        // grouped the way globals.h groups it so a missing line is obvious to anyone
        // diffing the two files.
        //
        // ui_rainbow kept its old key and its old 0/1 values on purpose: an existing
        // "ui_rainbow=1" now reads as mode 1 (cycle), which is what the bool meant.
        // The cycle itself is NOT stored -- the accent globals hold what the user
        // picked, and the animated hue is derived from the clock at render time.
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
        WriteEntry(f, "ui_row_space", ui_row_space);
        WriteEntry(f, "ui_rail_mode", ui_rail_mode);
        WriteEntry(f, "ui_session_card", ui_session_card);
        WriteEntry(f, "ui_hero_cards", ui_hero_cards);
        WriteEntry(f, "ui_hints", ui_hints);
        WriteEntry(f, "ui_collapse_groups", ui_collapse_groups);
        WriteStr(f, "ui_closed_groups", ui_closed_groups);
        WriteEntry(f, "flight_hold_mode", flight_hold_mode);
        WriteEntry(f, "inventory_checker_enabled", inventory_checker_enabled);
        WriteEntry(f, "inventory_checker_keybind", inventory_checker_keybind);
        WriteEntry(f, "menu_toggle_keybind", menu_toggle_keybind);
        WriteEntry(f, "noclip_enabled", noclip_enabled);
        WriteEntry(f, "noclip_keybind", noclip_keybind);
        // The players tab's three numbers. Deliberately WITHOUT spectate_enabled /
        // spectate_user_id: "who I am watching right now" is a moment, not a
        // preference, and a config that restored a spectate camera on load would
        // hand back a camera it never parked.
        WriteEntry(f, "spectate_distance", spectate_distance);
        WriteEntry(f, "spectate_height", spectate_height);
        WriteEntry(f, "tp_land_height", tp_land_height);


        // misc features
        WriteEntry(f, "skybox_changer_enabled", skybox_changer_enabled);
        WriteEntry(f, "skybox_type", skybox_type);

        // memory chams

        f.close();
        return true;
    }

    bool Load(const std::string& config_name) {
        if (config_name.empty()) return false;
        std::string path = GetConfigDir() + "\\" + config_name + ".cfg";
        std::ifstream f(path);
        if (!f.is_open()) return false;

        std::string line;
        while (std::getline(f, line)) {
            // aimbot
            if (ReadLine(line, "aimbot_enabled", aimbot_enabled)) continue;
            if (ReadLine(line, "aimbot_aim_type", aimbot_aim_type)) continue;
            if (ReadLine(line, "aimbot_keybind", aimbot_keybind)) continue;
            if (ReadLine(line, "aimbot_hold_mode", aimbot_hold_mode)) continue;
            if (ReadLine(line, "aimbot_part", aimbot_part)) continue;
            if (ReadLine(line, "sticky_aim", sticky_aim)) continue;
            if (ReadLine(line, "prediction_enabled", prediction_enabled)) continue;
            if (ReadLine(line, "prediction_x", prediction_x)) continue;
            if (ReadLine(line, "prediction_y", prediction_y)) continue;
            if (ReadLine(line, "smoothing_x", smoothing_x)) continue;
            if (ReadLine(line, "smoothing_y", smoothing_y)) continue;
            if (ReadLine(line, "humanizer_enabled", humanizer_enabled)) continue;
            if (ReadLine(line, "humanizer_strength", humanizer_strength)) continue;
            if (ReadLine(line, "fov_size", fov_size)) continue;
            if (ReadLine(line, "show_fov", show_fov)) continue;

            // esp
            if (ReadLine(line, "esp_enabled", esp_enabled)) continue;
            if (ReadLine(line, "box_esp", box_esp)) continue;
            if (ReadLine(line, "box_esp_type", box_esp_type)) continue;
            if (ReadLine(line, "healthbar", healthbar)) continue;
            if (ReadLine(line, "health_text", health_text)) continue;
            if (ReadLine(line, "name_esp", name)) continue;
            if (ReadLine(line, "rig_type", rig_type)) continue;
            if (ReadLine(line, "tool_esp", tool_esp)) continue;
            if (ReadLine(line, "aimviewer", aimviewer)) continue;
            if (ReadLine(line, "skeleton_esp", skeleton_esp)) continue;
            if (ReadLine(line, "chinahat", chinahat)) continue;
            if (ReadLine(line, "render_expanded_hitbox", render_expanded_hitbox)) continue;
            if (ReadLine(line, "hitbox_expander_enabled", hitbox_expander_enabled)) continue;
            if (ReadLine(line, "hitbox_expander_value", hitbox_expander_value)) continue;
            if (ReadLine(line, "esp_render_distance", esp_render_distance)) continue;
            if (ReadLine(line, "chams_enabled", chams_enabled)) continue;
            if (ReadLine(line, "box_fill", box_fill)) continue;
            if (ReadLine(line, "box_fill_gradient", box_fill_gradient)) continue;
            if (ReadLine(line, "box_fill_gradient_rotate", box_fill_gradient_rotate)) continue;
            if (ReadLine(line, "team_check", team_check)) continue;
            if (ReadLine(line, "wall_check", wall_check)) continue;
            if (ReadLine(line, "dead_check", dead_check)) continue;

            // colors
            if (ReadLine(line, "box_esp_color", box_esp_color)) continue;
            if (ReadLine(line, "name_color", name_color)) continue;
            // `distance` (the ESP distance toggle) was written by Save() and never
            // read back, so a config could not turn it on or off. Key spelled the
            // same way on both sides here and in the writer above.
            if (ReadLine(line, "distance", distance)) continue;
            if (ReadLine(line, "distance_color", distance_color)) continue;
            if (ReadLine(line, "health_text_color", health_text_color)) continue;
            if (ReadLine(line, "rig_type_color", rig_type_color)) continue;
            if (ReadLine(line, "tool_color", tool_color)) continue;
            if (ReadLine(line, "skeleton_color", skeleton_color)) continue;
            if (ReadLine(line, "chinahat_color", chinahat_color)) continue;
            if (ReadLine(line, "chams_color", chams_color)) continue;
            if (ReadLine(line, "box_fill_top", box_fill_top)) continue;
            if (ReadLine(line, "box_fill_bottom", box_fill_bottom)) continue;
            if (ReadLine(line, "healthbar_color", healthbar_color)) continue;
            if (ReadLine(line, "esp_line_width", esp_line_width)) continue;
            if (ReadLine(line, "esp_glow", esp_glow)) continue;
            if (ReadLine(line, "esp_outline", esp_outline)) continue;
            if (ReadLine(line, "esp_corner_frac", esp_corner_frac)) continue;
            if (ReadLine(line, "esp_tracer", esp_tracer)) continue;
            if (ReadLine(line, "esp_tracer_origin", esp_tracer_origin)) continue;
            if (ReadLine(line, "esp_tracer_attach", esp_tracer_attach)) continue;
            if (ReadLine(line, "esp_tracer_width", esp_tracer_width)) continue;
            if (ReadLine(line, "esp_tracer_max_dist", esp_tracer_max_dist)) continue;
            if (ReadLine(line, "esp_tracer_box_color", esp_tracer_box_color)) continue;
            if (ReadLine(line, "esp_tracer_color", esp_tracer_color)) continue;
            if (ReadLine(line, "esp_text_size", esp_text_size)) continue;
            if (ReadLine(line, "esp_name_plate", esp_name_plate)) continue;
            if (ReadLine(line, "whitelist_esp_color", whitelist_esp_color)) continue;
            if (ReadLine(line, "team_esp_color", team_esp_color)) continue;
            if (ReadLine(line, "show_fps", show_fps)) continue;
            if (ReadLine(line, "fps_pos_x", fps_pos_x)) continue;
            if (ReadLine(line, "fps_pos_y", fps_pos_y)) continue;
            if (ReadLine(line, "freecam_enabled", freecam_enabled)) continue;
            if (ReadLine(line, "freecam_keybind", freecam_keybind)) continue;
            if (ReadLine(line, "freecam_speed", freecam_speed)) continue;
            if (ReadLine(line, "show_waypoints", show_waypoints)) continue;
            if (ReadLine(line, "blacklist_hides_esp", blacklist_hides_esp)) continue;
            if (ReadTags(line, "player_blacklist", player_blacklist, player_blacklist_n)) continue;
            if (ReadTags(line, "player_whitelist", player_whitelist, player_whitelist_n)) continue;

            // misc
            if (ReadLine(line, "walkspeed_enabled", walkspeed_enabled)) continue;
            if (ReadLine(line, "walkspeed_keybind", walkspeed_keybind)) continue;
            if (ReadLine(line, "walkspeed_value", walkspeed_value)) continue;
            if (ReadLine(line, "flight_enabled", flight_enabled)) continue;
            if (ReadLine(line, "flight_keybind", flight_keybind)) continue;
            if (ReadLine(line, "flight_value", flight_value)) continue;
            if (ReadLine(line, "click_teleport_enabled", click_teleport_enabled)) continue;
            if (ReadLine(line, "click_teleport_keybind", click_teleport_keybind)) continue;
            if (ReadLine(line, "click_teleport_distance", click_teleport_distance)) continue;
            if (ReadLine(line, "infinite_jump_enabled", infinite_jump_enabled)) continue;
            if (ReadLine(line, "infinite_jump_power", infinite_jump_power)) continue;
            if (ReadLine(line, "fov_changer_enabled", fov_changer_enabled)) continue;
            if (ReadLine(line, "fov_value", fov_value)) continue;
            // ---- UI theme, material and layout: the mirror of the block above ------
            // Every key is optional, as everywhere in this file: a cfg from an older
            // build loads with the new defaults rather than with zeros, which is the
            // difference between "your accent survived the update" and "your menu is
            // black on black".
            if (ReadLine(line, "ui_theme", ui_theme)) continue;
            if (ReadLine(line, "ui_contrast", ui_contrast)) continue;
            if (ReadRGB(line, "ui_tint", ui_tint)) continue;
            if (ReadLine(line, "ui_tint_amount", ui_tint_amount)) continue;
            if (ReadRGB(line, "ui_accent_color", ui_accent_color)) continue;
            if (ReadLine(line, "ui_accent2_auto", ui_accent2_auto)) continue;
            if (ReadRGB(line, "ui_accent2_color", ui_accent2_color)) continue;
            if (ReadLine(line, "ui_rainbow", ui_rainbow)) continue;
            if (ReadLine(line, "ui_rainbow_speed", ui_rainbow_speed)) continue;
            if (ReadLine(line, "ui_rainbow_sat", ui_rainbow_sat)) continue;
            if (ReadLine(line, "ui_rainbow_span", ui_rainbow_span)) continue;
            if (ReadLine(line, "ui_polish", ui_polish)) continue;
            if (ReadLine(line, "ui_mat_sheen", ui_mat_sheen)) continue;
            if (ReadLine(line, "ui_mat_edge", ui_mat_edge)) continue;
            if (ReadLine(line, "ui_mat_bloom", ui_mat_bloom)) continue;
            if (ReadLine(line, "ui_mat_chroma", ui_mat_chroma)) continue;
            if (ReadLine(line, "ui_mat_frost", ui_mat_frost)) continue;
            if (ReadLine(line, "ui_mat_sweep", ui_mat_sweep)) continue;
            if (ReadLine(line, "ui_mat_aurora", ui_mat_aurora)) continue;
            if (ReadLine(line, "ui_mat_shimmer", ui_mat_shimmer)) continue;
            if (ReadLine(line, "ui_mat_lift", ui_mat_lift)) continue;
            if (ReadLine(line, "ui_mat_gradient", ui_mat_gradient)) continue;
            if (ReadLine(line, "ui_anim", ui_anim)) continue;
            if (ReadLine(line, "ui_anim_speed", ui_anim_speed)) continue;
            if (ReadLine(line, "ui_transparency", ui_transparency)) continue;
            if (ReadLine(line, "ui_rounded_corners", ui_rounded_corners)) continue;
            if (ReadLine(line, "ui_corner_radius", ui_corner_radius)) continue;
            if (ReadLine(line, "ui_window_shadow", ui_window_shadow)) continue;
            if (ReadLine(line, "ui_glow", ui_glow)) continue;
            if (ReadLine(line, "ui_win_w", ui_win_w)) continue;
            if (ReadLine(line, "ui_win_h", ui_win_h)) continue;
            if (ReadLine(line, "ui_min_w", ui_min_w)) continue;
            if (ReadLine(line, "ui_min_h", ui_min_h)) continue;
            if (ReadLine(line, "ui_max_w", ui_max_w)) continue;
            if (ReadLine(line, "ui_max_h", ui_max_h)) continue;
            if (ReadLine(line, "ui_scale", ui_scale)) continue;
            if (ReadLine(line, "ui_font_size", ui_font_size)) continue;
            if (ReadLine(line, "ui_title_size", ui_title_size)) continue;
            if (ReadLine(line, "ui_columns", ui_columns)) continue;
            if (ReadLine(line, "ui_card_h", ui_card_h)) continue;
            if (ReadLine(line, "ui_card_pad", ui_card_pad)) continue;
            if (ReadLine(line, "ui_gutter", ui_gutter)) continue;
            if (ReadLine(line, "ui_track_h", ui_track_h)) continue;
            if (ReadLine(line, "ui_control_h", ui_control_h)) continue;
            if (ReadLine(line, "ui_nav_w", ui_nav_w)) continue;
            if (ReadLine(line, "ui_nav_row_h", ui_nav_row_h)) continue;
            if (ReadLine(line, "ui_title_h", ui_title_h)) continue;
            if (ReadLine(line, "ui_page_pad", ui_page_pad)) continue;
            if (ReadLine(line, "ui_row_space", ui_row_space)) continue;
            if (ReadLine(line, "ui_rail_mode", ui_rail_mode)) continue;
            if (ReadLine(line, "ui_session_card", ui_session_card)) continue;
            if (ReadLine(line, "ui_hero_cards", ui_hero_cards)) continue;
            if (ReadLine(line, "ui_hints", ui_hints)) continue;
            if (ReadLine(line, "ui_collapse_groups", ui_collapse_groups)) continue;
            if (ReadStr(line, "ui_closed_groups", ui_closed_groups, sizeof(ui_closed_groups))) continue;
            if (ReadLine(line, "flight_hold_mode", flight_hold_mode)) continue;
            if (ReadLine(line, "inventory_checker_enabled", inventory_checker_enabled)) continue;
            if (ReadLine(line, "inventory_checker_keybind", inventory_checker_keybind)) continue;
            if (ReadLine(line, "menu_toggle_keybind", menu_toggle_keybind)) continue;
            if (ReadLine(line, "noclip_enabled", noclip_enabled)) continue;
            if (ReadLine(line, "noclip_keybind", noclip_keybind)) continue;
            if (ReadLine(line, "spectate_distance", spectate_distance)) continue;
            if (ReadLine(line, "spectate_height", spectate_height)) continue;
            if (ReadLine(line, "tp_land_height", tp_land_height)) continue;


            // misc features
            if (ReadLine(line, "skybox_changer_enabled", skybox_changer_enabled)) continue;
            if (ReadLine(line, "skybox_type", skybox_type)) continue;

            // memory chams
        }

        f.close();
        return true;
    }

    bool Delete(const std::string& config_name) {
        if (config_name.empty()) return false;
        std::string path = GetConfigDir() + "\\" + config_name + ".cfg";
        return DeleteFileA(path.c_str()) != 0;
    }

    bool Rename(const std::string& old_config_name, const std::string& new_config_name) {
        if (old_config_name.empty() || new_config_name.empty()) return false;
        std::string old_path = GetConfigDir() + "\\" + old_config_name + ".cfg";
        std::string new_path = GetConfigDir() + "\\" + new_config_name + ".cfg";
        return MoveFileA(old_path.c_str(), new_path.c_str()) != 0;
    }

    std::vector<std::string> GetConfigList() {
        std::vector<std::string> list;
        EnsureConfigDir();
        std::string dir = GetConfigDir() + "\\*.cfg";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(dir.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return list;
        do {
            std::string fname = fd.cFileName;
            // strip .cfg extension
            if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".cfg") {
                list.push_back(fname.substr(0, fname.size() - 4));
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
        std::sort(list.begin(), list.end());
        return list;
    }

    void OpenConfigFolder() {
        EnsureConfigDir();
        std::string dir = GetConfigDir();
        ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
}