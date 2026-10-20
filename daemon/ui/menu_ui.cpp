// -----------------------------------------------------------------------------
//  ui/menu_ui.cpp  --  the menu, stripped to a DX9ware-style column
// -----------------------------------------------------------------------------
//  The shell (this file's only window choreography):
//
//    main window = the app name, one status line, and a single COLUMN of
//                  features. Each row carries a +/- dropdown marker; clicking a
//                  row opens (or closes) that feature's settings window.
//    settings    = one ImGui window per feature, anchored to the right of the
//                  main column when first opened, then free. Inside: a plain
//                  vertical column of that feature's controls (enable first).
//
//  Exactly two colours, no exceptions: a white accent and a semi-opaque black
//  background. PushBareStyle()/PopBareStyle() wrap every draw this file does,
//  so the chrome cannot pick up a theme from anywhere else. Colour PICKERS for
//  the ESP itself are settings data, not chrome — they still edit float[4]s.
//
//  Nothing here touches the game. Memory-derived text arrives pre-formatted in
//  menuui::Snapshot; the platform calls arrive as menuui::Actions. Settings are
//  read and written straight through globals.h, so there is one copy of each.
//
//  Kept for hosts and tooling: BuildPalette()/ApplyTheme pipeline (the palette
//  still drives float plates and the headless theme checks), TickKeybinds,
//  FillServers, the page-name accessors (file naming in menu_shots), HasGroup
//  (the legacy group registry, now fed from the feature table so filing
//  assertions keep working), and PushFloatWindowStyle for overlay float plates.
// -----------------------------------------------------------------------------
#include "ui/menu_ui.h"

#include "features/servers/servers.h"        // View / Row, for FillServers below
#include "features/servers/servers_json.h"   // ShortGuid
#include "globals.h"

#include <cstdio>
#include <cstring>

namespace obs = obsidian;

namespace menuui {
namespace {

Actions g_act;
int*    s_wait_slot = nullptr;              // keybind slot currently capturing a key

// config page state (owned here; the app supplies only the file operations)
char     s_cfg_name[128]   = "";
char     s_cfg_rename[128] = "";
std::vector<std::string> s_configs;
int      s_cfg_sel         = -1;
bool     s_configs_fetched = false;

// players window: which row the action buttons act on. Re-clamped every frame.
int      s_player_sel = -1;

void RefreshConfigs()
{
    s_configs = g_act.list_configs ? g_act.list_configs() : std::vector<std::string>();
    s_configs_fetched = true;
}

// Tooling keeps the old page names (menu_shots names its files after them and
// SetPage still selects nothing — the menu draws every feature at once).
const char* const kPages[] = { "aimbot", "visuals", "movement", "world", "players",
                               "keybinds", "theme", "config", "debug", "dex" };
constexpr int kPageCount = (int)(sizeof(kPages) / sizeof(kPages[0]));
int s_page = 0;

const char* kAimTypes[]  = { "Camera", "Mouse position" };
const char* kBones[]     = { "Head", "Upper torso", "Lower torso", "Left hand",
                             "Right hand", "Left foot", "Right foot" };
const char* kBoxStyles[] = { "Full box", "Corners" };
const char* kOofStyles[] = { "Arrow", "Chevron", "Triangle" };

// =============================================================================
//  feature registry — the main column, and the legacy group ids it stands for
// =============================================================================
struct FeatureDef
{
    const char* id;      // stable id: window id, persistence token, registry key
    const char* label;   // what the row and the settings-window title say
    int         page;    // the page this used to live on (tooling only)
    void      (*draw)(Snapshot&);
    const char* legacy;  // ';'-joined "page/group" ids this feature absorbed —
                         // the filing registry the headless harness asserts on
};

// Draw bodies are defined below the table (C++ needs the declarations).
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
    "visuals/boxes;visuals/strokes;visuals/labels;visuals/extras;"
    "visuals/shared with aimbot" },
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

bool     s_open[kFeatureCount]      = {};   // settings windows, default closed
bool     s_placed[kFeatureCount]    = {};   // window has been positioned once
ImVec2   s_win_pos                  = {};   // main window rect (LastRect)
ImVec2   s_win_size                 = {};
ImVec2   s_feat_pos[kFeatureCount]  = {};
ImVec2   s_feat_size[kFeatureCount] = {};   // recorded while each window is current

// The persistence contract, unchanged in shape: ui_closed_groups is a ';'-joined
// id list. It now names the OPEN settings windows (the default — nothing in the
// string — is every window closed, which is the state a column of dropdowns
// starts in). Old config files stored old group ids, which match no feature, so
// they load as "nothing open": exactly the fresh default.
bool IdInList(const char* list, const char* id)
{
    if (!list || !list[0] || !id || !id[0]) return false;
    // token-aware: ";esp;" contains "esp" but not "esp extra"
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

// The legacy group registry HasGroup() answers from. Built once from the table
// above; ids are asserted after a frame has been drawn, same as before.
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
    auto& v = LegacyGroups();
    for (const FeatureDef& f : kFeatures) {
        if (!f.legacy || !f.legacy[0]) continue;
        const char* p = f.legacy;
        while (*p) {
            const char* semi = strchr(p, ';');
            const size_t n = semi ? (size_t)(semi - p) : strlen(p);
            if (n) v.emplace_back(p, n);
            p = semi ? semi + 1 : p + n;
        }
    }
}

// =============================================================================
//  the two-colour chrome
// =============================================================================
// White accent + semi-opaque black. Alpha does the work a second colour would:
// hover/active are white at low alpha over the black plate, and inverted rows
// (an open dropdown, a checked box) are solid white with black ink on top.
struct BareCol { ImGuiCol id; ImVec4 v; };
const BareCol kBareCols[] = {
        { ImGuiCol_Text,                 ImVec4(1, 1, 1, 1) },
        { ImGuiCol_TextDisabled,         ImVec4(1, 1, 1, 0.55f) },
        { ImGuiCol_WindowBg,             ImVec4(0, 0, 0, 0.72f) },
        { ImGuiCol_ChildBg,              ImVec4(0, 0, 0, 0.55f) },
        { ImGuiCol_PopupBg,              ImVec4(0, 0, 0, 0.85f) },
        { ImGuiCol_Border,               ImVec4(1, 1, 1, 0.30f) },
        { ImGuiCol_BorderShadow,         ImVec4(0, 0, 0, 0) },
        { ImGuiCol_FrameBg,              ImVec4(0, 0, 0, 0.65f) },
        { ImGuiCol_FrameBgHovered,       ImVec4(1, 1, 1, 0.20f) },
        { ImGuiCol_FrameBgActive,        ImVec4(1, 1, 1, 0.32f) },
        { ImGuiCol_TitleBg,              ImVec4(0, 0, 0, 0.72f) },
        { ImGuiCol_TitleBgActive,        ImVec4(1, 1, 1, 0.12f) },
        { ImGuiCol_TitleBgCollapsed,     ImVec4(0, 0, 0, 0.55f) },
        { ImGuiCol_MenuBarBg,            ImVec4(0, 0, 0, 0.72f) },
        { ImGuiCol_ScrollbarBg,          ImVec4(0, 0, 0, 0.40f) },
        { ImGuiCol_ScrollbarGrab,        ImVec4(1, 1, 1, 0.30f) },
        { ImGuiCol_ScrollbarGrabHovered, ImVec4(1, 1, 1, 0.45f) },
        { ImGuiCol_ScrollbarGrabActive,  ImVec4(1, 1, 1, 0.60f) },
        { ImGuiCol_CheckMark,            ImVec4(1, 1, 1, 1) },
        { ImGuiCol_SliderGrab,           ImVec4(1, 1, 1, 0.85f) },
        { ImGuiCol_SliderGrabActive,     ImVec4(1, 1, 1, 1) },
        { ImGuiCol_Button,               ImVec4(0, 0, 0, 0.55f) },
        { ImGuiCol_ButtonHovered,        ImVec4(1, 1, 1, 0.22f) },
        { ImGuiCol_ButtonActive,         ImVec4(1, 1, 1, 0.38f) },
        { ImGuiCol_Header,               ImVec4(1, 1, 1, 0.14f) },
        { ImGuiCol_HeaderHovered,        ImVec4(1, 1, 1, 0.26f) },
        { ImGuiCol_HeaderActive,         ImVec4(1, 1, 1, 0.36f) },
        { ImGuiCol_Separator,            ImVec4(1, 1, 1, 0.35f) },
        { ImGuiCol_SeparatorHovered,     ImVec4(1, 1, 1, 0.50f) },
        { ImGuiCol_SeparatorActive,      ImVec4(1, 1, 1, 0.70f) },
        { ImGuiCol_ResizeGrip,           ImVec4(1, 1, 1, 0.30f) },
        { ImGuiCol_ResizeGripHovered,    ImVec4(1, 1, 1, 0.60f) },
        { ImGuiCol_ResizeGripActive,     ImVec4(1, 1, 1, 0.80f) },
        { ImGuiCol_TabSelected,          ImVec4(1, 1, 1, 0.35f) },
        { ImGuiCol_TabSelectedOverline,  ImVec4(1, 1, 1, 0.80f) },
        { ImGuiCol_PlotLines,            ImVec4(1, 1, 1, 1) },
        { ImGuiCol_PlotHistogram,        ImVec4(1, 1, 1, 1) },
        { ImGuiCol_TableHeaderBg,        ImVec4(1, 1, 1, 0.12f) },
        { ImGuiCol_TableBorderStrong,    ImVec4(1, 1, 1, 0.35f) },
        { ImGuiCol_TableBorderLight,     ImVec4(1, 1, 1, 0.20f) },
        { ImGuiCol_TableRowBg,           ImVec4(0, 0, 0, 0) },
        { ImGuiCol_TableRowBgAlt,        ImVec4(1, 1, 1, 0.05f) },
        { ImGuiCol_TextSelectedBg,       ImVec4(1, 1, 1, 0.30f) },
        { ImGuiCol_DragDropTarget,       ImVec4(1, 1, 1, 0.90f) },
        { ImGuiCol_NavHighlight,         ImVec4(1, 1, 1, 0.60f) },
        { ImGuiCol_NavWindowingHighlight,ImVec4(1, 1, 1, 0.70f) },
        { ImGuiCol_NavWindowingDimBg,    ImVec4(0, 0, 0, 0.80f) },
    { ImGuiCol_ModalWindowDimBg,     ImVec4(0, 0, 0, 0.80f) },
};
constexpr int kBareColCount = (int)(sizeof(kBareCols) / sizeof(kBareCols[0]));

void PushBareStyle()
{
    for (const auto& c : kBareCols) ImGui::PushStyleColor(c.id, c.v);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    // 1, not 0: an unchecked checkbox is only its frame, and with no border the
    // frame vanishes into the plate. The border rides ImGuiCol_Border (white).
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
}

void PopBareStyle()
{
    ImGui::PopStyleColor(kBareColCount);
    ImGui::PopStyleVar(6);
}

// =============================================================================
//  plain widgets — one column, label left, control right
// =============================================================================
bool Tog(const char* label, bool* v)
{
    return ImGui::Checkbox(label, v);
}

bool Num(const char* label, float* v, float lo, float hi, const char* fmt)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140);
    ImGui::PushItemWidth(-1);
    const bool c = ImGui::SliderFloat("##n", v, lo, hi, fmt);
    ImGui::PopItemWidth();
    return c;
}

