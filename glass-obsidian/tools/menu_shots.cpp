// -----------------------------------------------------------------------------
//  tools/menu_shots.cpp  --  renders the real menu to PNGs, with no GPU
// -----------------------------------------------------------------------------
//  This compiles `roblox_external/ui/menu_ui.cpp` -- the file the overlay actually
//  draws -- against a stub platform layer, then rasterises each page with
//  tools/soft_raster.h. It exists because a layout change in a menu like this
//  fails visually, not logically: sliders that overflow their card, a rail that
//  does not pin its footer, a grid that goes ragged. Those are invisible to a
//  compiler and obvious in a picture.
//
//  It also runs the one automated check worth having on a menu:
//  every drawn vertex of a page must land inside the window rect (+/- the outer
//  shadow margin). Anything outside is content bleeding past a card or a page.
//
//  Usage: menu_shots [output_dir]
// -----------------------------------------------------------------------------
#include "ui/menu_ui.h"
#include "globals.h"     // the harness drives the same settings globals the app does

#include "../tools/soft_raster.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace obs = obsidian;

namespace {

const int DISP_W = 1440;
const int DISP_H = 900;

float FMax4(float a, float b, float c, float d)
{
    return (((a > b) ? a : b) > ((c > d) ? c : d)) ? ((a > b) ? a : b) : ((c > d) ? c : d);
}


// -----------------------------------------------------------------------------
//  the stub platform layer: enough for the UI to be exercised, nothing more
// -----------------------------------------------------------------------------
struct Stub
{
    // VK -> name, the subset a keybind capture can realistically produce
    static const char* KeyName(int vk)
    {
        switch (vk) {
        case 0:      return "unbound";
        case 0x24:   return "home";
        case 0x01:   return "mouse 1";
        case 0x02:   return "mouse 2";
        case 0x04:   return "mouse 3";
        case 0x45:   return "k";
        case 0x4C:   return "l";
        case 0x56:   return "v";
        case 0x58:   return "x";
        case 0x2E:   return "del";
        case 0x10:   return "shift";
        case 0x11:   return "ctrl";
        case 0x20:   return "space";
        default:     return "key";
        }
    }

    std::vector<std::string> files{ "default.txt", "legit.txt", "sunset.txt" };

    bool Save(const char* n)   { Add(n); LogLine("stub: saved %s", n); return true; }
    bool Load(const char* n)   { LogLine("stub: loaded %s", n); return true; }
    bool Remove(const char* n) {
        for (size_t i = 0; i < files.size(); ++i)
            if (files[i] == n) { files.erase(files.begin() + (long)i); break; }
        LogLine("stub: deleted %s", n);
        return true;
    }
    void Add(const char* n) {
        for (size_t i = 0; i < files.size(); ++i) if (files[i] == n) return;
        files.push_back(n);
    }

