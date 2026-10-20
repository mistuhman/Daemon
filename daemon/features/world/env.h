#pragma once

// -----------------------------------------------------------------------------
//  features/world/env.h  --  the sky, fog, ambient and grass writes
// -----------------------------------------------------------------------------
//  The reference repo's visual/world "env" set, in one place because they all
//  write the same two instances (Lighting and Terrain) and all of them have to
//  hand the originals back:
//
//    fullbright, ambient, fog, exposure, shadows (Lighting), grass (Terrain),
//    night (ambient + brightness), time/clock (the whole sky for one hour).
//
//  The values are Lighting/Terrain properties - persistent, not per-step - so a
//  write at a few hertz is enough; the refresh gate exists because the client
//  re-applies its own lighting when the place's scripts change it, and because a
//  1kHz feature loop must not turn twenty fields into twenty thousand writes a
//  second.
// -----------------------------------------------------------------------------

namespace features {
namespace worldenv {

// Apply whatever is on, restore whatever was on and is not any more, and
// re-resolve the two services if the place changed.
void RunWorldEnv();

// fullbright / ambient / night, painted over the game by the render loop before
// the ESP. A grade, not a write: nothing in the client to save or restore.
void RenderLightingGrade();

// Put every Lighting/Terrain property this feature touched back. Called from
// DetachRoblox and at exit, like mover's restore.
void ReleaseWorldEnv();

}  // namespace worldenv
}  // namespace features
