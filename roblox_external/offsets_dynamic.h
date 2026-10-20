// ---------------------------------------------------------------------------- 
//  offsets_dynamic.cpp — Linux PID-local offset patching (LINUX_PORT_PLAN §6)
// ---------------------------------------------------------------------------- 
//  On Linux, offsets.h's static globals are treated as UNVERIFIED: every scan
//  starts by ZEROING all of them (platform/offsets_zero_linux.inc — generated
//  by tools/offsets_zero_gen.py) and then fills only what the runtime
//  discovery validated:
//
//    static stage  sober::Scan   — anchor, DataModel, instance layout,
//                                  pointer members (camera, players, ...)
//    runtime stage sober::Refine — camera numerics, dual motion block,
//                                  BasePart::Primitive, humanoid fields
//
//  Zero is the honest "undiscovered" marker: consumers guard with
//  `Offsets::X &&` so a write can never land at object+0 (the vtable slot).
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

// Run the static scan once if not yet done. Returns true when the STATIC
// stage succeeded (DataModel walkable) — game::ReadDatamodel keys off this.
// On failure writes a human-readable reason into diag() — shown on the
// debug page. Thread-safe: first caller wins, subsequent calls re-probe at
// 500 ms until the static stage lands, then keep inviting the runtime stage.
bool Ensure();
const std::string& diag();
#ifdef __linux__
const sober::ScanResult& GetScanResult();
uintptr_t GetDataModel(); // 0 if not yet scanned

// True when the STATIC stage is done (tree walk works).
bool StaticReady();
// True when the runtime stage is done too: camera + motion + primitive
// pointers discovered — the gate main_linux puts in front of FeatureLoop
// so no write can fire with an undiscovered field.
bool RuntimeReady();
#endif

// Force a re-scan (e.g. after Sober bump detected via 5 s ReadDataModel==0).
// Resets the "already tried" flag so the next Ensure() re-scans, and drops
// every discovered fact — the next process must re-derive everything.
void Invalidate();

// For tests: inject a ScanResult directly, patching Offsets:: globals.
struct Patch {
    uintptr_t dataModel   = 0;
    uintptr_t workspace   = 0;
    uintptr_t world       = 0;
    uintptr_t primitives  = 0;
    uintptr_t primRotation = 0;
    uintptr_t primFlags    = 0;
};
bool PatchFrom(const Patch& p);

} // namespace offsets_dynamic
