#pragma once

namespace features {
    void RunAimbot();
    void RenderFOV();

    // True while the aimbot key is acting as a latch (aimbot_hold_mode off) and that
    // latch is ON. The menu reads it through the snapshot; the state itself lives in
    // the feature, next to the key it is derived from.
    bool AimbotKeyLatched();
}