bool Cmb(const char* label, int* v, const char* const* items, int count)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140);
    ImGui::PushItemWidth(-1);
    const bool c = ImGui::Combo("##c", v, items, count);
    ImGui::PopItemWidth();
    return c;
}

bool Col(const char* label, float* c)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140);
    ImGui::PushItemWidth(-1);
    // NoInputs: a plain swatch across the control column — four float fields in
    // 120px clip to "25! 25! 25!". Click opens the full picker popup.
    const bool e = ImGui::ColorEdit4("##col", c, ImGuiColorEditFlags_NoInputs);
    ImGui::PopItemWidth();
    return e;
}

bool Btn(const char* label)
{
    return ImGui::Button(label, ImVec2(-1.0f, 0.0f));
}

// One keybind row: click, then press a key or mouse button (TickKeybinds does
// the capture; Escape cancels). `live` dims the row while its feature is off.
void Key(const char* label, int* slot, bool live)
{
    const bool waiting = (s_wait_slot == slot);
    const char* kn = (waiting || !g_act.key_name) ? nullptr : g_act.key_name(*slot);
    const char* nm = waiting ? "..." : (kn ? kn : "unbound");
    char text[96];
    snprintf(text, sizeof(text), "%s: %s", label, waiting ? "press a key" : nm);
    if (!live) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
    if (ImGui::Button(text, ImVec2(-1.0f, 0.0f)))
        s_wait_slot = waiting ? nullptr : slot;
    if (!live) ImGui::PopStyleColor();
}

// =============================================================================
//  feature settings columns
// =============================================================================
void DrawAimbot(Snapshot& snap)
{
    Tog("enable aimbot", &aimbot_enabled);
    Cmb("aim from", &aimbot_aim_type, kAimTypes, IM_ARRAYSIZE(kAimTypes));
    Cmb("target bone", &aimbot_part, kBones, IM_ARRAYSIZE(kBones));
    Tog("sticky aim", &sticky_aim);
    Tog("lead prediction", &prediction_enabled);
    if (prediction_enabled) {
        Num("predict x", &prediction_x, 1.0f, 50.0f, "%.0f");
        Num("predict y", &prediction_y, 1.0f, 50.0f, "%.0f");
    }
    Num("smoothing x", &smoothing_x, 2.0f, 20.0f, "%.1f");
    Num("smoothing y", &smoothing_y, 2.0f, 20.0f, "%.1f");
    Tog("humanizer", &humanizer_enabled);
    if (humanizer_enabled)
        Num("humanize amount", &humanizer_strength, 0.0f, 1.0f, "%.2f");
    Num("fov radius", &fov_size, 10.0f, 500.0f, "%.0f px");
    Tog("draw fov ring", &show_fov);
    Tog("team check", &team_check);
    Tog("wall check", &wall_check);
    Tog("dead check", &dead_check);
    Key("aimbot key", &aimbot_keybind, aimbot_enabled);
    // On: hold the key. Off: the key flips a latch. (snap keeps the latch read
    // out in the status line; the row stays plain here.)
    (void)snap;
    Tog("hold to aim", &aimbot_hold_mode);
}