    void Test(int id)
    {
        switch (id) {
        case 0: LogLine("stub: position write ok"); break;
        case 1: LogLine("stub: velocity write ok"); break;
        default: LogLine("stub: probe -> 0xA0 1.00, 0xB0 0.00, 0xC0 196.00"); break;
        }
    }
};

Stub g_stub;
menuui::Snapshot g_snap;

const char* kSkyboxes[] = { "default", "moonless night", "sunset", "flat white",
                            "night 2", "foxvale", "corrupted", "foggy morning" };

void FillSnapshot()
{
    g_snap.attached = true;
    snprintf(g_snap.user, sizeof(g_snap.user), "builderman");
    g_snap.pid = 18432;
    snprintf(g_snap.status, sizeof(g_snap.status), "attached");
    snprintf(g_snap.game, sizeof(g_snap.game), "Sunset Ruins");
    g_snap.in_game = true;
    snprintf(g_snap.place, sizeof(g_snap.place), "185605725");
    snprintf(g_snap.version, sizeof(g_snap.version), "client version-6417");
    snprintf(g_snap.os, sizeof(g_snap.os), "Windows 11 Pro");

    // The players tab is driven by exactly one thing: rows the app formatted. These
    // are shaped like the real ones (nearest first, a dead one, the one we watch),
    // because the row layout is what needs eyeballing.
    struct Fake { const char* name; const char* detail; int state; };
    const Fake fake[] = {
        { "SharkBait",   "100/100 hp - 14 studs - Sword",        0 },
        { "pxr_zed",     "88/100 hp - 42 studs",                  0 },
        { "CoolCat2012", "dead - 61 studs",                       1 },
        { "builderman",  "100/100 hp - 96 studs - Rocket Launcher", 2 },
        { "a very long display name indeed", "76/100 hp - 214 studs", 0 },
        { "MegaMan_99",  "100/100 hp - 512 studs - Pizza",        0 },
    };
    g_snap.player_count = (int)(sizeof(fake) / sizeof(fake[0]));
    for (int i = 0; i < g_snap.player_count; ++i) {
        snprintf(g_snap.players[i].name, sizeof(g_snap.players[i].name), "%s", fake[i].name);
        snprintf(g_snap.players[i].detail, sizeof(g_snap.players[i].detail), "%s", fake[i].detail);
        g_snap.players[i].user_id = 1000u + (unsigned)i;
        g_snap.players[i].state = fake[i].state;
    }
    g_snap.spectating = true;
    snprintf(g_snap.player_note, sizeof(g_snap.player_note),
             "Watching builderman - camera 14 studs behind");

    // The world page's server list, in the shape the app hands it: shortened guids,
    // counts, and one row that is ours (accent) which is also full (state 3).
    struct FakeSrv { const char* id; const char* detail; int state; };
    const FakeSrv fsrv[] = {
        { "6a5f1c..0c99", "12/40", 0 },
        { "111111..5555", "40/40", 2 },
        { "b7c2d9..4e10", "3/40", 3 },
        { "0d44a1..9f77", "31/40", 0 },
        { "5e8b00..1cc3", "7/6",   2 },
        { "ffee21..aa10", "1/40",  0 },
    };
    g_snap.server_count = (int)(sizeof(fsrv) / sizeof(fsrv[0]));
    for (int i = 0; i < g_snap.server_count; ++i) {
        snprintf(g_snap.servers[i].id, sizeof(g_snap.servers[i].id), "%s", fsrv[i].id);
        snprintf(g_snap.servers[i].detail, sizeof(g_snap.servers[i].detail), "%s", fsrv[i].detail);
        g_snap.servers[i].state = fsrv[i].state;
    }
    g_snap.server_current = 2;
    g_snap.servers_can_rejoin = true;
    g_snap.server_age_sec = 9;
    snprintf(g_snap.servers_msg, sizeof(g_snap.servers_msg),
             "6 servers from games.roblox.com, this one marked");

    g_snap.skybox_names = kSkyboxes;
    g_snap.skybox_count = (int)(sizeof(kSkyboxes) / sizeof(kSkyboxes[0]));
    snprintf(g_snap.skybox_note, sizeof(g_snap.skybox_note),
             "skybox set; re-asserted every 5 s");

    auto row = [&](std::vector<menuui::Snapshot::Row>& v, const char* k, const char* f, int state) {
        menuui::Snapshot::Row r;
        r.key = k;
        snprintf(r.value, sizeof(r.value), f);
        r.state = state;
        v.push_back(r);
    };
    row(g_snap.runtime, "process", "18432", 1);
    row(g_snap.runtime, "base", "0x00007FF6A1C20000", 1);
    row(g_snap.runtime, "rbxasset", "0x000002B4F00A1000", 1);
    row(g_snap.runtime, "lua state", "0x000002B4D77C0000", 1);
    row(g_snap.runtime, "game instance", "0x000002B4A11E0000", 1);
    row(g_snap.runtime, "player list", "3 of 8 resolved", 2);

    row(g_snap.camera, "viewport", "2560 x 1440", 1);
    row(g_snap.camera, "fov", "70.0 deg", 1);
    row(g_snap.camera, "system angle", "1.34, -0.02", 1);
    row(g_snap.camera, "hrp position", "12.5, 3.2, -88.0", 1);
    row(g_snap.camera, "hrp velocity", "0.0, 196.0, 0.0", 2);
    row(g_snap.camera, "health", "100 / 100", 0);
    snprintf(g_snap.test_result, sizeof(g_snap.test_result),
             "velocity write: old 0.0, 196.0, 0.0 -> new 0.0, 246.0, 0.0 (read-back ok)");
    g_snap.can_test = true;
}

void InstallActions()
{
    menuui::Actions a;
    a.poll_key  = [] { return 0; };
    a.key_name  = &Stub::KeyName;
    a.list_configs = [] { return g_stub.files; };
    a.config_save   = [](const char* n) { return g_stub.Save(n); };
    a.config_load   = [](const char* n) { return g_stub.Load(n); };
    a.config_delete = [](const char* n) { return g_stub.Remove(n); };
    a.open_config_folder = [] { LogLine("stub: would open explorer.exe .\\config"); };
    a.run_test = [](int id) { g_stub.Test(id); };
    a.tp_to_player    = [](uint32_t id) { LogLine("stub: teleported to #%u", id); return true; };
    a.spectate_player = [](uint32_t id) { LogLine("stub: spectating #%u", id); };
    a.spectate_stop   = [] { LogLine("stub: camera handed back"); };
    menuui::SetActions(a);
}

// -----------------------------------------------------------------------------
//  harness
// -----------------------------------------------------------------------------
struct App
{
    softras::Framebuffer fb;
    softras::Raster      raster;

