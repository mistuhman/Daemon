// -----------------------------------------------------------------------------
//  process.cpp -- FindRoblox, as a two-line question to the platform layer
// -----------------------------------------------------------------------------
//  This file used to carry the NtQuerySystemInformation walk and the PEB
//  ImageBaseAddress read. Both moved to platform/platform_win.cpp when the
//  platform seam landed; what remains is the app-level question "is the game
//  running, and where is its main module" phrased once, in one place.
// -----------------------------------------------------------------------------
#include "roblox_process.h"
#include "platform/platform.h"

namespace process {

bool FindRoblox(uint32_t& pid, uintptr_t& base) {
    const uint32_t found = platform::FindGameProcess();
    if (!found) return false;
    const uintptr_t image_base = platform::GameBaseAddress(found);
    if (!image_base) return false;
    pid = found;
    base = image_base;
    return true;
}

} // namespace process
