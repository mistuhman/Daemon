#pragma once

namespace features {
    // Caps the CLIENT's frame rate through TaskScheduler::MaxFPS (see
    // fps_field.h for why the field is classified rather than assumed). The
    // original bytes are saved and put back when the switch goes off, and the
    // write is re-asserted if the client resets it (focus changes and resizes
    // make it re-apply its own cap).
    //
    // This is a feature of the TOOL, not of the overlay: the overlay's own
    // render pacing lives in overlay.cpp because it is not a memory write.
    void RunFpsCap();

    // The client's frame rate, MEASURED from `Workspace::DistributedGameTime`
    // (delta game clock / delta real time, >=200ms window, smoothed). 0 while the
    // clock is unreadable or still - the widget draws "--" then. Not the old
    // `RunService::HeartbeatFPS` read: that field classified as an int 1 on a
    // client running at 240, and a plausible wrong number is worse than none.
    float ClientFps();

    // Put the client's original value back now: called when the tool detaches or
    // exits, because a cap left in a live client outlives this process. Safe to
    // call when nothing was ever written.
    void ReleaseFpsCap();
}
