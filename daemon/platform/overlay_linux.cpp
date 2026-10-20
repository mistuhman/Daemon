// ----------------------------------------------------------------------------
//  platform/overlay_linux.cpp — Linux overlay (Void + ImGui wiring P3)
// ----------------------------------------------------------------------------
//  P3 first iteration was an opaque resizable window (1440x900) with the menu
//  inside — the risky half being "does the menu render and respond". The
//  operator receipt said it does. This is the second iteration, the one the
//  receipt line "click-through next" asked for: the window becomes a real
//  overlay when the display allows it —
//
//    * transparent: a 32-bit ARGB visual (SDL_GL_ALPHA_SIZE 8 + compositor)
//      with a zero-alpha clear, so only ESP/menu pixels exist;
//    * click-through: XFixes empty input shape while the menu is closed —
//      clicks land in the game below. HOME/F8 are root-grabbed (x11overlay)
//      so the menu can be reopened without focus;
//    * game-rect: borderless always-on-top sized to the game window
//      (platform::GameClientRect, EWMH) or the full display before that.
//
//  When any of those preconditions fail (no X11, no ARGB visual, Wayland-only)
//  the windowed menu from iteration one comes back unchanged — it is the
//  fallback, not a debug mode.
//
//  Reference: glass-obsidian/app/main_sdl2_gl3.cpp (core profile GL 3.0)
//  plus Windows's overlay.cpp -> main.cpp:overlay::render_ui (ESP + menu)
// ----------------------------------------------------------------------------
#include "platform/platform.h"
#include "globals.h"
#include "version.h"

#ifdef __linux__

#include "features/esp/esp.h"
#include "features/aimbot/aimbot.h"
#include "features/inventory_checker/inventory_checker.h"
#include "features/waypoints/waypoints.h"
#include "features/world/env.h"
#include "features/world/world_esp.h"
#include "features/fpscap/fps_cap.h"
#include "cache.h"
#include "platform/streamproof_watcher.h"
#include "media_tracker.h"

#include <atomic>

#include "platform/overlay_x11.h"

// Global menu hooks (defined in menu_linux.cpp) — declare at global scope so
// a declaration inside namespace overlay doesn't create overlay::TickKeybinds.
void TickKeybinds();
void RenderMenu();

// System/third-party headers MUST stay at global scope — see the note that
// was here before (SDL inside namespace => overlay::std on GCC14).
#ifdef HAS_SDL_OVERLAY
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#if defined(__has_include) && __has_include(<GL/gl.h>)
#include <GL/gl.h>
#elif defined(__has_include) && __has_include(<GL/glcorearb.h>)
// Khronos core registry: the same entry points (glClear/glViewport/...) as
// Mesa's GL/gl.h, for toolchains without mesa-devel installed. Its prototypes
// live behind GL_GLEXT_PROTOTYPES by design.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/glcorearb.h>
#else
#include <GL/gl.h>
#endif
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "obsidian/obsidian.h"
#include "ui/menu_ui.h"
#endif

