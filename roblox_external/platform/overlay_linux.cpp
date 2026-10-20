// ----------------------------------------------------------------------------
//  platform/overlay_linux.cpp — Linux overlay (Void + ImGui wiring P3)
// ----------------------------------------------------------------------------
//  P3 brings the menu to Linux: same obsidian menu as Windows, now through
//  SDL2 + OpenGL3 + ImGui. First iteration is an opaque resizable window
//  (1440x900) with the menu inside — not yet a transparent fullscreen overlay.
//  That is intentional: the risky half of P3 is "does the menu render and
//  respond", not "does the window cover the game". Transparency + click-
//  through (XFixes input shape) lands next, with no menu rework.
//
//  Reference: glass-obsidian/app/main_sdl2_gl3.cpp (core profile GL 3.0)
//  plus Windows's overlay.cpp -> main.cpp:overlay::render_ui (ESP + menu)
// ----------------------------------------------------------------------------
#include "platform/platform.h"
#include "globals.h"

#ifdef __linux__

#include "features/esp/esp.h"
#include "features/aimbot/aimbot.h"
#include "features/inventory_checker/inventory_checker.h"
#include "features/waypoints/waypoints.h"
#include "features/world/env.h"
#include "features/world/world_esp.h"
#include "features/fpscap/fps_cap.h"
#include "media_tracker.h"
#include "cache.h"

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
#include <GL/gl.h>
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

static void SetWindowIcon(SDL_Window*) {
}

static bool TryCreateWindow() {
    if (!platform::HasGraphicalDisplay()) {
        LogLine("overlay_linux: no DISPLAY/WAYLAND_DISPLAY — headless (ssh/headless)");
        return false;
    }
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
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* win = SDL_CreateWindow("Roblox External",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1440, 900, flags);
    if (!win) {
        LogLine("overlay_linux: SDL_CreateWindow failed: %s — headless", SDL_GetError());
        SDL_Quit();
        return false;
    }
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
    LogLine("overlay_linux: window created — Roblox External (1440x900) WM_CLASS=roblox-external — P3 menu wired");
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
        LogLine("overlay_linux: SDL2+GL ImGui shell active — menu opens with HOME/F8, click-through next");

        bool done = false;
        while (!g_request_exit && !done) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                ImGui_ImplSDL2_ProcessEvent(&ev);
                if (ev.type == SDL_QUIT) done = true;
                if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                    ev.window.windowID == SDL_GetWindowID(s_window))
                    done = true;
                if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.keysym.sym == SDLK_F8 || ev.key.keysym.sym == SDLK_HOME) {
                        g_state.menu_open = !g_state.menu_open;
                    }
                }
            }
            if (done || g_request_exit) break;

            if (menuui::SyncTheme()) {
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            g_menu_text_input.store(g_state.menu_open && ImGui::GetIO().WantTextInput, std::memory_order_relaxed);

            render_ui();

            ImGui::Render();

            int fb_w=0, fb_h=0;
            SDL_GL_GetDrawableSize(s_window, &fb_w, &fb_h);
            glViewport(0, 0, fb_w, fb_h);
            glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(s_window);
        }

        if (s_gl) SDL_GL_DeleteContext(s_gl);
        if (s_window) SDL_DestroyWindow(s_window);
        ShutdownImGui();
        SDL_Quit();
        LogLine("overlay_linux: window closed");
        return;
    }
    LogLine("overlay_linux: HAS_SDL_OVERLAY=1 but no display/context — falling to headless timer");
#endif
    LogLine("overlay_linux: headless run — no window (expected headless/ssh). ESP/aim run via logs; install .desktop for XFCE/Ulauncher: sh assets/install.sh");
    LogLine("overlay_linux: to enable windowed overlay: sudo xbps-install -y SDL2-devel mesa-devel pkg-config && rm -rf build && cmake -B build && cmake --build build");
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