void DrawTriggerbot(Snapshot& snap)
{
    Tog("enable triggerbot", &triggerbot_enabled);
    if (triggerbot_enabled) {
        Cmb("aim at", &triggerbot_part, kBones, IM_ARRAYSIZE(kBones));
        Num("crosshair radius", &triggerbot_fov, 1.0f, 60.0f, "%.0f px");
        Num("reaction delay", &triggerbot_delay_ms, 0.0f, 600.0f, "%.0f ms");
        Num("shot cooldown", &triggerbot_cooldown_ms, 60.0f, 1000.0f, "%.0f ms");
        Num("max distance", &triggerbot_max_dist, 0.0f, esp_draw_distance_max, "%.0f studs");
        Num("hit chance", &triggerbot_chance, 1.0f, 100.0f, "%.0f%%");
        Tog("require line of sight", &triggerbot_visible_check);
        Key("triggerbot key", &triggerbot_keybind, triggerbot_enabled);
        Tog("hold to fire", &triggerbot_hold_mode);
        Tog("hit sound", &hitsound_enabled);
        if (hitsound_enabled) {
            Num("volume", &hitsound_volume, 0.0f, 100.0f, "%.0f%%");
            if (snap.hitsound_names && snap.hitsound_count > 0) {
                if (hitsound_selected < 0) hitsound_selected = 0;
                if (hitsound_selected >= snap.hitsound_count)
                    hitsound_selected = snap.hitsound_count - 1;
                Cmb("sound", &hitsound_selected, snap.hitsound_names, snap.hitsound_count);
            }
            if (Btn("hitsound test") && g_act.hitsound_test) g_act.hitsound_test();
        }
    }
}

void DrawHitbox(Snapshot&)
{
    Tog("hitbox expander", &hitbox_expander_enabled);
    if (hitbox_expander_enabled)
        Num("hitbox size", &hitbox_expander_value, 1.0f, 50.0f, "%.0f");
}

void DrawEsp(Snapshot&)
{
    Tog("enable esp", &esp_enabled);
    Tog("box", &box_esp);
    if (box_esp) {
        Cmb("box style", &box_esp_type, kBoxStyles, IM_ARRAYSIZE(kBoxStyles));
        Col("box color", box_esp_color);
        Tog("fill", &box_fill);
        if (box_fill) {
            Col("fill top", box_fill_top);
            Tog("gradient fill", &box_fill_gradient);
            if (box_fill_gradient) {
                Tog("rotate gradient", &box_fill_gradient_rotate);
                Col("fill bottom", box_fill_bottom);
            }
        }
    }
    Num("line width", &esp_line_width, 0.5f, 4.0f, "%.1f px");
    Num("glow", &esp_glow, 0.0f, 1.0f, "%.2f");
    if (box_esp && box_esp_type == 1)
        Num("corner length", &esp_corner_frac, 0.05f, 0.5f, "%.0f%%");
    Num("text size", &esp_text_size, 9.0f, 22.0f, "%.0f px");
    {
        const char* kPlates[] = { "no plate", "solid plate", "fading plate" };
        Cmb("name plate", &esp_name_plate, kPlates, IM_ARRAYSIZE(kPlates));
    }
    Tog("player name", &name);
    if (name) Col("name color", name_color);
    Tog("health bar", &healthbar);
    if (healthbar) Col("bar color", healthbar_color);
    Tog("health text", &health_text);
    if (health_text) Col("health text color", health_text_color);
    Tog("distance", &distance);
    if (distance) Col("distance color", distance_color);
    Tog("rig type", &rig_type);
    if (rig_type) Col("rig color", rig_type_color);
    Tog("equipped tool", &tool_esp);
    if (tool_esp) Col("tool color", tool_color);
    Num("draw distance", &esp_render_distance, 0.0f, esp_draw_distance_max,
        esp_render_distance > 0.0f ? "%.0f studs" : "infinite");
    Tog("team check", &team_check);
    if (team_check) Col("team color", team_esp_color);
    Tog("wall check", &wall_check);
    Tog("hide blacklisted", &blacklist_hides_esp);
    Tog("skeleton", &skeleton_esp);
    if (skeleton_esp) Col("bone color", skeleton_color);
    Tog("aim viewer", &aimviewer);
    Tog("china hat", &chinahat);
    if (chinahat) Col("hat color", chinahat_color);
    Tog("head dot", &headdot_enabled);
    if (headdot_enabled) {
        Num("dot size", &headdot_thickness, 1.0f, 12.0f, "%.0f px");
        Col("dot color", headdot_color);
        Tog("dot fill", &headdot_fill);
        Tog("dot outline", &headdot_outline);
        if (headdot_outline) Col("dot edge", headdot_outline_color);
    }
    Tog("inventory checker", &inventory_checker_enabled);
    if (inventory_checker_enabled)
        Key("inventory key", &inventory_checker_keybind, inventory_checker_enabled);
}

void DrawTracers(Snapshot&)
{
    Tog("tracers", &esp_tracer);
    if (esp_tracer) {
        const char* kOrigins[] = { "bottom centre", "bottom left", "bottom right",
                                   "screen centre" };
        Cmb("from", &esp_tracer_origin, kOrigins, IM_ARRAYSIZE(kOrigins));
        const char* kAttach[] = { "feet", "box centre", "head" };
        Cmb("to", &esp_tracer_attach, kAttach, IM_ARRAYSIZE(kAttach));
        Num("tracer width", &esp_tracer_width, 0.5f, 6.0f, "%.1f px");
        Num("max distance", &esp_tracer_max_dist, 0.0f, esp_draw_distance_max,
            esp_tracer_max_dist > 0.0f ? "%.0f studs" : "no limit");
        Tog("use box color", &esp_tracer_box_color);
        if (!esp_tracer_box_color) Col("tracer color", esp_tracer_color);
    }
}

void DrawOffScreen(Snapshot&)
{
    Tog("off-screen arrows", &oof_enabled);
    if (oof_enabled) {
        Cmb("arrow style", &oof_arrow_style, kOofStyles, IM_ARRAYSIZE(kOofStyles));
        Num("arrow size", &oof_size, 2.0f, 20.0f, "%.0f px");
        Num("ring radius", &oof_radius, 4.0f, 200.0f, "%.0f px");
        Col("arrow color", oof_color);
        Tog("arrow outline", &oof_outline);
        if (oof_outline) Col("outline color", oof_outline_color);
        Tog("show names", &oof_names);
        Tog("show distance", &oof_distance);
    }
}

