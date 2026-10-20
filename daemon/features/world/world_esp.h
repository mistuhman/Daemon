#pragma once

namespace features {
namespace worldesp {

// The reference repo's visual/world set: a workspace scan that finds the named
// objects a survival place puts on the ground (ore nodes, plants, supply crates,
// keycards, storage/tools, animals, and the AI bodies with Humanoid health) and
// draws them through the same projection the player ESP uses.
//
// The scan is the expensive half, so it is SLICED: a bounded breadth-first walk
// of Workspace that keeps its queue between passes and discards entries whose
// instance stopped resolving. Positions are refreshed from the cached primitive
// addresses every pass - that is a 3-float read per object, not a tree walk.
void RunWorldEsp();      // feature loop: scan / refresh
void RenderWorldEsp();   // render: markers + labels
void ReleaseWorldEsp();  // detach/exit: forget every instance address

}  // namespace worldesp
}  // namespace features
