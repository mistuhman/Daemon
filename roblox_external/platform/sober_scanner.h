#pragma once
// ----------------------------------------------------------------------------
//  platform/sober_scanner.h — PID-local offset discovery for Sober
// ----------------------------------------------------------------------------
//  Replaces the static offsets.h / offsets_live pipeline on Linux.
//
//  Design record: docs/LINUX_PORT_PLAN.md §6. The scanner does not vendor
//  numbers from UnknownCheats — it vendors the *method* (TaskScheduler anchor
//  → job-table walk → RenderJob → DataModel chain) and re-derives the
//  numbers from the running libroblox.so image each launch.
//
//  Build: g++ -std=c++17 -fsyntax-only -I roblox_external platform/sober_scanner.h
//  No Windows.h, no WinAPI. Compiles on both Windows (unused) and Linux.
//
//  State: P0 scaffold — structure only, no signature bytes vendored until
//  void-a proves one (LINUX_PORT_PLAN §6.2 / P2). Scan() returns ok()=false
//  with diag explaining why, so callers can log it on the debug page.
// ----------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace sober {

// Each field is an absolute address in the target process after Scan().
// taskScheduler is the instance (not the pointer-to-pointer) — i.e. what
// TaskScheduler::Pointer in offsets.h pointed at, resolved via Lea scan.
struct ScanResult {
    uintptr_t taskScheduler = 0;
    uintptr_t renderJob     = 0;
    uintptr_t dataModel     = 0;
    uintptr_t visualEngine  = 0;
    uintptr_t workspace     = 0;
    uintptr_t world         = 0;
    uintptr_t primitives    = 0;

    // Derived: Primitive::* kept for mover::LayoutSane(). Filled after scan
    // by offsets_dynamic::PatchFromScan() if the chain resolved.
    uintptr_t primitiveRotation = 0;
    uintptr_t primitiveFlags    = 0;
    uintptr_t primitivePosition = 0;

    // Human-readable trace — shown on the debug page, photographed for
    // operator receipts. Never empty on failure.
    std::string diag;

    bool ok() const { return taskScheduler && dataModel && workspace; }
};

// One /proc region — parsed from /proc/<pid>/maps. Used to filter
// readable code vs heap vs guard pages before scanning.
struct MapRegion {
    uintptr_t start = 0;
    uintptr_t end = 0;
    std::string perms; // e.g. "r-xp", "rw-p", "r--p"
    std::string path;  // may be empty for anon
};

// ---- PID discovery --------------------------------------------------------
// Find Sober's pid. Flatpak Sober appears as bwrap on the host — this
// sniffs /proc/*/comm == "sober"/"Sober"/"app_process" AND a maps line
// containing both libroblox.so and libbadcpu.so (only the true host has
// both). Falls back to RobloxPlayerBeta.exe for Wine testing.
uint32_t FindSoberPid();

// Base of libroblox.so's r-xp segment (PIE ELF load base). 0 if not Sober
// (caller should fall back to platform::GameBaseAddress for Wine).
uintptr_t LibRobloxBase(uint32_t pid);

// Enumerate /proc/<pid>/maps into regions (perms + path preserved).
// Returns empty on failure (process vanished, permission denied).
std::vector<MapRegion> MapRegions(uint32_t pid);

// Full scan — the only call offsets_dynamic needs. Reads are via
// platform::ReadMemory(pid token, ...) — caller opens the pid once
// and passes the platform handle. This function does not open handles
// itself (keeps /proc permission errors in one place).
ScanResult Scan(void* platform_handle, uint32_t pid, uintptr_t libBase);

// Sober string pooling — mirrors UnknownCheats pooling section.
// Returns empty if not a pooled handle (caller falls back to std::string).
std::string TryPooledString(void* platform_handle, uintptr_t container);

// Utility: validate that an address lies inside a readable rw-p region
// (cheap sanity before dereferencing a job pointer).
bool InReadableRegion(uintptr_t addr, const std::vector<MapRegion>& regions);

} // namespace sober
