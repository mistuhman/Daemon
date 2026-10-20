#pragma once
// ---------------------------------------------------------------------------- 
//  platform/sober_scanner.h — PID-local offset discovery for Sober
// ---------------------------------------------------------------------------- 
//  Linux attachment is independently ELF-identified. Object-layout discovery
//  is resolved AT RUNTIME from the image itself, never from Windows numbers:
//
//    * anchor   — RTTI chain off the VisualEngine singleton (the method the
//                 established Sober dumper "orchestrator" / process.hpp by
//                 @h6di proves: vtable -> typeinfo -> mangled name, then
//                 member scans for a pointer whose pointee's RTTI names the
//                 class), with the TaskScheduler lea signature + job-walk
//                 (LINUX_PORT_PLAN §6.2) as the fallback anchor;
//    * instance — children vector, name container and class descriptor are
//                 discovered by structural validation against ground truth
//                 (decoded name == the RTTI-derived class token), never by
//                 copying an offsets.h number;
//    * motion   — the dual motion block is MEASURED inside a real primitive
//                 (orthonormal 9-float rotation + position/velocity triplet,
//                 repeated one stride later — ARCHITECTURE §4.0's layout,
//                 re-derived here rather than assumed);
//    * camera   — rotation/position by the same matrix+delta pattern, FOV and
//                 the VisualEngine dimensions by plausibility + window match,
//                 each candidate logged in diag for the live checklist.
//
//  No Windows/static anchor fallback. See docs/LINUX_ATTACH_DIAGNOSTIC.md.
// ---------------------------------------------------------------------------- 

#include <cstdint>
#include <string>
#include <vector>

namespace sober {

// Each field is an absolute address (or, where the name says Off, a field
// offset inside the target process) after the matching stage succeeded.
// taskScheduler is the instance (not the pointer-to-pointer) — i.e. what
// TaskScheduler::Pointer in offsets.h pointed at, resolved via Lea scan.
// A zero value means "stage not complete / not found", never "offset 0".
struct ScanResult {
    uintptr_t taskScheduler = 0;
    uintptr_t renderJob     = 0;
    uintptr_t dataModel     = 0;
    uintptr_t visualEngine  = 0;          // VE object (heap)
    uintptr_t visualEngineGlobal = 0;     // address of the global pointer to it
    uintptr_t visualEngineDims = 0;       // Off::VisualEngine::Dimensions within VE
    uintptr_t workspace     = 0;
    uintptr_t players       = 0;
    uintptr_t camera        = 0;          // camera instance (Workspace::CurrentCamera target)
    uintptr_t world         = 0;
    uintptr_t primitives    = 0;

    // ---- instance layout (offsets *inside* an Instance) — 0 = undiscovered
    uintptr_t instChildrenContainer = 0;  // Off::Instance::ChildrenStart
    uintptr_t instChildrenArray     = 0;  // offset of the first entry ptr in the container
    uintptr_t instChildrenEnd       = 0;  // Off::Instance::ChildrenEnd
    uintptr_t instChildrenStride    = 0;  // 8 or 16 bytes per entry
    uintptr_t instClassDescriptor   = 0;  // Off::Instance::ClassDescriptor
    uintptr_t instClassName         = 0;  // Off::Instance::ClassName (inside descriptor)
    uintptr_t instNameContainer     = 0;  // Off::Instance::NameContainer
    uintptr_t instName              = 0;  // Off::Instance::Name (inside name container)
    uintptr_t instParent            = 0;  // Off::Instance::Parent

    // ---- pointer members discovered by RTTI token on the owning instance
    uintptr_t wsCurrentCamera = 0;        // Off::Workspace::CurrentCamera
    uintptr_t playersLocalPlayer = 0;     // Off::Player::LocalPlayer (on Players)
    uintptr_t playerModelInstance = 0;    // Off::Player::ModelInstance (on Player)
    uintptr_t playerTeam = 0;             // Off::Player::Team (0 when the place has no teams)
    uintptr_t humanoidRootPart = 0;       // Off::Humanoid::HumanoidRootPart
    uintptr_t basePartPrimitive = 0;      // Off::BasePart::Primitive

