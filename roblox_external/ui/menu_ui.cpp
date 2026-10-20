// -----------------------------------------------------------------------------
//  ui/menu_ui.cpp  --  the Axiom menu: nav rail, card grid, ten pages
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

#include "features/servers/servers.h"        // View / Row, for FillServers below
#include "features/servers/servers_json.h"   // ShortGuid, so the list the app builds and
                                             // the list the host test proves are the same
                                             // function (docs §6)
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

// The theme page edits the accents through an RGBA copy, because the stock picker
// wants four floats and the globals keep three (alpha would be a fourth thing to
// explain on an accent). They are copied in and out around the picker, so the
// globals stay the single source of truth.
float    s_accent4[4]   = { 1.0f, 1.0f, 1.0f, 1.0f };
float    s_accent24[4]  = { 0.5f, 0.2f, 0.9f, 1.0f };
float    s_tint4[4]     = { 0.6f, 0.7f, 1.0f, 1.0f };
int      s_accent_sel   = -1;

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
                               "keybinds", "theme", "config", "debug", "dex" };
constexpr int kPageCount = (int)(sizeof(kPages) / sizeof(kPages[0]));

obs::IconDrawFn kPageIcons[kPageCount] = {
    obs::icon::Crosshair, obs::icon::Eye,    obs::icon::Sliders, obs::icon::Globe,
    obs::icon::Users,     obs::icon::Keyboard, obs::icon::Palette,
    obs::icon::Folder,    obs::icon::Terminal,  obs::icon::Cube,
};

bool s_nav_live[kPageCount]  = {};      // drives the rail's dot + label brightness
char s_nav_hint[kPageCount][16] = {};   // "N on"

const char* kAimTypes[]  = { "Camera", "Mouse position" };
const char* kBones[]     = { "Head", "Upper torso", "Lower torso", "Left hand",
                             "Right hand", "Left foot", "Right foot" };
const char* kBoxStyles[] = { "Full box", "Corners" };
// Off-screen indicator shapes. Named by what they look like, not by index: the
// reference called them 0/1/2 and a user reading "style 2" learns nothing.
const char* kOofStyles[] = { "Arrow", "Chevron", "Triangle" };

// TYPOGRAPHY: this language labels in caps, and obsidian does the shouting for
// you -- MicroCaps (card titles, group rules, page titles, the hero heading),
// chips and keycaps all upper-case their text inside the widget. So source here
// is written in ordinary words. What must NOT be lowercase is the *prose*: notes,
// hints and subtitles are real sentences, so they get capitals, and the acronyms
// they contain get theirs (FOV, ESP, PID, Escape, Roblox). A page that reads
// "fov changer" next to "ESP" is what made the old menu look unfinished.

// =============================================================================
//  theme table
// =============================================================================
// One row per preset the theme page offers. `palette` is the library name
// (obsidian::PaletteByName) so a preset added to the library shows up here with a
// one-line entry, and the menu never re-derives a colour ramp by hand -- which is
// what the old menu did, and why it could only ever be a dark menu.
struct ThemeDef
{
    const char* name;       // what the menu says
    const char* palette;    // what the library calls it
    const char* blurb;      // one line, shown under the picker
    bool        light;      // drives the HUD text colour as well as the glass
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

// The combo wants a C array of names, and the table is the only place the names
// are written down, so the list is built once from it. A second hard-coded array
// is how a menu ends up offering "Nebula" and having no idea what to do with it.
const char* const* ThemeNameList(int* out_count)
{
    static const char* names[kThemeCount];
    static bool init = false;
    if (!init) {
        init = true;
        for (int i = 0; i < kThemeCount; ++i) names[i] = kThemes[i].name;
    }
    if (out_count) *out_count = kThemeCount;
    return names;
}

const char* const kColumns[]     = { "1 column", "2 columns", "3 columns" };
const char* const kRainbowModes[] = { "off", "cycle", "sweep", "breathe", "duotone" };
const char* const kRailModes[]    = { "labels", "icons only", "hidden" };

int ThemeClamped() { return ui_theme < 0 ? 0 : (ui_theme >= kThemeCount ? kThemeCount - 1 : ui_theme); }
const ThemeDef& ThisTheme() { return kThemes[ThemeClamped()]; }

// -----------------------------------------------------------------------------
//  layout helpers
// -----------------------------------------------------------------------------
// Which groups the user closed. Keyed by "page/name" because two pages have a
// group called "movement" and a collapse state that leaked between them would look
// like a bug in the layout, not in the key. The registry is built as the pages run,
// so an id that stops existing costs nothing, and the config stores only the names
// that are closed -- the default state is open, which is the state a menu has to be
// in for its contents to be findable at all.
struct GroupReg
{
    char id[64];
    bool open;
};
std::vector<GroupReg>& Groups()
{
    static std::vector<GroupReg> g;
    return g;
}

bool GroupIsOpen(const char* id)
{
    for (const GroupReg& g : Groups())
        if (strcmp(g.id, id) == 0) return g.open;
    Groups().push_back({ {}, true });
    snprintf(Groups().back().id, sizeof(Groups().back().id), "%s", id);
    return true;
}

void GroupSetOpen(const char* id, bool open)
{
    for (GroupReg& g : Groups())
        if (strcmp(g.id, id) == 0) { g.open = open; return; }
    Groups().push_back({ {}, open });
    snprintf(Groups().back().id, sizeof(Groups().back().id), "%s", id);
}

void GroupSetAll(bool open)
{
    for (GroupReg& g : Groups()) g.open = open;
    if (open) Groups().clear();          // open is the default: forget the list
}

void GroupsSetClosed(const char* list);
void GroupsGetClosed(char* out, size_t n);

// The registry and `ui_closed_groups` are the same fact in two shapes: the vector is
// what the layout asks, the string is what the config file keeps. GroupsPersist()
// runs after every local change (a click, expand-all) and the mirror below is what
// stops the next frame from reading our own write back as an external edit.
// The mirror of what the registry last serialised. One buffer for both directions on
// purpose: if Persist() kept its own copy, every click would look like an external
// edit to SyncFromConfig() on the next frame and the registry would be rebuilt from
// the string we had just written. Same answer, wastefully.
char s_groups_mirror[sizeof(ui_closed_groups)] = { 0 };

void GroupsPersist()
{
    GroupsGetClosed(s_groups_mirror, sizeof(s_groups_mirror));
    memcpy(ui_closed_groups, s_groups_mirror, sizeof(ui_closed_groups));
}

// Called at the top of every frame: if the config file (or a RESET, or a loaded
// cfg) changed the string, that is the only truth and the registry is rebuilt from
// it. Comparing to the mirror rather than re-parsing keeps a frame where nothing
// happened free, and keeps a frame where the user clicked from being undone.
void GroupsSyncFromConfig()
{
    if (strcmp(s_groups_mirror, ui_closed_groups) == 0) return;
    snprintf(s_groups_mirror, sizeof(s_groups_mirror), "%s", ui_closed_groups);
    GroupsSetClosed(s_groups_mirror);
}

// Set from the config: everything named here starts closed.
void GroupsSetClosed(const char* list)
{
    Groups().clear();
    if (!list) return;
    const char* p = list;
    while (*p) {
        const char* e = strchr(p, ';');
        const size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > 0 && n < sizeof(GroupReg::id)) {
            char id[64];
            snprintf(id, sizeof(id), "%.*s", (int)n, p);
            Groups().push_back({ {}, false });
            snprintf(Groups().back().id, sizeof(Groups().back().id), "%s", id);
        }
        if (!e) break;
        p = e + 1;
    }
}

void GroupsGetClosed(char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    size_t used = 0;
    for (const GroupReg& g : Groups()) {
        if (g.open) continue;
        const int wrote = snprintf(out + used, n - used, "%s%s", used ? ";" : "", g.id);
        if (wrote < 0 || used + (size_t)wrote >= n) break;
        used += (size_t)wrote;
    }
}

// The group divider, and the grid it owns. Returns false when the group is closed,
// which is what the `if (Group("x")) { ... }` shape at every call site turns on:
// a closed group submits NO cards, so it costs the layout nothing rather than
// drawing them into a clipped box.
bool Group(const char* label, const char* summary = nullptr)
{
    char id[64];
    snprintf(id, sizeof(id), "%s/%s", kPages[s_page < kPageCount ? s_page : 0], label);

    // With collapsing off the rule is decoration, exactly as it was before the
    // feature existed -- one flag, and a user who never wanted chevrons gets the
    // plain divider back rather than a chevron they cannot turn off.
    if (!ui_collapse_groups) {
        obs::GroupLabel(label);
        obs::GridCfg g;
        g.columns = ui_columns < 1 ? 1 : (ui_columns > 3 ? 3 : ui_columns);
        obs::GridBegin(g);
        return true;
    }

    bool open = GroupIsOpen(id);
    if (obs::GroupLabelCollapsible(id, label, &open, summary)) {
        GroupSetOpen(id, open);
        GroupsPersist();
        obs::GridCfg g;
        g.columns = ui_columns < 1 ? 1 : (ui_columns > 3 ? 3 : ui_columns);
        obs::GridBegin(g);
        return true;
    }
    GroupSetOpen(id, false);
    GroupsPersist();
    return false;
}

void EndGroup()
{
    obs::GridEnd();
    ImGui::Dummy(ImVec2(0.0f, obs::S(4.0f)));
}

// The one string every card-drawing call uses to decide whether a hint line is
// worth its row: `ui_hints` off is the "I know what noclip does" mode, and it is
// the difference between a page of 30 rows and a page of 18.
const char* Hint(const char* text) { return ui_hints ? text : nullptr; }

