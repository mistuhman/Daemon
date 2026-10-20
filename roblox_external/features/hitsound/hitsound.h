#pragma once

namespace features {
namespace hitsound {

// The reference repo's misc/hitsound: a click you can hear when a shot lands.
// This repo already knows WHEN a shot landed - the triggerbot reads the
// target's health back 400ms after each click - so the sound is wired to that
// verdict instead of being its own guess.
//
// Sounds are plain .wav files in `sounds/` next to the executable. Two short
// tones are generated there on first run if the folder has none, so the
// feature works with no setup; drop your own .wav in and press REFRESH.
void Play();
void Test();                       // play the current selection, even with the switch off
void Refresh();                    // re-scan sounds/ (called by the menu button)

// The file names in sounds/, for the menu's combo. Stable until Refresh().
const char* const* SoundNames();
int SoundCount();

}  // namespace hitsound
}  // namespace features
