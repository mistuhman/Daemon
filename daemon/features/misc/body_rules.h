#pragma once

// -----------------------------------------------------------------------------
//  features/misc/body_rules.h  --  the rules the property-writing features share
// -----------------------------------------------------------------------------
//  gravity, hip height and no-fall are small features with one hard part each,
//  and it is the same hard part twice: "the value I read a moment ago - is that
//  the game's original, or my own write coming back?"
//
//  Get it wrong in one direction and the character is left with 196 studs/s of
//  gravity forever because the "original" that got saved was the 500 we wrote.
//  Get it wrong in the other and a feature restores into a respawned instance and
//  the new character inherits the old one's numbers. This repo has had both
//  (walkspeed's inline check, and mover's respawn rule), so the rule lives here,
//  in one place, with cases on the Linux side.
//
//  Free of Windows, ImGui and the offsets table: the harness runs it.
// -----------------------------------------------------------------------------

namespace features {

    // The value to remember as "what it was before we touched it".
    //
    //   cur         the value just read from the field
    //   lo, hi      the range in which a real original can live at all
    //   target      what this feature is about to write
    //   prev_saved  what was saved on an earlier pass (0 when nothing was)
    //   fallback    the value to use when nothing plausible was ever read
    //
    //  A read within `eps` of the target is treated as OUR OWN write, not an
    //  original: that is the case that matters, because the second pass reads
    //  back what the first one wrote. In that case the previous save is kept.
    inline float RememberOriginal(float cur, float lo, float hi, float target,
                                  float prev_saved, float fallback, float eps = 0.5f) {
        const bool in_range = (cur >= lo) && (cur <= hi);
        const float diff = (cur > target) ? (cur - target) : (target - cur);
        if (in_range && !(diff <= eps)) return cur;          // a real original
        if (prev_saved >= lo && prev_saved <= hi) return prev_saved;   // keep ours
        return fallback;
    }

    // Is a value worth restoring at all? (both bounds inclusive, NaN rejected)
    inline bool PlausibleValue(float v, float lo, float hi) {
        return (v >= lo) && (v <= hi);
    }

    // no-fall: hold the downward speed at `cap` studs/s.
    //
    //  Reports whether it changed anything, so the caller writes only when it
    //  must. A NaN velocity is left alone on purpose - the comparison is false
    //  either way, and "clamp" on a value we cannot reason about is a write that
    //  turns a read failure into a physics change.
    inline bool CapFallSpeed(float& vel_y, float cap) {
        if (!(vel_y < -cap)) return false;
        vel_y = -cap;
        return true;
    }

}  // namespace features