void DrawChams(Snapshot&)
{
    Tog("chams", &chams_enabled);
    if (chams_enabled) Col("chams color", chams_color);
    Tog("mesh chams", &mesh_chams);
    if (mesh_chams) Col("mesh color", mesh_chams_color);
    Tog("draw expanded hitbox", &render_expanded_hitbox);
    if (render_expanded_hitbox || hitbox_expander_enabled)
        Num("hitbox size", &hitbox_expander_value, 1.0f, 50.0f, "%.0f");
}

void DrawNpcEsp(Snapshot&)
{
    Tog("npc bots", &world_bots_enabled);
    Tog("npc animals", &world_animals_enabled);
    if (world_bots_enabled || world_animals_enabled) {
        Num("npc distance", &world_esp_distance, 0.0f, 20000.0f, "%.0f studs");
        Tog("npc labels", &world_esp_labels);
    }
}

void DrawFrameRate(Snapshot&)
{
    Tog("FPS counter", &show_fps);
    Tog("FPS cap", &fps_cap_enabled);
    if (fps_cap_enabled)
        Num("game fps cap", &fps_cap_value, 10.0f, 360.0f, "%.0f");
    Num("overlay cap", &overlay_fps_cap, 0.0f, 360.0f, "%.0f");
}

void DrawCamera(Snapshot&)
{
    Tog("FOV changer", &fov_changer_enabled);
    if (fov_changer_enabled)
        Num("field of view", &fov_value, 20.0f, 120.0f, "%.0f");
    Tog("freecam", &freecam_enabled);
    if (freecam_enabled)
        Num("freecam speed", &freecam_speed, 10.0f, 250.0f, "%.0f");
    Key("freecam key", &freecam_keybind, freecam_enabled);
}

void DrawMovement(Snapshot&)
{
    Tog("noclip", &noclip_enabled);
    Key("noclip key", &noclip_keybind, noclip_enabled);
    Tog("flight", &flight_enabled);
    if (flight_enabled) {
        Num("fly speed", &flight_value, 10.0f, 1000.0f, "%.0f");
        Tog("stealth flight", &flight_stealth);
        if (flight_stealth) {
            Num("speed cap", &flight_max_speed, 10.0f, 300.0f, "%.0f");
            Num("climb cap", &flight_max_climb, 5.0f, 200.0f, "%.0f");
            Num("ramp", &flight_accel, 20.0f, 600.0f, "%.0f");
        }
        Tog("hold to fly", &flight_hold_mode);
        Key("flight key", &flight_keybind, flight_enabled);
    }
    Tog("walkspeed", &walkspeed_enabled);
    if (walkspeed_enabled)
        Num("speed", &walkspeed_value, 0.0f, 1000.0f, "%.0f");
    Key("walkspeed key", &walkspeed_keybind, walkspeed_enabled);
    Tog("infinite jump", &infinite_jump_enabled);
    if (infinite_jump_enabled)
        Num("jump power", &infinite_jump_power, 25.0f, 500.0f, "%.0f");
}

void DrawPhysics(Snapshot&)
{
    Tog("gravity", &gravity_enabled);
    if (gravity_enabled)
        Num("gravity value", &gravity_value, 1.0f, 2000.0f, "%.0f");
    Key("gravity key", &gravity_keybind, gravity_enabled);
    Tog("hip height", &hipheight_enabled);
    if (hipheight_enabled)
        Num("hip value", &hipheight_value, 0.0f, 20.0f, "%.1f");
    Key("hip height key", &hipheight_keybind, hipheight_enabled);
    Tog("fling", &fling_enabled);
    if (fling_enabled)
        Num("spin rate", &fling_power, 60.0f, 2000.0f, "%.0f rad/s");
    Key("fling key", &fling_keybind, fling_enabled);
    Tog("no fall", &nofall_enabled);
    Key("no fall key", &nofall_keybind, nofall_enabled);
    Tog("jump power", &jumppower_enabled);
    if (jumppower_enabled)
        Num("jump value", &jumppower_value, 10.0f, 500.0f, "%.0f");
    Key("jump power key", &jumppower_keybind, jumppower_enabled);
}

void DrawKeepAlive(Snapshot&)
{
    Tog("anti-afk", &antiafk_enabled);
    if (antiafk_enabled)
        Num("interval", &antiafk_interval, 1.0f, 15.0f, "%.0f min");
}

void DrawTeleport(Snapshot&)
{
    Tog("click teleport", &click_teleport_enabled);
    if (click_teleport_enabled) {
        Num("max range", &click_teleport_distance, 5.0f, 200.0f, "%.0f studs");
        Num("land above by", &tp_land_height, 0.0f, 12.0f, "%.0f studs");
    }
    Key("teleport key", &click_teleport_keybind, click_teleport_enabled);
}

void DrawLighting(Snapshot&)
{
    Tog("fullbright", &fullbright_enabled);
    if (fullbright_enabled)
        Num("brightness", &fullbright_value, 1.0f, 5.0f, "%.1f");
    Tog("ambient", &world_ambient_enabled);
    if (world_ambient_enabled) Col("ambient color", world_ambient_color);
    Tog("fog", &world_fog_enabled);
    if (world_fog_enabled) {
        Num("fog start", &world_fog_start, 0.0f, 5000.0f, "%.0f");
        Num("fog end", &world_fog_end, 1.0f, 20000.0f, "%.0f");
        Col("fog color", world_fog_color);
    }
    Tog("exposure", &world_exposure_enabled);
    if (world_exposure_enabled)
        Num("exposure value", &world_exposure_value, -3.0f, 3.0f, "%.2f");
    Tog("no shadows", &noshadow_enabled);
    Tog("no grass", &nograss_enabled);
    Tog("night", &night_enabled);
    if (night_enabled)
        Num("night level", &night_level, 0.0f, 1.0f, "%.2f");
    Tog("time of day", &world_time_enabled);
    if (world_time_enabled)
        Num("hour", &world_time_value, 0.0f, 24.0f, "%.2f h");
}

