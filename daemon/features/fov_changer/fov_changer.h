#pragma once

namespace features {
    // Puts the camera's own FieldOfView back: on the switch going off, and on
    // detach (a stale FOV outlives the tool otherwise).
    void ReleaseFovChanger();
    void RunFovChanger();
}
