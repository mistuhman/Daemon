#pragma once
// ----------------------------------------------------------------------------
//  offsets_dynamic.h — Linux PID-local offset patching (LINUX_PORT_PLAN §6)
// ----------------------------------------------------------------------------
//  On Linux, offsets.h's 399 mutable globals are patched from a live Scan()
//  instead of from the 4+4 remote fetches in offsets_live.cpp. This header
//  is the bridge: game.cpp calls offsets_dynamic::Ensure() once on first
//  successful ReadDataModel, which runs sober::Scan and copies the result
//  into Offsets::Foo::Bar. After that callers keep reading Offsets:: as
//  before — no call-site change.
//
//  On Windows this header does nothing (Ensure() is a no-op and diag()
//  says "windows: use offsets_live") so the vcxproj does not need to
//  exclude it.
// ----------------------------------------------------------------------------

#include <string>
#include <cstdint>
#ifdef __linux__
#include "platform/sober_scanner.h"
#endif

namespace offsets_dynamic {

// Run the scanner once if not yet done. Returns true when ScanResult::ok().
// On failure writes a human-readable reason into diag() — shown on the
// debug page (same slot that logs "world: the Lighting service did not
// resolve"). Thread-safe: first caller wins, subsequent calls are no-ops.
bool Ensure();
const std::string& diag();
#ifdef __linux__
const sober::ScanResult& GetScanResult();
uintptr_t GetDataModel(); // 0 if not yet scanned
#endif

// Force a re-scan (e.g. after Sober bump detected via 5 s ReadDataModel==0).
// Resets the "already tried" flag so the next Ensure() re-scans.
void Invalidate();

// For tests: inject a ScanResult directly, patching Offsets:: globals.
// Used by tools/sober_sig_refresh.py self-test harness.
struct Patch {
    uintptr_t dataModel   = 0;
    uintptr_t workspace   = 0;
    uintptr_t world       = 0;
    uintptr_t primitives  = 0;
    // Primitive::* kept in sync with mover::LayoutSane() expectations
    uintptr_t primRotation = 0;
    uintptr_t primFlags    = 0;
};
bool PatchFrom(const Patch& p);

} // namespace offsets_dynamic