void DrawServers(Snapshot& snap)
{
    {
        const char* game = snap.game[0] ? snap.game
                           : (snap.attached ? "loading..." : "not attached");
        const char* place = snap.place[0] ? snap.place : "not in an experience";
        ImGui::TextWrapped("%s", game);
        ImGui::TextWrapped("%s", place);
    }
    char fresh[64];
    if (snap.servers_busy)        snprintf(fresh, sizeof(fresh), "%s", "request running");
    else if (snap.server_age_sec < 0)  snprintf(fresh, sizeof(fresh), "%s", "never fetched");
    else if (snap.server_age_sec < 120) snprintf(fresh, sizeof(fresh), "%ds ago", snap.server_age_sec);
    else snprintf(fresh, sizeof(fresh), "%dm ago", snap.server_age_sec / 60);

    if (ImGui::Button(snap.servers_busy ? "fetching..." : "refresh servers")) {
        if (g_act.servers_refresh) g_act.servers_refresh();
    }
    ImGui::SameLine();
    if (ImGui::Button("rejoin")) {
        if (g_act.servers_rejoin) g_act.servers_rejoin();
    }
    ImGui::SameLine();
    if (ImGui::Button("hop")) {
        if (g_act.servers_hop_openest) g_act.servers_hop_openest();
    }
    ImGui::TextDisabled("%s", fresh);

    int shown = snap.server_count;
    if (shown < 0) shown = 0;
    if (shown > Snapshot::kMaxServers) shown = Snapshot::kMaxServers;
    if (ImGui::BeginChild("##servers", ImVec2(0.0f, 150.0f), true)) {
        if (shown < 1) {
            ImGui::TextDisabled("%s", snap.servers_msg[0] ? snap.servers_msg
                                                          : "(no server list yet)");
        }
        for (int i = 0; i < shown; ++i) {
            const Snapshot::ServerRow& r = snap.servers[i];
            const bool ours = (r.state == 1 || r.state == 3);
            char lab[80];
            snprintf(lab, sizeof(lab), "#%d  %s  %s##srv%d", i + 1, r.id, r.detail, i);
            if (ours) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
            if (ImGui::Selectable(lab)) {
                if (g_act.servers_join) g_act.servers_join(i);
            }
            if (ours) ImGui::PopStyleColor();
        }
        if (snap.servers_msg[0]) ImGui::TextDisabled("%s", snap.servers_msg);
    }
    ImGui::EndChild();
}

void DrawSkybox(Snapshot& snap)
{
    Tog("skybox changer", &skybox_changer_enabled);
    if (skybox_changer_enabled && snap.skybox_names && snap.skybox_count > 0) {
        if (skybox_type < 0 || skybox_type >= snap.skybox_count) skybox_type = 0;
        Cmb("preset", &skybox_type, snap.skybox_names, snap.skybox_count);
    }
    if (snap.skybox_note[0]) ImGui::TextWrapped("%s", snap.skybox_note);
}

void DrawWaypoints(Snapshot&)
{
    Tog("show on overlay", &show_waypoints);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("waypoint name");
    ImGui::SameLine(140);
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##wname", waypoint_name_buf, sizeof(waypoint_name_buf));
    ImGui::PopItemWidth();
    if (ImGui::Button("pin this position")) {
        if (g_act.waypoint_add) g_act.waypoint_add(waypoint_name_buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("fill xyz")) {
        if (g_act.waypoint_fill) g_act.waypoint_fill();
    }
    Num("x", &waypoint_edit_x, -20000.0f, 20000.0f, "%.1f");
    Num("y", &waypoint_edit_y, -20000.0f, 20000.0f, "%.1f");
    Num("z", &waypoint_edit_z, -20000.0f, 20000.0f, "%.1f");
    if (ImGui::Button("add custom xyz")) {
        if (g_act.waypoint_add_xyz)
            g_act.waypoint_add_xyz(waypoint_name_buf, waypoint_edit_x,
                                   waypoint_edit_y, waypoint_edit_z);
    }
    if (waypoint_n > 0) {
        if (ImGui::BeginChild("##wps", ImVec2(0.0f, 110.0f), true)) {
            for (int i = 0; i < waypoint_n; ++i) {
                char lab[96];
                snprintf(lab, sizeof(lab), "%s  (%.0f, %.0f, %.0f)##wp%d",
                         waypoint_names[i], waypoint_x[i], waypoint_y[i], waypoint_z[i], i);
                if (ImGui::Selectable(lab)) {
                    if (g_act.waypoint_goto) g_act.waypoint_goto(i);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    if (g_act.waypoint_remove) g_act.waypoint_remove(i);
                    break;
                }
            }
        }
        ImGui::EndChild();
    }
}

void DrawPlayers(Snapshot& snap)
{
    // Re-clamp the selection every frame: rows come and go with the cache.
    if (snap.player_count < 1)                  s_player_sel = -1;
    else if (s_player_sel < 0)                  s_player_sel = 0;
    else if (s_player_sel >= snap.player_count) s_player_sel = snap.player_count - 1;
    const bool have_sel = (s_player_sel >= 0 && s_player_sel < snap.player_count);
    const Snapshot::PlayerRow& sel = snap.players[have_sel ? s_player_sel : 0];

    const char* watched = nullptr;
    for (int i = 0; i < snap.player_count; ++i)
        if (snap.players[i].state == 2) { watched = snap.players[i].name; break; }

    if (ImGui::BeginChild("##players", ImVec2(0.0f, 190.0f), true)) {
        if (snap.player_count < 1) {
            ImGui::TextDisabled("%s", snap.player_note[0] ? snap.player_note
                                                          : "(nobody resolved yet)");
        }
        for (int i = 0; i < snap.player_count; ++i) {
            const Snapshot::PlayerRow& r = snap.players[i];
            char lab[96];
            snprintf(lab, sizeof(lab), "%s##u%u", r.name, r.user_id);
            const bool picked = (s_player_sel == i);
            if (ImGui::Selectable(lab, picked)) s_player_sel = i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                have_sel && g_act.tp_to_player)
                g_act.tp_to_player(r.user_id);
            // detail on the row's right edge
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            ImFont* f = ImGui::GetFont();
            const float dw = f->CalcTextSizeA(f->FontSize, 1e9f, 0.0f, r.detail).x;
            ImGui::GetWindowDrawList()->AddText(
                f, f->FontSize,
                ImVec2(hi.x - dw - 6.0f, lo.y + (hi.y - lo.y - f->FontSize) * 0.5f),
                IM_COL32(255, 255, 255, 140), r.detail);
        }
        if (snap.player_note[0]) ImGui::TextDisabled("%s", snap.player_note);
    }
    ImGui::EndChild();

    if (Btn("rescan players") && g_act.rescan) g_act.rescan();
    if (Btn(have_sel ? sel.name : "pick a row for teleport")) {
        if (have_sel && g_act.tp_to_player) g_act.tp_to_player(sel.user_id);
    }
    if (!snap.spectating) {
        if (Btn("watch (spectate)") && have_sel && g_act.spectate_player)
            g_act.spectate_player(sel.user_id);
    } else {
        if (Btn("stop watching") && g_act.spectate_stop) g_act.spectate_stop();
        if (watched) ImGui::TextDisabled("watching: %s", watched);
    }
    Num("look from behind", &spectate_distance, 4.0f, 40.0f, "%.0f studs");
    Num("look from above", &spectate_height, 0.0f, 14.0f, "%.0f studs");
    Num("land above by", &tp_land_height, 0.0f, 12.0f, "%.0f studs");

    const char* tag = have_sel ? (sel.account[0] ? sel.account : sel.name) : "pick a row";
    ImGui::TextDisabled("acting on: %s", tag);
    if (Btn("blacklist selected") && have_sel)
        AddPlayerTag(player_blacklist, player_blacklist_n,
                     sel.account[0] ? sel.account : sel.name);
    if (Btn("whitelist selected") && have_sel)
        AddPlayerTag(player_whitelist, player_whitelist_n,
                     sel.account[0] ? sel.account : sel.name);
    if (Btn(sel.friended ? "unfriend selected" : "friend selected") && have_sel &&
        g_act.toggle_friend)
        g_act.toggle_friend(sel.user_id);
    Col("whitelist color", whitelist_esp_color);

    if (player_blacklist_n > 0) {
        if (ImGui::BeginChild("##bl", ImVec2(0.0f, 70.0f), true)) {
            for (int i = 0; i < player_blacklist_n; ++i) {
                if (ImGui::Selectable(player_blacklist[i]))
                    RemovePlayerTagAt(player_blacklist, player_blacklist_n, i);
            }
        }
        ImGui::EndChild();
    }
    if (player_whitelist_n > 0) {
        if (ImGui::BeginChild("##wl", ImVec2(0.0f, 70.0f), true)) {
            for (int i = 0; i < player_whitelist_n; ++i) {
                if (ImGui::Selectable(player_whitelist[i]))
                    RemovePlayerTagAt(player_whitelist, player_whitelist_n, i);
            }
        }
        ImGui::EndChild();
    }
}