    // ---- camera / viewport numerics (measured; candidates logged in diag)
    uintptr_t cameraRotation = 0;         // Off::Camera::Rotation (9 floats)
    uintptr_t cameraPosition = 0;         // Off::Camera::Position (vec3, +0x24 from rotation)
    uintptr_t cameraFov      = 0;         // Off::Camera::FieldOfView (radians)

    // ---- primitive motion (measured; see ARCHITECTURE §4.0)
    uintptr_t primMotionBlock = 0;        // block A inside the primitive (Off::Primitive::Rotation)
    uintptr_t primStride      = 0;        // A -> B (0x48 on the documented layout)

    // ---- humanoid numerics (adopted only when consistent across instances)
    uintptr_t humanoidHealth    = 0;      // Off::Humanoid::Health
    uintptr_t humanoidMaxHealth = 0;      // Off::Humanoid::MaxHealth (Health + 4)

    // ---- stage bookkeeping
    bool staticOk  = false;               // anchor + DataModel + instance layout
    bool runtimeOk = false;               // camera + motion + character members

    // Human-readable trace — shown on the debug page, photographed for
    // operator receipts. Never empty on failure.
    std::string diag;

    // Feature gate (main_linux / menu_linux): the static stage is enough to
    // walk the tree (explorer, players list). Camera/motion arrive through
    // Refine() and gate the drawing/writing features inside their own readers.
    bool ok() const { return staticOk && dataModel && workspace; }
    bool runtime_ok() const { return runtimeOk; }
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
// opened for permission reasons (EACCES/EPMERM = yama ptrace_scope blocking
// PTRACE_MODE_READ_ATTACH; ENOENT/ESRCH = pid gone), false on success or
// an unclassifiable IO error.
std::vector<MapRegion> MapRegions(uint32_t pid, bool* denied_out = nullptr);

// Static stage — the call offsets_dynamic::Ensure() makes. Resolves the
// anchor (VisualEngine RTTI chain, TaskScheduler signature fallback), the
// DataModel, the instance layout (children/name/class/parent) and the
// pointer members that need no character to exist. Re-runnable; each call
// revalidates the process identity via the caller's pid/base checks.
ScanResult Scan(void* platform_handle, uint32_t pid, uintptr_t libBase);

// Runtime stage — camera numerics, motion blocks, character members. Called
// repeatedly (throttled by the caller) until it returns true; needs a live
// place (players/characters) for the motion half. Mutates `result` in place,
// fills diag with what each attempt saw.
bool Refine(void* platform_handle, uint32_t pid, uintptr_t libBase, ScanResult& result);

// Forget every discovered fact (call on detach/invalidate: next attach must
// re-derive, never reuse a dead process's layout).
void ResetScanner();

// Publish the pid the string/class decoders read through (memory.cpp's
// TryPooledString / RttiClassName carry no handle). 0 clears it.
void SetDecodePid(uint32_t pid);

// String decode for memory.cpp — the Linux string object may be libc++
// (Bionic/clang), libstdc++ or the engine pool; each layout is decoded and
// validated (printable, length sane) or rejected with an empty string.
// Returns empty when nothing decodes — callers fall back to std::string.
std::string TryPooledString(void* platform_handle, uintptr_t container);

// Class name of any object through its vtable RTTI (Itanium ABI), e.g.
// "_ZTSN5Roblox9DataModelE" -> "DataModel". Empty when the chain does not
// read. memory.cpp uses this when the ClassDescriptor layout is unknown.
std::string RttiClassName(void* platform_handle, uintptr_t object);

// Utility: validate that an address lies inside a readable rw-p region
// (cheap sanity before dereferencing a job pointer).
bool InReadableRegion(uintptr_t addr, const std::vector<MapRegion>& regions);

} // namespace sober
