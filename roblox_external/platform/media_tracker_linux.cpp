// ----------------------------------------------------------------------------
//  platform/media_tracker_linux.cpp — Linux stub (no SMTC)
// ----------------------------------------------------------------------------
#ifdef __linux__

#include "../media_tracker.h"
#include <cstring>

namespace media {

void Tick() {}
NowPlaying Get() { NowPlaying n; return n; }
void PlayPause() {}
void NextTrack() {}
void PrevTrack() {}
void VolumeUp() {}
void VolumeDown() {}
void SeekForward() {}
void SeekBackward() {}

}

#endif