void DrawConfigs(Snapshot&)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("config name");
    ImGui::SameLine(140);
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##cname", s_cfg_name, sizeof(s_cfg_name));
    ImGui::PopItemWidth();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("rename to");
    ImGui::SameLine(140);
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##crename", s_cfg_rename, sizeof(s_cfg_rename));
    ImGui::PopItemWidth();

    const char* shown = s_cfg_name[0] ? s_cfg_name : "no name";
    if (ImGui::Button("save")) {
        if (s_cfg_name[0] && g_act.config_save && g_act.config_save(s_cfg_name))
            RefreshConfigs();
    }
    ImGui::SameLine();
    if (ImGui::Button("load")) {
        if (s_cfg_name[0] && g_act.config_load) g_act.config_load(s_cfg_name);
    }
    ImGui::SameLine();
    if (ImGui::Button("delete")) {
        if (s_cfg_name[0] && g_act.config_delete && g_act.config_delete(s_cfg_name)) {
            s_cfg_name[0] = '\0';
            s_cfg_sel = -1;
            RefreshConfigs();
        }
    }
    if (ImGui::Button("rename")) {
        if (s_cfg_name[0] && s_cfg_rename[0] && g_act.config_rename &&
            g_act.config_rename(s_cfg_name, s_cfg_rename)) {
            snprintf(s_cfg_name, sizeof(s_cfg_name), "%s", s_cfg_rename);
            s_cfg_rename[0] = '\0';
            s_cfg_sel = -1;
            RefreshConfigs();
        }
    }
    ImGui::TextDisabled("%s", shown);

    if (!s_configs_fetched) RefreshConfigs();
    if (ImGui::BeginChild("##cfgs", ImVec2(0.0f, 110.0f), true)) {
        if (s_configs.empty()) ImGui::TextDisabled("(nothing saved yet)");
        for (int i = 0; i < (int)s_configs.size(); ++i) {
            if (ImGui::Selectable(s_configs[i].c_str(), s_cfg_sel == i)) {
                s_cfg_sel = i;
                snprintf(s_cfg_name, sizeof(s_cfg_name), "%s", s_configs[i].c_str());
            }
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("open config folder") && g_act.open_config_folder)
        g_act.open_config_folder();
    ImGui::SameLine();
    if (ImGui::Button("refresh list")) RefreshConfigs();
}

void DrawDebug(Snapshot& snap)
{
    auto rows = [](const char* title, const std::vector<Snapshot::Row>& v) {
        if (v.empty()) return;
        ImGui::SeparatorText(title);
        for (const Snapshot::Row& r : v) {
            if (r.state == 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
            ImGui::Text("%s: %s", r.key, r.value);
            if (r.state == 0) ImGui::PopStyleColor();
        }
    };
    rows("runtime", snap.runtime);
    rows("camera + player", snap.camera);

    if (snap.can_test) {
        ImGui::SeparatorText("offset write tests");
        struct { const char* label; int id; } kTests[] = {
            { "position: +10 studs up", 0 },
            { "velocity: launch up", 1 },
            { "float probe 0xA0..0x1C0", 2 },
            { "camera probe 0xC0..0x160", 3 },
            { "survival: +25 studs, 500ms", 4 },
            { "survival ANCHORED: +25, released", 5 },
            { "who owns the position?", 6 },
            { "continuous write: hammer 500ms", 7 },
            { "camera: does Scriptable stick?", 8 },
            { "velocity: impulse survive 400ms", 9 },
            { "velocity: hammer 400ms", 10 },
            { "world probe: wall check source", 11 },
        };
        for (const auto& t : kTests) {
            if (ImGui::Button(t.label) && g_act.run_test) g_act.run_test(t.id);
        }
    }
    if (snap.test_result[0]) ImGui::TextWrapped("%s", snap.test_result);

    if (ImGui::BeginChild("##log", ImVec2(0.0f, 150.0f), true)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            for (const std::string& line : g_log_lines)
                ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopStyleColor();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    if (ImGui::Button("clear log")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_lines.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("copy log")) {
        std::string all;
        std::lock_guard<std::mutex> lock(g_log_mutex);
        for (const std::string& l : g_log_lines) { all += l; all += "\n"; }
        ImGui::SetClipboardText(all.c_str());
    }
}

void DrawExplorer(Snapshot&)
{
    if (!g_act.draw_dex) {
        ImGui::TextWrapped("The browser is not wired into this build.");
        return;
    }
    g_act.draw_dex();
}

// =============================================================================
//  the main column
// =============================================================================
void FeatureRow(const FeatureDef& f, int idx)
{
    ImGui::PushID(f.id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetTextLineHeightWithSpacing() + 5.0f;
    ImGui::Selectable("##row", s_open[idx], 0, ImVec2(w, h));
    const bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(pos.x + 6.0f, pos.y + 2.0f), IM_COL32(255, 255, 255, 255), f.label);
    const char* arrow = s_open[idx] ? "-" : "+";
    const float aw = ImGui::CalcTextSize(arrow).x;
    dl->AddText(ImVec2(pos.x + w - aw - 8.0f, pos.y + 2.0f),
                IM_COL32(255, 255, 255, 255), arrow);
    if (clicked) {
        s_open[idx] = !s_open[idx];
        PersistOpenToGlobal();
    }
    ImGui::PopID();
}

void DrawMainColumn(Snapshot& snap)
{
    // the name, at the top
    const char* name = "ROBLOX EXTERNAL";
    const float nw = ImGui::CalcTextSize(name).x;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - nw) * 0.5f);
    ImGui::TextUnformatted(name);
    ImGui::TextDisabled("%s", snap.status[0] ? snap.status
                          : (snap.attached ? "attached" : "waiting for roblox"));
    ImGui::Separator();

    for (int i = 0; i < kFeatureCount; ++i)
        FeatureRow(kFeatures[i], i);

    ImGui::Separator();
    Key("menu", &menu_toggle_keybind, menu_toggle_keybind != 0);
    if (!menu_toggle_keybind)
        ImGui::TextWrapped("unbound - you could not open the menu again!");
}

