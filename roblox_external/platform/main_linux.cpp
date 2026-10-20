// ----------------------------------------------------------------------------
//  platform/main_linux.cpp — Linux entry point (mirrors main.cpp WinMain)
// ----------------------------------------------------------------------------
//  Windows main.cpp is WinMain with DX11 overlay. Linux uses standard main()
//  with platform_linux + sober_scanner + port_watcher plug-and-play.
//  Feature code is shared; only attach/overlay differ.
// ----------------------------------------------------------------------------
#ifdef __linux__

#include "platform/platform.h"
#include "platform/sober_scanner.h"
#include "platform/port_watcher.h"
#include "offsets_dynamic.h"
#include "offsets.h"
#include "offsets_live.h"
#include "memory.h"
#include "process.h"
#include "game.h"
#include "cache.h"
#include "globals.h"

#include "features/aimbot/aimbot.h"
#include "features/triggerbot/triggerbot.h"
#include "features/antiafk/antiafk.h"
#include "features/gravity/gravity.h"
#include "features/hipheight/hipheight.h"
#include "features/nofall/nofall.h"
#include "features/fling/fling.h"
#include "features/fpscap/fps_cap.h"
#include "features/jumppower/jumppower.h"
#include "features/world/env.h"
#include "features/explorer/explorer.h"
#include "features/world/world_esp.h"
#include "features/hitsound/hitsound.h"
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

#include <csignal>
#include <cstdio>
#include <atomic>

void RenderMenu(); // defined in menu.cpp on Windows; stub on Linux headless
void TickKeybinds();

namespace overlay { void run(); void shutdown(); void render_ui(); }

static void FeatureLoop() {
    while (!g_request_exit) {
        if (g_base_address) {
            features::mover::Tick();
            if (aimbot_enabled) features::RunAimbot();
            features::RunTriggerbot();
            features::RunFlight();
            features::RunGravity();
            features::RunHipHeight();
            features::RunNoFall();
            features::RunFling();
            features::RunFpsCap();
            features::RunJumpPower();
            features::worldenv::RunWorldEnv();
            features::worldesp::RunWorldEsp();
            features::RunAntiafk();
            features::RunNoclip();
            features::RunWalkspeed();
            if (click_teleport_enabled) features::RunClickTeleport();
            features::RunInfiniteJump();
            features::RunFovChanger();
            if (skybox_changer_enabled) features::RunSkyboxChanger();
            features::RunSpectate();
            features::explorer::Run();
            features::RunFreecam();
            features::TickTeleportHold();
            features::servers::Tick();
            if (hitbox_expander_enabled) features::RunHitboxExpander();
        }
        platform::SleepMs(1);
    }
}

static bool RobloxStillAlive() {
    return platform::ProcessAlive(mem::roblox_h.load());
}

static void DetachRoblox() {
    features::ReleaseFpsCap();
    features::ReleaseNoFall();
    features::ReleaseFovChanger();
    features::ReleaseFling();
    features::ReleaseJumpPower();
    features::worldenv::ReleaseWorldEnv();
    features::worldesp::ReleaseWorldEsp();
    clear_instance_caches();
    g_base_address = 0;
    mem::process_id.store(0);
    platform::CloseHandle(mem::roblox_h.exchange(nullptr));
    platform::SetGamePid(0);
    g_memory.Handle = nullptr;
    offsets_dynamic::Invalidate();
}

