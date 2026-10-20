#pragma once
// ----------------------------------------------------------------------------
//  platform/sober_scanner.h — PID-local offset discovery for Sober
// ----------------------------------------------------------------------------
//  Linux attachment is independently ELF-identified. Object-layout discovery
//  deliberately fails closed until a validated resolver exists for the image.
//  No Windows/static anchor fallback. See docs/LINUX_ATTACH_DIAGNOSTIC.md.
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
    uintptr_t offset = 0;
    std::string perms; // e.g. "r-xp", "rw-p", "r--p"
    std::string path;  // may be empty for anon
};

// ---- PID discovery --------------------------------------------------------
// Unique same-user process containing an ELF self-identifying as libroblox.so.
// Reads file/memfd ELF headers; no name scoring or Windows/Wine fallback.
uint32_t FindSoberPid();
std::string ProcessStartTicks(uint32_t pid);

// Verified ELF load bias, not the executable segment start. 0 on missing,
// ambiguous, unreadable or changed process. denied_out reports maps denial.
uintptr_t LibRobloxBase(uint32_t pid, bool* denied_out = nullptr);

// Enumerate /proc/<pid>/maps into regions (perms + path preserved).
// Returns empty on failure. If errno-style feedback is needed, pass a
// non-null denied_out — it is set to true when the maps file could not be
// opened for permission reasons (EACCES/EPERM = yama ptrace_scope blocking
// PTRACE_MODE_READ_ATTACH; ENOENT/ESRCH = pid gone), false on success or
// an unclassifiable IO error.
std::vector<MapRegion> MapRegions(uint32_t pid, bool* denied_out = nullptr);

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
