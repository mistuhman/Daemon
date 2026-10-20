#pragma once
// -----------------------------------------------------------------------------
//  process.h -- app-level "is the game up, and where is it loaded"
//  Platform-free: the NT enumeration/PEB read that used to live in
//  process.cpp moved into platform_win.cpp (a Linux port answers the same two
//  questions from /proc). The game-WINDOW lookup also moved - into the
//  platform layer (GameClientRect/CursorClient), cached per attach.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace process {
    bool FindRoblox(uint32_t& pid, uintptr_t& base);
}
