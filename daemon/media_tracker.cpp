// -----------------------------------------------------------------------------
//  media_tracker.cpp — now-playing + playback controls (Linux stub)
// -----------------------------------------------------------------------------
#include "media_tracker.h"

#include <mutex>

namespace media {

namespace {
    std::mutex g_mutex;
    NowPlaying g_now{};
}

void Tick() {
    // Linux MPRIS D-Bus implementation would go here.
    // For now, return empty data.
    std::lock_guard<std::mutex> lock(g_mutex);
    g_now = NowPlaying{};
}

NowPlaying Get() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_now;
}

void PlayPause()   { /* TODO: MPRIS PlayPause */ }
void NextTrack()   { /* TODO: MPRIS Next */ }
void PrevTrack()   { /* TODO: MPRIS Previous */ }
void VolumeUp()    { /* TODO: MPRIS VolumeUp */ }
void VolumeDown()  { /* TODO: MPRIS VolumeDown */ }
void SeekForward() { /* TODO: MPRIS SeekForward */ }
void SeekBackward(){ /* TODO: MPRIS SeekBackward */ }

} // namespace media