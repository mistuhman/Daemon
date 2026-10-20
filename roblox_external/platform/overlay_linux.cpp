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
#include <cmath>
#include <math.h>
#include <cstdio>
#include <cstdlib>

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
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <GL/gl.h>

static SDL_Window* s_window = nullptr;
static SDL_GLContext s_gl = nullptr;

static void SetWindowIcon(SDL_Window*) {
    // No custom icon — placeholder per user request.
    // Window manager will use .desktop Icon=application-x-executable (generic) so
    // XFCE/Ulauncher still group it via StartupWMClass without a bandaid icon file.
}

static bool TryCreateWindow() {
    // Headless env (ssh, no X/Wayland) — don't spam SDL error, just go headless
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        LogLine("overlay_linux: no DISPLAY/WAYLAND_DISPLAY — headless (ssh/headless)");
        return false;
    }
    // Tell SDL the WM_CLASS so .desktop StartupWMClass=roblox-external matches
    // (XFCE + Ulauncher use this to group window to launcher entry)
    SDL_SetHint(SDL_HINT_VIDEO_X11_WMCLASS, "roblox-external");
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
    SDL_GL_SetSwapInterval(1); // vsync

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
        // Main loop — owns the main thread as SDL requires
        bool windowed = true;
        while (!g_request_exit && windowed) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) g_request_exit = true;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F8) {
                    g_state.menu_open = !g_state.menu_open;
                }
            }
            // Also poll attach status so logs still show it even windowed
            // (FeatureLoop/AttachLoop are on other threads)
            // Render a cheap clearing frame — ImGui will replace this in full P3
            glClear(GL_COLOR_BUFFER_BIT);
            // TODO P3: ImGui::NewFrame + RenderMenu() + ImGui::Render + SDL_GL_SwapWindow
            // For 004 we keep the window minimal so it's recognized as app without ImGui dep
            SDL_GL_SwapWindow(s_window);
            platform::SleepMs(16);
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
    while (!g_request_exit) {
        platform::SleepMs(16);
    }
    LogLine("overlay_linux: shutdown");
}

void shutdown() {
    LogLine("overlay_linux: shutdown called");
}

} // namespace overlay
#else
#endif
