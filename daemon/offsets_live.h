// -----------------------------------------------------------------------------
//  offsets_live.h — Linux offset version reporting (stub for network fetcher)
// -----------------------------------------------------------------------------
//  Windows version fetches live offsets from imtheo/GitHub at launch. Linux uses
//  runtime discovery via offsets_dynamic/sober_scanner — no network fetch needed.
//  These stubs provide the same API for the debug page.
// -----------------------------------------------------------------------------
#pragma once

#include <string>

namespace offsets_live {

// Linux: no network refresh — offsets are discovered at runtime by offsets_dynamic
inline bool RefreshOffsets() { return false; }

// Returns the version string of the offsets currently loaded (from offsets_dynamic)
const char* OffsetsLoadedVersion();

// Returns the client version string (from offsets.h ClientVersion)
const char* OffsetsClientVersion();

} // namespace offsets_live