    App()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)DISP_W, (float)DISP_H);
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        fb.Resize(DISP_W, DISP_H);
        Move(4.0f, 4.0f);

        obs::ThemeConfig cfg;
        obs::LoadFonts(cfg);
        io.Fonts->SetTexID((ImTextureID)1);
        raster.SetFontAtlas((ImTextureID)1);
    }
    ~App() { ImGui::DestroyContext(); }

    void Frame(const std::function<void()>& ui)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)DISP_W, (float)DISP_H);
        io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        ui();
        ImGui::Render();
    }

    void Move(float x, float y) { ImGui::GetIO().AddMousePosEvent(x, y); }

    // The area a frame can actually put PIXELS in, used for the bleed check. The
    // word "pixels" is the point: a shadow's vertex bounds reach a hundred px past
    // its window while its clip rect keeps every one of those pixels off screen, so
    // raw bounds are not evidence of a bleed. Intersect each command with its own
    // clip first and take the union of what survives -- anything left is genuinely
    // painted outside the panel it belongs to.
    void VisibleBounds(float* o)
    {
        o[0] = 1e9f; o[1] = 1e9f; o[2] = -1e9f; o[3] = -1e9f;
        const ImDrawData* dd = ImGui::GetDrawData();
        if (!dd) return;
        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* dl = dd->CmdLists[n];
            const int nv = dl->VtxBuffer.Size;
            for (int i = 0; i < dl->CmdBuffer.Size; ++i) {
                const ImDrawCmd& c = dl->CmdBuffer[i];
                if (c.UserCallback || c.ElemCount == 0) continue;
                const int v0 = (int)c.VtxOffset;
                const int v1 = (i + 1 < dl->CmdBuffer.Size)
                             ? (int)dl->CmdBuffer[i + 1].VtxOffset : nv;
                float b[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
                for (int v = v0; v < v1 && v < nv; ++v) {
                    const ImVec2& q = dl->VtxBuffer[v].pos;
                    if (q.x < b[0]) b[0] = q.x;
                    if (q.y < b[1]) b[1] = q.y;
                    if (q.x > b[2]) b[2] = q.x;
                    if (q.y > b[3]) b[3] = q.y;
                }
                const float x0 = (b[0] > c.ClipRect.x) ? b[0] : c.ClipRect.x;
                const float y0 = (b[1] > c.ClipRect.y) ? b[1] : c.ClipRect.y;
                const float x1 = (b[2] < c.ClipRect.z) ? b[2] : c.ClipRect.z;
                const float y1 = (b[3] < c.ClipRect.w) ? b[3] : c.ClipRect.w;
                if (x1 <= x0 || y1 <= y0) continue;          // clipped away entirely
                if (x0 < o[0]) o[0] = x0;
                if (y0 < o[1]) o[1] = y0;
                if (x1 > o[2]) o[2] = x1;
                if (y1 > o[3]) o[3] = y1;
            }
        }
        if (o[2] <= o[0] || o[3] <= o[1]) { o[0] = o[1] = 0; o[2] = o[3] = 0; }
    }

    // Every surface this UI paints is drawn in SCREEN space inside a child window
    // whose draw list is clipped to that child. Get the coordinate space wrong and
    // the pixels silently vanish -- the surface lands outside the clip and nothing
    // complains -- which is exactly how a card-background bug once hid for a whole
    // refactor. So: assert each command's vertices sit near its own clip rect.
    // A fullscreen pass is exempt (backdrop, window bloom), and only a gross
    // displacement fails, since strokes and AA legitimately cross a clip by a px.
    int Misplaced(int* worst_out)
    {
        const ImDrawData* dd = ImGui::GetDrawData();
        int bad = 0, worst = 0;
        if (!dd) return 0;
        const float full_area = (float)DISP_W * (float)DISP_H;
        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* dl = dd->CmdLists[n];
            const int nv = dl->VtxBuffer.Size;
            for (int i = 0; i < dl->CmdBuffer.Size; ++i) {
                const ImDrawCmd& c = dl->CmdBuffer[i];
                const float cw = c.ClipRect.z - c.ClipRect.x;
                const float ch = c.ClipRect.w - c.ClipRect.y;
                // Only card-sized surfaces are checked: a page or the window body
                // legitimately holds clipped overflow (a scrolled list, a bloom
                // that reaches past the edge), while a card's FIRST command is by
                // construction its background, which must land on the card.
                if (cw < 60.0f || ch < 18.0f || cw > 460.0f || ch > 320.0f) continue;
                if (i != 0) continue;
                if (c.UserCallback) continue;
                const int v0 = (int)c.VtxOffset;
                const int v1 = (i + 1 < dl->CmdBuffer.Size)
                             ? (int)dl->CmdBuffer[i + 1].VtxOffset : nv;
                if (v1 <= v0) continue;
                float b[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
                for (int v = v0; v < v1 && v < nv; ++v) {
                    const ImVec2& q = dl->VtxBuffer[v].pos;
                    if (q.x < b[0]) b[0] = q.x;
                    if (q.y < b[1]) b[1] = q.y;
                    if (q.x > b[2]) b[2] = q.x;
                    if (q.y > b[3]) b[3] = q.y;
                }
                const float over = FMax4(c.ClipRect.x - b[0], c.ClipRect.y - b[1],
                                         b[2] - c.ClipRect.z, b[3] - c.ClipRect.w);
                if (over > 3.0f) {
                    ++bad;
                    printf("        cmd0 of clip %.0f,%.0f-%.0f,%.0f reaches %.0f,%.0f-%.0f,%.0f\n",
                           c.ClipRect.x, c.ClipRect.y, c.ClipRect.z, c.ClipRect.w,
                           b[0], b[1], b[2], b[3]);
                }
                if (over > worst) worst = (int)over;
            }
        }
        if (worst_out) *worst_out = worst;
        return bad;
    }

    void Capture(const char* path)
    {
        fb.Clear(ImVec4(0.045f, 0.047f, 0.055f, 1.0f));
        raster.Render(ImGui::GetDrawData(), fb);
        if (softras::SavePNG(path, fb)) printf("    wrote %s\n", path);
        else                            printf("    FAILED to write %s\n", path);
    }
};