namespace overlay {

struct State { bool menu_open = true; };
State g_state;
void* g_device = nullptr;
void* g_context = nullptr;
void* g_render_target = nullptr;

// ---- overlay-mode state (P3 second iteration) -------------------------------
#ifdef HAS_SDL_OVERLAY
static bool         s_overlay_mode = false;   // borderless transparent overlay
static bool         s_per_pixel = false;      // ARGB visual — zero-alpha clear works
static bool         s_click_through = false;  // XFixes input shape is empty
static std::atomic<bool> s_toggle_requested{false};
static int          s_geom_x = 0, s_geom_y = 0, s_geom_w = 0, s_geom_h = 0;
static bool         s_geom_valid = false;

// Hotkey thread target — must stay trivial (no locks, no Xlib).
static void RequestMenuToggle() { s_toggle_requested.store(true, std::memory_order_relaxed); }
#endif

const char* OverlayStatus()
{
#ifdef HAS_SDL_OVERLAY
    static char buf[256];
    char extra[80];
    snprintf(extra, sizeof(extra), "%s%s",
             g_locked_to_sober ? "locked " : "",
             streamproof_enabled
                 ? (streamproof::CapturerName()[0] ? "streamproof(hidden)" : "streamproof")
                 : "");
    if (!s_overlay_mode) {
        snprintf(buf, sizeof(buf), "windowed menu%s%s — %s",
                 extra[0] ? " — " : "", extra, x11overlay::StatusLine());
        return buf;
    }
    snprintf(buf, sizeof(buf), "%s%s overlay %dx%d+%d+%d — click-through %s — %s",
             extra[0] ? extra : "free ",
             s_per_pixel ? "transparent" : "opaque",
             s_geom_w, s_geom_h, s_geom_x, s_geom_y,
             s_click_through ? "on (menu closed)" : "off (menu open)",
             x11overlay::StatusLine());
    return buf;
#else
    return "headless (no window)";
#endif
}

void render_ui() {
#ifdef HAS_SDL_OVERLAY
    g_menu_open.store(g_state.menu_open, std::memory_order_relaxed);
    ::TickKeybinds();

    features::worldenv::RenderLightingGrade();

    if (esp_enabled) {
        features::RenderESP();
        if (skeleton_esp) features::RenderSkeletonESP();
        if (chinahat) features::RenderChinaHatESP();
        if (aimviewer) features::RenderAimViewer();
        if (render_expanded_hitbox) features::RenderExpandedHitbox();
        if (headdot_enabled) features::RenderHeadDot();
        if (oof_enabled) features::RenderOof();
    }
    if (chams_enabled) features::RenderChams();
    if (mesh_chams) features::RenderMeshChams();
    if (aimbot_enabled) features::RenderFOV();
    if (inventory_checker_enabled) features::RenderInventoryChecker();
    if (show_waypoints) features::RenderWaypoints();
    features::worldesp::RenderWorldEsp();

    g_overlay_busy.store(
        g_state.menu_open || esp_enabled || aimbot_enabled || show_waypoints ||
        skeleton_esp || chinahat || aimviewer || chams_enabled || mesh_chams ||
        render_expanded_hitbox || headdot_enabled || oof_enabled ||
        inventory_checker_enabled || show_now_playing ||
        night_enabled || fullbright_enabled || world_ambient_enabled,
        std::memory_order_relaxed);

    if (show_fps) {
        ImGui::SetNextWindowPos(ImVec2(fps_pos_x, fps_pos_y), ImGuiCond_FirstUseEver);
        menuui::PushFloatWindowStyle(0.52f);
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##fps-float", nullptr, fl)) {
            const float game = features::ClientFps();
            if (game > 0.0f) ImGui::Text("%.0f game fps", game);
            else ImGui::TextUnformatted("-- game fps");
            ImGui::TextDisabled("%.0f overlay", ImGui::GetIO().Framerate);
            const ImVec2 p = ImGui::GetWindowPos();
            fps_pos_x = p.x;
            fps_pos_y = p.y;
        }
        ImGui::End();
        menuui::PopFloatWindowStyle();
    }

    if (show_now_playing) {
        media::NowPlaying np = media::Get();
        if (np.playing && np.track[0]) {
            ImGui::SetNextWindowPos(ImVec2(media_pos_x, media_pos_y), ImGuiCond_FirstUseEver);
            menuui::PushFloatWindowStyle(0.62f);
            ImGuiWindowFlags mfl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##media-float", nullptr, mfl)) {
                ImGui::TextUnformatted(np.track);
                if (np.artist[0]) ImGui::TextDisabled("%s", np.artist);
                const ImVec2 p = ImGui::GetWindowPos();
                media_pos_x = p.x;
                media_pos_y = p.y;
            }
            ImGui::End();
            menuui::PopFloatWindowStyle();
        }
    }

    if (g_state.menu_open) {
        ::RenderMenu();
    }
#else
    g_overlay_busy.store(esp_enabled || aimbot_enabled || show_waypoints || skeleton_esp || chinahat || aimviewer || chams_enabled || mesh_chams || headdot_enabled || oof_enabled || inventory_checker_enabled || show_now_playing || night_enabled || fullbright_enabled || world_ambient_enabled, std::memory_order_relaxed);
#endif
}

#ifdef HAS_SDL_OVERLAY

static SDL_Window* s_window = nullptr;
static SDL_GLContext s_gl = nullptr;

