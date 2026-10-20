#pragma once

namespace features {
    // Spin the local character fast enough that its parts sweep through the
    // players around it and throw them. Writes ANGULAR velocity only (see
    // mover::HoldSpin) - the character stays where it is and keeps its own
    // movement.
    void RunFling();
    // Disarms the spin. Called from the detach path, because the feature loop
    // stops while the handle is gone and a spin left armed would keep writing
    // into a dead primitive.
    void ReleaseFling();
}
