#pragma once

namespace features {
    // Caps how fast the character can fall, so a long drop is survivable.
    // Ported from jatosm/RobloxExternal-base's misc/nofall.
    void RunNoFall();
    // Disarms the hold thread's clamp. Called from the detach path and at exit,
    // because the feature loop does not run after the handle is gone and a hold
    // left armed would keep its address across a re-attach.
    void ReleaseNoFall();
}