static void AttachLoop(void*) {
    portwatch::ProcessWatcher watcher;
    bool warned_handle = false;

    while (!g_request_exit) {
        if (g_base_address && !RobloxStillAlive()) {
            LogLine("game closed - waiting for it to come back");
            DetachRoblox();
        }

        // Plug-and-play: watcher polls FindSoberPid every tick and reports pid/base change.
        // This enables "launch external before Sober, it auto-attaches" (Fleasion parity).
        uint32_t new_pid = 0;
        uintptr_t new_base = 0;
        if (watcher.Tick(&new_pid, &new_base)) {
            if (new_pid && new_base) {
                LogLine("plug-and-play: sober detected pid %u base 0x%llx — attaching", new_pid, (unsigned long long)new_base);
                if (g_base_address) DetachRoblox();
                mem::process_id.store(new_pid);
                if (mem::grabroblox_h()) {
                    g_base_address = new_base;
                    warned_handle = false;
                    LogLine("attached - pid %u, base 0x%llx (via port_watcher)", new_pid, (unsigned long long)new_base);
                    // Verify read by checking DataModel via scanner
                    if (offsets_dynamic::Ensure()) {
                        LogLine("plug-and-play: %s", offsets_dynamic::diag().c_str());
                        LogLine("watcher: %s", watcher.DebugLine().c_str());
                    } else {
                        LogLine("plug-and-play: scan pending — %s", offsets_dynamic::diag().c_str());
                    }
                } else {
                    if (!warned_handle) {
                        LogLine("found the game but couldn't open a handle - yama ptrace_scope?");
                        warned_handle = true;
                    }
                    mem::process_id.store(0);
                }
            } else if (!new_pid && g_base_address) {
                LogLine("sober vanished (pid 0) — detaching");
                DetachRoblox();
            }
        }

        // Also watch for game server port change (teleport) when already attached
        if (g_base_address) {
            uint32_t oldp = 0, newp = 0;
            if (watcher.GamePortChanged(&oldp, &newp)) {
                LogLine("portwatch: game port %u -> %u (teleport/server hop) — invalidating world", oldp, newp);
                offsets_dynamic::Invalidate();
                // InvalidateWorld is in cache, but we can just log; cache thread will resweep.
            }
        }

        // Legacy fallback (Wine PE MZ) — Sober uses ELF libroblox.so via port_watcher above.
        if (!g_base_address) {
            uint32_t pid = 0;
            uintptr_t base = 0;
            if (process::FindRoblox(pid, base)) {
                mem::process_id.store(pid);
                if (mem::grabroblox_h()) {
                    g_base_address = base;
                    warned_handle = false;
                    LogLine("attached - pid %u, base 0x%llx (via legacy FindRoblox/Wine)", pid, (unsigned long long)base);
                    const uint32_t hdr = read<uint32_t>(base);
                    if (hdr == 0x464C457FU) LogLine("read test: OK ELF at base (libroblox.so)");
                    else if ((hdr & 0xFFFFu) == 0x5A4Du) LogLine("read test: OK MZ at base (Wine PE)");
                    else if (hdr) LogLine("read test: WARNING 0x%08X at base (expected ELF 464C457F or MZ 5A4D) — base may be bwrap exe, not libroblox.so", (unsigned)hdr);
                    else LogLine("read test: FAILED read at base (ptrace_scope? run: echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope)");
                    // Help diagnose yama
                    {
                        FILE* yf = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
                        if (yf) {
                            char yb[16] = {};
                            fgets(yb, sizeof(yb), yf);
                            fclose(yf);
                            LogLine("yama ptrace_scope=%s (need 0 for cross-process readv when not parent)", yb);
                        }
                    }
                }
            }
        }

        platform::SleepMs(500); // 2 Hz attach poll — heavier than Windows 1s due to plug-and-play want
    }
}

void TickKeybinds() { /* Linux headless: no keybind capture yet (P3 X11) */ }
void RenderMenu() { /* headless stub */ }

static void OnSigInt(int) { g_request_exit = true; }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    fprintf(stdout, "roblox-external linux — plug-and-play + dynamic offset fetcher\n");
    fflush(stdout);
    platform::Init();
    std::signal(SIGINT, OnSigInt);
    std::signal(SIGTERM, OnSigInt);

    LogLine("roblox-external linux — plug-and-play + dynamic offset fetcher");
    LogLine("waiting for Sober... (launch via flatpak run org.vinegarhq.Sober or /opt/sober/sober)");

    // Offsets: on Linux we use dynamic fetcher; still try live fetch as fallback for Wine
    // but don't block on network — dynamic will win via libBase+scan.
    // RefreshOffsets() is Windows-only network path; skip it on Linux to avoid curl dep.
    // Instead, first attach will trigger offsets_dynamic::Ensure().

    cache::StartThread();
    platform::StartThread([](void*){ FeatureLoop(); }, nullptr);
    platform::StartThread(AttachLoop, nullptr);

    overlay::run(); // blocks until g_request_exit

    features::ReleaseFpsCap();
    features::ReleaseNoFall();
    features::ReleaseJumpPower();
    features::worldenv::ReleaseWorldEnv();
    features::worldesp::ReleaseWorldEsp();
    cache::StopThread();
    overlay::shutdown();
    LogLine("exit");
    return 0;
}

#endif // __linux__
