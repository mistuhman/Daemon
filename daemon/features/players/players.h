#pragma once
#include <cstdint>

// -----------------------------------------------------------------------------
//  features/players  --  "go to them" and "watch them"
// -----------------------------------------------------------------------------
//  Both halves of the players tab live here:
//
//    TeleportToPlayer()  one-shot, called from the menu's GO button. Writes our
//                        root part onto the target's root part.
//    RunSpectate()       the per-frame camera drive. Puts the camera behind and
//                        above the target, aimed the way they are aimed.
//
//  Nobody reads the game's datamodel from the UI thread: the menu asks for an
//  action, this file does the memory work, and the outcome lands in
//  players_status_msg for the menu to display.
// -----------------------------------------------------------------------------

namespace features {

// Jump to the player with this ROBLOX id. Returns false when either side could not
// be resolved; the reason goes into players_status_msg either way.
bool TeleportToPlayer(uint32_t user_id);
// Exact world point (no extra land height). Held for ~120ms against physics snap-back.
bool WriteRootTo(float x, float y, float z, const char* label = nullptr);
bool TeleportToWorld(float x, float y, float z, const char* label = nullptr);
void TickTeleportHold();

// Starts watching the player with this id. Pure state: the actual camera work
// happens in RunSpectate(), from the feature loop.
void StartSpectate(uint32_t user_id);

// Puts the game's own camera back. Safe to call when not spectating.
void StopSpectate(const char* reason = nullptr);

// Drive the camera onto the spectate target. Call every feature-loop tick, NOT
// gated on spectate_enabled: the frame the flag clears is exactly the frame that
// has to hand the camera back.
void RunSpectate();

} // namespace features
