#include <Windows.h>
#include <cstdio>
#include "memory.h"
#include "process.h"
#include "game.h"
#include "offsets.h"
#include "offsets_live.h"
#include "globals.h"
#include "cache.h"
#include "overlay.h"
#include "media_tracker.h"

#include "features/aimbot/aimbot.h"
#include "features/esp/esp.h"
#include "features/flight/flight.h"
#include "features/noclip/noclip.h"
#include "features/walkspeed/walkspeed.h"
#include "features/click_teleport/click_teleport.h"
#include "features/infinite_jump/infinite_jump.h"
#include "features/fov_changer/fov_changer.h"
#include "features/inventory_checker/inventory_checker.h"
#include "features/skybox_changer/skybox_changer.h"
#include "features/hitbox_expander/hitbox_expander.h"
#include "features/servers/servers.h"
#include "features/players/players.h"
#include "features/freecam/freecam.h"
#include "features/waypoints/waypoints.h"
#include "features/movement/mover.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

void RenderMenu();
void TickKeybinds();

// The float windows (fps, media) are plain ImGui, outside the obsidian window, so
// their colours come from the theme through this pair -- see menu_ui.h.
#include "ui/menu_ui.h"

namespace overlay {
    void render_ui() {
        // mirrored here rather than at each toggle site, so the flag cannot drift
        // from the window's actual state (HOME, the tray icon and X all move it)
        g_menu_open.store(g_state.menu_open, std::memory_order_relaxed);
        // and the flag that actually decides whether keys belong to a text field
        // (see globals.h - the menu being open must NOT eat feature keys)
        g_menu_text_input.store(g_state.menu_open && ImGui::GetIO().WantTextInput,
                                std::memory_order_relaxed);
        TickKeybinds();

        if (esp_enabled) {
            features::RenderESP();
            if (skeleton_esp) features::RenderSkeletonESP();
            if (chinahat) features::RenderChinaHatESP();
            if (aimviewer) features::RenderAimViewer();
            if (chams_enabled) features::RenderChams();
            if (render_expanded_hitbox) features::RenderExpandedHitbox();
        }
        if (aimbot_enabled) features::RenderFOV();
        if (inventory_checker_enabled) features::RenderInventoryChecker();
        if (show_waypoints) features::RenderWaypoints();
        if (show_fps) {
            ImGui::SetNextWindowPos(ImVec2(fps_pos_x, fps_pos_y), ImGuiCond_FirstUseEver);
            // No SetNextWindowBgAlpha here: the plate's opacity is the theme's call
            // (see menuui::PushFloatWindowStyle), and an override on the next line is
            // how a themed counter ends up showing a different panel than the menu.
            menuui::PushFloatWindowStyle(0.52f);
            ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##fps-float", nullptr, fl)) {
                ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
                const ImVec2 p = ImGui::GetWindowPos();
                fps_pos_x = p.x;
                fps_pos_y = p.y;
            }
            ImGui::End();
            menuui::PopFloatWindowStyle();
        }

        // ── Floating media player ───────────────────────────────────────────────
        if (show_now_playing) {
            media::NowPlaying np = media::Get();
            if (np.playing && np.track[0]) {
                ImGui::SetNextWindowPos(ImVec2(media_pos_x, media_pos_y), ImGuiCond_FirstUseEver);
                menuui::PushFloatWindowStyle(0.62f);
                ImGuiWindowFlags mfl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav;
                if (ImGui::Begin("##media-float", nullptr, mfl)) {
                    // Source badge with theme color
                    ImVec4 accent(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
                    ImVec4 accent_dim(accent.x * 0.7f, accent.y * 0.7f, accent.z * 0.7f, 1.0f);

                    // "White text, always" was fine while white was the only accent in
                    // the app: a cyan or lime accent is bright enough that white label
                    // text on it vanishes. Rec709 luminance, same threshold the library
                    // uses for its own chips, so the badge and a pill agree.
                    const float lum = 0.2126f * accent.x + 0.7152f * accent.y + 0.0722f * accent.z;
                    const ImVec4 on_accent = lum > 0.62f ? ImVec4(0.05f, 0.06f, 0.08f, 1.0f)
                                                          : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                    ImGui::PushStyleColor(ImGuiCol_Button, accent_dim);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
                    ImGui::PushStyleColor(ImGuiCol_Text, on_accent);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
                    
                    char badge[64];
                    snprintf(badge, sizeof(badge), "%s", np.source);
                    ImGui::SmallButton(badge);
                    
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(4);
                    
                    ImGui::SameLine();
                    ImGui::TextUnformatted(np.track);
                    
                    if (np.artist[0]) {
                        ImGui::TextDisabled("%s", np.artist);
                    }

                    // Controls with better styling
                    ImGui::Spacing();
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
                    
                    // Seek backward
                    if (ImGui::Button("<-", ImVec2(28, 0))) media::SeekBackward();
                    ImGui::SameLine();
                    
                    // Previous track
                    if (ImGui::Button("|<", ImVec2(28, 0))) media::PrevTrack();
                    ImGui::SameLine();
                    
                    // Play/Pause (larger, accented)
                    ImGui::PushStyleColor(ImGuiCol_Button, accent_dim);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
                    if (ImGui::Button(">/||", ImVec2(36, 0))) media::PlayPause();
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                    
                    // Next track
                    if (ImGui::Button(">|", ImVec2(28, 0))) media::NextTrack();
                    ImGui::SameLine();
                    
                    // Seek forward
                    if (ImGui::Button("->", ImVec2(28, 0))) media::SeekForward();
                    
                    ImGui::PopStyleVar(2);
                    
                    // Volume controls on next row
                    ImGui::SameLine();
                    ImGui::Text("  ");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) media::VolumeDown();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+")) media::VolumeUp();

                    const ImVec2 p = ImGui::GetWindowPos();
                    media_pos_x = p.x;
                    media_pos_y = p.y;
                }
                ImGui::End();
                menuui::PopFloatWindowStyle();
            }
        }

