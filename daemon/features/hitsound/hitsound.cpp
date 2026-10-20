#include "hitsound.h"
// -----------------------------------------------------------------------------
//  features/hitsound/hitsound.cpp  --  the reference's misc/hitsound
// -----------------------------------------------------------------------------
//  A short tone played through PlaySoundA when a shot is CONFIRMED to have
//  landed. The confirmation is the triggerbot's 400ms health read-back, which
//  is why this file has no aim logic and no hook: "did it hit" is already a
//  fact the tool can prove, and inventing a second, worse detector (a click
//  sound, a crosshair flash) would fire on misses.
//
//  The reference writes its own WAVs too - two decaying sine tones - and so does
//  this port, because a feature whose first run needs the user to find assets is
//  a feature that looks broken. The generator is 40 lines of RIFF writing and
//  the files are 10KB each.
//
//  Sounds live in `sounds/` beside the executable, like the reference's, so a
//  user can drop any .wav in and pick it from the menu.
// -----------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "globals.h"
#include "platform/platform.h"

namespace features {
namespace hitsound {

    namespace {

        std::vector<std::string> s_files;
        std::vector<const char*> s_names;
        bool s_initialized = false;
        bool s_said_error = false;

        // Little-endian by hand: RIFF is defined in bytes, not in the host's
        // integers, and a WAV written from a big-endian box (or a future port)
        // must still play.
        void Put32(std::ofstream& f, uint32_t v) {
            const char b[4] = {(char)(v & 0xFF), (char)((v >> 8) & 0xFF),
                               (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF)};
            f.write(b, 4);
        }
        void Put16(std::ofstream& f, uint16_t v) {
            const char b[2] = {(char)(v & 0xFF), (char)((v >> 8) & 0xFF)};
            f.write(b, 2);
        }

        void WriteDefaultWav(const char* path, float freq) {
            const uint32_t sample_rate = 44100;
            const float duration = 0.12f;
            const uint32_t samples = (uint32_t)(sample_rate * duration);
            const uint32_t data_size = samples * 2;
            const uint32_t file_size = 36 + data_size;

            std::ofstream f(path, std::ios::binary);
            if (!f.is_open()) return;

            f.write("RIFF", 4); Put32(f, file_size); f.write("WAVE", 4);
            f.write("fmt ", 4); Put32(f, 16);
            Put16(f, 1);            // PCM
            Put16(f, 1);            // mono
            Put32(f, sample_rate);
            Put32(f, sample_rate * 2);   // byte rate
            Put16(f, 2);            // block align
            Put16(f, 16);           // bits
            f.write("data", 4); Put32(f, data_size);
            for (uint32_t i = 0; i < samples; ++i) {
                const float t = (float)i / (float)sample_rate;
                const float env = std::exp(-t * 30.0f);
                const float wave = std::sin(2.0f * 3.14159265f * freq * t);
                Put16(f, (uint16_t)(int16_t)(wave * env * 28000.0f));
            }
        }

        void Scan() {
            s_files.clear();
            s_names.clear();

            platform::ListFilesWithExt("sounds", "wav", s_files);   // creates the folder
            if (s_files.empty()) {
                WriteDefaultWav("sounds\\ding.wav", 1200.0f);
                WriteDefaultWav("sounds\\bell.wav", 850.0f);
                platform::ListFilesWithExt("sounds", "wav", s_files);
            }
            for (const std::string& f : s_files) s_names.push_back(f.c_str());
            if (!s_said_error) {
                s_said_error = true;
                LogLine("hitsound: %d wav%s in sounds/", (int)s_files.size(),
                        s_files.size() == 1 ? "" : "s");
            }
        }

    }  // namespace

    const char* const* SoundNames() {
        if (!s_initialized) { Scan(); s_initialized = true; }
        return s_names.empty() ? nullptr : s_names.data();
    }

    int SoundCount() {
        if (!s_initialized) { Scan(); s_initialized = true; }
        return (int)s_files.size();
    }

    void Refresh() {
        s_initialized = true;
        Scan();
    }

    void Play() {
        if (!hitsound_enabled) return;
        if (!s_initialized) { Scan(); s_initialized = true; }
        if (s_files.empty()) return;

        int idx = hitsound_selected;
        if (idx < 0 || idx >= (int)s_files.size()) idx = 0;
        char path[512];
        snprintf(path, sizeof(path), "sounds\\%s", s_files[(size_t)idx].c_str());

        platform::PlayWav(path, hitsound_volume);
    }

    void Test() {
        const bool was = hitsound_enabled;
        hitsound_enabled = true;
        Play();
        hitsound_enabled = was;
    }

}  // namespace hitsound
}  // namespace features
