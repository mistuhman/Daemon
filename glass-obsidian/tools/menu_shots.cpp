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
//  Usage: menu_shots [output_dir]        env: THEME=<0..8> pins the page shots to a
//                                            POLISH=<0|1> starts with the material
//                                            layer switched off
//  palette (the theme sweep below covers all of them regardless).
// -----------------------------------------------------------------------------
#include "ui/menu_ui.h"
#include "globals.h"     // the harness drives the same settings globals the app does
#include "features/servers/servers.h"   // the View menuui::FillServers copies from

#include "../tools/soft_raster.h"

#include <cmath>
#include <cstdlib>
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

        // This is the harness's stand-in for a renderer backend's device-object
        // re-creation, and it has to run at the TOP of the frame: a re-bake (the scale
        // and font-size settings do one) frees the atlas buffer and zeroes its TexID,
        // and the commands' TextureId is taken from the atlas while the frame is being
        // built. Doing this after Render() is a frame too late -- which is exactly how
        // a shot taken after the 1.5x scale test came out as a page of solid
        // rectangles, since a draw command with no texture id is rasterised as flat
        // vertex colour. Getting the pixels is a cached read once the atlas is baked,
        // so paying for it every frame costs nothing.
        io.Fonts->SetTexID((ImTextureID)1);
        raster.SetFontAtlas((ImTextureID)1);

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
    // THEME=<index> pins the page sweep to one palette, so a screenshot session can
    // be about the layout instead of about the default. The theme sweep below moves
    // ui_theme itself; this only sets what the per-page shots start from.
    if (const char* e = getenv("THEME")) ui_theme = atoi(e);
    if (const char* e = getenv("POLISH")) ui_polish = atoi(e) != 0;
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

    // ---- every theme, in one sweep -----------------------------------------
    // The page loop above can only shoot the default theme, which is precisely the
    // blind spot a revamp like this needs closed: a light preset that is only
    // "light" in its own tokens, a contrast knob that flattens text instead of
    // hardening it, a preset whose accent vanishes on its own surfaces. So the
    // harness walks the same table the theme page offers, shoots each one, and
    // asserts the two things a theme has to get right -- polarity and legibility --
    // in numbers rather than by eye.
    auto Lum = [](ImVec4 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
    // `[&]` because Lum is a local lambda: an inner one has to be told to see it.
    auto Ratio = [&](ImVec4 a, ImVec4 b) {
        const float la = Lum(a), lb = Lum(b);
        const float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
        return (hi + 0.05f) / (lo + 0.05f);
    };

    float baseline_ratio = 0.0f;      // theme 0 at the default contrast, for the knob below
    for (int t = 0; t < 9; ++t) {
        ui_theme = t;
        ui_transparency = 6.0f;
        menuui::SetPage(6);                                   // the theme page
        for (int f = 0; f < 24; ++f) {
            app.Move(4.0f, 4.0f);
            app.Frame([] { menuui::Render(g_snap); });
        }
        const obs::Palette& pal = obs::ActivePalette();
        const int slash = (int)strlen(menuui::PageName(6));
        snprintf(path, sizeof(path), "%s/t%d-theme.png", dir, t);
        app.Capture(path);
        (void)slash;

        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        const bool inside = b[0] >= wp.x - 2.0f && b[1] >= wp.y - 2.0f &&
                            b[2] <= wp.x + ws.x + 2.0f && b[3] <= wp.y + ws.y + 2.0f;
        char nm[64];
        const obs::Palette probe = pal;    // copy: the names are for the log line
        (void)probe;
        snprintf(nm, sizeof(nm), "theme %d", t);
        printf("    %s: text/panel %.1f:1  label/surface %.1f:1%s%s\n", nm,
               Ratio(pal.text, pal.panel), Ratio(pal.text_dim, pal.panel),
               pal.light ? "  [light]" : "  [dark]", inside ? "" : "  <-- OFF-WINDOW");
        if (!inside) ++g_fail;

        // Polarity: the ink has to sit on the far side of the surface from the
        // middle grey, or the theme is not light/dark, it is mud.
        char check[96];
        snprintf(check, sizeof(check), "theme %d: ink is on the right side of its surface", t);
        Check(pal.light ? (Lum(pal.text) < Lum(pal.panel)) : (Lum(pal.text) > Lum(pal.panel)), check);
        snprintf(check, sizeof(check), "theme %d: text on panel >= 4.5:1", t);
        Check(Ratio(pal.text, pal.panel) >= 4.5f, check);
        snprintf(check, sizeof(check), "theme %d: dim label still readable (>= 3:1)", t);
        Check(Ratio(pal.text_dim, pal.panel) >= 3.0f, check);
        if (t == 0) baseline_ratio = Ratio(pal.text, pal.panel);
    }

    // The theme sweep only ever shoots one page, and a light theme's real test is the
    // densest one: sixteen toggles plus colour pickers, where a hard-coded text colour
    // in the layout file would sit on white. Same bleed check as the page loop.
    ui_theme = 3;
    menuui::SetPage(1);
    for (int f = 0; f < 24; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t95-frost-visuals.png", dir);
    app.Capture(path);
    {
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        const bool inside = b[0] >= wp.x - 2.0f && b[1] >= wp.y - 2.0f &&
                            b[2] <= wp.x + ws.x + 2.0f && b[3] <= wp.y + ws.y + 2.0f;
        printf("    frost + visuals: visible %.0f,%.0f - %.0f,%.0f%s\n", b[0], b[1], b[2], b[3],
               inside ? "" : "  <-- OFF-WINDOW");
        Check(inside, "the light theme holds the densest page");
    }
    ui_theme = 1;

    // ---- the two knobs that are supposed to be dramatic ----------------------
    ui_theme = 0;
    ui_contrast = 1.0f;                     // "very high contrast", OLED-black end
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t90-hi-contrast.png", dir);
    app.Capture(path);
    {
        const obs::Palette& pal = obs::ActivePalette();
        const float r = Ratio(pal.text, pal.panel);
        printf("    hi-contrast: text/panel %.1f:1 (same theme at default contrast %.1f:1)\n",
               r, baseline_ratio);
        // Two halves of one claim. WCAG's AAA bar is the absolute one -- "very high
        // contrast" that could not clear 7:1 would be a mislabelled theme -- and the
        // relative one is what matters to the user: at the top of the dial the menu has
        // to be visibly harder-edged than it was in the middle, not a shade nicer.
        Check(r >= 7.0f, "contrast 1.0 clears the AAA legibility bar");
        Check(baseline_ratio > 0.0f && r >= baseline_ratio * 1.25f,
              "contrast 1.0 is a step up, not the same theme");
    }

    ui_contrast = 0.20f;
    ui_theme = 1;
    ui_rainbow = 2;                         // sweep: the accent pair travels
    ui_mat_chroma = 1.0f; ui_mat_sweep = 1.0f; ui_mat_aurora = 1.0f;
    for (int f = 0; f < 30; ++f) {           // enough frames for the band to move
        app.Move(560.0f, 300.0f);
        app.Frame([] { menuui::Render(g_snap); });
    }
    snprintf(path, sizeof(path), "%s/t91-rainbow.png", dir);
    app.Capture(path);
    {
        // The cycle must not have eaten the setting: a palette whose accent came from
        // the clock has to leave the stored global alone, or a SAVE mid-cycle writes
        // a colour the user never picked.
        const float dr = fabsf(ui_accent_color[0] - 0.78f) +
                         fabsf(ui_accent_color[1] - 0.08f) +
                         fabsf(ui_accent_color[2] - 0.08f);
        printf("    rainbow: accent global moved by %.4f while cycling\n", dr);
        Check(dr < 0.001f, "the rainbow never writes back into the accent setting");
        const obs::Palette& pal = obs::ActivePalette();
        Check(pal.accent.x != ui_accent_color[0] || pal.accent.y != ui_accent_color[1] ||
              pal.accent.z != ui_accent_color[2], "the theme page still shows a cycling accent");
    }

    // ---- polish off: the one switch for the whole material layer --------------
    // Two separate claims, and only the first one is visible in a picture. (1) The
    // rendered glass has to go flat: every strength must arrive at the draw sites as
    // 0, because a "turn the shine off" switch that leaves the frost rings and the
    // specular sweep drawing is a switch that does not work. (2) The DIALS must not
    // move: the user is allowed to look at a flat menu and come back to the one they
    // tuned, so ui_mat_* has to survive the round trip and come right back. A build
    // that zeroes the globals to implement the switch passes (1) and fails this.
    ui_rainbow = 0;                         // a travelling accent is not the subject
    menuui::SetPage(6);                     // the theme page: the switch itself, in shot
    // The material group is the fourth one down, and the harness cannot scroll a page
    // from outside it, so the three groups above it are closed instead. That puts the
    // switch at the top of the frame and doubles as a second look at collapsing.
    menuui::SetClosedGroups("theme/base theme;theme/accent;theme/glass");
    ui_polish = false;
    const float dials_before[10] = { ui_mat_sheen, ui_mat_edge, ui_mat_bloom,
                                      ui_mat_chroma, ui_mat_frost, ui_mat_sweep,
                                      ui_mat_aurora, ui_mat_shimmer, ui_mat_lift,
                                      ui_mat_gradient };
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t96-polish-off.png", dir);
    app.Capture(path);
    {
        const obs::Palette& pal = obs::ActivePalette();
        const float strengths[10] = { pal.mat.sheen, pal.mat.edge, pal.mat.bloom,
                                      pal.mat.chroma, pal.mat.frost, pal.mat.sweep,
                                      pal.mat.aurora, pal.mat.shimmer, pal.mat.lift,
                                      pal.mat.gradient };
        float sum = 0.0f;
        for (int i = 0; i < 10; ++i) sum += fabsf(strengths[i]);
        printf("    polish off: every material strength sums to %.4f\n", sum);
        Check(sum < 0.0001f, "polish off flattens the whole material layer");
        // The clock is not polish: freezing movement and removing shine are separate
        // questions, and a user who turns one off must not lose the other.
        Check(pal.mat.animate == ui_anim && pal.mat.speed == ui_anim_speed,
              "polish off leaves the animation settings alone");
        float kept = 0.0f;
        const float now[10] = { ui_mat_sheen, ui_mat_edge, ui_mat_bloom, ui_mat_chroma,
                                ui_mat_frost, ui_mat_sweep, ui_mat_aurora, ui_mat_shimmer,
                                ui_mat_lift, ui_mat_gradient };
        for (int i = 0; i < 10; ++i) kept += fabsf(now[i] - dials_before[i]);
        printf("    polish off: the dials moved by %.4f\n", kept);
        Check(kept < 0.0001f, "polish off does not consume the values it hides");

        ui_polish = true;
        for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
        snprintf(path, sizeof(path), "%s/t97-polish-on.png", dir);
        app.Capture(path);
        const obs::Palette& back = obs::ActivePalette();
        const float restored[10] = { back.mat.sheen, back.mat.edge, back.mat.bloom,
                                     back.mat.chroma, back.mat.frost, back.mat.sweep,
                                     back.mat.aurora, back.mat.shimmer, back.mat.lift,
                                     back.mat.gradient };
        float diff = 0.0f;
        for (int i = 0; i < 10; ++i) diff += fabsf(restored[i] - dials_before[i]);
        printf("    polish back on: %.4f away from what was dialled in\n", diff);
        Check(diff < 0.0001f, "polish on restores exactly what was tuned");
    }
    menuui::SetPage(1);
    menuui::SetClosedGroups("");

    // ---- collapsing is a layout claim, so it gets a picture ------------------
    // A closed group must submit NOTHING: not an empty card, not a clipped one. The
    // only way to see that from a shot is to close the top of a long page and check
    // that what was underneath moved up to fill it, with no gap and no orphan rule.
    ui_rainbow = 0;
    ui_mat_chroma = 0.40f; ui_mat_sweep = 0.55f; ui_mat_aurora = 0.45f;
    menuui::SetPage(6);                     // the groups being closed are on this page
    menuui::SetClosedGroups("theme/base theme;theme/accent;theme/glass");
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t92-collapsed.png", dir);
    app.Capture(path);
    menuui::SetClosedGroups("");            // the registry goes back to all-open
    menuui::SetPage(1);
    for (int f = 0; f < 6; ++f) app.Frame([] { menuui::Render(g_snap); });

    // A narrow rail and a 1-column grid at 1.5x is the combination most likely to
    // overflow, and the combination a user on a small screen actually picks.
    ui_rail_mode = 1; ui_columns = 1; ui_scale = 1.5f;
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t93-icons-1col-150.png", dir);
    app.Capture(path);
    {
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        printf("    icons + 1 column at 150%%: visible %.0f,%.0f - %.0f,%.0f (window %.0fx%.0f)\n",
               b[0], b[1], b[2], b[3], ws.x, ws.y);
    }
    // The third rail mode is the one that changes the window's structure rather than
    // its look: with no sidebar child at all, the nav rows and the page are submitted
    // to the window itself, which is precisely where a PushClipRect or a child-only
    // assumption would put content outside its rect. So it gets its own picture and
    // its own bleed check, not a variation on the shot above.
    ui_rail_mode = 2;
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t94-rail-hidden.png", dir);
    app.Capture(path);
    {
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        const bool inside = b[0] >= wp.x - 2.0f && b[1] >= wp.y - 2.0f &&
                            b[2] <= wp.x + ws.x + 2.0f && b[3] <= wp.y + ws.y + 2.0f;
        printf("    rail hidden: visible %.0f,%.0f - %.0f,%.0f (window %.0fx%.0f)%s\n",
               b[0], b[1], b[2], b[3], ws.x, ws.y, inside ? "" : "  <-- OFF-WINDOW");
        Check(inside, "the full-width page stays inside the window");
    }
    ui_rail_mode = 0; ui_columns = 2; ui_scale = 1.0f;
    menuui::SyncTheme();                    // and the fonts come back down with it
    for (int f = 0; f < 6; ++f) app.Frame([] { menuui::Render(g_snap); });
    ui_theme = 1;

    // ---- the aimbot key as a latch: the armed state has to be READABLE ---------
    // A toggle you cannot see is a footgun, so the page says "armed" in the header
    // badge and the rail row lights with it. Both come from one snapshot field, which
    // is also the point of the shot: the menu cannot ask the feature anything, so if
    // the wiring is missing the picture is the only place it shows up. The two groups
    // above the filters are closed so the latch card is in frame as well.
    menuui::SetPage(0);
    // `filters` stays OPEN so the frame carries the dead check too: the three filters
    // are one decision (who may be locked at all), and a shot of a page's bottom half
    // is what proves a new card laid out next to its siblings instead of over them.
    menuui::SetClosedGroups("aimbot/targeting;aimbot/feel");
    aimbot_enabled = true;
    aimbot_hold_mode = false;             // the key latches
    aimbot_keybind = 'R';                 // and a latch needs a key to press
    dead_check = true;
    g_snap.aimbot_latched = true;
    ui_win_h = 720.0f;                    // tall enough that the bind group is not cut
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t98-aimbot-armed.png", dir);
    app.Capture(path);
    printf("    aimbot latch: armed state on the page and the rail dot\n");
    menuui::SetClosedGroups("");
    g_snap.aimbot_latched = false;
    aimbot_enabled = false;
    aimbot_hold_mode = true;
    aimbot_keybind = 0;
    dead_check = false;
    ui_win_h = 620.0f;
    for (int f = 0; f < 6; ++f) app.Frame([] { menuui::Render(g_snap); });

    // ---- the players list group, after the blacklist changed scope ------------
    // The card used to be labelled "aimbot + esp", which was two features in one
    // label and a promise the list no longer makes by default. It gained a switch for
    // that promise, so the group gets a frame: a row of two buttons, a colour card and
    // a toggle has to survive the grid, and the scroll card below it is the other half
    // of the same label.
    menuui::SetPage(4);
    menuui::SetClosedGroups("players/status;players/actions");
    AddPlayerTag(player_blacklist, player_blacklist_n, "pxr_zed");
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t99-player-lists.png", dir);
    app.Capture(path);
    {
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        const bool inside = b[0] >= wp.x - 2.0f && b[1] >= wp.y - 2.0f &&
                            b[2] <= wp.x + ws.x + 2.0f && b[3] <= wp.y + ws.y + 2.0f;
        printf("    player lists: visible %.0f,%.0f - %.0f,%.0f (window %.0fx%.0f)%s\n",
               b[0], b[1], b[2], b[3], ws.x, ws.y, inside ? "" : "  <-- OFF-WINDOW");
        Check(inside, "the lists group stays inside the window");
    }
    player_blacklist_n = 0;
    menuui::SetClosedGroups("");

    // ---- the camera group, after FOV and freecam moved off movement ------------
    // Both are filed on visuals now because each answers "what does this look like"
    // rather than "how do I get there". The group is the last thing on the page, below
    // what a 620px window can show, so the groups above it are closed to bring it into
    // frame - and both switches are on, because a slider a card reveals is the layout
    // that actually gets used and the one most likely to overflow a cell.
    menuui::SetPage(1);
    menuui::SetClosedGroups("visuals/boxes;visuals/strokes;visuals/tracers;visuals/labels;"
                            "visuals/shared with aimbot;visuals/extras;visuals/chams");
    fov_changer_enabled = true;
    freecam_enabled     = true;
    // Taller than the usual frame on purpose: seven collapsed rules still stack up to
    // 300px of chrome, and a shot that clips the group it exists to show is worse than
    // no shot. It is also the check that the last group on the page is reachable.
    ui_win_h = 820.0f;
    for (int f = 0; f < 12; ++f) app.Frame([] { menuui::Render(g_snap); });
    snprintf(path, sizeof(path), "%s/t100-visuals-camera.png", dir);
    app.Capture(path);
    {
        float b[4];
        app.VisibleBounds(b);
        ImVec2 wp, ws;
        menuui::LastRect(&wp, &ws);
        const bool inside = b[0] >= wp.x - 2.0f && b[1] >= wp.y - 2.0f &&
                            b[2] <= wp.x + ws.x + 2.0f && b[3] <= wp.y + ws.y + 2.0f;
        printf("    camera group: visible %.0f,%.0f - %.0f,%.0f (window %.0fx%.0f)%s\n",
               b[0], b[1], b[2], b[3], ws.x, ws.y, inside ? "" : "  <-- OFF-WINDOW");
        Check(inside, "the camera group stays inside the window");
    }
    fov_changer_enabled = false;
    freecam_enabled     = false;
    ui_win_h = 620.0f;
    menuui::SetClosedGroups("");

    // ---- structural checks --------------------------------------------------
    menuui::SetPage(0);
    app.Frame([] { menuui::Render(g_snap); });
    Check(menuui::PageCount() == 9, "nine pages");
    Check(std::strcmp(menuui::PageName(4), "players") == 0, "players is page 5");
    Check(std::strcmp(menuui::PageName(7), "config") == 0, "page names stable");

    // ---- filing: which tab a setting lives on is a contract, not a vibe -------
    // Group ids register themselves as a page draws, and every SetClosedGroups() above
    // clears the registry, so the check has to walk the pages itself - otherwise the
    // negatives all pass by never having been asked. The walk is what makes "the old tab
    // does not own this any more" mean something, and that is the half a screenshot of
    // the new page cannot show: the new page looks right either way.
    menuui::SetClosedGroups("");
    for (int pg = 0; pg < menuui::PageCount(); ++pg) {
        menuui::SetPage(pg);
        app.Frame([] { menuui::Render(g_snap); });
    }
    menuui::SetPage(0);
    app.Frame([] { menuui::Render(g_snap); });
    Check(menuui::HasGroup("visuals/camera"),   "the camera is filed under visuals");
    Check(!menuui::HasGroup("movement/view"),   "movement does not own a camera group");
    Check(menuui::HasGroup("movement/teleport"), "click teleport has its own group");
    Check(menuui::HasGroup("keybinds/visuals"), "binds are grouped by the tab that owns them");
    Check(!menuui::HasGroup("keybinds/view"),   "and not by a category nothing else uses");
    Check(!menuui::HasGroup("visuals/checks"),  "the borrowed-filter group is named plainly");
    Check(!menuui::HasGroup("aimbot/enabled"),  "one enable switch per page");

    // ---- the server-list copy: bounds, and the fields behind the array ----------
    // `Snapshot::servers` is a fixed row array with `server_count`, `server_current`
    // and `servers_msg` sitting immediately behind it in the struct. A producer that
    // writes one row past the end therefore rewrites the numbers the next frame loops
    // over - and menu_ui's row loop runs to `server_count`, so an overwritten count is
    // an out-of-bounds walk. This is not hypothetical: the index used to be the count.
    {
        menuui::Snapshot t;
        features::servers::View v{};
        v.count = features::servers::kMaxRows;              // a full page (== 24)
        for (int i = 0; i < v.count; ++i) {
            // The index rides in the tail, because that is the half ShortGuid keeps
            // along with the head: a row that landed at the wrong index shows up as the
            // wrong tail, which a fixture whose head and tail are all zeros cannot tell.
            snprintf(v.rows[i].id, sizeof(v.rows[i].id),
                     "aaaaaaaa-bbbb-cccc-dddd-%012x", (unsigned)i);
            v.rows[i].playing     = i;
            v.rows[i].max_players = 40;
            snprintf(v.msg, sizeof(v.msg), "%s",
                     "24 servers from games.roblox.com, this one marked");
        }
        v.current = 3; v.busy = false; v.can_rejoin = true; v.age_sec = 4;
        snprintf(t.servers_msg, sizeof(t.servers_msg), "%s", "POISON");
        t.server_count = -12345; t.server_current = -12345;
        t.servers_can_rejoin = false; t.server_age_sec = -7;

        menuui::FillServers(t, v);
        Check(t.server_count == 24,               "a full page fills 24 rows and nothing else");
        Check(t.server_current == 3,              "which row is ours survives the copy");
        Check(t.server_age_sec == 4,              "the age is copied");
        Check(t.servers_can_rejoin == true,       "and so is rejoin's gate");
        Check(strcmp(t.servers_msg, v.msg) == 0,  "the message lands in servers_msg");
        Check(strcmp(t.servers[0].id, "aaaaaa..0000") == 0, "row 0 is written at index 0");
        Check(strcmp(t.servers[23].id, "aaaaaa..0017") == 0, "the last row lands in the last slot");
        Check(strcmp(t.servers[11].detail, "11/40") == 0, "the counts are formatted per row");

        // A producer that lies about size or index must not be able to escape the array.
        features::servers::View w{};
        w.count = 9999; w.current = 9998; w.age_sec = 1;
        snprintf(w.msg, sizeof(w.msg), "%s", "absurd");
        menuui::Snapshot u;
        menuui::FillServers(u, w);
        Check(u.server_count == menuui::Snapshot::kMaxServers, "an absurd count is clamped");
        Check(u.server_current == -1, "a current outside the list says unknown, not somebody else's row");

        // The skip: the copy is guarded so a 24-row list is not reformatted 60-144
        // times a second on the render thread. An unchanged list must leave the rows
        // alone; a changed message must not.
        snprintf(t.servers[7].id, sizeof(t.servers[7].id), "%s", "SCRIB");
        menuui::FillServers(t, v);
        Check(strcmp(t.servers[7].id, "SCRIB") == 0, "an unchanged list is not reformatted");
        Check(t.server_age_sec == 4, "the age still updates when the rows do not");
        snprintf(v.msg, sizeof(v.msg), "%s", "second message");
        menuui::FillServers(t, v);
        Check(strcmp(t.servers[7].id, "SCRIB") != 0, "a new message re-copies the rows");
    }

    printf("%s\n", g_fail ? "FAILURES" : "all ok");
    return g_fail;
}
