// -----------------------------------------------------------------------------
//  ui/menu_ui.cpp  --  the Axiom menu: nav rail, card grid, nine pages
// -----------------------------------------------------------------------------
//  Layout contract (worth knowing before editing):
//
//    window = obsidian::ObsidianWindow   header: wordmark, status chip, collapse,
//                                        close. Nothing is painted outside its rect.
//      rail = obsidian::SidebarBegin     place name, nav rows, pinned session card
//      page = a scrolling child          PageTitle, then GroupLabel + card grid
//
//    Inside a page every setting is ONE card. Cards sit in a 2-column grid with a
//    fixed row height, so a card that grows a slider keeps its row aligned and the
//    eye can scan down a straight edge. Full-width cards (GridCell(2)) are
//    reserved for a page's master switch and for anything that needs room: notes,
//    the config list, the log.
//
//  Nothing here touches the game. Memory-derived text arrives pre-formatted in
//  menuui::Snapshot; the platform calls arrive as menuui::Actions. Settings are
//  read and written straight through globals.h, so there is one copy of each.
// -----------------------------------------------------------------------------
#include "ui/menu_ui.h"

#include "globals.h"
#include "media_tracker.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace obs = obsidian;

namespace menuui {
namespace {

Actions g_act;
int     s_page      = 0;
int*    s_wait_slot = nullptr;      // keybind slot currently capturing a key

// config page state (owned here; the app supplies only the file operations)
char     s_cfg_name[128]   = "";
char     s_cfg_rename[128] = "";
std::vector<std::string> s_configs;
int      s_cfg_sel         = -1;
bool     s_configs_fetched = false;

// the ui page edits the accent through an RGBA copy (stock picker wants 4 floats)
float    s_accent4[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
int      s_accent_sel = -1;

// players page: which row the action cards act on. Held here, not in the app --
// the list is rebuilt every frame, so an index has to be clamped where it is used.
int      s_player_sel = -1;

void RefreshConfigs()
{
    s_configs = g_act.list_configs ? g_act.list_configs() : std::vector<std::string>();
    s_configs_fetched = true;
}

// =============================================================================
//  pages
// =============================================================================
const char* const kPages[] = { "aimbot", "visuals", "movement", "world", "players",
                               "keybinds", "theme", "config", "debug" };
constexpr int kPageCount = (int)(sizeof(kPages) / sizeof(kPages[0]));

obs::IconDrawFn kPageIcons[kPageCount] = {
    obs::icon::Crosshair, obs::icon::Eye,    obs::icon::Sliders, obs::icon::Globe,
    obs::icon::Users,     obs::icon::Keyboard, obs::icon::Palette,
    obs::icon::Folder,    obs::icon::Terminal,
};

bool s_nav_live[kPageCount]  = {};      // drives the rail's dot + label brightness
char s_nav_hint[kPageCount][16] = {};   // "N on"

const char* kAimTypes[]  = { "Camera", "Mouse position" };
const char* kBones[]     = { "Head", "Upper torso", "Lower torso", "Left hand",
                             "Right hand", "Left foot", "Right foot" };
const char* kBoxStyles[] = { "Full box", "Corners" };

// TYPOGRAPHY: this language labels in caps, and obsidian does the shouting for
// you -- MicroCaps (card titles, group rules, page titles, the hero heading),
// chips and keycaps all upper-case their text inside the widget. So source here
// is written in ordinary words. What must NOT be lowercase is the *prose*: notes,
// hints and subtitles are real sentences, so they get capitals, and the acronyms
// they contain get theirs (FOV, ESP, PID, Escape, Roblox). A page that reads
// "fov changer" next to "ESP" is what made the old menu look unfinished.

// -----------------------------------------------------------------------------
//  layout helpers
// -----------------------------------------------------------------------------
void Group(const char* label)
{
    obs::GroupLabel(label);
    obs::GridCfg g;
    g.columns = 2;
    obs::GridBegin(g);
}
void EndGroup()
{
    obs::GridEnd();
    ImGui::Dummy(ImVec2(0.0f, obs::S(4.0f)));
}

int CountOn(std::initializer_list<int> flags)
{
    int n = 0;
    for (int f : flags) n += f ? 1 : 0;
    return n;
}

// The master switch of a page: full-width card, feature name in the title font,
// a one-line description, and the pill. "What is on" should never need scrolling.
void Hero(const char* id, const char* title, const char* desc, bool* v)
{
    obs::GridCell(2, obs::S(64.0f));
    obs::CardBegin(id, ImVec2(0.0f, 0.0f),
                   (obs::CardFlags)(obs::Card_Hoverable | (*v ? obs::Card_Live : 0)));
    if (obs::CardClicked()) *v = !*v;

    const obs::Palette& p = obs::ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 o = obs::CardOrigin();
    const float  H = obs::CardHeight();
    const float  pad = obs::S(obs::AM().card_pad);
    const float  inner = obs::CardInnerWidth();   // the card's own content width
    const float pw = obs::S(obs::AM().pill_w), ph = obs::S(obs::AM().pill_h);

    char seed[96];
    snprintf(seed, sizeof(seed), "hero|%s", id);
    char seedh[96];
    snprintf(seedh, sizeof(seedh), "herohov|%s", id);
    const float on  = obs::anim::Step(seed,  *v ? 1.0f : 0.0f, 18.0f);
    const float hov = obs::anim::Step(seedh, obs::CardHovered() ? 1.0f : 0.0f, 17.0f);

    // Tracked caps, sized between the micro-caps labels and the page title: big
    // enough to be the card's heading, and caps because MicroCaps makes them so.
    const float  fs   = obs::S(14.5f);
    const float  trk  = obs::S(1.30f);
    const float  ty   = o.y + (H - fs - obs::S(13.0f)) * 0.5f;
    const ImVec4 tcol = obs::Mix(p.text, p.accent, *v ? 0.22f : 0.0f);
    obs::MicroCaps(d, ImVec2(o.x + pad, ty), title,
                   ImGui::ColorConvertFloat4ToU32(obs::Fade(tcol, p.TextAlphaScale())),
                   fs, trk);
    if (desc && desc[0])
        d->AddText(ImVec2(o.x + pad, ty + fs + obs::S(3.0f)),
                   ImGui::ColorConvertFloat4ToU32(obs::Fade(p.text_faint, p.TextAlphaScale())), desc);
    obs::DrawPill(d, ImVec2(o.x + pad + inner - pw, o.y + (H - ph) * 0.5f), *v, on, hov);
    obs::CardEnd();
}

// One keybind card. Clicking hands capture to the app's poll_key(); escape cancels.
// `live` is the switch the bind belongs to, so the card lights exactly when the
// feature it arms is armed: a keybind list that cannot tell you what is running is
// a list you have to cross-reference against five other pages.
void Bind(const char* label, int* slot, bool live = false)
{
    const bool waiting = (s_wait_slot == slot);
    const char* nm = (waiting || !g_act.key_name) ? "" : g_act.key_name(*slot);
    if (obs::KeyCard(label, nm, waiting, live))
        s_wait_slot = waiting ? nullptr : slot;
}

// How wide a stat card is when it is laid out. Debug sets this to 1 so two of
// them share a row; 2 (the default) keeps a read-out the width of the page.
int s_stat_span = 2;

// A read-only key/value card built from data the app already formatted.
void Rows(const char* title, const std::vector<Snapshot::Row>& rows)
{
    if (rows.empty()) return;
    const char* keys[40];
    const char* vals[40];
    ImVec4 tints[40];
    const obs::Palette& p = obs::ActivePalette();
    const int n = (int)rows.size() < 40 ? (int)rows.size() : 40;
    for (int i = 0; i < n; ++i) {
        const Snapshot::Row& r = rows[i];
        keys[i] = r.key;
        vals[i] = r.value;
        tints[i] = r.state == 1 ? p.ok   : r.state == 2 ? p.warn
                 : r.state == 3 ? p.danger : obs::Mix(p.text_dim, p.text, 0.6f);
    }
    // `span` 1: two read-out cards can then sit side by side in a two-column grid
    // (the debug page does exactly that).
    obs::StatCard(title, keys, vals, n, tints, s_stat_span);
}

// A full-width card holding a text field: micro-caps caption on top, the field
// under it. Handing the label to TextInput inline would set the caption in the
// body font, and this language uses tracked caps for every label.
void WideInput(const char* label, char* buf, size_t sz, const char* placeholder)
{
    const obs::Palette& p = obs::ActivePalette();
    // 80, not 64: the field keeps a label row even when its label is ID-only, so
    // the input is taller than it looks and the card must reserve for it.
    obs::GridCell(2, obs::S(80.0f));
    obs::CardBegin(label, ImVec2(0.0f, 0.0f), (obs::CardFlags)(obs::Card_Inset | obs::Card_NoPad));
    const ImVec2 o = obs::CardOrigin();
    const float pad = obs::S(obs::AM().card_pad);
    obs::MicroCaps(ImGui::GetWindowDrawList(), ImVec2(o.x + pad, o.y + pad - obs::S(1.0f)),
                   obs::VisibleLabel(label),
                   ImGui::ColorConvertFloat4ToU32(obs::Fade(p.text_faint, p.TextAlphaScale())));
    ImGui::SetCursorPos(ImVec2(pad, pad + obs::S(15.0f)));
    // Card_NoPad means the child has no padding to size the field against, so the
    // item width is stated explicitly: the padded inner width, nothing more.
    ImGui::PushItemWidth(obs::CardInnerWidth());
    obs::TextInput("##field", buf, sz, placeholder);
    ImGui::PopItemWidth();
    obs::CardEnd();
}

// =============================================================================
//  page: aimbot
// =============================================================================
void PageAimbot()
{
    obs::PageTitle("aimbot",
                   "Turns the view onto the closest target inside the FOV ring",
                   aimbot_enabled ? "active" : nullptr,
                   aimbot_enabled ? &obs::ActivePalette().ok : nullptr);

    obs::GridCfg one; one.columns = 2; obs::GridBegin(one);
    Hero("aimbot-master", "Aimbot",
         "No key bound = always on while this switch is on", &aimbot_enabled);
    obs::GridEnd();

    Group("targeting");
    obs::ComboCard("aim from", &aimbot_aim_type, kAimTypes, IM_ARRAYSIZE(kAimTypes));
    obs::ComboCard("target bone", &aimbot_part, kBones, IM_ARRAYSIZE(kBones));
    obs::ToggleCard("sticky aim", &sticky_aim, "Holds the lock while the key is down");
    obs::ToggleCard("lead prediction", &prediction_enabled, "Extrapolates their velocity");
    if (prediction_enabled) {
        obs::SliderCard("predict x", &prediction_x, 1.0f, 50.0f, "%.0f", 1.0f, 10.0f, true);
        obs::SliderCard("predict y", &prediction_y, 1.0f, 50.0f, "%.0f", 1.0f, 10.0f, true);
    }
    EndGroup();

    Group("feel");
    obs::SliderCard("smoothing x", &smoothing_x, 2.0f, 20.0f, "%.1f");
    obs::SliderCard("smoothing y", &smoothing_y, 2.0f, 20.0f, "%.1f");
    obs::ToggleCard("humanizer", &humanizer_enabled, "Reaction delay, curved path, a little jitter");
    if (humanizer_enabled)
        obs::SliderCard("humanize amount", &humanizer_strength, 0.0f, 1.0f, "%.2f",
                        1.0f, 0.5f, true, true);
    obs::SliderCard("fov radius", &fov_size, 10.0f, 500.0f, "%.0f px");
    obs::ToggleCard("draw fov ring", &show_fov);
    EndGroup();

    Group("filters");
    obs::ToggleCard("team check", &team_check, "Skip players on your team");
    obs::ToggleCard("wall check", &wall_check, "Never lock through another player");
    EndGroup();

    Group("bind");
    Bind("aimbot key", &aimbot_keybind, aimbot_enabled);
    obs::ToggleCard("enabled", &aimbot_enabled);
    EndGroup();
}

// =============================================================================
//  page: visuals (esp)
// =============================================================================
void PageVisuals()
{
    obs::PageTitle("visuals",
                   "ESP boxes, labels, skeleton and chams for every other player",
                   esp_enabled ? "active" : nullptr,
                   esp_enabled ? &obs::ActivePalette().ok : nullptr);

    obs::GridCfg one; obs::GridBegin(one);   // span-2 cards => one per row
    Hero("esp-master", "ESP",
         "Drawn on the overlay window, never inside the game", &esp_enabled);
    obs::GridEnd();

    Group("boxes");
    obs::ToggleCard("box", &box_esp);
    if (box_esp) {
        obs::ComboCard("box style", &box_esp_type, kBoxStyles, IM_ARRAYSIZE(kBoxStyles));
        obs::ColorCard("box color", box_esp_color, true);
        obs::ToggleCard("fill", &box_fill);
        if (box_fill) {
            obs::ColorCard("fill top", box_fill_top, true);
            obs::ToggleCard("gradient fill", &box_fill_gradient);
            if (box_fill_gradient) {
                obs::ToggleCard("rotate gradient", &box_fill_gradient_rotate);
                obs::ColorCard("fill bottom", box_fill_bottom, true);
            }
        }
    }
    EndGroup();

    Group("labels");
    obs::ToggleCard("player name", &name);
    if (name) obs::ColorCard("name color", name_color, true);
    obs::ToggleCard("health bar", &healthbar);
    if (healthbar) obs::ColorCard("bar color", healthbar_color, true);
    obs::ToggleCard("health text", &health_text);
    if (health_text) obs::ColorCard("health text color", health_text_color, true);
    obs::ToggleCard("distance", &distance);
    if (distance) obs::ColorCard("distance color", distance_color, true);
    obs::ToggleCard("rig type", &rig_type);
    if (rig_type) obs::ColorCard("rig color", rig_type_color, true);
    obs::ToggleCard("equipped tool", &tool_esp);
    if (tool_esp) obs::ColorCard("tool color", tool_color, true);
    obs::SliderCard("draw distance", &esp_render_distance, 0.0f, 2000.0f, "%.0f studs",
                    2.0f, 0.0f, true);
    EndGroup();

    Group("checks");
    obs::ToggleCard("team check", &team_check,
                    "Teammates are drawn in the team color; the aimbot skips them");
    if (team_check) obs::ColorCard("team color", team_esp_color, true);
    obs::ToggleCard("wall check", &wall_check,
                    "Hide players behind walls (world geometry) or another player");
    EndGroup();

    Group("extras");
    obs::ToggleCard("skeleton", &skeleton_esp);
    if (skeleton_esp) obs::ColorCard("bone color", skeleton_color, true);
    obs::ToggleCard("aim viewer", &aimviewer, "Line from the target to where they look");
    obs::ToggleCard("china hat", &chinahat);
    if (chinahat) obs::ColorCard("hat color", chinahat_color, true);
    obs::ToggleCard("FPS counter", &show_fps);
    EndGroup();

    Group("chams + hitboxes");
    obs::ToggleCard("chams", &chams_enabled, "Tint the model through walls");
    if (chams_enabled) obs::ColorCard("chams color", chams_color, true);
    obs::ToggleCard("draw expanded hitbox", &render_expanded_hitbox);
    obs::ToggleCard("hitbox expander", &hitbox_expander_enabled);
    if (render_expanded_hitbox || hitbox_expander_enabled)
        obs::SliderCard("hitbox size", &hitbox_expander_value, 1.0f, 50.0f, "%.0f",
                        1.0f, 10.0f, true, hitbox_expander_enabled);
    EndGroup();
}

// =============================================================================
//  page: movement / misc
// =============================================================================
void PageMovement()
{
    obs::PageTitle("movement", "Speed, flight, and the little writes that make the game kinder");

    Group("movement");
    obs::ToggleCard("noclip", &noclip_enabled, "Collision off");
    obs::ToggleCard("flight", &flight_enabled);
    if (flight_enabled) {
        obs::SliderCard("fly speed", &flight_value, 10.0f, 1000.0f, "%.0f", 2.0f, 70.0f, true, true);
        obs::ToggleCard("hold instead of toggle", &flight_hold_mode);
    }
    obs::ToggleCard("walkspeed", &walkspeed_enabled);
    if (walkspeed_enabled)
        obs::SliderCard("speed", &walkspeed_value, 0.0f, 1000.0f, "%.0f", 1.0f, 30.0f, true, true);
    obs::ToggleCard("infinite jump", &infinite_jump_enabled, "Tap space again mid-air");
    if (infinite_jump_enabled)
        obs::SliderCard("jump power", &infinite_jump_power, 25.0f, 500.0f, "%.0f",
                        1.0f, 100.0f, true, true);
    obs::ToggleCard("click teleport", &click_teleport_enabled,
                    click_teleport_keybind ? "Hold it, then aim the cursor"
                                           : "Click the floor or a player");
    if (click_teleport_enabled) {
        // Not a distance to travel: a CAP. The jump lands on the floor the cursor is
        // over, and this only bites when that floor is further away than this.
        obs::SliderCard("max range", &click_teleport_distance, 5.0f, 200.0f, "%.0f studs",
                        1.0f, 30.0f, true, true);
        // The same global the players page exposes: one landing clearance for both
        // ways of arriving somewhere, duplicated here because the jump that needs it
        // is decided by this page's click, and "where do I set that?" should not be a
        // hunt. Move either slider and the other follows.
        obs::SliderCard("land above by", &tp_land_height, 0.0f, 12.0f, "%.0f studs",
                        1.0f, 2.0f, true, true);
    }
    EndGroup();

    Group("view");
    obs::ToggleCard("FOV changer", &fov_changer_enabled);
    if (fov_changer_enabled)
        obs::SliderCard("field of view", &fov_value, 20.0f, 120.0f, "%.0f", 1.0f, 70.0f, true, true);
    obs::ToggleCard("inventory checker", &inventory_checker_enabled,
                     "What the player under your cursor is holding");
    obs::ToggleCard("freecam", &freecam_enabled, "WASD anytime, hold RIGHT MOUSE to look");
    if (freecam_enabled)
        obs::SliderCard("freecam speed", &freecam_speed, 10.0f, 250.0f, "%.0f",
                        1.0f, 60.0f, true, true);
    EndGroup();

    Group("binds");
    Bind("click teleport", &click_teleport_keybind, click_teleport_enabled);
    Bind("noclip", &noclip_keybind, noclip_enabled);
    Bind("walkspeed", &walkspeed_keybind, walkspeed_enabled);
    Bind("flight", &flight_keybind, flight_enabled);
    Bind("inventory checker", &inventory_checker_keybind, inventory_checker_enabled);
    Bind("freecam", &freecam_keybind, freecam_enabled);
    EndGroup();
}

// =============================================================================
//  page: world
// =============================================================================
// Shrink `s` until it draws no wider than `room`, marking the cut with "...".
// The rows right-align a detail string on the same line, so without this a long
// display name and "100/100 hp - 142 studs - Sword" would overlap.
void ShrinkToWidth(char* s, size_t n, float room)
{
    ImFont* f = ImGui::GetFont();
    if (obs::TextWidth(f, s) <= room) return;
    size_t len = strlen(s);
    while (len > 1) {
        s[--len] = '\0';
        if (obs::TextWidth(f, s) <= room) {
            if (len + 4 <= n) { s[len] = '.'; s[len+1] = '.'; s[len+2] = '.'; s[len+3] = '\0'; }
            return;
        }
    }
}

void PageWorld(Snapshot& snap)
{
    const obs::Palette& pal = obs::ActivePalette();
    obs::PageTitle("world", nullptr,
                   snap.servers_busy ? "fetching" : nullptr,
                   snap.servers_busy ? &pal.accent : nullptr);

    obs::GridCfg one; obs::GridBegin(one);   // span-2 cards => one per row
    Hero("skybox-master", "Skybox changer", "", &skybox_changer_enabled);
    obs::GridEnd();

    // ---------------------------------------------------------------- servers --
    // Roblox decides how you enter a server, so this page only ever builds a
    // roblox:// deeplink and hands it to the client: see features/servers/servers.h.
    Group("servers");
    {
        char body[256];
        const char* game = snap.game[0] ? snap.game
                           : (snap.attached ? "loading..." : "not attached");
        const char* place = snap.place[0] ? snap.place : "not in an experience";
        snprintf(body, sizeof(body), "%s\n%s", game, place);
        obs::NoteCard("this experience", body);
    }

    // "fetched 8s ago" is the whole point of this line: the emptiest server is a
    // suggestion that ages, and the card has to say how old it is while you read it.
    auto age_text = [&snap](char* out, size_t n) {
        if (snap.servers_busy)        { snprintf(out, n, "%s", "request running"); return; }
        if (snap.server_age_sec < 0)  { snprintf(out, n, "%s", "never fetched"); return; }
        if (snap.server_age_sec < 120){ snprintf(out, n, "%ds ago", snap.server_age_sec); return; }
        snprintf(out, n, "%dm ago", snap.server_age_sec / 60);
    };
    // A card in this grid is a third of the page wide and has to hold a label, a
    // value and a button, so all three are short on purpose: the ellipsis the library
    // draws is graceful, but three of them in a row is a card you cannot read.
    char fresh[64];
    age_text(fresh, sizeof(fresh));

    if (obs::ButtonCard("servers", snap.servers_busy ? "FETCHING" : "REFRESH",
                        obs::ButtonKind_Default, fresh)) {
        if (g_act.servers_refresh) g_act.servers_refresh();
    }
    // REJOIN stays reachable when it cannot work, because the message it produces
    // says which of the three reasons applies (no id in memory, an offset that moved,
    // or a private server that is not in the public list). Disabling it hides that.
    if (obs::ButtonCard("this server", "REJOIN", obs::ButtonKind_Primary,
                        snap.servers_can_rejoin ? "same session" : "id not listed")) {
        if (g_act.servers_rejoin) g_act.servers_rejoin();
    }
    if (obs::ButtonCard("next server", "HOP", obs::ButtonKind_Primary,
                        snap.server_count > 0 ? "most room" : "refresh first")) {
        if (g_act.servers_hop_openest) g_act.servers_hop_openest();
    }

    // Six rows is what is left once the controls above are on screen; the card
    // scrolls from there, which is what a ScrollCard is for.
    const int rows = snap.server_count < 6 ? snap.server_count : 6;
    char list_title[80];
    if (snap.server_count < 1) snprintf(list_title, sizeof(list_title), "%s", "servers");
    else snprintf(list_title, sizeof(list_title), "%s",
                  "click a server to join it");
    const float list_h = obs::S(46.0f) + obs::S(21.0f) * (float)(rows > 0 ? rows : 1) +
                         (snap.servers_msg[0] ? obs::S(17.0f) : 0.0f);
    if (obs::ScrollCardBegin(list_title, list_h)) {
        if (snap.server_count < 1) {
            ImGui::TextDisabled("%s", snap.servers_msg[0] ? snap.servers_msg
                                                           : "(no server list yet)");
        }
        for (int i = 0; i < snap.server_count; ++i) {
            const Snapshot::ServerRow& r = snap.servers[i];
            const bool ours = (r.state == 1 || r.state == 3);
            const bool full = (r.state == 2 || r.state == 3);
            const ImVec4* tint = ours ? &pal.accent : (full ? &pal.text_faint : nullptr);

            // Same measure-then-shrink as the players list, so a long guid and the
            // counts on its right cannot draw over each other in a narrow window.
            const float dw = obs::TextWidth(ImGui::GetFont(), r.detail);
            const float avail = ImGui::GetContentRegionAvail().x;
            char shown[48];
            snprintf(shown, sizeof(shown), "#%d  %s", i + 1, r.id);
            ShrinkToWidth(shown, sizeof(shown),
                          avail > dw + obs::S(56.0f) ? avail - dw - obs::S(40.0f) : 32.0f);
            char lab[80];
            snprintf(lab, sizeof(lab), "%s##srv%d", shown, i);

            if (tint) ImGui::PushStyleColor(ImGuiCol_Text, obs::Fade(*tint, pal.TextAlphaScale()));
            if (ImGui::Selectable(lab, false)) {
                if (g_act.servers_join) g_act.servers_join(i);
            }
            if (tint) ImGui::PopStyleColor();

            // the counts right-aligned on the row, same idiom as the players list:
            // the number is what you compare, so it is the one thing in the row that
            // must not move when a guid gets long.
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            ImFont* f = ImGui::GetFont();
            ImGui::GetWindowDrawList()->AddText(
                f, f->FontSize,
                ImVec2(hi.x - dw - obs::S(obs::AM().card_pad),
                       lo.y + (hi.y - lo.y - f->FontSize) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(
                    obs::Fade(full ? pal.text_faint : pal.text_dim, pal.TextAlphaScale())),
                r.detail);
        }
        if (snap.servers_msg[0]) {
            ImGui::Dummy(ImVec2(0.0f, obs::S(4.0f)));
            ImGui::TextDisabled("%s", snap.servers_msg);
        }
    }
    obs::ScrollCardEnd();
    EndGroup();

    if (skybox_changer_enabled) {
        Group("skybox");
        if (snap.skybox_names && snap.skybox_count > 0) {
            if (skybox_type < 0 || skybox_type >= snap.skybox_count) skybox_type = 0;
            obs::ComboCard("preset", &skybox_type, snap.skybox_names, snap.skybox_count);
        }
        EndGroup();
    }

    Group("waypoints");
    obs::ToggleCard("show on overlay", &show_waypoints);
    WideInput("waypoint name", waypoint_name_buf, sizeof(waypoint_name_buf), "Name, then pin");
    if (obs::ButtonCard("this position", "PIN", obs::ButtonKind_Primary,
                        waypoint_name_buf[0] ? waypoint_name_buf : "wp")) {
        if (g_act.waypoint_add) g_act.waypoint_add(waypoint_name_buf);
    }
    if (obs::ButtonCard("copy my xyz", "FILL", obs::ButtonKind_Default, "into the sliders")) {
        if (g_act.waypoint_fill) g_act.waypoint_fill();
    }
    obs::SliderCard("x", &waypoint_edit_x, -20000.0f, 20000.0f, "%.1f");
    obs::SliderCard("y", &waypoint_edit_y, -20000.0f, 20000.0f, "%.1f");
    obs::SliderCard("z", &waypoint_edit_z, -20000.0f, 20000.0f, "%.1f");
    if (obs::ButtonCard("custom xyz", "ADD", obs::ButtonKind_Primary,
                        waypoint_name_buf[0] ? waypoint_name_buf : "custom")) {
        if (g_act.waypoint_add_xyz)
            g_act.waypoint_add_xyz(waypoint_name_buf, waypoint_edit_x, waypoint_edit_y, waypoint_edit_z);
    }
    EndGroup();
    if (waypoint_n > 0) {
        const float h = obs::S(46.0f) + obs::S(22.0f) * (float)waypoint_n;
        if (obs::ScrollCardBegin("click to teleport, right-click removes", h)) {
            for (int i = 0; i < waypoint_n; ++i) {
                char lab[80];
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
        obs::ScrollCardEnd();
    }

    if (snap.skybox_note[0]) {
        Group("status");
        obs::NoteCard("the renderer reports", snap.skybox_note);
        EndGroup();
    }
}

// =============================================================================
//  page: players
// =============================================================================
void PagePlayers(Snapshot& snap)
{
    const obs::Palette& pal = obs::ActivePalette();
    obs::PageTitle("players", "PLAYER LIST",
                   snap.spectating ? "watching" : nullptr,
                   snap.spectating ? &pal.accent : nullptr);

    // Rows appear and disappear as the cache resolves characters, so the selection
    // is re-clamped every frame instead of trusted from the last one. With nothing
    // picked, the nearest player is: the list is sorted by distance, and that makes
    // the two action cards useful the moment the page opens.
    if (snap.player_count < 1)                     s_player_sel = -1;
    else if (s_player_sel < 0)                     s_player_sel = 0;
    else if (s_player_sel >= snap.player_count)    s_player_sel = snap.player_count - 1;
    const bool have_sel = (s_player_sel >= 0 && s_player_sel < snap.player_count);
    const Snapshot::PlayerRow& sel =
        snap.players[have_sel ? s_player_sel : 0];

    // who we are watching, for the release card. Derived from the rows rather than
    // a name kept side-by-side, so it cannot go stale when they leave.
    const char* watched = nullptr;
    for (int i = 0; i < snap.player_count; ++i)
        if (snap.players[i].state == 2) { watched = snap.players[i].name; break; }

    // A fixed-height list is hollow with three players in the server and useless
    // with forty, so it grows with the rows and stops at nine; past that it scrolls,
    // which is what the card is for.
    const int shown_rows = snap.player_count < 9 ? snap.player_count : 9;
    const float list_h = obs::S(46.0f) + obs::S(21.0f) * (float)shown_rows +
                         (snap.player_note[0] ? obs::S(17.0f) : 0.0f);
    if (obs::ScrollCardBegin("player list", list_h)) {
        if (snap.player_count < 1) {
            ImGui::TextDisabled("%s", snap.player_note[0] ? snap.player_note
                                                           : "(nobody resolved yet)");
        }
        for (int i = 0; i < snap.player_count; ++i) {
            const Snapshot::PlayerRow& r = snap.players[i];
            const ImVec4* tint = r.state == 2 ? &pal.accent
                               : (r.state == 1 ? &pal.text_faint : nullptr);

            // measure the detail first, then give the name whatever is left, so the
            // two can never draw over each other in a narrow window
            const float dw = obs::TextWidth(ImGui::GetFont(), r.detail);
            const float avail = ImGui::GetContentRegionAvail().x;
            char shown[64];
            snprintf(shown, sizeof(shown), "%s", r.name);
            ShrinkToWidth(shown, sizeof(shown),
                          avail > dw + obs::S(56.0f) ? avail - dw - obs::S(40.0f) : 32.0f);
            char lab[96];
            snprintf(lab, sizeof(lab), "%s##u%u", shown, r.user_id);

            if (tint) ImGui::PushStyleColor(ImGuiCol_Text, obs::Fade(*tint, pal.TextAlphaScale()));
            const bool picked = (s_player_sel == i);
            if (ImGui::Selectable(lab, picked)) s_player_sel = i;
            // double-click jumps. There is no public IsItemDoubleClicked(), so: the
            // item is hot and the second click of a double landed this frame.
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                have_sel && g_act.tp_to_player)
                g_act.tp_to_player(r.user_id);
            if (tint) ImGui::PopStyleColor();

            // right-aligned detail on the row itself: name on the left, the numbers
            // you compare on the right, so the column is scannable.
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            ImFont* f = ImGui::GetFont();
            ImGui::GetWindowDrawList()->AddText(
                f, f->FontSize,
                ImVec2(hi.x - dw - obs::S(obs::AM().card_pad),
                       lo.y + (hi.y - lo.y - f->FontSize) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(
                    obs::Fade(r.state == 1 ? pal.text_faint : pal.text_dim,
                              pal.TextAlphaScale())),
                r.detail);
        }
        if (snap.player_note[0]) {
            ImGui::Dummy(ImVec2(0.0f, obs::S(4.0f)));
            ImGui::TextDisabled("%s", snap.player_note);
        }
    }
    obs::ScrollCardEnd();

    Group("actions");
    if (obs::ButtonCard("players", "RESCAN", obs::ButtonKind_Default,
                        snap.player_count > 0 ? "force refresh" : "find players")) {
        if (g_act.rescan) g_act.rescan();
    }
    // The card is a third of the page wide and has to hold a label, who it acts on
    // and the button, so the label is one word: the button says whether it is the
    // watch or the release, and the two are never on screen together anyway.
    if (obs::ButtonCard("teleport", "GO", obs::ButtonKind_Primary,
                        have_sel ? sel.name : "pick a row")) {
        if (have_sel && g_act.tp_to_player) g_act.tp_to_player(sel.user_id);
    }
    if (!snap.spectating) {
        if (obs::ButtonCard("spectate", "WATCH", obs::ButtonKind_Default,
                            have_sel ? sel.name : "pick a row")) {
            if (have_sel && g_act.spectate_player) g_act.spectate_player(sel.user_id);
        }
    } else {
        if (obs::ButtonCard("spectate", "STOP", obs::ButtonKind_Danger,
                            watched ? watched : "the camera")) {
            if (g_act.spectate_stop) g_act.spectate_stop();
        }
    }
    // the three numbers that decide whether this feels good, all live: they are
    // written on the same frame the slider moves, so you can dial them in on a
    // moving target instead of restarting the watch.
    // Not a camera we drive any more, so not camera controls: these two are the
    // game's own Humanoid::CameraOffset for the subject we are following, which is
    // how a game expects to be nudged. Games free to ignore them; the watch works
    // either way, because the subject change is what does the following.
    obs::SliderCard("look from behind", &spectate_distance, 4.0f, 40.0f, "%.0f studs",
                    1.0f, 14.0f, true, spectate_enabled);
    obs::SliderCard("look from above", &spectate_height, 0.0f, 14.0f, "%.0f studs",
                    1.0f, 3.0f, true, spectate_enabled);
    obs::SliderCard("land above by", &tp_land_height, 0.0f, 12.0f, "%.0f studs",
                    1.0f, 2.0f, true);
    EndGroup();

    Group("lists");
    const char* tag = have_sel ? (sel.account[0] ? sel.account : sel.name) : "pick a row";
    if (obs::ButtonCard("aimbot + esp", "BLACKLIST", obs::ButtonKind_Danger, tag)) {
        if (have_sel) AddPlayerTag(player_blacklist, player_blacklist_n,
                                   sel.account[0] ? sel.account : sel.name);
    }
    if (obs::ButtonCard("esp color", "WHITELIST", obs::ButtonKind_Primary, tag)) {
        if (have_sel) AddPlayerTag(player_whitelist, player_whitelist_n,
                                   sel.account[0] ? sel.account : sel.name);
    }
    obs::ColorCard("whitelist color", whitelist_esp_color, true);
    EndGroup();

    if (player_blacklist_n > 0) {
        const float h = obs::S(46.0f) + obs::S(21.0f) * (float)player_blacklist_n;
        if (obs::ScrollCardBegin("blacklisted - click to remove", h)) {
            for (int i = 0; i < player_blacklist_n; ++i) {
                if (ImGui::Selectable(player_blacklist[i]))
                    RemovePlayerTagAt(player_blacklist, player_blacklist_n, i);
            }
        }
        obs::ScrollCardEnd();
    }
    if (player_whitelist_n > 0) {
        const float h = obs::S(46.0f) + obs::S(21.0f) * (float)player_whitelist_n;
        if (obs::ScrollCardBegin("whitelisted - click to remove", h)) {
            for (int i = 0; i < player_whitelist_n; ++i) {
                if (ImGui::Selectable(player_whitelist[i]))
                    RemovePlayerTagAt(player_whitelist, player_whitelist_n, i);
            }
        }
        obs::ScrollCardEnd();
    }
}

// =============================================================================
//  page: keybinds
// =============================================================================
void PageKeybinds()
{
    const obs::Palette& pal = obs::ActivePalette();
    obs::PageTitle("keybinds",
                   s_wait_slot ? "Press a key or click a mouse button - Escape keeps the old bind"
                                : "Click a slot, then press a key or mouse button - Escape cancels",
                   s_wait_slot ? "capturing" : nullptr,
                   s_wait_slot ? &pal.accent : nullptr);

    Group("menu");
    Bind("toggle menu", &menu_toggle_keybind);
    if (!menu_toggle_keybind) {
        obs::GridCell(2, obs::S(44.0f));
        obs::CardBegin("menu-bind-warning");
        const ImVec2 o = obs::CardOrigin();
        ImGui::SetCursorScreenPos(ImVec2(o.x + obs::S(obs::AM().card_pad),
                                         o.y + obs::S(obs::AM().card_pad)));
        ImGui::PushStyleColor(ImGuiCol_Text, obs::Fade(pal.danger, pal.TextAlphaScale()));
        ImGui::TextUnformatted("Unbound - you could not open the menu again!");
        ImGui::PopStyleColor();
        obs::CardEnd();
    }
    EndGroup();

    Group("combat");
    Bind("aimbot", &aimbot_keybind, aimbot_enabled);
    EndGroup();

    Group("movement");
    Bind("noclip", &noclip_keybind, noclip_enabled);
    Bind("walkspeed", &walkspeed_keybind, walkspeed_enabled);
    Bind("flight", &flight_keybind, flight_enabled);
    Bind("click teleport", &click_teleport_keybind, click_teleport_enabled);
    Bind("inventory checker", &inventory_checker_keybind, inventory_checker_enabled);
    Bind("freecam", &freecam_keybind, freecam_enabled);
    EndGroup();
}

// =============================================================================
//  page: theme
// =============================================================================
struct AccentPreset { const char* name; unsigned int rgb; };
// the reference's own swatch set, in its order (white is its default accent)
const AccentPreset kAccents[] = {
    { "white",  0xFFFFFFu },
    { "cyan",   0x59D2FEu },
    { "red",    0xE94552u },
    { "violet", 0xB39DDBu },
    { "lime",   0x7BE38Bu },
};
constexpr int kAccentCount = (int)(sizeof(kAccents) / sizeof(kAccents[0]));

void PageTheme()
{
    obs::PageTitle("theme", "The glass itself: transparency, radius and accent");

    Group("surface");
    obs::SliderCard("menu transparency", &ui_transparency, 0.0f, 90.0f, "%.0f%%");
    obs::ToggleCard("rounded corners", &ui_rounded_corners);
    if (ui_rounded_corners) {
        // spans both columns: with the bloom slider gone this group would leave a
        // hole in row 2, and a lone half-width card reads as a mistake.
        obs::SliderCard("corner radius", &ui_corner_radius, 0.0f, 24.0f, "%.0f px",
                        1.0f, 0.0f, false, false, 2);
    }
    EndGroup();

    Group("accent");
    {
        // keep the swatch row honest: if the accent happens to equal a preset
        // (a loaded config, or the app's default) that dot shows as selected.
        int match = -1;
        for (int i = 0; i < kAccentCount; ++i) {
            const ImVec4 c = obs::Hex(kAccents[i].rgb, 1.0f);
            const float dx = c.x - ui_accent_color[0];
            const float dy = c.y - ui_accent_color[1];
            const float dz = c.z - ui_accent_color[2];
            if (dx * dx + dy * dy + dz * dz < 0.0025f) { match = i; break; }
        }
        if (match >= 0) s_accent_sel = match;
        else if (ui_rainbow) s_accent_sel = -1;

        // preset swatches, in a wide card of their own. The height is the padding
        // plus the title row plus a swatch plus its caption -- 74 clipped the
        // names off the bottom, which is the only reason they are there.
        obs::GridCell(2, obs::S(88.0f));
        obs::CardBegin("accent-presets");
        obs::CardTitle("presets", 0.0f, true);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + obs::S(20.0f));
        const char* names[kAccentCount];
        unsigned int rgbs[kAccentCount];
        for (int i = 0; i < kAccentCount; ++i) { names[i] = kAccents[i].name; rgbs[i] = kAccents[i].rgb; }
        if (obs::AccentDots(names, rgbs, kAccentCount, &s_accent_sel, 28.0f, true)) {
            const ImVec4 c = obs::Hex(rgbs[s_accent_sel < 0 ? 0 : s_accent_sel], 1.0f);
            ui_accent_color[0] = c.x; ui_accent_color[1] = c.y; ui_accent_color[2] = c.z;
        }
        obs::CardEnd();
    }
    s_accent4[0] = ui_accent_color[0];
    s_accent4[1] = ui_accent_color[1];
    s_accent4[2] = ui_accent_color[2];
    if (obs::ColorCard("custom accent", s_accent4, false)) {
        ui_accent_color[0] = s_accent4[0];
        ui_accent_color[1] = s_accent4[1];
        ui_accent_color[2] = s_accent4[2];
        s_accent_sel = -1;                 // off-preset now
    }
    obs::ToggleCard("rainbow accent", &ui_rainbow, "Cycles the hue over time");
    EndGroup();

    Group("overlay");
    obs::ToggleCard("media player", &show_now_playing, "Floating now-playing with controls");
    EndGroup();

    Group("reset");
    if (obs::ButtonCard("all theme settings", "RESET", obs::ButtonKind_Danger, nullptr, 2)) {
        ui_transparency    = 6.0f;
        ui_rounded_corners = true;
        ui_corner_radius   = 16.0f;
        ui_rainbow         = false;
        show_now_playing   = true;
        ui_accent_color[0] = 1.0f; ui_accent_color[1] = 1.0f; ui_accent_color[2] = 1.0f;
        s_accent_sel = 0;
    }
    EndGroup();
}

// =============================================================================
//  page: config
// =============================================================================
void PageConfig()
{
    obs::PageTitle("config", "Named presets, stored next to the executable",
                   nullptr, nullptr);

    obs::GridCfg one; obs::GridBegin(one);   // span-2 cards => one per row
    WideInput("config name", s_cfg_name, sizeof(s_cfg_name), "Type a name, then save");
    WideInput("rename the config above to", s_cfg_rename, sizeof(s_cfg_rename),
              "New name for rename");
    obs::GridEnd();

    const char* shown = s_cfg_name[0] ? s_cfg_name : "no name";

    Group("actions");
    if (obs::ButtonCard("save config", "SAVE", obs::ButtonKind_Primary, shown)) {
        if (s_cfg_name[0] && g_act.config_save && g_act.config_save(s_cfg_name)) RefreshConfigs();
    }
    if (obs::ButtonCard("load config", "LOAD", obs::ButtonKind_Default, shown)) {
        if (s_cfg_name[0] && g_act.config_load) g_act.config_load(s_cfg_name);
    }
    if (obs::ButtonCard("delete config", "DELETE", obs::ButtonKind_Danger, shown)) {
        if (s_cfg_name[0] && g_act.config_delete && g_act.config_delete(s_cfg_name)) {
            s_cfg_name[0] = '\0';
            s_cfg_sel = -1;
            RefreshConfigs();
        }
    }
    if (obs::ButtonCard("rename config", "RENAME", obs::ButtonKind_Default,
                        (s_cfg_name[0] && s_cfg_rename[0]) ? s_cfg_rename : "both")) {
        if (s_cfg_name[0] && s_cfg_rename[0] && g_act.config_rename &&
            g_act.config_rename(s_cfg_name, s_cfg_rename)) {
            snprintf(s_cfg_name, sizeof(s_cfg_name), "%s", s_cfg_rename);
            s_cfg_rename[0] = '\0';
            s_cfg_sel = -1;
            RefreshConfigs();
        }
    }
    EndGroup();

    if (!s_configs_fetched) RefreshConfigs();
    if (obs::ScrollCardBegin("saved configs - click to pick", obs::S(140.0f))) {
        if (s_configs.empty())
            ImGui::TextDisabled("(nothing saved yet)");
        for (int i = 0; i < (int)s_configs.size(); ++i) {
            if (ImGui::Selectable(s_configs[i].c_str(), s_cfg_sel == i)) {
                s_cfg_sel = i;
                snprintf(s_cfg_name, sizeof(s_cfg_name), "%s", s_configs[i].c_str());
            }
        }
    }
    obs::ScrollCardEnd();

    Group("files");
    char countbuf[32];
    snprintf(countbuf, sizeof(countbuf), "%d on disk", (int)s_configs.size());
    if (obs::ButtonCard("config folder", "OPEN", obs::ButtonKind_Default, countbuf)) {
        if (g_act.open_config_folder) g_act.open_config_folder();
    }
    if (obs::ButtonCard("saved list", "REFRESH", obs::ButtonKind_Default, nullptr))
        RefreshConfigs();
    EndGroup();
}

// =============================================================================
//  page: debug
// =============================================================================
void PageDebug(Snapshot& snap)
{
    const obs::Palette& pal = obs::ActivePalette();
    obs::PageTitle("debug", "What the reader can actually see right now",
                   snap.attached ? "attached" : "waiting",
                   snap.attached ? &pal.ok : &pal.warn);

    obs::GridCfg two; two.columns = 2; obs::GridBegin(two);
    s_stat_span = 1;
    Rows("runtime", snap.runtime);
    Rows("camera + player", snap.camera);
    s_stat_span = 2;
    obs::GridEnd();


    Group("offset write tests");
    if (snap.can_test) {
        if (obs::ButtonCard("position: +10 studs up", "RUN", obs::ButtonKind_Primary))
            if (g_act.run_test) g_act.run_test(0);
        if (obs::ButtonCard("velocity: launch up", "RUN", obs::ButtonKind_Primary))
            if (g_act.run_test) g_act.run_test(1);
        // the offset range goes in the value slot: card labels are shouted by the
        // widget, and "0XA0..0X1C0" is not how anyone writes a hex offset.
        if (obs::ButtonCard("float probe", "RUN", obs::ButtonKind_Default, "0xA0..0x1C0"))
            if (g_act.run_test) g_act.run_test(2);
        if (obs::ButtonCard("camera probe", "RUN", obs::ButtonKind_Default, "0xC0..0x160"))
            if (g_act.run_test) g_act.run_test(3);
        // The one that distinguishes "the write landed" from "the write survived".
        // Blocks ~500ms by design; the menu is the only caller and it is a button.
        if (obs::ButtonCard("survival: does a write stick?", "RUN",
                            obs::ButtonKind_Primary, "+25 studs, sampled 500ms"))
            if (g_act.run_test) g_act.run_test(4);
        // The anchored case, which I shipped a fix for without ever measuring.
        if (obs::ButtonCard("survival: does it stick ANCHORED?", "RUN",
                            obs::ButtonKind_Primary, "+25 studs, anchored, then released"))
            if (g_act.run_test) g_act.run_test(5);
        if (obs::ButtonCard("who owns the position?", "RUN",
                            obs::ButtonKind_Primary, "scan for the authoritative XYZ"))
            if (g_act.run_test) g_act.run_test(6);
        if (obs::ButtonCard("continuous write: does hammering hold?", "RUN",
                            obs::ButtonKind_Primary, "+25 studs, 500ms"))
            if (g_act.run_test) g_act.run_test(7);
        if (obs::ButtonCard("camera: does Scriptable stick?", "RUN",
                            obs::ButtonKind_Primary, "type=6, then push the camera"))
            if (g_act.run_test) g_act.run_test(8);
        if (obs::ButtonCard("velocity: does the impulse SURVIVE?", "RUN",
                            obs::ButtonKind_Primary, "y=100 once, sampled 400ms"))
            if (g_act.run_test) g_act.run_test(9);
        if (obs::ButtonCard("velocity: does hammering hold it?", "RUN",
                            obs::ButtonKind_Primary, "y=100 hammered 400ms"))
            if (g_act.run_test) g_act.run_test(10);
        // Wall check calibration: dumps the physics world's primitive
        // container and the auto-detection verdict. If the wall check ever
        // logs "container not recognised", this is the output to send.
        if (obs::ButtonCard("world probe: wall check source", "RUN",
                            obs::ButtonKind_Default, "world+Primitives dump"))
            if (g_act.run_test) g_act.run_test(11);
    }
    EndGroup();

    if (snap.test_result[0]) {
        obs::GridCfg one; obs::GridBegin(one);   // span-2 cards => one per row
        obs::NoteCard("last test", snap.test_result);
        obs::GridEnd();
    }

    if (obs::ScrollCardBegin("log", obs::S(170.0f))) {
        ImGui::PushStyleColor(ImGuiCol_Text, obs::Fade(pal.text_dim, pal.TextAlphaScale()));
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            for (const std::string& line : g_log_lines)
                ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopStyleColor();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    }
    obs::ScrollCardEnd();

    Group("log actions");
    if (obs::ButtonCard("clear the buffer", "CLEAR", obs::ButtonKind_Danger)) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_lines.clear();
    }
    if (obs::ButtonCard("copy everything", "COPY", obs::ButtonKind_Default)) {
        std::string all;
        std::lock_guard<std::mutex> lock(g_log_mutex);
        for (const std::string& l : g_log_lines) { all += l; all += "\n"; }
        ImGui::SetClipboardText(all.c_str());
    }
    EndGroup();
}

// =============================================================================
//  nav state
// =============================================================================
void UpdateNavState(const Snapshot& snap)
{
    const int aim  = CountOn({ aimbot_enabled, sticky_aim, prediction_enabled,
                               humanizer_enabled, show_fov, team_check, wall_check });
    const int vis  = CountOn({ esp_enabled, box_esp, box_fill, box_fill_gradient,
                               healthbar, health_text, name, distance, rig_type, tool_esp,
                               skeleton_esp, aimviewer, chinahat, chams_enabled,
                               render_expanded_hitbox, hitbox_expander_enabled, show_fps });
    const int mov  = CountOn({ noclip_enabled, flight_enabled, walkspeed_enabled,
                               infinite_jump_enabled, click_teleport_enabled,
                               fov_changer_enabled, inventory_checker_enabled,
                               freecam_enabled });
    const int wrld = CountOn({ skybox_changer_enabled, waypoint_n > 0 });
    const int binds = CountOn({ menu_toggle_keybind != 0, aimbot_keybind != 0,
                                noclip_keybind != 0, walkspeed_keybind != 0,
                                flight_keybind != 0, click_teleport_keybind != 0,
                                inventory_checker_keybind != 0 });
    // A bind is only doing something while its feature is on -- which is what
    // "live" means on the keybinds row. The page has no switch of its own, and
    // hard-wiring this to false is why it never lit up with the others.
    const int armed = CountOn({ (aimbot_keybind != 0 && aimbot_enabled),
                                (noclip_keybind != 0 && noclip_enabled),
                                (walkspeed_keybind != 0 && walkspeed_enabled),
                                (flight_keybind != 0 && flight_enabled),
                                (click_teleport_keybind != 0 && click_teleport_enabled),
                                (inventory_checker_keybind != 0 && inventory_checker_enabled) });

    s_nav_live[0] = aimbot_enabled;
    s_nav_live[1] = esp_enabled;
    s_nav_live[2] = mov  > 0;
    s_nav_live[3] = skybox_changer_enabled;
    s_nav_live[4] = spectate_enabled;
    s_nav_live[5] = armed > 0;
    s_nav_live[6] = ui_rainbow;
    s_nav_live[7] = false;
    s_nav_live[8] = false;

    // Each row counts something different, so each says what it counts: "3 on" is
    // armed settings, and neither people in a server nor bound keys is that.
    struct NavCount { int n; const char* unit; };
    const NavCount counts[kPageCount] = {
        { aim, " on" }, { vis, " on" }, { mov, " on" }, { wrld, " on" },
        { snap.player_count, " here" }, { binds, " bound" },
        { 0, nullptr }, { 0, nullptr }, { 0, nullptr },
    };
    for (int i = 0; i < kPageCount; ++i) {
        if (counts[i].n <= 0 || !counts[i].unit) { s_nav_hint[i][0] = '\0'; continue; }
        snprintf(s_nav_hint[i], sizeof(s_nav_hint[i]), "%d%s", counts[i].n, counts[i].unit);
    }
}

} // namespace

// =============================================================================
//  theme: the ui_* globals folded into a live Axiom palette
// =============================================================================
obs::Palette BuildPalette()
{
    obs::Palette p = obs::PaletteAxiom();

    // Darker, higher contrast base: push surfaces toward pure black and
    // lift text brightness.  Alphas are 1.0 here — the transparency slider
    // (which drives p.opacity) is the single source of truth for see-through.
    p.backdrop      = ImVec4(0.01f, 0.01f, 0.02f, 1.0f);
    p.window_top    = ImVec4(0.03f, 0.03f, 0.04f, 1.0f);
    p.window_bottom = ImVec4(0.02f, 0.02f, 0.03f, 1.0f);
    p.header      = ImVec4(0.02f, 0.02f, 0.03f, 1.0f);
    p.panel       = ImVec4(0.05f, 0.05f, 0.07f, 1.0f);
    p.panel_hover = ImVec4(0.08f, 0.08f, 0.11f, 1.0f);
    p.field       = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
    p.field_hover = ImVec4(0.09f, 0.09f, 0.12f, 1.0f);
    p.field_active= ImVec4(0.07f, 0.07f, 0.10f, 1.0f);
    p.popup       = ImVec4(0.04f, 0.04f, 0.06f, 1.0f);
    p.text        = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
    p.text_dim    = ImVec4(0.70f, 0.70f, 0.75f, 1.0f);
    p.text_faint  = ImVec4(0.45f, 0.45f, 0.50f, 1.0f);
    p.edge        = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    p.edge_soft   = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);

    static bool  s_rainbow_was_on = false;
    static float s_prev[3] = { 1.0f, 1.0f, 1.0f };
    if (ui_rainbow) {
        if (!s_rainbow_was_on) {
            s_prev[0] = ui_accent_color[0];
            s_prev[1] = ui_accent_color[1];
            s_prev[2] = ui_accent_color[2];
            s_rainbow_was_on = true;
        }
        const float t = (float)ImGui::GetTime() * 0.22f;
        ImGui::ColorConvertHSVtoRGB(t - (float)(int)t, 0.80f, 1.0f,
                                    ui_accent_color[0], ui_accent_color[1], ui_accent_color[2]);
    } else if (s_rainbow_was_on) {
        s_rainbow_was_on = false;
        ui_accent_color[0] = s_prev[0];
        ui_accent_color[1] = s_prev[1];
        ui_accent_color[2] = s_prev[2];
    }

    obs::ApplyAccent(p, ImVec4(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f));

    // "transparency" is a see-through amount; the palette wants surface opacity.
    // It never goes fully transparent: below ~0.30 the text stops being readable.
    p.opacity = 1.0f - (ui_transparency / 100.0f) * 0.70f;
    // ...and the knob moves ONLY that. Fading the letters with the glass is what
    // made a transparent menu unreadable over a bright skybox: the point of the
    // slider is to see the game through the panels, not to see through the words.
    p.text_stays_opaque = true;

    const float r = ui_rounded_corners ? ui_corner_radius : 0.0f;
    p.corner_lg = r * 0.72f + obs::S(3.0f);
    p.corner    = r * 0.46f + obs::S(2.0f);
    p.corner_sm = r * 0.30f + obs::S(1.5f);
    return p;
}

// =============================================================================
//  shell
// =============================================================================
char s_header_status[96] = "";   // menuui scope: the header borrows it

obs::ObsidianWindow& Window()
{
    static obs::ObsidianWindow win([] {
        obs::WindowConfig wc;
        wc.title           = "ROBLOX";
        wc.title_accent    = "EXTERNAL";
        wc.subtitle        = s_header_status;   // borrowed: static storage
        wc.subtitle_chip   = true;
        wc.show_mark       = false;
        wc.title_bar_height = 40.0f;
        wc.initial_size    = ImVec2(900.0f, 620.0f);
        wc.min_width       = 580.0f;
        wc.min_height      = 380.0f;
        ImVec2 d = ImGui::GetIO().DisplaySize;
        if (d.x > 0.0f && d.y > 0.0f)
            wc.initial_pos = ImVec2(d.x * 0.5f - wc.initial_size.x * 0.5f,
                                    d.y * 0.5f - wc.initial_size.y * 0.5f);
        return wc;
    }());
    return win;
}

void Render(Snapshot& snap)
{
    UpdateNavState(snap);

    const obs::Palette pal = BuildPalette();
    obs::ApplyTheme(pal, obs::ActiveConfig());
    obs::ObsidianWindow& win = Window();

    snprintf(s_header_status, sizeof(s_header_status), "%s",
             snap.status[0] ? snap.status : (snap.attached ? "attached" : "waiting for roblox"));
    win.Config().subtitle      = s_header_status;
    win.Config().subtitle_tint = snap.attached ? pal.ok : pal.warn;

    static obs::NavDef nav[kPageCount];
    for (int i = 0; i < kPageCount; ++i) {
        nav[i].label    = kPages[i];
        nav[i].icon     = kPageIcons[i];
        nav[i].live     = &s_nav_live[i];
        nav[i].trailing = s_nav_hint[i][0] ? s_nav_hint[i] : nullptr;
    }

    bool open = true;
    if (win.Begin(&open)) {
        // ---- the rail ------------------------------------------------------
        obs::SidebarBegin("menu-rail", ImVec2(0.0f, -1.0f), obs::AM().nav_w);

        // No brand block here at all. The title bar already carries the wordmark and
        // the attach chip, so anything repeated in the rail was three rows of the tab
        // spent saying what row one of the window already says - and the page name
        // lives in the footer card now, where it sits next to the rest of the session.

        obs::SidebarNav("pages", nav, kPageCount, &s_page);

        // Pinned footer: the session, as four read-only rows. The reservation has to
        // cover the whole card or the rail clips its bottom edge and the last row
        // vanishes -- which is why the game row is counted in the height, not hoped for.
        const bool has_game = snap.game[0] != '\0';
        float session_h = obs::S(102.0f);                        // three rows
        if (has_game) session_h += obs::S(17.0f);                 // and the experience
        obs::SidebarFooter(session_h + obs::S(10.0f));
        obs::CardBegin("rail-session", ImVec2(0.0f, session_h), obs::Card_Inset);
        {
            char buf[96];
            ImGui::SetCursorPos(ImVec2(obs::S(obs::AM().card_pad) - obs::S(2.0f),
                                       obs::S(obs::AM().card_pad) - obs::S(4.0f)));
            if (snap.user[0]) {
                snprintf(buf, sizeof(buf), "PID %u", snap.pid);
                obs::RailStat("player", snap.user, &pal.ok);
                obs::RailStat("process", buf);
            } else {
                obs::RailStat("player", snap.attached ? "Resolving..." : "Not attached",
                              snap.attached ? &pal.warn : &pal.text_faint);
                snprintf(buf, sizeof(buf), "0x%012llX", (unsigned long long)g_base_address);
                obs::RailStat("base", buf);
            }
            // Which experience this is, so a screenshot of the menu says where the
            // bug happened. RailStat fits the value to the card, so a 40-character
            // title is ellipsized rather than walked over its own label.
            if (has_game) obs::RailStat("game", snap.game);
            // Whichever Windows this is running on: worth seeing when a build is
            // fine here and misbehaves there, and it is the one row that is never
            // "resolving" -- we already know it.
            if (snap.os[0]) obs::RailStat("system", snap.os);
        }
        obs::CardEnd();
        obs::SidebarEnd();

        // ---- the page (its own scrolling child, so the rail never scrolls) --
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(obs::S(20.0f), obs::S(18.0f)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        if (ImGui::BeginChild("##page", ImVec2(-1.0f, -1.0f),
                              ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None)) {
            ImGui::PushItemWidth(-1.0f);
            switch (s_page) {
            case 0:  PageAimbot();    break;
            case 1:  PageVisuals();   break;
            case 2:  PageMovement();  break;
            case 3:  PageWorld(snap); break;
            case 4:  PagePlayers(snap); break;
            case 5:  PageKeybinds();  break;
            case 6:  PageTheme();     break;
            case 7:  PageConfig();    break;
            default: PageDebug(snap); break;
            }
            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    win.End();

    if (!open) g_request_exit = true;
}

// =============================================================================
//  public entry points
// =============================================================================
void SetActions(const Actions& a) { g_act = a; }

void TickKeybinds()
{
    // Frames of grace between "a slot started capturing" and the first key that may
    // be taken. Capture begins with a click on the card, and the mouse button for
    // that click is still down on the next frame -- without this, binding Mouse 1
    // would be impossible and (worse) clicking a card would bind whatever the
    // previous slot was waiting for. Six frames is ~100ms: longer than a click,
    // shorter than anyone notices.
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
        // Named, because "bind captured" cannot answer "did it take mouse 1?"
        LogLine("bound %s", (g_act.key_name ? g_act.key_name(vk) : "key"));
    }
}

int  CurrentPage() { return s_page; }

void LastRect(ImVec2* pos, ImVec2* size)
{
    obs::ObsidianWindow& win = Window();
    if (pos)  *pos  = win.WindowPos();
    if (size) *size = win.CurrentSize();
}
void SetPage(int p)
{
    s_page = p < 0 ? 0 : (p >= kPageCount ? kPageCount - 1 : p);
}
int  PageCount() { return kPageCount; }
const char* PageName(int p) { return (p >= 0 && p < kPageCount) ? kPages[p] : ""; }

} // namespace menuui
