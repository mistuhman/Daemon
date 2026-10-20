// -----------------------------------------------------------------------------
//  ui/menu_ui.cpp  --  Daemon menu UI implementation (Linux only)
// -----------------------------------------------------------------------------
//  Minimal structural implementation. All visual design is deferred to the user.
//  This provides the API required by the rest of the codebase.
// -----------------------------------------------------------------------------
#include "ui/menu_ui.h"

#include "features/servers/servers.h"
#include "features/servers/servers_json.h"
#include "globals.h"

#include <cstdio>
#include <cstring>

namespace obs = obsidian;

namespace menuui {
namespace {

Actions g_act;
int*    s_wait_slot = nullptr;

char     s_cfg_name[128]   = "";
char     s_cfg_rename[128] = "";
std::vector<std::string> s_configs;
int      s_cfg_sel         = -1;
bool     s_configs_fetched = false;

int      s_player_sel = -1;

void RefreshConfigs()
{
    s_configs = g_act.list_configs ? g_act.list_configs() : std::vector<std::string>();
    s_configs_fetched = true;
}

const char* const kPages[] = { "aimbot", "visuals", "movement", "world", "players",
                               "keybinds", "theme", "config", "debug", "dex" };
constexpr int kPageCount = (int)(sizeof(kPages) / sizeof(kPages[0]));
int s_page = 0;

struct FeatureDef
{
    const char* id;
    const char* label;
    int         page;
    void      (*draw)(Snapshot&);
    const char* legacy;
};

void DrawAimbot(Snapshot&);
void DrawTriggerbot(Snapshot&);
void DrawHitbox(Snapshot&);
void DrawEsp(Snapshot&);
void DrawTracers(Snapshot&);
void DrawOffScreen(Snapshot&);
void DrawChams(Snapshot&);
void DrawNpcEsp(Snapshot&);
void DrawFrameRate(Snapshot&);
void DrawCamera(Snapshot&);
void DrawMovement(Snapshot&);
void DrawPhysics(Snapshot&);
void DrawKeepAlive(Snapshot&);
void DrawTeleport(Snapshot&);
void DrawLighting(Snapshot&);
void DrawServers(Snapshot&);
void DrawSkybox(Snapshot&);
void DrawWaypoints(Snapshot&);
void DrawPlayers(Snapshot&);
void DrawConfigs(Snapshot&);
void DrawDebug(Snapshot&);
void DrawExplorer(Snapshot&);

const FeatureDef kFeatures[] = {
  { "aimbot",      "aimbot",       0, DrawAimbot,
    "aimbot/targeting;aimbot/feel;aimbot/filters;aimbot/bind;keybinds/aimbot" },
  { "triggerbot",  "triggerbot",   0, DrawTriggerbot, "aimbot/triggerbot" },
  { "hitbox",      "hitbox",       0, DrawHitbox,     "aimbot/hitbox" },
  { "esp",         "esp",          1, DrawEsp,
    "visuals/boxes;visuals/strokes;visuals/labels;visuals/extras;visuals/shared with aimbot" },
  { "tracers",     "tracers",      1, DrawTracers,    "visuals/tracers" },
  { "off-screen",  "off-screen",   1, DrawOffScreen,  "visuals/off-screen" },
  { "chams",       "chams",        1, DrawChams,      "visuals/chams" },
  { "npc esp",     "npc esp",      1, DrawNpcEsp,
    "visuals/npc esp;world/world objects" },
  { "frame rate",  "frame rate",   1, DrawFrameRate,  "visuals/frame rate" },
  { "camera",      "camera",       1, DrawCamera,
    "visuals/camera;keybinds/visuals" },
  { "movement",    "movement",     2, DrawMovement,   "movement/movement;keybinds/movement" },
  { "physics",     "physics",      2, DrawPhysics,    "movement/physics;keybinds/movement" },
  { "keep alive",  "keep alive",   2, DrawKeepAlive,  "movement/keep alive" },
  { "teleport",    "teleport",     2, DrawTeleport,   "movement/teleport;keybinds/movement" },
  { "lighting",    "lighting",     3, DrawLighting,   "world/lighting" },
  { "servers",     "servers",      3, DrawServers,    "world/servers" },
  { "skybox",      "skybox",       3, DrawSkybox,     "world/skybox" },
  { "waypoints",   "waypoints",    3, DrawWaypoints,  "world/waypoints" },
  { "players",     "players",      4, DrawPlayers,
    "players/status;players/actions;players/lists" },
  { "configs",     "configs",      7, DrawConfigs,    "config/actions;config/files" },
  { "debug",       "debug",        8, DrawDebug,
    "debug/offset write tests;debug/log actions" },
  { "explorer",    "explorer",     9, DrawExplorer,   "dex/browser" },
};
constexpr int kFeatureCount = (int)(sizeof(kFeatures) / sizeof(kFeatures[0]));

bool     s_open[kFeatureCount]      = {};
bool     s_placed[kFeatureCount]    = {};
float    s_win_pos[2]               = {0, 0};
float    s_win_size[2]              = {0, 0};
float    s_feat_pos[kFeatureCount][2]  = {};
float    s_feat_size[kFeatureCount][2] = {};

bool IdInList(const char* list, const char* id)
{
    if (!list || !list[0] || !id || !id[0]) return false;
    char buf[512];
    snprintf(buf, sizeof(buf), ";%s;", list);
    char needle[96];
    snprintf(needle, sizeof(needle), ";%s;", id);
    return strstr(buf, needle) != nullptr;
}

void SyncOpenFromGlobal()
{
    for (int i = 0; i < kFeatureCount; ++i)
        s_open[i] = IdInList(ui_closed_groups, kFeatures[i].id);
}

void PersistOpenToGlobal()
{
    char out[sizeof(ui_closed_groups)] = "";
    size_t used = 0;
    for (int i = 0; i < kFeatureCount; ++i) {
        if (!s_open[i]) continue;
        const size_t len = strlen(kFeatures[i].id);
        if (used + len + 2 >= sizeof(out)) break;
        if (used) out[used++] = ';';
        memcpy(out + used, kFeatures[i].id, len);
        used += len;
        out[used] = '\0';
    }
    snprintf(ui_closed_groups, sizeof(ui_closed_groups), "%s", out);
}

std::vector<std::string>& LegacyGroups()
{
    static std::vector<std::string> g;
    return g;
}

void RegisterLegacyOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    for (const auto& f : kFeatures) {
        if (f.legacy && f.legacy[0]) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s", f.legacy);
            char* tok = strtok(buf, ";");
            while (tok) {
                LegacyGroups().push_back(tok);
                tok = strtok(nullptr, ";");
            }
        }
    }
}

