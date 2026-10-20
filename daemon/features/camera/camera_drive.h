#pragma once
#include <cstdint>
#include "game.h"

namespace features {
namespace cam {

instance Current();

// The cached Workspace instance's address (0 while unresolved) - callers that
// need to watch/pin Workspace.CurrentCamera at high frequency take this once
// per arm instead of re-walking the datamodel from their own thread.
uintptr_t WorkspaceAddr();

// Roblox CameraType::Scriptable. Re-asserted every tick so the game's camera
// script cannot snatch the CFrame back.
enum { kScriptable = 6 };

void Take(const instance& cam);
// Optionally reports the type/subject values it restored, so a caller can
// guard them for a short window afterwards (see mover::GuardCameraFields).
void Restore(int* wrote_type = nullptr, uintptr_t* wrote_subject = nullptr);
bool Ours();

void WriteLookAt(const instance& cam, const float pos[3], const float look_at[3]);
void WriteCFrame(const instance& cam, const float pos[3], const float rot[9]);
bool ReadPos(const instance& cam, float out[3]);
bool ReadRot(const instance& cam, float out[9]);

void LookVectors(const float rot[9], float fwd[3], float right[3], float up[3]);
void LookAtMatrix(const float from[3], const float to[3], float rot[9]);

} // namespace cam
} // namespace features