int g_fail = 0;
void Check(bool ok, const char* what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

} // namespace

int main(int argc, char** argv)
{
    const char* dir = (argc > 1) ? argv[1] : "menu_shots";
    char path[512];
    snprintf(path, sizeof(path), "mkdir -p \"%s\"", dir);
    if (system(path) != 0) {}

    printf("menu shots -> %s\n", dir);

    App app;
    FillSnapshot();
    InstallActions();

    // Some things on, so the render shows the "live" state of the shell rather
    // than an all-off menu: rail dots, accent edges, revealed sub-controls.
    aimbot_enabled = true;
    sticky_aim = true;
    prediction_enabled = true;
    show_fov = true;
    team_check = true;
    esp_enabled = true;
    box_esp = true;
    name = true;
    healthbar = true;
    skeleton_esp = true;
    walkspeed_enabled = true;
    flight_enabled = true;
    skybox_changer_enabled = true;
    skybox_type = 2;
    aimbot_keybind = 0x4C;
    menu_toggle_keybind = 0x24;
    click_teleport_keybind = 0x56;      // a bound one: the hint must change
    spectate_enabled = true;
    spectate_distance = 14.0f;
    spectate_height = 3.0f;
    ui_accent_color[0] = 0.78f; ui_accent_color[1] = 0.08f; ui_accent_color[2] = 0.08f;

    LogLine("overlay initialised");
    LogLine("found RobloxPlayerBeta.exe (pid 18432)");
    LogLine("base 0x7FF6A1C20000, rbxasset +0x1D8E000");
    LogLine("resolved 8 players, 3 in viewport");
    LogLine("aimbot locked onto 'sharkie' (head)");

    const int pages = menuui::PageCount();
    for (int p = 0; p < pages; ++p) {
        menuui::SetPage(p);
        // Hover the middle of the window on one page, so the hover bloom and
        // sheen are visible in at least one shot.
        for (int f = 0; f < 24; ++f) {
            // only the first page is hovered, so the rest show their resting state
            app.Move(p == 0 && f > 10 ? 560.0f : 4.0f, p == 0 && f > 10 ? 300.0f : 4.0f);
            app.Frame([] { menuui::Render(g_snap); });
        }
        snprintf(path, sizeof(path), "%s/%02d-%s.png", dir, p + 1, menuui::PageName(p));
        app.Capture(path);

        // bleed check: everything drawn must sit inside the window + a hair of
        // margin. A card whose content is too wide pushes vertices past the page
        // edge, and that is exactly the failure a grid of child windows can hide.
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        // Two pixels, which is the AA fringe of a stroke that ends on the window's
        // own edge. Nothing else may escape the rect in any direction: the chrome
        // paints no bloom, ApplyTheme switched off ImGui's stock window shadow, and
        // a card or a slider that runs off the grid -- or a label that outgrows the
        // panel -- now fails the run instead of hiding in a generous margin.
        const float margin = 2.0f;
        const bool inside = b[0] >= wp.x - margin && b[1] >= wp.y - margin &&
                            b[2] <= wp.x + ws.x + margin && b[3] <= wp.y + ws.y + margin;
        printf("    window %.0f,%.0f %.0fx%.0f | visible %.0f,%.0f - %.0f,%.0f%s\n",
               wp.x, wp.y, ws.x, ws.y, b[0], b[1], b[2], b[3], inside ? "" : "  <-- OFF-WINDOW");
        if (!inside) ++g_fail;

        int worst = 0;
        const int misplaced = app.Misplaced(&worst);
        printf("    %d command(s) outside their clip, worst overshoot %dpx\n", misplaced, worst);
        if (worst > 60) { printf("    FAIL: a surface is being painted in the wrong space\n"); ++g_fail; }
    }

    // ---- the two states the page loop cannot reach ---------------------------
    // Out of game there is no attach, no place name, no players and no OS to show,
    // and the rail's brand row has to degrade to the status chip alone. Separately,
    // transparency at its maximum must fade surfaces and never the letters. Neither
    // is a page, so neither is shot above -- and both are silent regressions: one
    // renders "null" or an orphan wordmark, the other washes the text out with the
    // background it is supposed to sit on.
    auto shoot = [&](const char* nm) {
        menuui::SetPage(4);
        for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
        snprintf(path, sizeof(path), "%s/%s.png", dir, nm);
        app.Capture(path);
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        const bool inside = b[0] >= wp.x - 2.0f && b[1] >= wp.y - 2.0f &&
                            b[2] <= wp.x + ws.x + 2.0f && b[3] <= wp.y + ws.y + 2.0f;
        printf("    %s: window %.0fx%.0f, visible %.0f,%.0f - %.0f,%.0f%s\n",
               nm, ws.x, ws.y, b[0], b[1], b[2], b[3], inside ? "" : "  <-- OFF-WINDOW");
        if (!inside) ++g_fail;
    };

    g_snap.attached = false;
    g_snap.in_game  = false;
    g_snap.player_count = 0;
    g_snap.spectating = false;
    g_snap.user[0] = g_snap.game[0] = g_snap.place[0] = g_snap.os[0] = '\0';
    snprintf(g_snap.status, sizeof(g_snap.status), "idle");
    snprintf(g_snap.player_note, sizeof(g_snap.player_note), "not attached to a game process");
    // the world page has to prove its empty state too: an old list left in place is
    // exactly how a shot ends up lying about what the app would show
    g_snap.server_count = 0;
    g_snap.server_current = -1;
    g_snap.servers_can_rejoin = false;
    g_snap.server_age_sec = -1;
    g_snap.servers_busy = false;
    snprintf(g_snap.servers_msg, sizeof(g_snap.servers_msg),
             "not attached - there is no place to look up");
    shoot("00-detached");

    g_snap.attached = true;
    g_snap.in_game  = true;
    FillSnapshot();                       // back to the busy, attached state
    ui_transparency = 90;
    shoot("00-faded");
    {
        const obs::Palette& pal = obs::ActivePalette();
        const float ta = pal.TextAlphaScale(), sa = pal.SurfaceAlphaScale();
        printf("    at 90%% transparency: text alpha %.2f, surface alpha %.2f\n", ta, sa);
        Check(ta > 0.999f, "text stays opaque while transparency is up");
        Check(sa < 0.60f, "backgrounds actually fade");
    }
    ui_transparency = 0;

    // ---- structural checks --------------------------------------------------
    menuui::SetPage(0);
    app.Frame([] { menuui::Render(g_snap); });
    Check(menuui::PageCount() == 9, "nine pages");
    Check(std::strcmp(menuui::PageName(4), "players") == 0, "players is page 5");
    Check(std::strcmp(menuui::PageName(7), "config") == 0, "page names stable");

    printf("%s\n", g_fail ? "FAILURES" : "all ok");
    return g_fail;
}
