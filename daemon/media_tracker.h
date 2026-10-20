// -----------------------------------------------------------------------------
//  media_tracker.h — now-playing + playback controls (Linux stub)
// -----------------------------------------------------------------------------
//  Detects Spotify, YouTube, YouTube Music, and Monochrome by reading window
//  titles. Provides media key controls (play/pause, next/prev, seek, volume).
//  Linux implementation uses MPRIS via D-Bus when available, falls back to
//  window title parsing. Currently a stub — returns empty data.
// -----------------------------------------------------------------------------
#pragma once

#include <string>

namespace media {

    struct NowPlaying {
        char artist[128]{};
        char track[128]{};
        char source[32]{};   // "spotify", "youtube", "monochrome", ""
        bool playing = false;
    };

    // Call once per cache tick (~16ms). Cheap when nothing changed.
    void Tick();

    // Thread-safe snapshot.
    NowPlaying Get();

    // ── Playback controls (send media keys globally) ────────────────────────
    void PlayPause();
    void NextTrack();
    void PrevTrack();
    void VolumeUp();
    void VolumeDown();
    void SeekForward();    // +5s (Spotify) / +10s (YouTube)
    void SeekBackward();   // -5s (Spotify) / -10s (YouTube)

} // namespace media