// visibility contract (docs/BOOTSTRAPPER.md): see ApplyVisibility()
static bool s_have_game = false;
static bool s_hidden = false;

static void SetWindowIcon(SDL_Window*) {
}

// Follow the game window (EWMH) or cover the display before it exists.
// Throttled by the caller; logs only on actual change.
// --locked (bootstrapper contract): no game window => no overlay at all —
// this is what keeps us from ever being a free-floating desktop overlay.
static void FollowGameRect() {
    int x = 0, y = 0, w = 0, h = 0;
    if (platform::GameClientRect(x, y, w, h) && w >= 320 && h >= 240) {
        s_have_game = true;
        if (!s_geom_valid || s_geom_x != x || s_geom_y != y || s_geom_w != w || s_geom_h != h) {
            SDL_SetWindowPosition(s_window, x, y);
            SDL_SetWindowSize(s_window, w, h);
            s_geom_x = x; s_geom_y = y; s_geom_w = w; s_geom_h = h;
            s_geom_valid = true;
            LogLine("Daemon: following the game window — %dx%d+%d+%d", w, h, x, y);
        }
    } else {
        s_have_game = false;
        if (g_locked_to_sober) {
            s_geom_valid = false;   // geometry is stale the moment Sober moves
            return;
        }
        if (!s_geom_valid) {
            SDL_DisplayMode dm{};
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
                SDL_SetWindowPosition(s_window, 0, 0);
                SDL_SetWindowSize(s_window, dm.w, dm.h);
                s_geom_x = 0; s_geom_y = 0; s_geom_w = dm.w; s_geom_h = dm.h;
                s_geom_valid = true;
                LogLine("Daemon: no game window yet — covering the display %dx%d (game rect when Sober shows)", dm.w, dm.h);
            }
        }
    }
}

// One place decides whether the window exists on screen at all. Two reasons
// to hide, both logged: --locked with no game window, and streamproof while a
// capture app runs (X11 has no per-window exclude-from-capture — unmap is the
// honest mechanism, and window capture of Sober never sees a separate window).
static void ApplyVisibility(bool capturer, const char* capturer_name) {
    const bool want_hidden = (g_locked_to_sober && !s_have_game) ||
                             (streamproof_enabled && capturer);
    if (want_hidden == s_hidden) return;
    s_hidden = want_hidden;
    if (want_hidden) {
        SDL_HideWindow(s_window);
        if (streamproof_enabled && capturer)
            LogLine("Daemon: streamproof — %s is capturing, overlay hidden from the frame",
                    capturer_name && capturer_name[0] ? capturer_name : "a capture app");
        else
            LogLine("Daemon: locked to Sober — no game window yet, overlay hidden");
    } else {
        SDL_ShowWindow(s_window);
        LogLine("Daemon: visible again — %s",
                g_locked_to_sober ? "Sober's window is back under the overlay" : "capturer gone");
    }
}

static void ShutdownImGui();   // defined below

// Overlay-or-headless, operator verdict in docs/LINUX_ATTACH_DIAGNOSTIC.md:
// the decorated 1440x900 fallback window IS the rejected artifact. When the
// overlay contract fails, tear the window down and run without a GUI.
static void FailHeadless(const char* why) {
    LogLine("Daemon: %s — headless (no decorated-window fallback)", why);
    ShutdownImGui();
    if (s_gl) { SDL_GL_DeleteContext(s_gl); s_gl = nullptr; }
    if (s_window) { SDL_DestroyWindow(s_window); s_window = nullptr; }
    s_overlay_mode = false;
    s_per_pixel = false;
    SDL_Quit();
}