obsidian::Palette BuildPalette()
{
    obsidian::Palette p = obsidian::DefaultPalette();
    // Base colors - minimal defaults, user will customize
    p.window_bg        = ImVec4(0.07f, 0.07f, 0.09f, 0.92f);
    p.child_bg         = ImVec4(0.09f, 0.09f, 0.11f, 0.80f);
    p.popup_bg         = ImVec4(0.07f, 0.07f, 0.09f, 0.96f);
    p.border           = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    p.border_strong    = ImVec4(0.35f, 0.35f, 0.42f, 0.80f);
    p.text             = ImVec4(0.95f, 0.95f, 0.98f, 1.00f);
    p.text_disabled    = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
    p.accent           = ImVec4(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
    p.accent_hover     = ImVec4(ui_accent_color[0]*1.15f, ui_accent_color[1]*1.15f, ui_accent_color[2]*1.15f, 1.0f);
    p.accent_active    = ImVec4(ui_accent_color[0]*0.85f, ui_accent_color[1]*0.85f, ui_accent_color[2]*0.85f, 1.0f);
    p.frame_bg         = ImVec4(0.13f, 0.13f, 0.16f, 0.80f);
    p.frame_hover      = ImVec4(0.18f, 0.18f, 0.22f, 0.90f);
    p.frame_active     = ImVec4(0.22f, 0.22f, 0.27f, 1.00f);
    p.title_bg         = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    p.title_bg_active  = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    p.scrollbar_bg     = ImVec4(0.05f, 0.05f, 0.06f, 0.50f);
    p.scrollbar_grab   = ImVec4(0.30f, 0.30f, 0.35f, 0.80f);
    p.scrollbar_hover  = ImVec4(0.40f, 0.40f, 0.47f, 1.00f);
    p.check_mark       = ImVec4(0.95f, 0.95f, 0.98f, 1.00f);
    p.slider_grab      = ImVec4(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
    p.button           = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    p.button_hover     = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    p.button_active    = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    p.header           = ImVec4(0.13f, 0.13f, 0.16f, 0.80f);
    p.header_hover     = ImVec4(0.18f, 0.18f, 0.22f, 0.90f);
    p.header_active    = ImVec4(0.22f, 0.22f, 0.27f, 1.00f);
    p.separator        = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    p.resize_grip      = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);
    p.resize_hover     = ImVec4(0.40f, 0.40f, 0.47f, 0.80f);
    p.resize_active    = ImVec4(0.50f, 0.50f, 0.57f, 1.00f);
    p.plot_lines       = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
    p.plot_lines_hover = ImVec4(0.80f, 0.80f, 0.90f, 1.00f);
    p.plot_histogram   = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
    p.plot_hist_hover  = ImVec4(0.80f, 0.80f, 0.90f, 1.00f);
    p.text_link        = ImVec4(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
    p.modal_dim        = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
    return p;
}

void TickKeybinds()
{
    if (!s_wait_slot || !g_act.poll_key) return;
    int vk = g_act.poll_key();
    if (vk) {
        *s_wait_slot = vk;
        s_wait_slot = nullptr;
    }
}

bool SyncTheme()
{
    static float s_last_scale = 0.0f;
    static float s_last_font  = 0.0f;
    static float s_last_title = 0.0f;
    if (s_last_scale != ui_scale || s_last_font != ui_font_size || s_last_title != ui_title_size) {
        s_last_scale = ui_scale;
        s_last_font  = ui_font_size;
        s_last_title = ui_title_size;
        obsidian::ThemeConfig cfg;
        cfg.ui_scale = ui_scale;
        cfg.base_font_size = ui_font_size;
        cfg.title_font_size = ui_title_size;
        cfg.mono_font_size = ui_font_size * 0.87f;
        cfg.move_from_title_bar_only = true;
        obsidian::LoadFonts(cfg);
        return true;
    }
    return false;
}

void SetClosedGroups(const char* list)
{
    if (list) snprintf(ui_closed_groups, sizeof(ui_closed_groups), "%s", list);
    else ui_closed_groups[0] = '\0';
    SyncOpenFromGlobal();
}

void GetClosedGroups(char* out, size_t n)
{
    if (out && n) snprintf(out, n, "%s", ui_closed_groups);
}

void PushFloatWindowStyle(float bg_alpha)
{
    obsidian::Palette p = BuildPalette();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(p.window_bg.x, p.window_bg.y, p.window_bg.z, bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, p.border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui_rounded_corners ? ui_corner_radius : 0.0f);
}

void PopFloatWindowStyle()
{
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

int CurrentPage() { return s_page; }

void LastRect(float* pos_x, float* pos_y, float* size_x, float* size_y)
{
    if (pos_x) *pos_x = s_win_pos[0];
    if (pos_y) *pos_y = s_win_pos[1];
    if (size_x) *size_x = s_win_size[0];
    if (size_y) *size_y = s_win_size[1];
}

int FeatureCount() { return kFeatureCount; }

bool FeatureRect(int idx, float* pos_x, float* pos_y, float* size_x, float* size_y)
{
    if (idx < 0 || idx >= kFeatureCount || !s_open[idx]) return false;
    if (pos_x) *pos_x = s_feat_pos[idx][0];
    if (pos_y) *pos_y = s_feat_pos[idx][1];
    if (size_x) *size_x = s_feat_size[idx][0];
    if (size_y) *size_y = s_feat_size[idx][1];
    return true;
}

void SetPage(int page)
{
    if (page >= 0 && page < kPageCount) s_page = page;
}

int PageCount() { return kPageCount; }

const char* PageName(int page)
{
    if (page >= 0 && page < kPageCount) return kPages[page];
    return "";
}

bool HasGroup(const char* id)
{
    if (!id) return false;
    for (const auto& g : LegacyGroups()) if (g == id) return true;
    return false;
}

void FillServers(Snapshot& s, const ::features::servers::View& sv)
{
    s.server_count = 0;
    s.server_current = -1;
    s.servers_busy = sv.busy;
    s.servers_can_rejoin = sv.can_rejoin;
    s.server_age_sec = sv.age_sec;
    if (sv.msg[0]) snprintf(s.servers_msg, sizeof(s.servers_msg), "%s", sv.msg);
    else s.servers_msg[0] = '\0';

    for (size_t i = 0; i < sv.rows.size() && s.server_count < kMaxServers; ++i) {
        const auto& r = sv.rows[i];
        Snapshot::ServerRow& row = s.servers[s.server_count];
        if (r.short_guid[0]) snprintf(row.id, sizeof(row.id), "%s", r.short_guid);
        else snprintf(row.id, sizeof(row.id), "server %zu", i + 1);
        snprintf(row.detail, sizeof(row.detail), "%d/%d", r.players, r.max_players);
        row.state = r.is_current ? 1 : (r.full ? 2 : 0);
        if (r.is_current) s.server_current = s.server_count;
        ++s.server_count;
    }
}

// =============================================================================
//  Feature draw bodies — MINIMAL PLACEHOLDERS
//  Visual design is entirely deferred to the user.
// =============================================================================

void DrawAimbot(Snapshot& s) {
    (void)s;
    ImGui::Text("Aimbot settings — design TBD");
    ImGui::Checkbox("Enabled", &aimbot_enabled);
    ImGui::Checkbox("Hold mode", &aimbot_hold_mode);
}

void DrawTriggerbot(Snapshot& s) {
    (void)s;
    ImGui::Text("Triggerbot settings — design TBD");
    ImGui::Checkbox("Enabled", &triggerbot_enabled);
    ImGui::Checkbox("Hold mode", &triggerbot_hold_mode);
}

void DrawHitbox(Snapshot& s) {
    (void)s;
    ImGui::Text("Hitbox settings — design TBD");
    ImGui::Checkbox("Hitbox expander", &hitbox_expander_enabled);
    ImGui::SliderFloat("Value", &hitbox_expander_value, 1.0f, 50.0f);
}

void DrawEsp(Snapshot& s) {
    (void)s;
    ImGui::Text("ESP settings — design TBD");
    ImGui::Checkbox("Enabled", &esp_enabled);
    ImGui::Checkbox("Box", &box_esp);
    ImGui::Checkbox("Skeleton", &skeleton_esp);
    ImGui::Checkbox("Chams", &chams_enabled);
    ImGui::SliderFloat("Line width", &esp_line_width, 0.5f, 5.0f);
    ImGui::SliderFloat("Glow", &esp_glow, 0.0f, 1.0f);
}

void DrawTracers(Snapshot& s) {
    (void)s;
    ImGui::Text("Tracers — design TBD");
    ImGui::Checkbox("Enabled", &esp_tracer);
}

void DrawOffScreen(Snapshot& s) {
    (void)s;
    ImGui::Text("Off-screen indicators — design TBD");
    ImGui::Checkbox("Enabled", &oof_enabled);
}

void DrawChams(Snapshot& s) {
    (void)s;
    ImGui::Text("Chams — design TBD");
    ImGui::Checkbox("Enabled", &chams_enabled);
    ImGui::Checkbox("Mesh chams", &mesh_chams);
}

void DrawNpcEsp(Snapshot& s) {
    (void)s;
    ImGui::Text("NPC ESP — design TBD");
    ImGui::Checkbox("Bots", &world_bots_enabled);
    ImGui::Checkbox("Animals", &world_animals_enabled);
}

void DrawFrameRate(Snapshot& s) {
    (void)s;
    ImGui::Text("Frame rate — design TBD");
    ImGui::Checkbox("Show FPS", &show_fps);
    ImGui::Checkbox("FPS cap", &fps_cap_enabled);
    ImGui::SliderFloat("Cap value", &fps_cap_value, 10.0f, 360.0f);
}

void DrawCamera(Snapshot& s) {
    (void)s;
    ImGui::Text("Camera — design TBD");
    ImGui::Checkbox("FOV changer", &fov_changer_enabled);
    ImGui::SliderFloat("FOV", &fov_value, 30.0f, 120.0f);
}

void DrawMovement(Snapshot& s) {
    (void)s;
    ImGui::Text("Movement — design TBD");
    ImGui::Checkbox("Walkspeed", &walkspeed_enabled);
    ImGui::SliderFloat("Value", &walkspeed_value, 1.0f, 100.0f);
    ImGui::Checkbox("Flight", &flight_enabled);
    ImGui::SliderFloat("Speed", &flight_value, 10.0f, 200.0f);
    ImGui::Checkbox("Infinite jump", &infinite_jump_enabled);
    ImGui::SliderFloat("Jump power", &infinite_jump_power, 10.0f, 300.0f);
    ImGui::Checkbox("Noclip", &noclip_enabled);
}

void DrawPhysics(Snapshot& s) {
    (void)s;
    ImGui::Text("Physics — design TBD");
    ImGui::Checkbox("Gravity", &gravity_enabled);
    ImGui::SliderFloat("Value", &gravity_value, 0.0f, 500.0f);
    ImGui::Checkbox("Hip height", &hipheight_enabled);
    ImGui::SliderFloat("Value", &hipheight_value, 0.0f, 10.0f);
    ImGui::Checkbox("No-fall (position hold)", &nofall_enabled);
    ImGui::Checkbox("Fling", &fling_enabled);
    ImGui::SliderFloat("Power (rad/s)", &fling_power, 60.0f, 2000.0f);
    ImGui::Checkbox("Jump power", &jumppower_enabled);
    ImGui::SliderFloat("Value", &jumppower_value, 10.0f, 500.0f);
}

void DrawKeepAlive(Snapshot& s) {
    (void)s;
    ImGui::Text("Keep alive — design TBD");
    ImGui::Checkbox("Anti-AFK", &antiafk_enabled);
    ImGui::SliderFloat("Interval (min)", &antiafk_interval, 1.0f, 15.0f);
}

void DrawTeleport(Snapshot& s) {
    (void)s;
    ImGui::Text("Teleport — design TBD");
    ImGui::Checkbox("Click teleport", &click_teleport_enabled);
    ImGui::SliderFloat("Distance", &click_teleport_distance, 10.0f, 500.0f);
}

void DrawLighting(Snapshot& s) {
    (void)s;
    ImGui::Text("Lighting — design TBD");
    ImGui::Checkbox("Fullbright", &fullbright_enabled);
    ImGui::Checkbox("World ambient", &world_ambient_enabled);
    ImGui::Checkbox("Fog", &world_fog_enabled);
    ImGui::Checkbox("Exposure", &world_exposure_enabled);
    ImGui::Checkbox("No shadows", &noshadow_enabled);
    ImGui::Checkbox("No grass", &nograss_enabled);
    ImGui::Checkbox("Night", &night_enabled);
}

void DrawServers(Snapshot& s) {
    (void)s;
    ImGui::Text("Servers — design TBD");
    if (ImGui::Button("Refresh")) g_act.servers_refresh ? g_act.servers_refresh() : void();
    if (ImGui::Button("Hop to openest")) g_act.servers_hop_openest ? g_act.servers_hop_openest() : void();
    if (ImGui::Button("Rejoin")) g_act.servers_rejoin ? g_act.servers_rejoin() : void();
}

void DrawSkybox(Snapshot& s) {
    (void)s;
    ImGui::Text("Skybox — design TBD");
    ImGui::Checkbox("Enabled", &skybox_changer_enabled);
    if (skybox_debug_msg[0]) ImGui::Text("%s", skybox_debug_msg);
}

void DrawWaypoints(Snapshot& s) {
    (void)s;
    ImGui::Text("Waypoints — design TBD");
    ImGui::Checkbox("Show", &show_waypoints);
}

void DrawPlayers(Snapshot& s) {
    (void)s;
    ImGui::Text("Players — design TBD");
    ImGui::Text("%d players", s.player_count);
}

void DrawConfigs(Snapshot& s) {
    (void)s;
    ImGui::Text("Configs — design TBD");
}

void DrawDebug(Snapshot& s) {
    (void)s;
    ImGui::Text("Debug — design TBD");
    if (ImGui::Button("Rescan")) g_act.rescan ? g_act.rescan() : void();
    if (ImGui::Button("Test 0 (position write)")) g_act.run_test ? g_act.run_test(0) : void();
    if (ImGui::Button("Test 1 (velocity write)")) g_act.run_test ? g_act.run_test(1) : void();
    if (ImGui::Button("Test 4 (survival)")) g_act.run_test ? g_act.run_test(4) : void();
    if (ImGui::Button("Test 5 (anchored survival)")) g_act.run_test ? g_act.run_test(5) : void();
    if (ImGui::Button("Test 6 (float probe)")) g_act.run_test ? g_act.run_test(6) : void();
    if (s.test_result[0]) ImGui::TextWrapped("%s", s.test_result);
}

void DrawExplorer(Snapshot& s) {
    (void)s;
    ImGui::Text("Explorer (Dex) — design TBD");
    if (g_act.draw_dex) g_act.draw_dex();
}

// =============================================================================
//  Main Render
// =============================================================================

void Render(Snapshot& snap)
{
    SyncOpenFromGlobal();
    RegisterLegacyOnce();

    obsidian::Palette palette = BuildPalette();
    obsidian::ApplyTheme(palette, obsidian::ThemeConfig{
        .ui_scale = ui_scale,
        .base_font_size = ui_font_size,
        .title_font_size = ui_title_size,
        .mono_font_size = ui_font_size * 0.87f,
        .move_from_title_bar_only = true
    });

    // Main window
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::SetNextWindowSize(ImVec2(ui_win_w, ui_win_h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui_min_w, ui_min_h), ImVec2(ui_max_w > 0 ? ui_max_w : 10000, ui_max_h > 0 ? ui_max_h : 10000));

    bool open = true;
    if (ImGui::Begin("Daemon", &open, flags)) {
        s_win_pos[0] = ImGui::GetWindowPos().x;
        s_win_pos[1] = ImGui::GetWindowPos().y;
        s_win_size[0] = ImGui::GetWindowSize().x;
        s_win_size[1] = ImGui::GetWindowSize().y;

        // Header: name + status
        ImGui::Text("Daemon");
        ImGui::SameLine();
        if (snap.attached) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Attached (pid %u)", snap.pid);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Waiting for Sober...");
        }
        ImGui::Separator();

        // Feature column
        for (int i = 0; i < kFeatureCount; ++i) {
            const FeatureDef& f = kFeatures[i];
            bool is_open = s_open[i];
            if (ImGui::Selectable(f.label, is_open, 0, ImVec2(0, 28))) {
                s_open[i] = !is_open;
                PersistOpenToGlobal();
            }
            if (s_open[i] && !s_placed[i]) {
                ImVec2 main_pos = ImGui::GetWindowPos();
                ImVec2 main_size = ImGui::GetWindowSize();
                s_feat_pos[i][0] = main_pos.x + main_size.x + 8;
                s_feat_pos[i][1] = main_pos.y;
                s_placed[i] = true;
            }
        }

        ImGui::End();
    }
    g_request_exit = !open;

    // Settings windows
    for (int i = 0; i < kFeatureCount; ++i) {
        if (!s_open[i]) { s_placed[i] = false; continue; }
        const FeatureDef& f = kFeatures[i];
        ImGui::SetNextWindowPos(ImVec2(s_feat_pos[i][0], s_feat_pos[i][1]), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 400), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags wflags = ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(f.label, &s_open[i], wflags)) {
            s_feat_pos[i][0] = ImGui::GetWindowPos().x;
            s_feat_pos[i][1] = ImGui::GetWindowPos().y;
            s_feat_size[i][0] = ImGui::GetWindowSize().x;
            s_feat_size[i][1] = ImGui::GetWindowSize().y;
            f.draw(snap);
        }
        if (!ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            // click outside closes - optional
        }
        ImGui::End();
        if (!s_open[i]) PersistOpenToGlobal();
    }
}

} // namespace menuui