        if (g_state.menu_open) {
            // the obsidian window owns its own position/size/collapse state
            RenderMenu();
        }
    }
}

static void FeatureLoop() {
    while (true) {
        if (g_base_address) {
            // FIRST, always: this samples the real clock, re-reads the local
            // character, decides whether this iteration is a physics step, and
            // hands back anything a dead character still had saved. Every
            // movement feature below reads its answers, so nothing may run
            // before it.
            features::mover::Tick();
            if (aimbot_enabled) features::RunAimbot();
            features::RunFlight();
            features::RunNoclip();
            features::RunWalkspeed();
            if (click_teleport_enabled) features::RunClickTeleport();
            features::RunInfiniteJump();
            if (fov_changer_enabled) features::RunFovChanger();
            if (skybox_changer_enabled) features::RunSkyboxChanger();
            features::RunSpectate();
            features::RunFreecam();
            features::TickTeleportHold();
            // Ungated for the same reason again: this is where a finished web
            // request gets reaped and where a place change is noticed (a hop that
            // worked looks exactly like that from here). While nothing is pending it
            // is two reads a second.
            features::servers::Tick();
            if (hitbox_expander_enabled) features::RunHitboxExpander();
        }
        platform::SleepMs(1);
    }
}


// returns false once the process we're attached to has exited
static bool RobloxStillAlive() {
    return platform::ProcessAlive(mem::roblox_h.load());
}

static void DetachRoblox() {
    clear_instance_caches();
    g_base_address = 0;
    mem::process_id.store(0);
    platform::CloseHandle(mem::roblox_h.exchange(nullptr));
    platform::SetGamePid(0);          // forget the window cache with the attach
    g_memory.Handle = nullptr;
}

// handles the initial attach AND re-attaching after roblox is closed/reopened,
// so the overlay can be launched before roblox and just keeps working
static void AttachLoop(void*) {
    bool warned_handle = false;

    while (true) {
        if (g_base_address && !RobloxStillAlive()) {
            LogLine("game closed - waiting for it to come back");
            DetachRoblox();
        }

        if (!g_base_address) {
            uint32_t pid = 0;
            uintptr_t base = 0;
            if (process::FindRoblox(pid, base)) {
                mem::process_id.store(pid);
                if (mem::grabroblox_h()) {
                    g_base_address = base;
                    warned_handle = false;
                    LogLine("attached - pid %u, base 0x%llx",
                            pid, (unsigned long long)base);

                    // Diagnostic: verify reads work by reading the PE signature
                    // at the base address.  A valid PE starts with 'MZ' (0x5A4D).
                    // (Goes through the same read path every feature uses.)
                    const uint16_t mz = read<uint16_t>(base);
                    if (mz == 0x5A4D)
                        LogLine("read test: OK (MZ signature found at base)");
                    else if (mz != 0)
                        LogLine("read test: WARNING - got 0x%04X instead of MZ (0x5A4D) at base",
                                (unsigned)mz);
                    else
                        LogLine("read test: FAILED - the read at base came back empty");
                } else {
                    if (!warned_handle) {
                        LogLine("found the game but couldn't open a handle - run as administrator");
                        warned_handle = true;
                    }
                    mem::process_id.store(0);
                }
            }
        }

        platform::SleepMs(1000);
    }
}

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nS) {
    (void)hI; (void)hP; (void)lpC; (void)nS;

    // Fetch offsets FIRST — RefreshOffsets() tries several sources with two
    // transports (WinHTTP, then System32\curl.exe), because offsets.imtheo.lol
    // is behind Cloudflare and drops WinHTTP's TLS hello (error 12175) even
    // with ntdll untouched — that error is server-side bot protection, not the
    // unhook. Keeping this before platform::Init() (the nt::Init unhook) still matters: WinHTTP works
    // normally here, and after the unhook all syscalls go indirect.
    LogLine("started");
    RefreshOffsets();

    // Now unhook ntdll + resolve SSNs.  After this point all process/memory
    // calls go through indirect syscalls, but WinHTTP is done so it's fine.
    if (!platform::Init()) {
        LogLine("FATAL: platform init failed - cannot proceed");
        return 1;
    }

    LogLine("waiting for the game...");

    cache::StartThread();
    platform::StartThread([](void*) { FeatureLoop(); }, nullptr);
    platform::StartThread(AttachLoop, nullptr);

    overlay::run();

    cache::StopThread();
    overlay::shutdown();
    return 0;
}