// =============================================================================
//  theme pipeline (kept: BuildPalette drives float plates, SyncTheme and the
//  headless palette checks — it never touches this menu's chrome)
// =============================================================================
struct ThemeDef
{
    const char* name;
    const char* palette;
    const char* blurb;
    bool        light;
};

const ThemeDef kThemes[] = {
    { "Obsidian", "Axiom",   "Near-black glass, hairlines, one white accent",     false },
    { "Liquid",   "Liquid",  "Blue glass: rim light, chroma, moving shine",       false },
    { "Ink",      "Ink",     "Pure black, pure white. Nothing else",              false },
    { "Frost",    "Frost",   "Light frosted glass over dark ink",                 true  },
    { "Paper",    "Paper",   "Opaque white, black ink, maximum legibility",       true  },
    { "Ember",    "Ember",   "Molten gold over cold volcanic glass",              false },
    { "Aurora",   "Aurora",  "Cyan into violet iridescence",                      false },
    { "Verdant",  "Verdant", "Jade and mint on obsidian",                         false },
    { "Crimson",  "Crimson", "Garnet and rose",                                   false },
};
constexpr int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));

int ThemeClamped() { return ui_theme < 0 ? 0 : (ui_theme >= kThemeCount ? kThemeCount - 1 : ui_theme); }
const ThemeDef& ThisTheme() { return kThemes[ThemeClamped()]; }

// The rainbow derives from the CLOCK, never from ui_accent_color: a config saved
// mid-cycle stores the user's pick, and switching it off needs no stash.
void AccentFromSettings(obs::Palette& p)
{
    ImVec4 a = ImVec4(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
    ImVec4 b = ui_accent2_auto ? obs::Shade(a, -0.26f)
                               : ImVec4(ui_accent2_color[0], ui_accent2_color[1],
                                        ui_accent2_color[2], 1.0f);
    bool have_secondary = !ui_accent2_auto;

    const float speed = ui_rainbow_speed > 0.0f ? ui_rainbow_speed : 0.0001f;
    const float t = obs::anim::Clock() * speed;

    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(a.x, a.y, a.z, h, s, v);
    float h2 = h, s2 = s, v2 = v;
    ImGui::ColorConvertRGBtoHSV(b.x, b.y, b.z, h2, s2, v2);

    switch (ui_rainbow) {
    case 1:
        a = obs::Hue(t, ui_rainbow_sat, 1.0f, 1.0f);
        break;
    case 2:
        a  = obs::Hue(t, ui_rainbow_sat, 1.0f, 1.0f);
        b  = obs::Hue(t + ui_rainbow_span + 0.34f, ui_rainbow_sat, 0.92f, 1.0f);
        have_secondary = true;
        break;
    case 3:
        {
            const float k = 0.5f + 0.5f * sinf(t * 6.2831853f);
            a = obs::Hue(h + 0.02f * k, s * (0.45f + 0.55f * k), v * (0.80f + 0.20f * k), 1.0f);
            b = obs::Hue(h + 0.02f * k + ui_rainbow_span, s * (0.85f + 0.15f * k), v * 0.7f, 1.0f);
            have_secondary = true;
        }
        break;
    case 4:
        b = obs::Hue(t * 0.7f + 0.5f, ui_rainbow_sat, 0.95f, 1.0f);
        have_secondary = true;
        break;
    default: break;
    }
    (void)s2; (void)v2;
    obs::ApplyAccent(p, a, have_secondary ? &b : nullptr);
}

} // namespace

obs::Palette BuildPalette()
{
    const ThemeDef& th = ThisTheme();
    obs::Palette p = obs::PaletteAxiom();
    if (const obs::Palette* preset = obs::PaletteByName(th.palette)) p = *preset;
    p.light = th.light;

    p.mat.sheen    = ui_mat_sheen;
    p.mat.edge     = ui_mat_edge;
    p.mat.bloom    = ui_mat_bloom;
    p.mat.chroma   = ui_mat_chroma;
    p.mat.frost    = ui_mat_frost;
    p.mat.sweep    = ui_mat_sweep;
    p.mat.aurora   = ui_mat_aurora + ui_glow * 0.6f;
    p.mat.shimmer  = ui_mat_shimmer;
    p.mat.lift     = ui_mat_lift;
    p.mat.gradient = ui_mat_gradient;
    p.mat.animate  = ui_anim;
    p.mat.speed    = ui_anim_speed;
    p.mat.polish   = ui_polish ? 1.0f : 0.0f;
    p.mat.ApplyPolish();

    p.opacity = 1.0f - (ui_transparency / 100.0f) * 0.70f;
    p.text_stays_opaque = true;
    p.contrast = ui_contrast;

    if (ui_tint_amount > 0.0f)
        obs::TintSurfaces(p, ImVec4(ui_tint[0], ui_tint[1], ui_tint[2], 1.0f), ui_tint_amount);
    AccentFromSettings(p);
    obs::BoostContrast(p, ui_contrast);

    const float r = ui_rounded_corners ? ui_corner_radius : 0.0f;
    p.corner_lg = r * 0.72f + obs::S(3.0f);
    p.corner    = r * 0.46f + obs::S(2.0f);
    p.corner_sm = r * 0.30f + obs::S(1.5f);
    return p;
}

// -----------------------------------------------------------------------------
//  public API (menuui scope — the anon block above has been closed)
//  public API
// -----------------------------------------------------------------------------
void SetActions(const Actions& a) { g_act = a; }

void TickKeybinds()
{
    // Frames of grace between "a slot started capturing" and the first key that may
    // be taken (the click that started capture is still down on the next frame).
    static const void* s_prev_slot = nullptr;
    static int s_settle = 0;
    if ((const void*)s_wait_slot != s_prev_slot) {
        s_prev_slot = (const void*)s_wait_slot;
        s_settle = s_wait_slot ? 6 : 0;
    }
    if (!s_wait_slot) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { s_wait_slot = nullptr; return; }
    if (s_settle > 0) { --s_settle; return; }
    if (!g_act.poll_key) return;
    const int vk = g_act.poll_key();
    if (vk) {
        *s_wait_slot = vk;
        s_wait_slot = nullptr;
        LogLine("bound %s", (g_act.key_name ? g_act.key_name(vk) : "key"));
    }
}

