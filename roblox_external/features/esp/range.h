#pragma once

// -----------------------------------------------------------------------------
//  features/esp/range.h  --  "is this thing inside the draw distance?"
// -----------------------------------------------------------------------------
//  One rule, in one place, because three copies of it drifted apart before:
//
//    * a setting of 0 means INFINITE, not "off" (what the user asked for);
//    * a positive setting means studs, and something *at* the edge is inside;
//    * a negative setting is treated as infinite as well, because the only ways
//      to get one are a hand-edited cfg and a NaN sneaking through a slider --
//      and of the two ways to be wrong, "draw everything" is the visible one,
//      "draw nothing" is the one that looks like a broken tool;
//    * NaN distance never passes, so a bad read cannot be used to keep an entity
//      alive past its range.
//
//  Deliberately free of Windows, ImGui and the offsets table: the ESP compiles
//  this into the game's process, but the rule itself is testable on Linux
//  (glass-obsidian/tools/world_ray_test.cpp runs these cases).
// -----------------------------------------------------------------------------

namespace features {

    inline bool DrawRangeAllows(float dist_studs, float setting_studs) {
        if (!(setting_studs > 0.0f)) return true;   // 0, negative, NaN: infinite
        if (!(dist_studs >= 0.0f)) return false;    // NaN distance: never shown
        return dist_studs <= setting_studs;
    }

}  // namespace features
