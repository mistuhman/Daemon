#pragma once

namespace features {
    // Keeps the client from going idle: a spacebar tap every N minutes, and only
    // while the game's window is in the foreground (an input sent to a background
    // window is exactly the input a cheat-detector is looking for, and it would
    // also fight whatever the user is actually typing into).
    // Ported from jatosm/RobloxExternal-base's misc/antiafk.
    void RunAntiafk();
}