static bool TryCreateWindow() {
    if (!platform::HasGraphicalDisplay()) {
        LogLine("Daemon: no DISPLAY/WAYLAND_DISPLAY — headless (ssh/headless)");
        return false;
    }
    // The whole control path (visual, click-through, hotkeys, stacking) is X11.
    // XWayland counts — it sets DISPLAY. A native-Wayland session has neither
    // the visual API nor XFixes, so it gets headless instead of a broken window.
    if (!SDL_getenv("DISPLAY")) {
        LogLine("Daemon: wayland-only session (no DISPLAY) — headless");
        return false;
    }
    // Pin the x11 backend before SDL_Init: SDL_HINT_VIDEO_X11_WINDOW_VISUALID
    // and X11_CreateWindow's visual override only exist there.
    SDL_setenv("SDL_VIDEODRIVER", "x11", 0);
    SDL_setenv("SDL_VIDEO_X11_WMCLASS", "daemon", 1);
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        LogLine("Daemon: SDL_Init failed: %s — headless fallback", SDL_GetError());
        return false;
    }

    // Overlay mode needs an X display and a 32-bit ARGB visual decided BEFORE
    // the window exists. Anything missing -> headless (overlay-or-headless).
    const bool want_overlay = x11overlay::Available() && x11overlay::ArgbVisualAvailable();
    if (!want_overlay) {
        LogLine("Daemon: no 32-bit ARGB visual on this display — headless");
        SDL_Quit();
        return false;
    }

    // Context creation is where the baseline-009 overlay died: the forced ARGB
    // visual and GLX disagreed (SDL_GL_CreateContext: BadMatch -> headless, no
    // window over the game at all). The visual is now GLX-validated
    // (x11overlay::ArgbVisualId picks from the fbconfig list; 0 = nothing
    // bindable => do not force). If creation still fails we retry once with
    // SDL's own visual and relaxed attribs before going headless — a window
    // over the game is the product; the tier of transparency is not. Every
    // attempt logs what it asked for and what the driver said.
    struct Attempt { bool force_visual; int depth, stencil, alpha; const char* what; };
    static const Attempt kAttempts[] = {
        { true,  24, 8, 8, "GLX ARGB32 visual + core3.0 d24 s8 a8" },
        { false,  0, 0, 8, "SDL-chosen visual + core3.0 a8, depth/stencil relaxed" },
    };

    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    for (const Attempt& a : kAttempts) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, a.depth);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, a.stencil);
        // Alpha in the default framebuffer is what the compositor reads for
        // per-pixel transparency on an ARGB visual.
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, a.alpha);

        // Force the 32-bit ARGB visual BY ID. SDL2's X11 backend honours
        // SDL_VIDEO_X11_WINDOW_VISUALID (string name works in every SDL2) before it
        // asks GLX for a visual — upstream's supported override, and the direct
        // answer to the field report "window visual is not 32-bit ARGB": the alpha-8
        // GLX request alone still produced depth-24 windows. Cleared between
        // attempts — a leftover force would replay the failure it caused.
        SDL_ResetHint("SDL_VIDEO_X11_WINDOW_VISUALID");
        if (a.force_visual) {
            const uint64_t argb_vid = x11overlay::ArgbVisualId();
            if (argb_vid) {
                char vid_buf[24];
                snprintf(vid_buf, sizeof(vid_buf), "%llu", (unsigned long long)argb_vid);
                SDL_SetHint("SDL_VIDEO_X11_WINDOW_VISUALID", vid_buf);
            } else {
                LogLine("Daemon: no GLX-bindable 32-bit ARGB visual — not forcing one (see ArgbVisualId)");
            }
        }

        int gx = 0, gy = 0, gw = 0, gh = 0;
        if (!platform::GameClientRect(gx, gy, gw, gh) || gw < 320 || gh < 240) {
            SDL_DisplayMode dm{};
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { gx = 0; gy = 0; gw = dm.w; gh = dm.h; }
            else { gx = 0; gy = 0; gw = 1280; gh = 720; }
        }
        SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
                                                  SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR |
                                                  SDL_WINDOW_ALLOW_HIGHDPI);
        win = SDL_CreateWindow(DAEMON_DISPLAY_NAME, gx, gy, gw, gh, flags);
        if (!win) {
            LogLine("Daemon: SDL_CreateWindow failed (%s): %s", a.what, SDL_GetError());
            continue;
        }
        s_geom_x = gx; s_geom_y = gy; s_geom_w = gw; s_geom_h = gh;
        s_geom_valid = true;
        SDL_SetWindowTitle(win, DAEMON_DISPLAY_NAME);
        SetWindowIcon(win);

        ctx = SDL_GL_CreateContext(win);
        if (!ctx) {
            LogLine("Daemon: SDL_GL_CreateContext failed (%s): %s", a.what, SDL_GetError());
            SDL_DestroyWindow(win);
            win = nullptr;
            continue;
        }
        if (SDL_GL_MakeCurrent(win, ctx) != 0) {
            LogLine("Daemon: SDL_GL_MakeCurrent failed (%s): %s", a.what, SDL_GetError());
            SDL_GL_DeleteContext(ctx);
            SDL_DestroyWindow(win);
            win = nullptr;
            ctx = nullptr;
            continue;
        }
        LogLine("Daemon: overlay window up (%s)", a.what);
        break;
    }
    if (!win || !ctx) {
        LogLine("Daemon: no GL context could be created — headless (the two attempts above name the driver errors)");
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        return false;
    }
    s_overlay_mode = true;
    SDL_GL_SetSwapInterval(1);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    int win_w=0, win_h=0, fb_w=0, fb_h=0;
    SDL_GetWindowSize(win, &win_w, &win_h);
    SDL_GL_GetDrawableSize(win, &fb_w, &fb_h);
    const float dpi_scale = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0f;

    obsidian::ThemeConfig cfg;
    cfg.ui_scale = ui_scale * dpi_scale;
    cfg.base_font_size = ui_font_size;
    cfg.title_font_size = ui_title_size;
    cfg.mono_font_size = ui_font_size * 0.87f;
    cfg.move_from_title_bar_only = true;
    if (!obsidian::LoadFonts(cfg))
        LogLine("glass obsidian: no system font found, using fallback");
    obsidian::ApplyTheme(menuui::BuildPalette(), cfg);

    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 130");

    s_window = win;
    s_gl = ctx;

    // Wire the X11 overlay control: our window by WM_CLASS, dock hints,
    // hotkeys, and the initial input shape (menu starts open -> input on).
    if (!x11overlay::AttachSelf("daemon")) {
        FailHeadless("overlay window not found via WM_CLASS (no EWMH?)");
        return false;
    }
    x11overlay::SetDockHints();
    x11overlay::StartToggleHotkeys(&RequestMenuToggle);
    s_per_pixel = (x11overlay::WindowDepth() == 32);
    if (!s_per_pixel) {
        FailHeadless("window visual is not 32-bit ARGB after forcing the visual id");
        return false;
    }
    x11overlay::SetClickThrough(false);   // menu open: input on
    s_click_through = false;
    LogLine("Daemon: transparent overlay active — %dx%d+%d+%d, ARGB depth 32, XFixes click-through armed",
            s_geom_w, s_geom_h, s_geom_x, s_geom_y);
    return true;
}

