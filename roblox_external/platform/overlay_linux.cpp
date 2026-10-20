// ----------------------------------------------------------------------------
//  platform/overlay_linux.cpp — Linux overlay (XFCE + Ulauncher recognizable)
// ----------------------------------------------------------------------------
//  Headless fallback keeps attach working; when SDL2+GL found (HAS_SDL_OVERLAY)
//  we create a real SDL window so XFCE/whisker + Ulauncher can index it as
//  an app (matching .desktop StartupWMClass=roblox-external). No bandaid:
//  window has proper title, WM_CLASS, icon, and .desktop entry.
//
//  Reference: glass-obsidian/app/main_sdl2_gl3.cpp (core profile GL 3.0)
//  ImGui wiring is P3 next — for 004 we prove a windowed shell so the app is
//  \"universally recognized\"; ESP/aim still run headless via FeatureLoop.
// ----------------------------------------------------------------------------
#include "platform/platform.h"
#include "globals.h"

#ifdef __linux__

// System/third-party headers MUST stay at global scope. These used to sit
// inside namespace overlay below; any libstdc++ header their include graph
// pulls that was not already guard-loaded then compiled inside the namespace
// and created overlay::std. On GCC 14's libstdc++ SDL's graph reaches the
// tr1/specfun chain and the C++ <math.h> wrapper that way, which fails with
// "'abs' is not a member of 'overlay::std'" and ambiguous std::sqrt. On GCC
// 12 the same headers were already loaded globally, so the guard made the
// nested inclusion a no-op and the bug stayed latent.
#ifdef HAS_SDL_OVERLAY
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <GL/gl.h>
#endif

namespace overlay {

struct State { bool menu_open = true; };
State g_state;
void* g_device = nullptr;
void* g_context = nullptr;
void* g_render_target = nullptr;

void render_ui() {
    extern void RenderMenu();
    g_overlay_busy.store(esp_enabled || aimbot_enabled || show_waypoints || skeleton_esp || chinahat || aimviewer || chams_enabled || mesh_chams || headdot_enabled || oof_enabled || inventory_checker_enabled || show_now_playing || night_enabled || fullbright_enabled || world_ambient_enabled, std::memory_order_relaxed);
}

#ifdef HAS_SDL_OVERLAY
static SDL_Window* s_window = nullptr;
static SDL_GLContext s_gl = nullptr;

static void SetWindowIcon(SDL_Window*) {
    // No custom icon — placeholder per user request.
    // Window manager will use .desktop Icon=application-x-executable (generic) so
    // XFCE/Ulauncher still group it via StartupWMClass without a bandaid icon file.
}

static bool TryCreateWindow() {
    // Headless env (ssh, no X/Wayland) — don't spam SDL error, just go headless
    if (!platform::HasGraphicalDisplay()) {
        LogLine("overlay_linux: no DISPLAY/WAYLAND_DISPLAY — headless (ssh/headless)");
        return false;
    }
    // Set WM_CLASS so .desktop StartupWMClass=roblox-external matches (XFCE +
    // Ulauncher group the window to the launcher entry by this). The
    // SDL_HINT_VIDEO_X11_WMCLASS hint was removed from SDL2 by 2.30: the X11 and
    // Wayland backends now read the SDL_VIDEO_X11_WMCLASS env var directly while
    // the video subsystem initialises, so set it with SDL_setenv BEFORE SDL_Init.
    SDL_setenv("SDL_VIDEO_X11_WMCLASS", "roblox-external", 1);
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        LogLine("overlay_linux: SDL_Init failed: %s — headless fallback", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* win = SDL_CreateWindow("Roblox External",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
    if (!win) {
        LogLine("overlay_linux: SDL_CreateWindow failed: %s — headless", SDL_GetError());
        SDL_Quit();
        return false;
    }
    // Set WM_CLASS explicitly for XFCE (some WMs need SDL_HINT set before create, which we did)
    SDL_SetWindowTitle(win, "Roblox External");
    SetWindowIcon(win);

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        LogLine("overlay_linux: SDL_GL_CreateContext failed: %s — headless", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(1); // vsync; frames are event-driven, so this only paces occasional redraws

    // Minimal GL clear so window is not black — proves it's alive to XFCE panel
    glClearColor(0.07f, 0.07f, 0.09f, 1.0f);

    s_window = win;
    s_gl = ctx;
    LogLine("overlay_linux: window created — Roblox External (1280x720) WM_CLASS=roblox-external — XFCE/Ulauncher will index via .desktop");
    return true;
}
#endif // HAS_SDL_OVERLAY

void run() {
#ifdef HAS_SDL_OVERLAY
    if (TryCreateWindow()) {
        LogLine("overlay_linux: SDL2 windowed — glass-obsidian shell active (P3 menu wiring next)");
        // Event-driven redraw. The shell's only content today is a static clear
        // color (the P3 ImGui frame does not exist yet), so repainting at
        // refresh rate burned a GPU clear+swap and compositor damage ~60x/s for
        // identical frames — the low-end choke in the 007 receipt. Paint once,
        // then block on the event queue and redraw only when the content could
        // actually change (expose, resize, the F8 toggle). P3 restores
        // continuous frames while the menu or ESP is active.
        bool dirty = true;
        SDL_Event ev;
        while (!g_request_exit) {
            if (dirty) {
                // TODO P3: ImGui::NewFrame + RenderMenu() + ImGui::Render + SDL_GL_SwapWindow
                glClear(GL_COLOR_BUFFER_BIT);
                SDL_GL_SwapWindow(s_window);
                dirty = false;
            }
            // 200 ms cap: thread/SIGINT-driven exit stays responsive, while an
            // idle window makes zero GL calls between real events.
            if (SDL_WaitEventTimeout(&ev, 200) == 0) {
                render_ui();
                continue;
            }
            do {
                switch (ev.type) {
                case SDL_QUIT:
                    g_request_exit = true;
                    break;
                case SDL_WINDOWEVENT:
                    // Expose (compositor lost our pixels) and size changes need
                    // a fresh frame; focus/move events do not.
                    if (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
                        ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                        dirty = true;
                    }
                    break;
                case SDL_KEYDOWN:
                    if (ev.key.keysym.sym == SDLK_F8) {
                        g_state.menu_open = !g_state.menu_open;
                        dirty = true; // no visible effect until P3; keep the redraw contract now
                    }
                    break;
                default:
                    break;
                }
            } while (SDL_PollEvent(&ev) && !g_request_exit);
            render_ui();
        }
        if (s_gl) SDL_GL_DeleteContext(s_gl);
        if (s_window) SDL_DestroyWindow(s_window);
        SDL_Quit();
        LogLine("overlay_linux: window closed");
        return;
    }
    LogLine("overlay_linux: HAS_SDL_OVERLAY=1 but no display/context — falling to headless timer");
#endif
    LogLine("overlay_linux: headless run — no window (expected headless/ssh). ESP/aim run via logs; install .desktop for XFCE/Ulauncher: sh assets/install.sh");
    LogLine("overlay_linux: to enable windowed overlay: sudo xbps-install -y SDL2-devel mesa-devel pkg-config && rm -rf build && cmake -B build && cmake --build build");
    // 10 Hz idle: no window to service and the attach/feature/cache threads own
    // all work; 16 ms here was 62 wakeups/s for nothing.
    while (!g_request_exit) {
        platform::SleepMs(100);
    }
    LogLine("overlay_linux: shutdown");
}

void shutdown() {
    LogLine("overlay_linux: shutdown called");
}

} // namespace overlay
#else
#endif
