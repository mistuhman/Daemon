#pragma once

namespace features {
    // Fires a click when a valid target is under the crosshair.
    //
    // What it deliberately is NOT: a hit-registration oracle. An external cannot
    // see the server's verdict, so this fires on the same evidence the player has
    // - a body on the crosshair - plus the checks the rest of the tool already
    // agrees on (team, dead, blacklist, range, line of sight). The reference
    // repo's autoshoot reads the same way; its "hit" is a projectile/raycast hook
    // we do not have and are not adding (see docs/ARCHITECTURE.md §12).
    void RunTriggerbot();

    // Shots this run (diagnostics; the debug page can show it).
    unsigned TriggerbotShotsFired();
}
