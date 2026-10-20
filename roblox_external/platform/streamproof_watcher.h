#pragma once
// -----------------------------------------------------------------------------
//  platform/streamproof_watcher.h — "is a capture app running?" (/proc scan)
// -----------------------------------------------------------------------------
//  Streamproofing on Linux, honestly: X11 has no per-window
//  exclude-from-capture flag (Windows has SetWindowDisplayAffinity), and a
//  screen capture reads every visible pixel. What IS true: we are a separate
//  window from the game, so OBS *game/window* capture of Sober never includes
//  us; and when a *screen* capture app is running, hiding the overlay (X11
//  unmap) removes us from the frame entirely. That is what this watcher feeds.
//
//  Detection is a /proc comm+cmdline scan against a default list of capture
//  apps plus the operator's own ';'-separated list (globals::streamproof_apps).
//  Scans are internally throttled to one pass per 2 s — the overlay asks every
//  frame and must not walk /proc at 60 Hz.
// -----------------------------------------------------------------------------

namespace streamproof {

// True when a capture/stream app is running. extra_apps is ';'-separated
// additional process names (may be null/empty).
bool CapturerRunning(const char* extra_apps);

// Name of the capturer found by the most recent CapturerRunning() call
// ("" when none). For log lines and the debug page.
const char* CapturerName();

// The built-in list, ';'-separated — shown by the bootstrapper/debug page so
// the operator knows what counts as a capturer.
const char* DefaultApps();

} // namespace streamproof
