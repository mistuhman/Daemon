#pragma once

namespace features {
    // The reference repo's misc/jumppower. The Humanoid has TWO jump models -
    // JumpPower (studs/s) and JumpHeight (studs) - and a per-humanoid switch
    // (UseJumpPower) that says which one the controller reads. Writing JumpPower
    // without setting that switch is how "jump power" silently does nothing on a
    // place that uses height, so this feature sets both and restores both.
    void RunJumpPower();
    void ReleaseJumpPower();
}