// The card-shaped toggle, with the menu's own display switch applied: hints off
// means the one-line explanation is not passed at all, so the card collapses to
// its one-line form rather than leaving a gap where the text used to be.
bool ToggleCard(const char* label, bool* v, const char* hint = nullptr, int span = 1)
{
    return obs::ToggleCard(label, v, Hint(hint), span);
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
    // "Hero cards off" is not "the master switch is missing": the same setting stays
    // in the grid as a plain row, it just stops taking a page's worth of space.
    if (!ui_hero_cards) { ToggleCard(title, v, desc, 2); return; }

    obs::GridCell(2, obs::S(64.0f));
    obs::CardBegin(id, ImVec2(0.0f, 0.0f),
                   (obs::CardFlags)(obs::Card_Hoverable | obs::Card_Hero |
                                    (*v ? obs::Card_Live : 0)));
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
    if (desc && desc[0] && ui_hints)
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
void PageAimbot(Snapshot& snap)
{
    // "armed" instead of "active" when the key is a latch that is on: the two feel
    // the same to the aimbot and are not the same to the hand on the mouse.
    const bool latched = snap.aimbot_latched;
    obs::PageTitle("aimbot",
                   "Turns the view onto the closest target inside the FOV ring",
                   latched ? "armed" : (aimbot_enabled ? "active" : nullptr),
                   latched ? &obs::ActivePalette().warn
                           : (aimbot_enabled ? &obs::ActivePalette().ok : nullptr));

    obs::GridCfg one; one.columns = 2; obs::GridBegin(one);
    Hero("aimbot-master", "Aimbot",
         "No key bound = always on while this switch is on", &aimbot_enabled);
    obs::GridEnd();

    if (Group("targeting")) {
    obs::ComboCard("aim from", &aimbot_aim_type, kAimTypes, IM_ARRAYSIZE(kAimTypes));
    obs::ComboCard("target bone", &aimbot_part, kBones, IM_ARRAYSIZE(kBones));
    ToggleCard("sticky aim", &sticky_aim,
               "Holds the lock while the key is held - or latched on");
    ToggleCard("lead prediction", &prediction_enabled, "Aim where they will be");
    if (prediction_enabled) {
        obs::SliderCard("predict x", &prediction_x, 1.0f, 50.0f, "%.0f", 1.0f, 10.0f, true);
        obs::SliderCard("predict y", &prediction_y, 1.0f, 50.0f, "%.0f", 1.0f, 10.0f, true);
    }
    EndGroup();
    }

    if (Group("feel")) {
    obs::SliderCard("smoothing x", &smoothing_x, 2.0f, 20.0f, "%.1f");
    obs::SliderCard("smoothing y", &smoothing_y, 2.0f, 20.0f, "%.1f");
    ToggleCard("humanizer", &humanizer_enabled, "Reaction delay, curved path, a little jitter");
    if (humanizer_enabled)
        obs::SliderCard("humanize amount", &humanizer_strength, 0.0f, 1.0f, "%.2f",
                        1.0f, 0.5f, true, true);
    obs::SliderCard("fov radius", &fov_size, 10.0f, 500.0f, "%.0f px");
    ToggleCard("draw fov ring", &show_fov);
    EndGroup();
    }

    if (Group("filters")) {
    ToggleCard("team check", &team_check, "Skip players on your team");
    ToggleCard("wall check", &wall_check, "Never lock through another player");
    ToggleCard("dead check", &dead_check,
               "Skip them, and let go of a target who dies mid-lock");
    EndGroup();
    }

    if (Group("triggerbot")) {
    // Its own switch and its own key, not the aimbot's: aiming and firing are
    // separate decisions, and someone who wants one rarely wants the other by
    // default. Shared filters (team / wall / dead / blacklist) live above and in
    // the visuals page, and this group says so rather than duplicating them.
    ToggleCard("triggerbot", &triggerbot_enabled,
               "Fires on a body on the crosshair; shares the filters");
    if (triggerbot_enabled) {
        obs::ComboCard("aim at", &triggerbot_part, kBones, IM_ARRAYSIZE(kBones));
        obs::SliderCard("crosshair radius", &triggerbot_fov, 1.0f, 60.0f, "%.0f px");
        obs::SliderCard("reaction delay", &triggerbot_delay_ms, 0.0f, 600.0f, "%.0f ms");
        obs::SliderCard("shot cooldown", &triggerbot_cooldown_ms, 60.0f, 1000.0f, "%.0f ms");
        obs::SliderCard("max distance", &triggerbot_max_dist, 0.0f, esp_draw_distance_max,
                        "%.0f studs", 1.0f, 0.0f, true);
        obs::SliderCard("hit chance", &triggerbot_chance, 1.0f, 100.0f, "%.0f%%");
        ToggleCard("require line of sight", &triggerbot_visible_check,
                   "Same raycast as the wall check");
        Bind("triggerbot key", &triggerbot_keybind, triggerbot_enabled);
        ToggleCard("hold to fire", &triggerbot_hold_mode,
                   "Off: one press arms, one disarms");
        // The reference's misc/hitsound, wired to the health read-back that
        // already proves a shot landed: no second detector, no click sounds on
        // misses.
        ToggleCard("hit sound", &hitsound_enabled,
                   "Plays when the 400ms health read-back proves a hit");
        if (hitsound_enabled) {
            obs::SliderCard("volume", &hitsound_volume, 0.0f, 100.0f, "%.0f%%");
            if (snap.hitsound_names && snap.hitsound_count > 0) {
                if (hitsound_selected < 0) hitsound_selected = 0;
                if (hitsound_selected >= snap.hitsound_count)
                    hitsound_selected = snap.hitsound_count - 1;
                obs::ComboCard("sound", &hitsound_selected, snap.hitsound_names,
                               snap.hitsound_count);
            }
            if (obs::ButtonCard("hitsound test", "PLAY", obs::ButtonKind_Default,
                                "sounds/ next to the exe"))
                if (g_act.hitsound_test) g_act.hitsound_test();
        }
    }
    EndGroup();
    }

    if (Group("hitbox")) {
    // On the movement pages this used to sit under "chams + hitboxes" on the visuals
    // tab, which is where you go to change what is DRAWN. Expanding a hitbox changes
    // what your bullets hit, so it lives with the thing that benefits from it; the
    // draw switch stays on visuals, next to the colours it would be seen in.
    ToggleCard("hitbox expander", &hitbox_expander_enabled,
               "Bigger hitboxes on their character, so shots connect");
    if (hitbox_expander_enabled)
        obs::SliderCard("hitbox size", &hitbox_expander_value, 1.0f, 50.0f, "%.0f",
                        1.0f, 10.0f, true, true);
    EndGroup();
    }

    if (Group("bind")) {
    Bind("aimbot key", &aimbot_keybind, aimbot_enabled);
    // On: hold the key to aim (what this always did). Off: the key flips a latch,
    // so one press arms the aimbot and the next disarms it. The hint doubles as the
    // state read-out, because a toggle you cannot see is a footgun.
    ToggleCard("hold to aim", &aimbot_hold_mode,
               latched ? "latched - press the key to disarm"
                       : "Off: one press arms, one disarms");
    // There used to be a second "enabled" card here for the same global the hero card
    // above toggles. Two switches for one setting on one page is not a shortcut, it is
    // a question -- and the hero already falls back to a plain toggle card when hero
    // cards are off, so nothing is unreachable by dropping it.
    EndGroup();
    }
}

// =============================================================================
//  page: visuals (esp)
// =============================================================================
void PageVisuals()
{
    obs::PageTitle("visuals",
                   "ESP boxes, labels, chams - and the camera they are seen through",
                   esp_enabled ? "active" : nullptr,
                   esp_enabled ? &obs::ActivePalette().ok : nullptr);

    obs::GridCfg one; obs::GridBegin(one);   // span-2 cards => one per row
    Hero("esp-master", "ESP",
         "Drawn on the overlay window, never inside the game", &esp_enabled);
    obs::GridEnd();

    if (Group("boxes")) {
    ToggleCard("box", &box_esp);
    if (box_esp) {
        obs::ComboCard("box style", &box_esp_type, kBoxStyles, IM_ARRAYSIZE(kBoxStyles));
        obs::ColorCard("box color", box_esp_color, true);
        ToggleCard("fill", &box_fill);
        if (box_fill) {
            obs::ColorCard("fill top", box_fill_top, true);
            ToggleCard("gradient fill", &box_fill_gradient);
            if (box_fill_gradient) {
                ToggleCard("rotate gradient", &box_fill_gradient_rotate);
                obs::ColorCard("fill bottom", box_fill_bottom, true);
            }
        }
    }
    EndGroup();
    }

    if (Group("strokes")) {
        // One width for everything, and the two knobs that decide whether the ESP is
        // readable over a bright skybox. Kept in their own group because they are not
        // per-feature settings: they are the reason box, skeleton and tracer look like
        // one drawing rather than three overlays bolted together.
        obs::SliderCard("line width", &esp_line_width, 0.5f, 4.0f, "%.1f px", 1.0f, 1.0f, true);
        obs::SliderCard("glow", &esp_glow, 0.0f, 1.0f, "%.2f", 1.0f, 0.35f, true, false, 2);
        if (box_esp && box_esp_type == 1)
            obs::SliderCard("corner length", &esp_corner_frac, 0.05f, 0.5f, "%.0f%%",
                            1.0f, 0.25f, true);
        EndGroup();
    }

    if (Group("tracers")) {
        ToggleCard("tracers", &esp_tracer,
                        "A line from a screen corner to their feet - works off-screen");
        if (esp_tracer) {
            const char* kOrigins[] = { "bottom centre", "bottom left", "bottom right",
                                       "screen centre" };
            obs::ComboCard("from", &esp_tracer_origin, kOrigins, IM_ARRAYSIZE(kOrigins), 1);
            const char* kAttach[] = { "feet", "box centre", "head" };
            obs::ComboCard("to", &esp_tracer_attach, kAttach, IM_ARRAYSIZE(kAttach), 1);
            obs::SliderCard("tracer width", &esp_tracer_width, 0.5f, 6.0f, "%.1f px",
                            1.0f, 1.5f, true);
            obs::SliderCard("max distance", &esp_tracer_max_dist, 0.0f, esp_draw_distance_max,
                            esp_tracer_max_dist > 0.0f ? "%.0f studs" : "no limit",
                            2.0f, 0.0f, true);
            ToggleCard("use box color", &esp_tracer_box_color,
                       "Team and whitelist tints carry over to the line");
            if (!esp_tracer_box_color) obs::ColorCard("tracer color", esp_tracer_color, true);
        }
        EndGroup();
    }

    if (Group("off-screen")) {
    // The reference's visual/oof: an arrow at the edge of the view for every player
    // the game is not showing at all. Its own group because it answers a different
    // question from the rest of the page - not "where on screen are they" but "which
    // way is the one I cannot see".
    ToggleCard("off-screen arrows", &oof_enabled,
               "For players outside the view - which way they are");
    if (oof_enabled) {
        obs::ComboCard("arrow style", &oof_arrow_style, kOofStyles, IM_ARRAYSIZE(kOofStyles));
        obs::SliderCard("arrow size", &oof_size, 2.0f, 20.0f, "%.0f px");
        obs::SliderCard("ring radius", &oof_radius, 4.0f, 200.0f, "%.0f px");
        obs::ColorCard("arrow color", oof_color, true);
        ToggleCard("arrow outline", &oof_outline);
        if (oof_outline) obs::ColorCard("outline color", oof_outline_color, true);
        ToggleCard("show names", &oof_names);
        ToggleCard("show distance", &oof_distance, "Next to the arrow");
    }
    EndGroup();
    }

    if (Group("labels")) {
    obs::SliderCard("text size", &esp_text_size, 9.0f, 22.0f, "%.0f px", 1.0f, 12.0f, true);
    {
        const char* kPlates[] = { "no plate", "solid plate", "fading plate" };
        obs::ComboCard("name plate", &esp_name_plate, kPlates, IM_ARRAYSIZE(kPlates), 1);
    }
    ToggleCard("player name", &name);
    if (name) obs::ColorCard("name color", name_color, true);
    ToggleCard("health bar", &healthbar);
    if (healthbar) obs::ColorCard("bar color", healthbar_color, true);
    ToggleCard("health text", &health_text);
    if (health_text) obs::ColorCard("health text color", health_text_color, true);
    ToggleCard("distance", &distance);
    if (distance) obs::ColorCard("distance color", distance_color, true);
    ToggleCard("rig type", &rig_type);
    if (rig_type) obs::ColorCard("rig color", rig_type_color, true);
    ToggleCard("equipped tool", &tool_esp);
    if (tool_esp) obs::ColorCard("tool color", tool_color, true);
    // 25k, not 2k. Most public maps are ~10k studs across, so the old ceiling
    // meant "see the whole map? no", and the people who wanted infinite had to
    // discover that 0 is the special case instead. 0 still means infinite and the
    // card says so; the range is now wider than any shipped place.
    // 25,000 studs, not 2,000: most public places are ~10k across, so the old
    // ceiling meant "you can see about a fifth of the map", and the people who
    // wanted the whole thing had to discover that 0 is the special case (infinite)
    // rather than a value. 0 still means infinite; the slider just does not stop
    // short of every shipped place any more.
    obs::SliderCard("draw distance", &esp_render_distance, 0.0f, esp_draw_distance_max,
                    esp_render_distance > 0.0f ? "%.0f studs" : "infinite",
                    2.0f, 0.0f, true);
    EndGroup();
    }

    // "checks" said nothing. These are the aimbot's filters, and the ESP honours the
    // same two globals because a target list and a drawn box should agree -- so the
    // group says who else they belong to, and the one switch that is ONLY about drawing
    // (hiding blacklisted names) lives here rather than on the page that owns the list.
    if (Group("shared with aimbot")) {
    ToggleCard("team check", &team_check,
                    "Teammates are drawn in the team color; the aimbot skips them");
    if (team_check) obs::ColorCard("team color", team_esp_color, true);
    ToggleCard("wall check", &wall_check,
                    "Hide players behind walls (world geometry) or another player");
    ToggleCard("hide blacklisted", &blacklist_hides_esp,
               "The list itself is on the players page");
    EndGroup();
    }

    if (Group("extras")) {
    ToggleCard("skeleton", &skeleton_esp);
    if (skeleton_esp) obs::ColorCard("bone color", skeleton_color, true);
    ToggleCard("aim viewer", &aimviewer, "Line from the target to where they look");
    ToggleCard("china hat", &chinahat);
    if (chinahat) obs::ColorCard("hat color", chinahat_color, true);
    // A dot on every head, the reference's visual/headdot: the cheapest positive ID
    // there is, and the one that keeps working when a box is too small to read.
    ToggleCard("head dot", &headdot_enabled, "A marker on every head");
    if (headdot_enabled) {
        obs::SliderCard("dot size", &headdot_thickness, 1.0f, 12.0f, "%.0f px");
        obs::ColorCard("dot color", headdot_color, true);
        ToggleCard("dot fill", &headdot_fill);
        ToggleCard("dot outline", &headdot_outline);
        if (headdot_outline) obs::ColorCard("dot edge", headdot_outline_color, true);
    }
    // The inventory checker moved from the movement tab's camera group, where it had
    // nothing to do with moving: this draws on the overlay, about another player, which
    // is what this tab is for. It sits by "equipped tool" (labels) because it is the
    // hover-only version of the same answer.
    ToggleCard("inventory checker", &inventory_checker_enabled,
               "What the player under your cursor is holding");
    EndGroup();
    }

    // Its own group, because these are not visual aids: two of the three cards act
    // on frame rates, and the third is the counter that reads them. Grouped, the
    // three numbers are legible together; in "extras" the cap read like another ESP
    // toggle.
    if (Group("frame rate")) {
    ToggleCard("FPS counter", &show_fps, "Game fps, plus the overlay's");
    // The client's cap and the overlay's pacing are different frame rates, so they
    // get different cards: one is a write into the game's scheduler, the other is
    // how often this tool redraws - and the overlay one is what the tool costs.
    ToggleCard("FPS cap", &fps_cap_enabled, "Cap the game's frame rate");
    if (fps_cap_enabled)
        obs::SliderCard("game fps cap", &fps_cap_value, 10.0f, 360.0f, "%.0f",
                        1.0f, 60.0f, true, true);
    obs::SliderCard("overlay cap", &overlay_fps_cap, 0.0f, 360.0f, "%.0f",
                    1.0f, 144.0f, true, true);
    EndGroup();
    }

    if (Group("chams")) {
    ToggleCard("chams", &chams_enabled, "Tint the model through walls");
    if (chams_enabled) obs::ColorCard("chams color", chams_color, true);
    // Mesh chams: the same parts with the backfaces culled and the faces painted
    // far-to-near, so the figure reads as a solid silhouette instead of a pile of
    // translucent boxes. Separate switch and colour - it answers a different
    // question from the box chams above.
    ToggleCard("mesh chams", &mesh_chams, "Solid, backface-culled silhouette of the body");
    if (mesh_chams) obs::ColorCard("mesh color", mesh_chams_color, true);
    // The only half of the hitbox expander that belongs on a drawing tab: whether the
    // (possibly bigger) box is drawn. The size and the switch are on the aimbot page,
    // and the slider is shared, so this still has one when it needs one.
    ToggleCard("draw expanded hitbox", &render_expanded_hitbox,
               "The expander is on the aimbot page");
    if (render_expanded_hitbox || hitbox_expander_enabled)
        obs::SliderCard("hitbox size", &hitbox_expander_value, 1.0f, 50.0f, "%.0f",
                        1.0f, 10.0f, true, hitbox_expander_enabled);
    EndGroup();
    }

    // NPC ESP: the two world categories that are CHARACTERS (AI bodies and
    // animals). They used to be two of the seven "world objects" toggles on the
    // world page next to ore and crates; the user's verdict was to remove the
    // objects and keep the bodies - and a body belongs on the ESP tab.
    if (Group("npc esp")) {
    ToggleCard("npc bots", &world_bots_enabled, "Military / Zombie / BTR / Heli bodies");
    ToggleCard("npc animals", &world_animals_enabled, "Deer / Boar / Wolf");
    if (world_bots_enabled || world_animals_enabled) {
        obs::SliderCard("npc distance", &world_esp_distance, 0.0f, 20000.0f, "%.0f studs",
                        1.0f, 2000.0f, true, true);
        ToggleCard("npc labels", &world_esp_labels, "Name and distance beside each marker");
    }
    EndGroup();
    }

    if (Group("camera")) {
    // The two camera switches, moved off the movement tab. FOV is how much of the
    // screen you see and freecam is which point of view you see it from -- both are
    // answers to "what does this look like", not to "how do I get there", so they sit
    // with the rest of the look and leave a movement page that only contains writes
    // that move your character. The last group on the page on purpose: everything
    // above it is drawn on the overlay, this is the thing it is drawn on.
    //
    // Both hints are short for a reason: the line inside a card has about two dozen
    // characters of room at two columns before the switch on its right eats the rest,
    // and the ellipsis lands exactly where the information was.
    ToggleCard("FOV changer", &fov_changer_enabled, "Not the aimbot's FOV ring");
    if (fov_changer_enabled)
        obs::SliderCard("field of view", &fov_value, 20.0f, 120.0f, "%.0f", 1.0f, 70.0f, true, true);
    ToggleCard("freecam", &freecam_enabled, "WASD to move, RIGHT MOUSE to look");
    if (freecam_enabled)
        obs::SliderCard("freecam speed", &freecam_speed, 10.0f, 250.0f, "%.0f",
                        1.0f, 60.0f, true, true);
    EndGroup();
    }
}

// =============================================================================
//  page: movement / misc
// =============================================================================
void PageMovement()
{
    // Two groups: the writes that move your character, and the one that moves it
    // somewhere else. The camera used to be here (FOV, freecam) and is on visuals
    // now, which is why the subtitle says so -- "where did my freecam key go?" should
    // be answered by the page you are already on. Binds are not on this page any more;
    // the keybinds tab groups them, once.
    obs::PageTitle("movement",
                   "Speed, flight, jumps, teleport - the camera moved to visuals");

    if (Group("movement")) {
    ToggleCard("noclip", &noclip_enabled, "Collision off");
    ToggleCard("flight", &flight_enabled);
    if (flight_enabled) {
        obs::SliderCard("fly speed", &flight_value, 10.0f, 1000.0f, "%.0f", 2.0f, 70.0f, true, true);
        // The stealth profile: keep the world's gravity alone and ramp instead of
        // stepping. Three caps and a slope, because "how fast" and "how abruptly"
        // are the two halves of what makes a flying body look impossible.
        ToggleCard("stealth flight", &flight_stealth, "Gravity untouched; speed ramps");
        if (flight_stealth) {
            obs::SliderCard("speed cap", &flight_max_speed, 10.0f, 300.0f, "%.0f",
                            1.0f, 80.0f, true, true);
            obs::SliderCard("climb cap", &flight_max_climb, 5.0f, 200.0f, "%.0f",
                            1.0f, 45.0f, true, true);
            obs::SliderCard("ramp", &flight_accel, 20.0f, 600.0f, "%.0f",
                            1.0f, 180.0f, true, true);
        }
        // Same phrase as the aimbot's "hold to aim", because it is the same switch
        // over the same idea -- two wordings for one concept is how a user ends up
        // looking for a setting that is already on screen under another name.
        ToggleCard("hold to fly", &flight_hold_mode,
                   "Off: one press starts, one stops");
    }
    ToggleCard("walkspeed", &walkspeed_enabled);
    if (walkspeed_enabled)
        obs::SliderCard("speed", &walkspeed_value, 0.0f, 1000.0f, "%.0f", 1.0f, 30.0f, true, true);
    ToggleCard("infinite jump", &infinite_jump_enabled, "Tap space again mid-air");
    if (infinite_jump_enabled)
        obs::SliderCard("jump power", &infinite_jump_power, 25.0f, 500.0f, "%.0f",
                        1.0f, 100.0f, true, true);
    EndGroup();
    }

    if (Group("physics")) {
    // Three properties, not three motion writers: gravity is the world's, hip
    // height and the fall cap are the character's, and all three are put back the
    // way they were when the switch goes off (each feature owns its own save).
    // Filed together because they answer the same question - how does the body
    // behave in the air - which is also why they are not in the movement group
    // next to walk speed.
    ToggleCard("gravity", &gravity_enabled, "Workspace gravity while on");
    if (gravity_enabled)
        obs::SliderCard("gravity value", &gravity_value, 1.0f, 2000.0f, "%.0f", 1.0f,
                        196.2f, true, true);
    ToggleCard("hip height", &hipheight_enabled, "How far the root floats above the ground");
    if (hipheight_enabled)
        obs::SliderCard("hip value", &hipheight_value, 0.0f, 20.0f, "%.1f", 1.0f,
                        2.0f, true, true);
    ToggleCard("fling", &fling_enabled,
               "Spin the character to launch players who touch it");
    if (fling_enabled)
        obs::SliderCard("spin rate", &fling_power, 60.0f, 2000.0f, "%.0f rad/s", 10.0f,
                        420.0f, false, true);
    // No fall is a POSITION hold now, so there is no speed to set: you cannot go
    // below the level you were at, and the level follows you while you walk.
    ToggleCard("no fall", &nofall_enabled,
               "Hold your height while falling; slopes still follow you");
    // The reference's misc/jumppower: sets Both jump models and the switch that
    // says which one the controller reads, then puts all three back.
    ToggleCard("jump power", &jumppower_enabled, "Sets both jump models; saved and restored");
    if (jumppower_enabled)
        obs::SliderCard("jump value", &jumppower_value, 10.0f, 500.0f, "%.0f", 1.0f,
                        50.0f, true, true);
    EndGroup();
    }

    if (Group("keep alive")) {
    // anti-AFK is the one feature here that types at the game, and the card says so:
    // the tap only goes out while the game's window is in the foreground.
    ToggleCard("anti-afk", &antiafk_enabled, "Taps space so the session survives");
    if (antiafk_enabled)
        obs::SliderCard("interval", &antiafk_interval, 1.0f, 15.0f, "%.0f min", 1.0f,
                        5.0f, true, true);
    EndGroup();
    }

    if (Group("teleport")) {
    // Split out of the movement group when the camera left: five switches about how
    // the character moves, and one about arriving at a place, which is also the thing
    // the players page's GO and the world page's waypoints do with these two numbers.
    ToggleCard("click teleport", &click_teleport_enabled,
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
    }

    // The six bind rows that used to end this page are on the keybinds tab, which is
    // the tab whose only job is binds -- and there they sit next to the menu key they
    // compete for attention with, in a list of everything, grouped by what they bind
    // to. A shortcut copy here meant two answers to "where do I set that".
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

    // --------------------------------------------------------------- lighting --
    // The reference's visual/world/env set: plain Lighting/Terrain properties,
    // each saved and restored. Night composes over fullbright and ambient (all
    // three write Ambient), which is why turning night on visibly wins.
    if (Group("lighting")) {
    ToggleCard("fullbright", &fullbright_enabled, "White ambient + brightness");
    if (fullbright_enabled)
        obs::SliderCard("brightness", &fullbright_value, 1.0f, 5.0f, "%.1f");
    ToggleCard("ambient", &world_ambient_enabled, "One colour for ambient and gradients");
    if (world_ambient_enabled)
        obs::ColorCard("ambient color", world_ambient_color, true);
    ToggleCard("fog", &world_fog_enabled, "Fog start / end / colour");
    if (world_fog_enabled) {
        obs::SliderCard("fog start", &world_fog_start, 0.0f, 5000.0f, "%.0f", 1.0f,
                        260.0f, true, true);
        obs::SliderCard("fog end", &world_fog_end, 1.0f, 20000.0f, "%.0f", 1.0f,
                        900.0f, true, true);
        obs::ColorCard("fog color", world_fog_color, true);
    }
    ToggleCard("exposure", &world_exposure_enabled, "Lighting.ExposureCompensation");
    if (world_exposure_enabled)
        obs::SliderCard("exposure value", &world_exposure_value, -3.0f, 3.0f, "%.2f", 0.05f,
                        0.2f, true, true);
    ToggleCard("no shadows", &noshadow_enabled, "Lighting.GlobalShadows = false");
    ToggleCard("no grass", &nograss_enabled, "Terrain.GrassLength = 0");
    ToggleCard("night", &night_enabled, "Dims ambient and brightness");
    if (night_enabled)
        obs::SliderCard("night level", &night_level, 0.0f, 1.0f, "%.2f");
    ToggleCard("time of day", &world_time_enabled, "Recomputes the whole sky for one hour");
    if (world_time_enabled)
        obs::SliderCard("hour", &world_time_value, 0.0f, 24.0f, "%.2f h", 0.25f, 14.0f,
                        true, true);
    EndGroup();
    }

    // ---------------------------------------------------------------- servers --
    // Roblox decides how you enter a server, so this page only ever builds a
    // roblox:// deeplink and hands it to the client: see features/servers/servers.h.
    if (Group("servers")) {
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
    //
    // `shown` is clamped to the array even though FillServers already clamped
    // `server_count` to it. That is the point of clamping twice: the snapshot is one
    // struct that several producers write, and a row copy that ever overshot used to
    // land on `server_count` itself - a number this loop is about to run to. A draw
    // loop that trusts a count it cannot check is how a producer bug becomes an access
    // violation 42 GB wide, so the bound here is the UI's own, not the data's.
    int shown = snap.server_count;
    if (shown < 0) shown = 0;
    if (shown > Snapshot::kMaxServers) shown = Snapshot::kMaxServers;
    const int rows = shown < 6 ? shown : 6;
    char list_title[80];
    if (shown < 1) snprintf(list_title, sizeof(list_title), "%s", "servers");
    else snprintf(list_title, sizeof(list_title), "%s",
                  "click a server to join it");
    const float list_h = obs::S(46.0f) + obs::S(21.0f) * (float)(rows > 0 ? rows : 1) +
                         (snap.servers_msg[0] ? obs::S(17.0f) : 0.0f);
    if (obs::ScrollCardBegin(list_title, list_h)) {
        if (shown < 1) {
            ImGui::TextDisabled("%s", snap.servers_msg[0] ? snap.servers_msg
                                                           : "(no server list yet)");
        }
        for (int i = 0; i < shown; ++i) {
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
    }

    if (skybox_changer_enabled) {
        if (Group("skybox")) {
        if (snap.skybox_names && snap.skybox_count > 0) {
            if (skybox_type < 0 || skybox_type >= snap.skybox_count) skybox_type = 0;
            obs::ComboCard("preset", &skybox_type, snap.skybox_names, snap.skybox_count);
        }
        EndGroup();
        }
    }

    if (Group("waypoints")) {
    ToggleCard("show on overlay", &show_waypoints);
    WideInput("waypoint name", waypoint_name_buf, sizeof(waypoint_name_buf), "Name, then pin");
    if (obs::ButtonCard("this position", "PIN", obs::ButtonKind_Primary,
                        waypoint_name_buf[0] ? waypoint_name_buf : "wp")) {
        if (g_act.waypoint_add) g_act.waypoint_add(waypoint_name_buf);
    }
    if (obs::ButtonCard("copy my xyz", "FILL", obs::ButtonKind_Default)) {
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
    }
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
        if (Group("status")) {
        obs::NoteCard("the renderer reports", snap.skybox_note);
        EndGroup();
        }
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

    if (Group("actions")) {
    // No value line: "force refresh" next to a button that says RESCAN is the button
    // saying it twice, and why the list is empty is already written under it.
    if (obs::ButtonCard("players", "RESCAN", obs::ButtonKind_Default)) {
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
        // The name is the whole value of the line, so when there is no name the card
        // gets no line: "the camera" under STOP explains nothing the button does not.
        if (obs::ButtonCard("spectate", "STOP", obs::ButtonKind_Danger, watched)) {
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
    }

    if (Group("lists")) {
    const char* tag = have_sel ? (sel.account[0] ? sel.account : sel.name) : "pick a row";
    if (obs::ButtonCard("aimbot", "BLACKLIST", obs::ButtonKind_Danger, tag)) {
        if (have_sel) AddPlayerTag(player_blacklist, player_blacklist_n,
                                   sel.account[0] ? sel.account : sel.name);
    }
    // "Should a blacklisted name also stop being drawn" is a decision about the
    // overlay, so it lives on the visuals tab (group: shared with aimbot) rather than
    // beside the list that only decides who the aimbot may lock onto.
    if (obs::ButtonCard("esp color", "WHITELIST", obs::ButtonKind_Primary, tag)) {
        if (have_sel) AddPlayerTag(player_whitelist, player_whitelist_n,
                                   sel.account[0] ? sel.account : sel.name);
    }
    // The reference's visual/players/friends, next to the whitelist because both
    // answer "make this person special on the ESP" - but keyed by user id, so the
    // mark follows the account through a respawn (an instance address does not).
    // The button carries the state (FRIEND / UNFRIEND) and the line under the
    // label is who it acts on, like every other card in the group: a value line of
    // prose here was the thing that pushed the label into an ellipsis.
    if (obs::ButtonCard("friend", sel.friended ? "UNFRIEND" : "FRIEND",
                        sel.friended ? obs::ButtonKind_Danger : obs::ButtonKind_Default,
                        have_sel ? tag : "pick a row")) {
        if (have_sel && g_act.toggle_friend) g_act.toggle_friend(sel.user_id);
    }
    obs::ColorCard("whitelist color", whitelist_esp_color, true);
    EndGroup();
    }

    if (player_blacklist_n > 0) {
        const float h = obs::S(46.0f) + obs::S(21.0f) * (float)player_blacklist_n;
        if (obs::ScrollCardBegin("aimbot blacklist - click to remove", h)) {
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

    if (Group("menu")) {
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
    }

    // Every bind on this page is borrowed, so each group is named after the tab that
    // owns the setting the key flips - the same rule as everywhere else. "combat" once
    // held exactly one row and named a category nothing else uses, and the camera keys
    // sat under "movement" because freecam moves you, which is true of about half of
    // this tool and useless as a filing rule.
    if (Group("aimbot")) {
    Bind("aimbot", &aimbot_keybind, aimbot_enabled);
    Bind("triggerbot", &triggerbot_keybind, triggerbot_enabled);
    EndGroup();
    }

    if (Group("movement")) {
    Bind("noclip", &noclip_keybind, noclip_enabled);
    Bind("walkspeed", &walkspeed_keybind, walkspeed_enabled);
    Bind("flight", &flight_keybind, flight_enabled);
    Bind("click teleport", &click_teleport_keybind, click_teleport_enabled);
    // The misc set's three keys, on the tab whose page owns the features. Their
    // cards sit next to the switches, but a bind belongs in the bind list - that is
    // the rule this page exists to keep ("binds are grouped by the tab that owns
    // them", and the menu harness checks it).
    Bind("gravity", &gravity_keybind, gravity_enabled);
    Bind("hip height", &hipheight_keybind, hipheight_enabled);
    Bind("fling", &fling_keybind, fling_enabled);
    Bind("no fall", &nofall_keybind, nofall_enabled);
    Bind("jump power", &jumppower_keybind, jumppower_enabled);
    EndGroup();
    }

    if (Group("visuals")) {
    Bind("freecam", &freecam_keybind, freecam_enabled);
    Bind("inventory checker", &inventory_checker_keybind, inventory_checker_enabled);
    EndGroup();
    }
}

// =============================================================================
//  page: theme
// =============================================================================
//  Nine groups, and every one of them is collapsible, because a customiser this
//  wide is otherwise a wall: the user who only wants a colour should see one row,
//  not eighty. Nothing here writes a colour into the library directly -- each card
//  moves a ui_* global and BuildPalette() turns the whole set into a Palette once
//  a frame, which is why every control here repaints the menu the frame it moves.
struct AccentPreset { const char* name; unsigned int rgb; };
// The reference's swatch set, kept, plus the six the reference's own gradient stops
// imply. Order is "the five you are most likely to click first".
const AccentPreset kAccents[] = {
    { "white",   0xFFFFFFu }, { "cyan",    0x59D2FEu }, { "red",     0xE94552u },
    { "violet",  0xB39DDBu }, { "lime",    0x7BE38Bu }, { "amber",   0xF0A840u },
    { "magenta", 0xFF5FA8u }, { "mint",    0x4FE3B0u }, { "ice",     0x9FC7FFu },
    { "coral",   0xFF8A66u },
};
constexpr int kAccentCount = (int)(sizeof(kAccents) / sizeof(kAccents[0]));

// One swatch row that sets the accent. Split out because the theme page wants it
// twice: at the top, as the quick choice, and inside "accent" as the fine one.
void AccentSwatches(float height_pad = 20.0f)
{
    // keep the swatch row honest: if the accent happens to equal a preset (a
    // loaded config, or the app's default) that dot shows as selected
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

    // The height is padding + the title row + a swatch + its caption: clipping the
    // captions off the bottom was the only reason they were there.
    obs::GridCell(2, obs::S(88.0f + height_pad));
    obs::CardBegin("accent-presets");
    obs::CardTitle("presets", 0.0f, true);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + obs::S(20.0f));
    const char* names[kAccentCount];
    unsigned int rgbs[kAccentCount];
    for (int i = 0; i < kAccentCount; ++i) { names[i] = kAccents[i].name; rgbs[i] = kAccents[i].rgb; }
    if (obs::AccentDots(names, rgbs, kAccentCount, &s_accent_sel, 26.0f, true)) {
        const ImVec4 c = obs::Hex(rgbs[s_accent_sel < 0 ? 0 : s_accent_sel], 1.0f);
        ui_accent_color[0] = c.x; ui_accent_color[1] = c.y; ui_accent_color[2] = c.z;
    }
    obs::CardEnd();
}


// A quick two-button choice instead of a fake "light surfaces" toggle: the theme
// IS the polarity, and a second switch that fights the picker is how a UI ends up
// with two settings where one is right.
void LightDarkRow()
{
    obs::GridCell(2, obs::S(46.0f));
    obs::CardBegin("polarity", ImVec2(0.0f, 0.0f), (obs::CardFlags)(obs::Card_Inset | obs::Card_NoPad));
    const obs::Palette p0 = obs::ActivePalette();
    const ImVec2 o = obs::CardOrigin();
    const float pad = obs::S(obs::AM().card_pad);
    ImDrawList* d = ImGui::GetWindowDrawList();
    obs::MicroCaps(d, ImVec2(o.x + pad, o.y + pad - obs::S(1.0f)), "jump to",
                   ImGui::ColorConvertFloat4ToU32(obs::Fade(p0.text_faint, p0.TextAlphaScale())));
    ImGui::SetCursorPos(ImVec2(pad, pad + obs::S(15.0f)));
    if (obs::Button("DARK", ImVec2(obs::S(84.0f), obs::S(22.0f))))
        ui_theme = 0;                       // Obsidian
    ImGui::SameLine(0.0f, obs::S(6.0f));
    if (obs::Button("LIGHT", ImVec2(obs::S(84.0f), obs::S(22.0f))))
        ui_theme = 3;                       // Frost
    obs::CardEnd();
}

// The two resets, split the way the page is split: a colour experiment and a
// layout experiment fail differently, and "reset" that undoes both is a reason not
// to try either. Defaults here are the values in globals.h, stated once more on
// purpose -- a reset that read the header would drift from it silently.
void ResetTheme()
{
    ui_theme = 1;
    ui_contrast = 0.20f;
    ui_tint[0] = 0.55f; ui_tint[1] = 0.65f; ui_tint[2] = 1.00f;
    ui_tint_amount = 0.0f;
    ui_accent_color[0] = 0.78f; ui_accent_color[1] = 0.08f; ui_accent_color[2] = 0.08f;
    ui_accent2_auto = true;
    ui_accent2_color[0] = 0.45f; ui_accent2_color[1] = 0.20f; ui_accent2_color[2] = 0.90f;
    ui_rainbow = 0;
    ui_rainbow_speed = 0.22f;
    ui_rainbow_sat = 0.80f;
    ui_rainbow_span = 0.22f;
    ui_polish = true;
    ui_mat_sheen = 1.0f; ui_mat_edge = 1.0f; ui_mat_bloom = 1.0f;
    ui_mat_chroma = 0.40f; ui_mat_frost = 0.35f; ui_mat_sweep = 0.55f;
    ui_mat_aurora = 0.45f; ui_mat_shimmer = 0.60f; ui_mat_lift = 1.0f;
    ui_mat_gradient = 1.0f;
    ui_anim = true; ui_anim_speed = 1.0f;
    ui_transparency = 6.0f; ui_rounded_corners = true; ui_corner_radius = 16.0f;
    ui_window_shadow = false; ui_glow = 0.0f;
    s_accent_sel = -1;
    LogLine("theme reset to the defaults");
}

void ResetLayout()
{
    ui_win_w = 900.0f; ui_win_h = 620.0f;
    ui_min_w = 580.0f; ui_min_h = 380.0f;
    ui_max_w = 0.0f;   ui_max_h = 0.0f;
    ui_scale = 1.0f; ui_font_size = 15.0f; ui_title_size = 19.0f;
    ui_columns = 2;
    ui_card_h = 50.0f; ui_card_pad = 11.0f; ui_gutter = 8.0f;
    ui_track_h = 8.0f; ui_control_h = 26.0f;
    ui_nav_w = 220.0f; ui_nav_row_h = 36.0f;
    ui_title_h = 40.0f; ui_page_pad = 20.0f; ui_row_space = 8.0f;
    ui_rail_mode = 0; ui_session_card = true;
    ui_hero_cards = true; ui_hints = true; ui_collapse_groups = true;
    GroupSetAll(true);
    // No explicit resize request: Render() applies the size whenever the setting
    // differs from what it last asked for, so writing the globals IS the request,
    // and one more imperative path to the window would be one more to get wrong.
    LogLine("layout reset - size %.0fx%.0f", ui_win_w, ui_win_h);
}

void PageTheme()
{
    const obs::Palette& pal = obs::ActivePalette();
    obs::PageTitle("theme", "Colour, material and layout, all of it live",
                   ui_rainbow ? "cycling" : (pal.light ? "light" : "dark"),
                   ui_rainbow ? &pal.accent : (pal.light ? &pal.info : &pal.text_dim));

    // ---- base: the preset, and the two knobs that make any preset yours ------
    if (Group("base theme")) {
        obs::ComboCard("preset", &ui_theme, ThemeNameList(nullptr), kThemeCount, 1);
        obs::SliderCard("contrast", &ui_contrast, 0.0f, 1.0f, "%.2f", 1.0f, 0.2f, true, false, 1);
        LightDarkRow();
        // A colour mixed into the glass only. It sits next to the amount on purpose:
        // a tint picker whose strength is fixed at "some" is a colour you cannot
        // actually ask for.
        s_tint4[0] = ui_tint[0]; s_tint4[1] = ui_tint[1]; s_tint4[2] = ui_tint[2];
        if (obs::ColorCard("glass tint", s_tint4, false, 1)) {
            ui_tint[0] = s_tint4[0]; ui_tint[1] = s_tint4[1]; ui_tint[2] = s_tint4[2];
        }
        obs::SliderCard("tint amount", &ui_tint_amount, 0.0f, 1.0f, "%.2f", 1.0f, 0.0f, true);
        EndGroup();
    }

    // ---- accent --------------------------------------------------------------
    if (Group("accent")) {
        AccentSwatches();

        s_accent4[0] = ui_accent_color[0];
        s_accent4[1] = ui_accent_color[1];
        s_accent4[2] = ui_accent_color[2];
        if (obs::ColorCard("accent", s_accent4, false)) {
            ui_accent_color[0] = s_accent4[0];
            ui_accent_color[1] = s_accent4[1];
            ui_accent_color[2] = s_accent4[2];
            s_accent_sel = -1;                 // off-preset now
        }
        obs::ToggleCard("auto secondary", &ui_accent2_auto,
                        "Derives the far end of every gradient");

        s_accent24[0] = ui_accent2_color[0];
        s_accent24[1] = ui_accent2_color[1];
        s_accent24[2] = ui_accent2_color[2];
        if (obs::ColorCard("secondary accent", s_accent24, false)) {
            ui_accent2_color[0] = s_accent24[0];
            ui_accent2_color[1] = s_accent24[1];
            ui_accent2_color[2] = s_accent24[2];
            ui_accent2_auto = false;           // picking it IS the decision
        }
        obs::ComboCard("rainbow", &ui_rainbow, kRainbowModes,
                       (int)(sizeof(kRainbowModes) / sizeof(kRainbowModes[0])), 1);
        obs::SliderCard("rainbow speed", &ui_rainbow_speed, 0.02f, 1.5f, "%.2f x", 2.0f,
                        0.22f, true, ui_rainbow != 0);
        obs::SliderCard("rainbow saturation", &ui_rainbow_sat, 0.1f, 1.0f, "%.2f", 1.0f,
                        0.8f, true, ui_rainbow != 0);
        obs::SliderCard("hue spread", &ui_rainbow_span, 0.02f, 0.5f, "%.2f", 1.0f,
                        0.22f, true, ui_rainbow == 2 || ui_rainbow == 3);
        EndGroup();
    }

    // ---- surface -------------------------------------------------------------
    if (Group("glass")) {
        obs::SliderCard("transparency", &ui_transparency, 0.0f, 100.0f, "%.0f%%", 1.0f, 6.0f, true);
        obs::ToggleCard("rounded corners", &ui_rounded_corners);
        if (ui_rounded_corners)
            obs::SliderCard("corner radius", &ui_corner_radius, 0.0f, 28.0f, "%.0f px",
                            1.0f, 16.0f, true);
        obs::ToggleCard("window shadow", &ui_window_shadow,
                        "Off is right over a game: see the notes in window.h");
        obs::SliderCard("inner glow", &ui_glow, 0.0f, 1.0f, "%.2f", 1.0f, 0.0f, true);
        EndGroup();
    }

    // ---- material: the eye candy, each layer on its own dial ------------------
    if (Group("material")) {
        // The dials below are the polish; this is the switch for all of them at once.
        // Moving a dial while polish is off means the user wants THAT dial, so the
        // switch flips itself back on instead of leaving the slider inert.
        auto MatSlider = [](const char* name, float* v, float lo, float hi,
                            const char* fmt, float def) {
            if (obs::SliderCard(name, v, lo, hi, fmt, 1.0f, def, true))
                ui_polish = true;
        };
        ToggleCard("polish", &ui_polish,
                   "Off reads every strength below as 0: flat surfaces, no sheen, "
                   "bloom, frost or movement. Your values are kept");
        obs::ToggleCard("animate", &ui_anim,
                        "Off freezes every moving layer, and keeps the polish");
        obs::SliderCard("animation speed", &ui_anim_speed, 0.1f, 3.0f, "%.2f x", 1.0f,
                        1.0f, true, ui_anim);
        MatSlider("sheen", &ui_mat_sheen, 0.0f, 2.0f, "%.2f", 1.0f);
        MatSlider("edge weight", &ui_mat_edge, 0.0f, 2.0f, "%.2f", 1.0f);
        MatSlider("bloom", &ui_mat_bloom, 0.0f, 2.0f, "%.2f", 1.0f);
        MatSlider("rim chroma", &ui_mat_chroma, 0.0f, 1.0f, "%.2f", 0.4f);
        MatSlider("frost", &ui_mat_frost, 0.0f, 1.0f, "%.2f", 0.35f);
        MatSlider("specular sweep", &ui_mat_sweep, 0.0f, 1.0f, "%.2f", 0.55f);
        MatSlider("aurora", &ui_mat_aurora, 0.0f, 1.0f, "%.2f", 0.45f);
        MatSlider("track shimmer", &ui_mat_shimmer, 0.0f, 1.0f, "%.2f", 0.6f);
        MatSlider("hover lift", &ui_mat_lift, 0.0f, 1.5f, "%.2f", 1.0f);
        MatSlider("surface gradient", &ui_mat_gradient, 0.0f, 1.5f, "%.2f", 1.0f);
        EndGroup();
    }

    // ---- layout: the size of the thing, and of everything in it --------------
    if (Group("layout")) {
        obs::SliderCard("menu width", &ui_win_w, 480.0f, 1920.0f, "%.0f px", 1.0f, 900.0f, true);
        obs::SliderCard("menu height", &ui_win_h, 320.0f, 1200.0f, "%.0f px", 1.0f, 620.0f, true);
        obs::SliderCard("min width", &ui_min_w, 360.0f, 1200.0f, "%.0f px", 1.0f, 580.0f, true);
        obs::SliderCard("min height", &ui_min_h, 220.0f, 1000.0f, "%.0f px", 1.0f, 380.0f, true);
        // A ceiling, for a menu that gets dragged across a desk and then will not
        // fit back on screen. 0 is "no ceiling", so the slider's rest position is
        // the honest no-op rather than a magic number.
        obs::SliderCard("max width", &ui_max_w, 0.0f, 3840.0f, "%.0f px", 1.0f, 0.0f, true);
        obs::SliderCard("max height", &ui_max_h, 0.0f, 2160.0f, "%.0f px", 1.0f, 0.0f, true);
        obs::SliderCard("ui scale", &ui_scale, 0.75f, 1.75f, "%.2f x", 1.0f, 1.0f, true);
        obs::SliderCard("font size", &ui_font_size, 11.0f, 24.0f, "%.0f px", 1.0f, 15.0f, true);
        obs::SliderCard("title size", &ui_title_size, 13.0f, 30.0f, "%.0f px", 1.0f, 19.0f, true);
        EndGroup();
    }

    // ---- density: how much room each setting gets ----------------------------
    if (Group("density")) {
        {
            // The combo indexes, the setting counts, and the conversion lives here
            // rather than in the metric: a grid of "column 0" is an off-by-one the
            // user would have to discover by hand.
            int idx = ui_columns < 1 ? 0 : (ui_columns > 3 ? 2 : ui_columns - 1);
            if (obs::ComboCard("grid columns", &idx, kColumns,
                               (int)(sizeof(kColumns) / sizeof(kColumns[0])), 1))
                ui_columns = idx + 1;
        }
        obs::SliderCard("card height", &ui_card_h, 34.0f, 90.0f, "%.0f px", 1.0f, 50.0f, true);
        obs::SliderCard("card padding", &ui_card_pad, 4.0f, 24.0f, "%.0f px", 1.0f, 11.0f, true);
        obs::SliderCard("gutter", &ui_gutter, 0.0f, 24.0f, "%.0f px", 1.0f, 8.0f, true);
        obs::SliderCard("row spacing", &ui_row_space, 2.0f, 20.0f, "%.0f px", 1.0f, 8.0f, true);
        obs::SliderCard("track thickness", &ui_track_h, 4.0f, 18.0f, "%.0f px", 1.0f, 8.0f, true);
        obs::SliderCard("control height", &ui_control_h, 18.0f, 44.0f, "%.0f px", 1.0f, 26.0f, true);
        obs::SliderCard("page padding", &ui_page_pad, 6.0f, 44.0f, "%.0f px", 1.0f, 20.0f, true);
        EndGroup();
    }

    // ---- navigation: the rail, in three widths ------------------------------
    if (Group("navigation")) {
        obs::ComboCard("rail", &ui_rail_mode, kRailModes,
                       (int)(sizeof(kRailModes) / sizeof(kRailModes[0])), 1);
        obs::SliderCard("rail width", &ui_nav_w, 56.0f, 320.0f, "%.0f px", 1.0f, 220.0f,
                        true, ui_rail_mode == 0);
        obs::SliderCard("row height", &ui_nav_row_h, 24.0f, 56.0f, "%.0f px", 1.0f, 36.0f,
                        true, ui_rail_mode != 2);
        obs::SliderCard("title bar", &ui_title_h, 26.0f, 64.0f, "%.0f px", 1.0f, 40.0f, true);
        obs::ToggleCard("session card", &ui_session_card, "Player, pid and place in the rail");
        EndGroup();
    }

    // ---- what is shown -------------------------------------------------------
    if (Group("detail")) {
        obs::ToggleCard("hero cards", &ui_hero_cards,
                        "A page's master switch as a wide lit card");
        obs::ToggleCard("hints", &ui_hints, "The one-line explanation under a label");
        obs::ToggleCard("collapsible groups", &ui_collapse_groups,
                        "Chevrons on every group divider");
        if (obs::ButtonCard("every group on this page", "EXPAND ALL", obs::ButtonKind_Default,
                            nullptr, 1))
        { GroupSetAll(true); GroupsPersist(); }
        if (obs::ButtonCard("every group on this page", "COLLAPSE ALL", obs::ButtonKind_Primary,
                            nullptr, 1))
        { GroupSetAll(false); GroupsPersist(); }
        EndGroup();
    }

    // ---- reset ---------------------------------------------------------------
    if (Group("reset")) {
        if (obs::ButtonCard("colours, material, glass", "RESET THEME",
                            obs::ButtonKind_Danger, nullptr, 1))
            ResetTheme();
        if (obs::ButtonCard("sizes, density, rail", "RESET LAYOUT",
                            obs::ButtonKind_Primary, nullptr, 1))
            ResetLayout();
        EndGroup();
    }
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

    if (Group("actions")) {
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
                        (s_cfg_name[0] && s_cfg_rename[0]) ? s_cfg_rename : nullptr)) {
        if (s_cfg_name[0] && s_cfg_rename[0] && g_act.config_rename &&
            g_act.config_rename(s_cfg_name, s_cfg_rename)) {
            snprintf(s_cfg_name, sizeof(s_cfg_name), "%s", s_cfg_rename);
            s_cfg_rename[0] = '\0';
            s_cfg_sel = -1;
            RefreshConfigs();
        }
    }
    EndGroup();
    }

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

    if (Group("files")) {
    char countbuf[32];
    snprintf(countbuf, sizeof(countbuf), "%d on disk", (int)s_configs.size());
    if (obs::ButtonCard("config folder", "OPEN", obs::ButtonKind_Default, countbuf)) {
        if (g_act.open_config_folder) g_act.open_config_folder();
    }
    if (obs::ButtonCard("saved list", "REFRESH", obs::ButtonKind_Default, nullptr))
        RefreshConfigs();
    EndGroup();
    }
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



    if (Group("offset write tests")) {
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
    }

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

    if (Group("log actions")) {
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
}

// -----------------------------------------------------------------------------
//  the dex tab
// -----------------------------------------------------------------------------
//  The browser itself is features/explorer: it reads memory, so it cannot live in
//  this translation unit (the headless harness renders this file without a game).
//  The page hands it a rectangle through Actions::draw_dex; with no callback the
//  tab says so instead of drawing an empty box that looks broken.
void PageDex()
{
    obs::PageTitle("dex", "The DataModel, walked on its own thread", nullptr, nullptr);
    if (!g_act.draw_dex) {
        obs::GridCfg one; obs::GridBegin(one);
        obs::NoteCard("explorer", "The browser is not wired into this build.");
        obs::GridEnd();
        return;
    }
    g_act.draw_dex();
}

// =============================================================================
//  nav state
// =============================================================================
void UpdateNavState(const Snapshot& snap)
{
    // Counted per page, so a move between pages has to move the term with it or the
    // rail number starts lying about the tab it sits next to.
    const int aim  = CountOn({ aimbot_enabled, sticky_aim, prediction_enabled,
                               humanizer_enabled, show_fov, team_check, wall_check,
                               dead_check, hitbox_expander_enabled, triggerbot_enabled });
    const int vis  = CountOn({ esp_enabled, box_esp, box_fill, box_fill_gradient,
                               healthbar, health_text, name, distance, rig_type, tool_esp,
                               skeleton_esp, aimviewer, chinahat, chams_enabled,
                               render_expanded_hitbox, show_fps,
                               inventory_checker_enabled, blacklist_hides_esp,
                               fov_changer_enabled, freecam_enabled,
                               headdot_enabled, oof_enabled });
    const int mov  = CountOn({ noclip_enabled, flight_enabled, walkspeed_enabled,
                               infinite_jump_enabled, click_teleport_enabled,
                               // the misc set lives on this page too: a badge that
                               // ignores half the page it counts is worse than none
                               gravity_enabled, hipheight_enabled, nofall_enabled,
                               fling_enabled, antiafk_enabled });
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

    // The aimbot row lights while it is actually aiming: in latch mode the dot
    // tracks the latch, so the armed state is visible from any page of the menu.
    s_nav_live[0] = (aimbot_enabled && (aimbot_hold_mode || snap.aimbot_latched)) ||
                    triggerbot_enabled;
    s_nav_live[1] = esp_enabled;
    s_nav_live[2] = mov  > 0;
    s_nav_live[3] = skybox_changer_enabled;
    s_nav_live[4] = spectate_enabled;
    s_nav_live[5] = armed > 0;
    s_nav_live[6] = ui_rainbow != 0;
    s_nav_live[7] = false;
    s_nav_live[8] = false;

    // Each row counts something different, so each says what it counts: "3 on" is
    // armed settings, and neither people in a server nor bound keys is that.
    struct NavCount { int n; const char* unit; };
    const NavCount counts[kPageCount] = {
        { aim, " on" }, { vis, " on" }, { mov, " on" }, { wrld, " on" },
        { snap.player_count, " here" }, { binds, " bound" },
        // The theme row counts the dials that are actually lit, so "3 on" here means
        // "three material layers are moving" -- the one number on this page worth
        // glancing at from another tab.
        { CountOn({ ui_mat_sheen > 0.0f, ui_mat_chroma > 0.0f, ui_mat_sweep > 0.0f,
                    ui_mat_aurora > 0.0f, ui_mat_shimmer > 0.0f, ui_mat_frost > 0.0f,
                    ui_anim && ui_rainbow != 0 }), " on" },
        { 0, nullptr }, { 0, nullptr },
    };
    for (int i = 0; i < kPageCount; ++i) {
        if (counts[i].n <= 0 || !counts[i].unit) { s_nav_hint[i][0] = '\0'; continue; }
        snprintf(s_nav_hint[i], sizeof(s_nav_hint[i]), "%d%s", counts[i].n, counts[i].unit);
    }
}

} // namespace

// =============================================================================
//  theme: every ui_* global folded into one live palette
// =============================================================================
//  This is the only place the settings become colours. It runs every frame and it
//  is pure: read the globals, return a Palette. Nothing is cached, nothing is
//  written back, so a theme change is one frame late at worst and a config load
//  cannot leave a stale style behind. The library's ApplyTheme() rebuilds
//  ImGuiStyle from the tokens, and every obsidian widget reads ActivePalette(),
//  so the whole UI -- including the parts this file draws by hand -- repaints.
namespace {

// The rainbow, in the one place it can be honest. The two modes that move a hue
// derive the colour from the CLOCK rather than overwriting ui_accent_color: the
// setting is then always "what the user picked", a config saved mid-cycle stores
// the pick, and turning the mode off needs no stashed previous value to restore.
// (The old menu wrote the cycle into the global and restored a copy when it was
// switched off -- one more piece of state, and it corrupted a saved config if the
// user happened to hit SAVE while it was cycling.)
void AccentFromSettings(obs::Palette& p)
{
    ImVec4 a = ImVec4(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
    ImVec4 b = ui_accent2_auto ? obs::Shade(a, -0.26f)
                               : ImVec4(ui_accent2_color[0], ui_accent2_color[1],
                                        ui_accent2_color[2], 1.0f);
    bool have_secondary = !ui_accent2_auto;

    const float speed = ui_rainbow_speed > 0.0f ? ui_rainbow_speed : 0.0001f;
    // Frozen when the material's clock is frozen: "stop the animation" stops this
    // too, and it stops it in phase with every sweep and shimmer in the theme.
    const float t = obs::anim::Clock() * speed;

    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(a.x, a.y, a.z, h, s, v);
    float h2 = h, s2 = s, v2 = v;
    ImGui::ColorConvertRGBtoHSV(b.x, b.y, b.z, h2, s2, v2);

    switch (ui_rainbow) {
    case 1:     // cycle: one hue, the whole wheel, the picked saturation
        a = obs::Hue(t, ui_rainbow_sat, 1.0f, 1.0f);
        break;
    case 2:     // sweep: both stops ride the wheel, keeping their interval, so the
                // gradients stay gradients instead of becoming a flat colour
        a  = obs::Hue(t, ui_rainbow_sat, 1.0f, 1.0f);
        b  = obs::Hue(t + ui_rainbow_span + 0.34f, ui_rainbow_sat, 0.92f, 1.0f);
        have_secondary = true;
        break;
    case 3:     // breathe: the pick stays the pick, its saturation and value swell
        {
            const float k = 0.5f + 0.5f * sinf(t * 6.2831853f);
            a = obs::Hue(h + 0.02f * k, s * (0.45f + 0.55f * k), v * (0.80f + 0.20f * k), 1.0f);
            b = obs::Hue(h + 0.02f * k + ui_rainbow_span, s * (0.85f + 0.15f * k), v * 0.7f, 1.0f);
            have_secondary = true;
        }
        break;
    case 4:     // duotone: the accent holds, the far stop of every gradient cycles
                // under it -- the quiet one, and the only rainbow that does not make
                // the ON state move out from under you
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
    // The flag every Lift() in the widget layer reads. A host that draws its own
    // HUD needs it too: white ink over a light theme is not a style choice.
    p.light = th.light;

    // ---- material ----------------------------------------------------------
    p.mat.sheen    = ui_mat_sheen;
    p.mat.edge     = ui_mat_edge;
    p.mat.bloom    = ui_mat_bloom;
    p.mat.chroma   = ui_mat_chroma;
    p.mat.frost    = ui_mat_frost;
    p.mat.sweep    = ui_mat_sweep;
    p.mat.aurora   = ui_mat_aurora + ui_glow * 0.6f;   // "glow" is aurora plus more of it
    p.mat.shimmer  = ui_mat_shimmer;
    p.mat.lift     = ui_mat_lift;
    p.mat.gradient = ui_mat_gradient;
    p.mat.animate  = ui_anim;
    p.mat.speed    = ui_anim_speed;
    // One switch for the whole material layer. Folded in here (and not left to
    // SetActivePalette) because this function's own caller reads `p.mat.edge` to
    // decide the window border: the answer has to be true of the palette on its way
    // out, not only after it is installed.
    p.mat.polish   = ui_polish ? 1.0f : 0.0f;
    p.mat.ApplyPolish();

    // ---- opacity ------------------------------------------------------------
    // "transparency" is a see-through AMOUNT; the palette wants surface opacity, and
    // the ramp deliberately stops short of zero: below about 0.30 the ink stops
    // being readable over a bright world, which is the one thing a menu must never
    // do -- so the slider's top end is a floor, not a vanish.
    p.opacity = 1.0f - (ui_transparency / 100.0f) * 0.70f;
    p.text_stays_opaque = true;   // the knob moves the glass, never the letters
    p.contrast = ui_contrast;

    // ---- colour -------------------------------------------------------------
    if (ui_tint_amount > 0.0f)
        obs::TintSurfaces(p, ImVec4(ui_tint[0], ui_tint[1], ui_tint[2], 1.0f), ui_tint_amount);
    AccentFromSettings(p);
    obs::BoostContrast(p, ui_contrast);

    // ---- geometry -----------------------------------------------------------
    const float r = ui_rounded_corners ? ui_corner_radius : 0.0f;
    // The three radii stay in proportion to each other (window > card > control),
    // which is what makes a 4px theme and a 24px theme read as the SAME theme at
    // two sizes instead of as two different UIs.
    p.corner_lg = r * 0.72f + obs::S(3.0f);
    p.corner    = r * 0.46f + obs::S(2.0f);
    p.corner_sm = r * 0.30f + obs::S(1.5f);
    return p;
}

// =============================================================================
//  layout: the same argument, for sizes
// =============================================================================
namespace {

void ApplyLayout()
{
    obs::AxiomMetrics am = obs::AM();
    am.card_h    = ui_card_h;
    am.card_pad  = ui_card_pad;
    am.gutter    = ui_gutter;
    am.nav_row_h = ui_nav_row_h;
    am.nav_w     = ui_rail_mode == 1 ? obs::FMax(56.0f, ui_nav_row_h * 2.2f) : ui_nav_w;
    obs::SetAxiomMetrics(am);

    obs::Metrics m;
    m.pad_panel = ui_card_pad + 1.0f;
    m.pad_row   = ui_row_space;
    m.track_h   = ui_track_h;
    m.control_h = ui_control_h;
    m.knob_d    = obs::FMax(9.0f, ui_track_h * 1.9f);
    obs::SetMetrics(m);

    obs::NavStyle ns;
    ns.labels   = (ui_rail_mode == 0);
    ns.trailing = (ui_rail_mode == 0);
    ns.live_dot = (ui_rail_mode != 2);
    ns.icon_size = ui_rail_mode == 1 ? 19.0f : 16.0f;
    obs::SetNavStyle(ns);

    // ApplyTheme() owns ItemSpacing and resets it every frame, so the row rhythm
    // is set after it rather than in the palette -- the style struct is the one
    // place both the library widgets and the stock ones read from.
    ImGuiStyle& st = ImGui::GetStyle();
    st.ItemSpacing.y = obs::FClamp(ui_row_space, 2.0f, 20.0f);
    st.ItemSpacing.x = obs::FClamp(ui_gutter, 4.0f, 24.0f);
}

} // namespace

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
        wc.title_bar_height = ui_title_h;
        wc.initial_size    = ImVec2(ui_win_w, ui_win_h);
        wc.min_width       = ui_min_w;
        wc.min_height      = ui_min_h;
        wc.max_width       = ui_max_w;
        wc.max_height      = ui_max_h;
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
    GroupsSyncFromConfig();
    UpdateNavState(snap);

    const obs::Palette pal = BuildPalette();
    obs::ApplyTheme(pal, obs::ActiveConfig());
    ApplyLayout();

    obs::ObsidianWindow& win = Window();

    // The window's own limits are settings, so they are pushed every frame: a
    // ceiling the user drags up has to apply to the constraint, not to a config
    // that was read once at startup.
    const float sc = obs::S(1.0f) > 0.0f ? obs::S(1.0f) : 1.0f;
    win.Config().min_width    = ui_min_w / sc;
    win.Config().min_height   = ui_min_h / sc;
    win.Config().max_width    = ui_max_w / sc;
    win.Config().max_height   = ui_max_h / sc;
    win.Config().title_bar_height = ui_title_h;
    win.Config().corner_radius    = ui_rounded_corners ? ui_corner_radius * 0.72f : 0.0f;
    win.Config().draw_shadow      = ui_window_shadow;
    win.Config().allow_resize     = true;
    win.Config().allow_collapse   = true;

    snprintf(s_header_status, sizeof(s_header_status), "%s",
             snap.status[0] ? snap.status : (snap.attached ? "attached" : "waiting for roblox"));
    win.Config().subtitle      = s_header_status;
    win.Config().subtitle_tint = snap.attached ? pal.ok : pal.warn;

    // ---- the size, both ways ------------------------------------------------
    // A slider sets the window, and dragging the window's edge moves the slider
    // back. The pair needs the two guards it has: only request a size when the
    // setting actually changed (a per-frame QueueSize is level-triggered forcing,
    // which is the exact bug window.h exists to kill), and adopt the live size
    // only while the user is dragging (adopting always would let the animation
    // write its own in-between frames back into the settings).
    static float s_req_w = -1.0f, s_req_h = -1.0f;
    const bool want_resize = fabsf(s_req_w - ui_win_w) > 0.6f || fabsf(s_req_h - ui_win_h) > 0.6f;
    if (want_resize) {
        s_req_w = ui_win_w; s_req_h = ui_win_h;
        win.QueueSize(ImVec2(ui_win_w, ui_win_h));
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 cur = win.CurrentSize();
        if (cur.x > 1.0f && cur.y > 1.0f &&
            (fabsf(cur.x - ui_win_w) > 0.8f || fabsf(cur.y - ui_win_h) > 0.8f) &&
            !want_resize) {
            ui_win_w = cur.x; ui_win_h = cur.y;
            s_req_w = cur.x; s_req_h = cur.y;
        }
    } else {
        const ImVec2 cur = win.CurrentSize();
        if (cur.x > 1.0f) { ui_win_w = cur.x; ui_win_h = cur.y; s_req_w = cur.x; s_req_h = cur.y; }
    }

    static obs::NavDef nav[kPageCount];
    for (int i = 0; i < kPageCount; ++i) {
        nav[i].label    = kPages[i];
        nav[i].icon     = kPageIcons[i];
        nav[i].live     = &s_nav_live[i];
        nav[i].trailing = s_nav_hint[i][0] ? s_nav_hint[i] : nullptr;
    }

    bool open = true;
    if (win.Begin(&open)) {
        // ---- the rail (or the strip, or nothing at all) ----------------------
        // Three shapes of the same navigation from one setting: full, icon strip,
        // and gone. Hiding it is not a cosmetic option -- it buys the page 220px,
        // which is the difference between a one-column and a two-column grid on a
        // 1080p screen, and it is the mode a menu dragged to a monitor edge wants.
        const bool show_rail = (ui_rail_mode != 2);
        if (show_rail)
            obs::SidebarBegin("menu-rail", ImVec2(0.0f, -1.0f), obs::AM().nav_w / sc);

        // Icons-only still submits the same rows -- NavStyle is what drops the
        // labels and re-centres the glyph, so the selection, the keyboard focus and
        // the tooltip all survive the narrower rail instead of being a second,
        // hand-written version of the navigation.
        obs::SidebarNav("pages", nav, kPageCount, &s_page);

        // Pinned footer: the session, as read-only rows. Skipped entirely when the
        // user turns it off -- the reservation is what the rail's own height is
        // measured against, so "hidden" has to mean "not reserved", or the nav gets
        // a dead band at the bottom the size of a card.
        //
        // It is also skipped in the icon strip, and for the same reason in both: the
        // card is four lines of label/value text, and text is exactly what a 56px
        // rail has no room for. An unreadable card is worse than no card.
        if (show_rail && ui_session_card && ui_rail_mode == 0) {
            const bool has_game = snap.game[0] != '\0';
            float session_h = obs::S(102.0f);                        // three rows
            if (has_game) session_h += obs::S(17.0f);               // and the experience
            obs::SidebarFooter(session_h / sc + obs::S(10.0f));
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
                if (has_game) obs::RailStat("game", snap.game);
                if (snap.os[0]) obs::RailStat("system", snap.os);
            }
            obs::CardEnd();
        }
        if (show_rail) obs::SidebarEnd();

        // ---- the page (its own scrolling child, so the rail never scrolls) --
        if (show_rail) ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(obs::S(ui_page_pad), obs::S(ui_page_pad * 0.9f)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        if (ImGui::BeginChild("##page", ImVec2(-1.0f, -1.0f),
                              ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None)) {
            ImGui::PushItemWidth(-1.0f);
            switch (s_page) {
            case 0:  PageAimbot(snap); break;
            case 1:  PageVisuals();   break;
            case 2:  PageMovement();  break;
            case 3:  PageWorld(snap); break;
            case 4:  PagePlayers(snap); break;
            case 5:  PageKeybinds();  break;
            case 6:  PageTheme();     break;
            case 7:  PageConfig();    break;
            case 8:  PageDebug(snap); break;
            default: PageDex();       break;
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

// Fonts are the one part of a theme that cannot be changed inside a frame (the
// atlas is locked between NewFrame() and Render()), so this runs from the overlay
// loop, between frames, and does nothing until a size setting actually moves.
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

    // The one legal window for a re-bake. LoadFonts() itself refuses inside a
    // frame; this check is here so a host that forgets gets a no-op rather than an
    // assert, and so the log says which happened.
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

// Public entry points for the config file: the strings are parsed into the group
// registry, which keeps the set, because "which page am I on" is per-frame state and
// a config file should not have to know that.
void SetClosedGroups(const char* list) { GroupsSetClosed(list); }
void GetClosedGroups(char* out, size_t n) { GroupsGetClosed(out, n); }

// Float windows: the palette's own tokens, so they read as parts of one UI instead
// of as a second UI that happens to be nearby. The text is kept fully opaque -- over
// a game view, contrast with the *scene* matters more than consistency with the
// menu's translucency, and the plate underneath is what the alpha is for.
void PushFloatWindowStyle(float bg_alpha)
{
    const obs::Palette& p = obs::ActivePalette();
    ImGuiStyle& st = ImGui::GetStyle();

    const float round = ui_rounded_corners ? obs::FClamp(p.corner, 0.0f, 16.0f) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, p.text_dim);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(p.panel.x, p.panel.y, p.panel.z, bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, p.edge);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, round);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, round * 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, p.mat.edge > 0.0f ? 1.0f : 0.0f);
    // A float plate is not a page: it wants its own tighter rhythm than the grid's,
    // and it still has to scale with the theme or a 1.5x menu sits beside a 1x badge.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(obs::S(8.0f), obs::S(5.0f)));
    (void)st;
    (void)bg_alpha;
}

void PopFloatWindowStyle()
{
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
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

// Whether a "page/group" id exists at all -- only meaningful once the pages have
// been drawn, since the registry is built as they run. The headless harness uses it
// to pin the FILING of a setting: a card that quietly moves to another tab is a
// regression no screenshot of the new tab would show, because the new tab looks
// right. Asking for the old id and getting "no such group" is the assertion.
bool HasGroup(const char* id)
{
    for (const GroupReg& g : Groups())
        if (strcmp(g.id, id) == 0) return true;
    return false;
}

// The one snapshot copy the UI cannot get wrong by arithmetic, so it is not inline in
// FillSnapshot. See the declaration in menu_ui.h for the contract it exists to keep.
void FillServers(Snapshot& s, const ::features::servers::View& sv)
{
    int n = sv.count;
    if (n < 0) n = 0;
    if (n > Snapshot::kMaxServers) n = Snapshot::kMaxServers;

    // A row that is not on the list is not "ours", and a negative `current` is the
    // feature's way of saying "unknown" - keep that, the row tint and REJOIN both read
    // it, and a stale index would highlight somebody else's server as yours.
    const int current = (sv.current >= 0 && sv.current < n) ? sv.current : -1;

    // The scalars are free, so they are always current (the age in particular is what
    // the card's "8s ago" line shows). Only the row copy is guarded.
    const bool same = (n == s.server_count) && (current == s.server_current) &&
                      (sv.busy == s.servers_busy) &&
                      (strcmp(sv.msg, s.servers_msg) == 0);   // strcmp, not memcmp over
                      // the buffer: a shorter message leaves the tail of the old one
                      // standing behind its NUL, and that difference means nothing here
    s.servers_busy       = sv.busy;
    s.servers_can_rejoin = sv.can_rejoin;
    s.server_age_sec     = sv.age_sec;
    if (same) return;

    snprintf(s.servers_msg, sizeof(s.servers_msg), "%s", sv.msg);
    s.server_count   = n;
    s.server_current = current;
    for (int i = 0; i < n; ++i) {
        const ::features::servers::Row& src = sv.rows[i];
        Snapshot::ServerRow& d = s.servers[i];          // `i`, never `n`: see above
        if (!::features::serversjson::ShortGuid(src.id, d.id, sizeof(d.id)))
            snprintf(d.id, sizeof(d.id), "%s", src.id);
        snprintf(d.detail, sizeof(d.detail), "%d/%d%s", src.playing, src.max_players,
                 src.state == 3 ? " full" : "");
        d.state = src.state;
    }
}

} // namespace menuui