static void ShutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

#endif // HAS_SDL_OVERLAY

void run() {
#ifdef HAS_SDL_OVERLAY
    if (TryCreateWindow()) {
        LogLine("Daemon: SDL2+GL ImGui shell active — HOME/F8 toggles the menu (and its click-through)");
        if (s_overlay_mode) LogLine("Daemon: %s", OverlayStatus());

        bool done = false;
        bool needs_repaint = true;
        uint64_t last_follow = 0;

        auto handle_event = [&](const SDL_Event& ev) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) done = true;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                ev.window.windowID == SDL_GetWindowID(s_window))
                done = true;
            if (ev.type == SDL_WINDOWEVENT &&
                (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
                 ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                 ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 ev.window.event == SDL_WINDOWEVENT_MOVED))
                needs_repaint = true;
            if (ev.type == SDL_KEYDOWN) {
                // Fallback toggle path: normally HOME/F8 arrive through the
                // root grab (x11overlay hotkey thread) even while the overlay
                // is click-through; this covers a failed grab with focus.
                if (ev.key.keysym.sym == SDLK_F8 || ev.key.keysym.sym == SDLK_HOME) {
                    s_toggle_requested.store(true, std::memory_order_relaxed);
                }
            }
        };

        while (!g_request_exit && !done) {
            SDL_Event ev;
            if (needs_repaint) {
                while (SDL_PollEvent(&ev)) handle_event(ev);
            } else {
                // Idle: nothing to draw (menu closed, no ESP/HUD). Block on
                // events instead of presenting empty frames at refresh rate —
                // the same fix as the static shell in payload 007.
                if (SDL_WaitEventTimeout(&ev, 200) == 1) handle_event(ev);
                while (SDL_PollEvent(&ev)) handle_event(ev);
            }
            if (done || g_request_exit) break;

            if (s_toggle_requested.exchange(false, std::memory_order_relaxed)) {
                g_state.menu_open = !g_state.menu_open;
            }
            // render_ui() publishes this, but the idle gate means no frame runs
            // while the menu is closed and nothing else draws — publish here so
            // the flag never lags the actual menu state.
            g_menu_open.store(g_state.menu_open, std::memory_order_relaxed);

            // Geometry + visibility, in BOTH window modes: --locked and
            // streamproof are contracts, not overlay-mode conveniences.
            {
                const uint64_t now = platform::NowMs();
                if (now - last_follow >= 250) {
                    last_follow = now;
                    if (s_overlay_mode) {
                        FollowGameRect();
                        // A focus-stealing override can drop us under the game
                        // between frames; every follow tick re-checks the EWMH
                        // stacking list and only raises when it says we lost.
                        x11overlay::EnsureAboveGame();
                    } else {
                        int gx = 0, gy = 0, gw = 0, gh = 0;
                        s_have_game = platform::GameClientRect(gx, gy, gw, gh) &&
                                      gw >= 320 && gh >= 240;
                    }
                }
                // Visibility (locked / streamproof) — cheap: the watcher
                // throttles its /proc scan internally to 2 s.
                ApplyVisibility(streamproof_enabled &&
                                    streamproof::CapturerRunning(streamproof_apps),
                                streamproof::CapturerName());
                if (s_hidden) {
                    // Unmapped window: nothing to draw. Keep pumping events so
                    // the unload hotkey and close path stay live.
                    SDL_PumpEvents();
                    SDL_Delay(50);
                    continue;
                }
            }

            if (s_overlay_mode) {
                // Input shape follows the menu: open = interactive window,
                // closed = empty XFixes input shape, clicks fall to the game.
                const bool want_ct = !g_state.menu_open;
                if (want_ct != s_click_through) {
                    if (x11overlay::SetClickThrough(want_ct)) {
                        s_click_through = want_ct;
                        LogLine("Daemon: click-through %s",
                                want_ct ? "on (menu closed) — clicks pass to the game"
                                        : "off (menu open) — the overlay takes input");
                    }
                }
            }

            const bool draw_anything =
                g_state.menu_open || esp_enabled || aimbot_enabled || show_waypoints ||
                skeleton_esp || chinahat || aimviewer || chams_enabled || mesh_chams ||
                render_expanded_hitbox || headdot_enabled || oof_enabled ||
                inventory_checker_enabled || show_now_playing || show_fps ||
                night_enabled || fullbright_enabled || world_ambient_enabled;
            if (!draw_anything && !needs_repaint) continue;
            needs_repaint = false;

            if (menuui::SyncTheme()) {
            }

            int fb_w=0, fb_h=0;
            SDL_GL_GetDrawableSize(s_window, &fb_w, &fb_h);
            glViewport(0, 0, fb_w, fb_h);
            if (s_per_pixel) glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            else glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            if (draw_anything) {
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();

                g_menu_text_input.store(g_state.menu_open && ImGui::GetIO().WantTextInput, std::memory_order_relaxed);

                render_ui();

                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
            SDL_GL_SwapWindow(s_window);
        }

        if (s_gl) SDL_GL_DeleteContext(s_gl);
        if (s_window) SDL_DestroyWindow(s_window);
        ShutdownImGui();
        SDL_Quit();
        LogLine("Daemon: window closed");
        return;
    }
    LogLine("Daemon: HAS_SDL_OVERLAY=1 but no display/context — falling to headless timer");
#endif
    LogLine("Daemon: headless run — no window (expected headless/ssh). ESP/aim run via logs; install .desktop for XFCE/Ulauncher: sh assets/install.sh");
    LogLine("Daemon: to enable windowed overlay: sudo xbps-install -y SDL2-devel mesa-devel pkg-config && rm -rf build && cmake -B build && cmake --build build");
    while (!g_request_exit) {
        platform::SleepMs(100);
    }
    LogLine("Daemon: shutdown");
}

void shutdown() {
    LogLine("Daemon: shutdown called");
}

} // namespace overlay
#else
#endif