void Render(Snapshot& snap)
{
    // Palette pipeline stays alive for hosts and the headless theme checks —
    // then the two-colour push below covers every pixel THIS menu draws, so no
    // theme can reach the chrome either way.
    obs::ApplyTheme(BuildPalette(), obs::ActiveConfig());

    RegisterLegacyOnce();
    SyncOpenFromGlobal();
    PushBareStyle();

    bool main_open = true;
    ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f), ImGuiCond_Always);
    // Near the top, not centred: the column is taller than a third of the
    // screen and a centred first placement pushes the menu-key footer off a
    // 900px display.
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 120.0f, 24.0f),
        ImGuiCond_FirstUseEver);
    if (ImGui::Begin("##main", &main_open,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoResize)) {
        DrawMainColumn(snap);
        // captured INSIDE Begin/End: after End() the stack is empty and
        // GetWindowPos() reports the fallback window, not ours (the harness
        // compares every visible vertex against this rect).
        s_win_pos = ImGui::GetWindowPos();
        s_win_size = ImGui::GetWindowSize();
    }
    ImGui::End();
    if (!main_open) g_request_exit = true;

    // each open feature's settings window, to the right of the column
    for (int i = 0; i < kFeatureCount; ++i) {
        if (!s_open[i]) continue;
        if (!s_placed[i]) {
            s_placed[i] = true;
            // Grid cascade beside the column: window j among the open set goes
            // to column j%3 / row j/3, so three dropdowns sit side by side and
            // none buries another (the old stacked offsets overlapped ~all of
            // a 460px window). Placed on first open, then the user's free.
            int j = 0;
            for (int k = 0; k < i; ++k) if (s_open[k]) ++j;
            ImGui::SetNextWindowPos(
                ImVec2(s_win_pos.x + s_win_size.x + 8.0f + (float)(j % 3) * 298.0f,
                       s_win_pos.y + 24.0f + (float)(j / 3) * 468.0f),
                ImGuiCond_Always);
            // Fixed start height with room to scroll: a content-sized window
            // (the esp column is ~900px) hangs off the bottom of a 900px
            // display. FirstUseEver keeps a user's own resize across reopen.
            ImGui::SetNextWindowSize(ImVec2(290.0f, 460.0f), ImGuiCond_FirstUseEver);
        }
        bool w_open = true;
        ImGui::PushID(kFeatures[i].id);
        char title[80];
        snprintf(title, sizeof(title), "%s##set", kFeatures[i].label);
        if (ImGui::Begin(title, &w_open, ImGuiWindowFlags_NoCollapse)) {
            kFeatures[i].draw(snap);
            s_feat_pos[i] = ImGui::GetWindowPos();
            s_feat_size[i] = ImGui::GetWindowSize();
        }
        ImGui::End();
        if (!w_open) {
            s_open[i] = false;
            PersistOpenToGlobal();
        }
        ImGui::PopID();
    }

    PopBareStyle();
}

// Fonts are the one part of a theme that cannot change inside a frame.
bool SyncTheme()
{
    obs::ThemeConfig cfg;
    cfg.ui_scale        = ui_scale;
    cfg.base_font_size  = ui_font_size;
    cfg.title_font_size = ui_title_size;
    cfg.mono_font_size  = ui_font_size * 0.87f;
    cfg.move_from_title_bar_only = true;

    const obs::ThemeConfig& cur = obs::ActiveConfig();
    const bool same = cur.ui_scale == cfg.ui_scale &&
                      cur.base_font_size == cfg.base_font_size &&
                      cur.title_font_size == cfg.title_font_size &&
                      cur.mono_font_size == cfg.mono_font_size;
    if (same) return false;

    if (ImGui::GetIO().Fonts->Locked) {
        LogLine("ui scale change deferred: the font atlas is locked inside a frame");
        return false;
    }
    if (!obs::LoadFonts(cfg))
        LogLine("glass obsidian: no system font for the new scale, kept the fallback");
    obs::ApplyTheme(BuildPalette(), cfg);
    LogLine("ui scale %.2fx, font %.0fpx", cfg.ui_scale, cfg.base_font_size);
    return true;
}

// The config file's window list: ids of the OPEN settings windows (see the
// header comment in menu_ui.h — the key name is historic).
void SetClosedGroups(const char* list)
{
    snprintf(ui_closed_groups, sizeof(ui_closed_groups), "%s", list ? list : "");
}
void GetClosedGroups(char* out, size_t n)
{
    if (!out || !n) return;
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < kFeatureCount; ++i) {
        if (!s_open[i]) continue;
        const size_t len = strlen(kFeatures[i].id);
        if (used + len + 2 >= n) break;
        if (used) out[used++] = ';';
        memcpy(out + used, kFeatures[i].id, len);
        used += len;
        out[used] = '\0';
    }
}

// Float plates (fps counter, media badge): palette tokens, two-colour safe.
void PushFloatWindowStyle(float bg_alpha)
{
    const obs::Palette& p = obs::ActivePalette();
    const float round = ui_rounded_corners ? obs::FClamp(p.corner, 0.0f, 16.0f) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, p.text_dim);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(p.panel.x, p.panel.y, p.panel.z, bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, p.edge);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, round);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, round * 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, p.mat.edge > 0.0f ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(obs::S(8.0f), obs::S(5.0f)));
}

void PopFloatWindowStyle()
{
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
}

int  CurrentPage() { return s_page; }
void LastRect(ImVec2* pos, ImVec2* size)
{
    if (pos)  *pos  = s_win_pos;
    if (size) *size = s_win_size;
}
int FeatureCount() { return kFeatureCount; }
bool FeatureRect(int idx, ImVec2* pos, ImVec2* size)
{
    if (idx < 0 || idx >= kFeatureCount) return false;
    if (!s_open[idx] || s_feat_size[idx].x <= 0.0f) return false;
    if (pos)  *pos  = s_feat_pos[idx];
    if (size) *size = s_feat_size[idx];
    return true;
}
void SetPage(int p)
{
    s_page = p < 0 ? 0 : (p >= kPageCount ? kPageCount - 1 : p);
}
int  PageCount() { return kPageCount; }
const char* PageName(int p) { return (p >= 0 && p < kPageCount) ? kPages[p] : ""; }

bool HasGroup(const char* id)
{
    for (const std::string& g : LegacyGroups())
        if (g == id) return true;
    return false;
}

} // namespace menuui

namespace menuui {

// The one snapshot copy the UI cannot get wrong by arithmetic — contract in
// menu_ui.h (clamped copies, scalars always current, rows only when changed).
void FillServers(Snapshot& s, const ::features::servers::View& sv)
{
    int n = sv.count;
    if (n < 0) n = 0;
    if (n > Snapshot::kMaxServers) n = Snapshot::kMaxServers;

    const int current = (sv.current >= 0 && sv.current < n) ? sv.current : -1;

    const bool same = (n == s.server_count) && (current == s.server_current) &&
                      (sv.busy == s.servers_busy) &&
                      (strcmp(sv.msg, s.servers_msg) == 0);
    s.servers_busy       = sv.busy;
    s.servers_can_rejoin = sv.can_rejoin;
    s.server_age_sec     = sv.age_sec;
    if (same) return;

    snprintf(s.servers_msg, sizeof(s.servers_msg), "%s", sv.msg);
    s.server_count   = n;
    s.server_current = current;
    for (int i = 0; i < n; ++i) {
        const ::features::servers::Row& src = sv.rows[i];
        Snapshot::ServerRow& d = s.servers[i];
        if (!::features::serversjson::ShortGuid(src.id, d.id, sizeof(d.id)))
            snprintf(d.id, sizeof(d.id), "%s", src.id);
        snprintf(d.detail, sizeof(d.detail), "%d/%d%s", src.playing, src.max_players,
                 src.state == 3 ? " full" : "");
        d.state = src.state;
    }
}

} // namespace menuui
