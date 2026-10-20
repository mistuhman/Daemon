#pragma once
// -----------------------------------------------------------------------------
//  boot/bootstrap_core.h — the bootstrapper's brain (UI-free, fully testable)
// -----------------------------------------------------------------------------
//  dx9ware shape (operator directive 2026-10-19): the app starts as a
//  bootstrapper that finds Sober, reports readiness, and only then offers
//  Load — which launches the overlay locked to Sober. This header is the
//  manager core behind that one screen: process discovery, readiness,
//  overlay supervision. It deliberately knows nothing about GTK, so the
//  same core drives the GTK window, the --console mode, and the fake-GTK
//  smoke test — and so future roles (update fetch from the mirror, doctor,
//  profiles, log viewer; docs/BOOTSTRAPPER.md) grow here while the UI layer
//  stays thin.
// -----------------------------------------------------------------------------
#include <cstdint>
#include <cstddef>

namespace bootcore {

struct Status {
    bool sober = false;          // Sober/Roblox process found
    uint32_t sober_pid = 0;
    bool libroblox = false;      // libroblox.so visible in its maps
    bool maps_denied = false;    // /proc/<pid>/maps open failed (EACCES/ENOENT) —
                                 // typically yama ptrace_scope blocking PTRACE_MODE_READ,
                                 // NOT a flatpak-ns issue
    int yama = -1;               // /proc/sys/kernel/yama/ptrace_scope (-1 unknown)
    bool display = false;        // DISPLAY/WAYLAND_DISPLAY set (overlay needs it)
    bool overlay_file = false;   // the overlay binary sits beside us
    bool overlay_running = false;
    bool ready = false;          // Load may be clicked
    char line[160] = "";         // headline for the window
    char detail[256] = "";       // what is missing / what was found
};

// One readiness scan. Cheap (/proc, throttled internals); call from a timer.
Status Poll();

// Start the overlay beside this executable: <exe-dir>/external --locked.
// err (optional) receives a human reason on failure.
bool StartOverlay(char* err, std::size_t errlen);

// Politely stop the overlay (SIGTERM — its detach path restores client
// writes). True when a signal was delivered.
bool StopOverlay();

// Directory of this executable, with trailing slash ("./" fallback).
const char* ExeDir();

} // namespace bootcore
