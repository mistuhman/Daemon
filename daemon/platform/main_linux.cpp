// ----------------------------------------------------------------------------
//  platform/main_linux.cpp — Daemon entry point (Linux only)
// ----------------------------------------------------------------------------
//  Daemon is a Linux-only external for Sober (Roblox on Linux).
//  No Windows code, no injection — pure external memory access via
//  process_vm_readv/writev and dynamic offset discovery.
// ----------------------------------------------------------------------------
#ifdef __linux__

#include "platform/platform.h"
#include "platform/sober_scanner.h"
#include "platform/port_watcher.h"
#include "offsets_dynamic.h"
#include "offsets.h"
#include "memory.h"
#include "game.h"
#include "cache.h"
#include "globals.h"
#include "version.h"
#include <cstring>

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
#include <fstream>
#include <string>
#include <atomic>

void RenderMenu(); // defined in menu.cpp on Windows; stub on Linux headless
void TickKeybinds();

namespace overlay { void run(); void shutdown(); void render_ui(); }

static std::atomic<bool> g_layout_ready{false};

static void FeatureLoop() {
    while (!g_request_exit) {
        if (g_base_address && g_layout_ready.load()) {
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
        // Idle backoff: while detached every Run* above is skipped by the
        // g_base_address gate, so a 1 ms tick only wakes the CPU ~1000x/s for
        // nothing (the desktop lag reported on Void before Sober attaches).
        // Attach is driven by AttachLoop at 2 Hz, so a 10 Hz tick here changes
        // no attach latency. The ATTACHED cadence is deliberately untouched:
        // the mover's ~600 writes/s depends on the 1 ms tick.
        platform::SleepMs(g_base_address && g_layout_ready.load() ? 1 : 100);
    }
}

static bool RobloxStillAlive() {
    return platform::ProcessAlive(mem::roblox_h.load());
}

static void DetachRoblox() {
    g_layout_ready.store(false);
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
                    // Only the STATIC stage can be complete this early (tree
                    // walk). layout_ready additionally needs the runtime stage
                    // (camera + motion), polled below — no write fires until
                    // every field it touches was discovered on THIS process.
                    if (offsets_dynamic::Ensure()) {
                        LogLine("static layout ok: %s", offsets_dynamic::diag().c_str());
                        LogLine("watcher: %s", watcher.DebugLine().c_str());
                    } else {
                        LogLine("memory attached; features disabled — %s", offsets_dynamic::diag().c_str());
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

        // Advance discovery while attached: Ensure() retries the static scan
        // until it lands and then runs the runtime stage (camera/motion need a
        // spawned character). FeatureLoop is admitted only when BOTH stages
        // succeeded — zero means "undiscovered" and never reaches a write.
        if (g_base_address && !g_layout_ready.load()) {
            if (offsets_dynamic::Ensure() && offsets_dynamic::RuntimeReady()) {
                g_layout_ready.store(true);
                LogLine("layout ready (static + runtime): %s", offsets_dynamic::diag().c_str());
            }
        }

        // Also watch for game server port change (teleport) when already attached
        if (g_base_address) {
            uint32_t oldp = 0, newp = 0;
            if (watcher.GamePortChanged(&oldp, &newp)) {
                LogLine("portwatch: game port %u -> %u (teleport/server hop) — invalidating world", oldp, newp);
                g_layout_ready.store(false);
                offsets_dynamic::Invalidate();
                // InvalidateWorld is in cache, but we can just log; cache thread will resweep.
            }
        }

        platform::SleepMs(500); // 2 Hz attach poll — heavier than Windows 1s due to plug-and-play want
    }
}

// TickKeybinds/RenderMenu are provided by menu_linux.cpp (P3 wiring).
// The former headless stubs here would duplicate those symbols at link time,
// so they are intentionally omitted — menu_linux.cpp is the single definition.
// (If you remove menu_linux.cpp from CORE_SOURCES, re-add a guarded stub here.)

static void OnSigInt(int) { g_request_exit = true; }

int main(int argc, char** argv) {
    // --locked: the bootstrapper (boot/main_boot.cpp) launched us. From here
    // the overlay lives ONLY over Sober: hidden without its window, geometry
    // following it — never a free-floating desktop overlay.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--locked") == 0) g_locked_to_sober = true;
    }
    fprintf(stdout, DAEMON_DISPLAY_NAME " linux — plug-and-play + dynamic offset fetcher%s\n",
            g_locked_to_sober ? " (--locked)" : "");
    fflush(stdout);
    platform::Init();
    std::signal(SIGINT, OnSigInt);
    std::signal(SIGTERM, OnSigInt);

    LogLine(DAEMON_DISPLAY_NAME " linux — plug-and-play + dynamic offset fetcher");
    if (g_locked_to_sober) {
        LogLine("launched --locked by the bootstrapper — overlay shows only over Sober, streamproofing active");
    } else {
        LogLine("waiting for Sober... (launch via flatpak run org.vinegarhq.Sober or /opt/sober/sober)");
    }

    // Linux never falls back to Windows profiles or guessed static anchors.
    // First attach will trigger offsets_dynamic::Ensure().

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
