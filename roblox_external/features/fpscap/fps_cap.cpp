#include "fps_cap.h"
// -----------------------------------------------------------------------------
//  features/fpscap/fps_cap.cpp  --  the client's frame-rate cap, and its FPS
// -----------------------------------------------------------------------------
//  The FPS widget in the corner is the OVERLAY's frame rate - the rate at which
//  this tool redraws its own window - and on an uncapped Present that number is
//  in the thousands while the game sits at sixty. The number a player wants is
//  the CLIENT's, and the two things this file adds are the pair that makes it
//  controllable:
//
//    * ClientFps(): MEASURED, not looked up. The old read was
//      RunService::HeartbeatFPS, and it reported "1 fps" on a client running at
//      240: whatever lives at that offset on the current build, it is not the
//      frame rate (a plausible-looking 1 is exactly what a boolean or a mode
//      flag reads as, which is why the value was so confidently wrong). A frame
//      rate is a RATE, so it is now derived the way one always is - watch the
//      game clock and divide the distance it moved by the wall time it took.
//      Workspace::DistributedGameTime advances once per simulation frame, so
//      Δgame / Δreal is the client's frame rate, independent of any offset
//      being labelled correctly. Nothing is shown until two samples prove the
//      clock is advancing, and a clock that stops reports "--" rather than a
//      stale number.
//
//    * RunFpsCap(): write TaskScheduler::MaxFPS. The dump gives the offset but
//      not the type; the classifier decides, and an unrecognised field is left
//      alone with one log line rather than written blind. The original 4 bytes
//      are saved verbatim and restored on release, and the write is re-asserted
//      once a second because the client re-applies its own cap on focus and
//      resize. A read-back after each write is what tells "set" from "set and
//      immediately overwritten".
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>
#include <cstring>

#include "fps_field.h"
#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "cache.h"
#include "game.h"
#include "platform/platform.h"

namespace features {

    namespace {

        struct State {
            bool      active = false;
            uintptr_t ts = 0;             // the TaskScheduler instance we found
            uint64_t  ts_checked = 0;     // when we last looked for it
            uintptr_t field = 0;          // ts + MaxFPS, the 4 bytes
            uint32_t  saved_bits = 0;     // the original, verbatim
            bool      saved = false;
            int       form = fpsfield::Unknown;
            float     written = 0.0f;
            uint64_t  last_check = 0;
            bool      said_unrecognised = false;
            bool      said_reset = false;
            bool      said_armed = false;
        };
        State s_cap;

        bool ReadField(uintptr_t field, int& as_int, float& as_float) {
            uint32_t bits = 0;
            if (!read_raw(field, &bits, sizeof(bits))) return false;
            as_int = (int)bits;
            std::memcpy(&as_float, &bits, sizeof(as_float));
            return true;
        }

        uintptr_t FindScheduler() {
            if (!g_base_address) return 0;
            const uintptr_t ts =
                read<uintptr_t>(g_base_address + Offsets::TaskScheduler::Pointer);
            return is_valid_address(ts) ? ts : 0;
        }

        void Restore() {
            if (s_cap.saved && s_cap.field) {
                // If the field no longer holds what WE wrote, the user (or the client)
                // changed the cap after us: writing the saved original now would undo
                // their change. Leave it alone and say so.
                if (s_cap.written <= 0.0f) { s_cap = State{}; return; }   // never wrote
                int cur_i = 0;
                float cur_f = 0.0f;
                float saved_f = 0.0f;
                const fpsfield::Form form = (fpsfield::Form)s_cap.form;
                std::memcpy(&saved_f, &s_cap.saved_bits, sizeof(saved_f));
                if (form != fpsfield::Unknown && ReadField(s_cap.field, cur_i, cur_f) &&
                    std::fabs(fpsfield::Value(form, cur_i, cur_f) - s_cap.written) > 0.5f) {
                    LogLine("fps cap: the client's own value changed while the cap was on "
                            "(now %.0f) - left it alone instead of restoring %.0f",
                            (double)fpsfield::Value(form, cur_i, cur_f),
                            (double)fpsfield::Value(form, (int)s_cap.saved_bits, saved_f));
                    s_cap = State{};
                    return;
                }

                if (write_raw(s_cap.field, &s_cap.saved_bits, sizeof(s_cap.saved_bits))) {
                    int i = 0;
                    float f = 0.0f;
                    const bool back = ReadField(s_cap.field, i, f) &&
                                      (uint32_t)i == s_cap.saved_bits;
                    LogLine("fps cap: restored the client's own value (%s)",
                            back ? "read back, ok" : "read back MISMATCH - the client may "
                                                      "re-apply its own cap anyway");
                }
            }
            s_cap = State{};
        }

    }  // namespace

    float ClientFps() {
        // One meter, two candidate encodings of the game clock. DistributedGameTime
        // is a double in the engine's reflection, but a build that stores a float
        // there (or an offset that landed four bytes early) must not read as a
        // subnormal and then as "0 fps" - so the first encoding that produces a
        // frame rate in the plausible band is kept, and the other is ignored.
        struct Meter {
            int      form = 0;        // 0 none, 1 double, 2 float
            double   last_t = 0.0;
            uint64_t last_ms = 0;
            double   acc_dt = 0.0;    // game seconds accumulated this window
            double   acc_s = 0.0;     // real seconds accumulated this window
            float    value = 0.0f;    // published rate
            uintptr_t ws = 0;         // cached Workspace instance
            uint64_t ws_at = 0;       // when it was cached
            bool     primed = false;  // last_t holds a real sample
            int      tries = 0;       // encodings tried since the last reset
        };
        static Meter m;

        const uint64_t now = platform::NowMs();
        if (!g_base_address) { m = Meter{}; return 0.0f; }

        if (!m.ws || now - m.ws_at > 2000) {
            m.ws_at = now;
            instance dm = game::ReadDatamodel(g_base_address);
            m.ws = dm.is_valid() ? dm.read_service("Workspace").address : 0;
            m.primed = false;   // the address may have changed; never subtract across it
        }
        if (!m.ws) { m = Meter{}; return 0.0f; }

        const uintptr_t clock = m.ws + Offsets::Workspace::DistributedGameTime;
        double t = 0.0;
        bool got = false;
        if (m.form == 2) {
            float f = 0.0f;
            got = read_raw(clock, &f, sizeof(f)) && std::isfinite(f);
            t = (double)f;
        } else {
            got = read_raw(clock, &t, sizeof(t)) && std::isfinite(t);
        }

        if (!got || t < 0.0 || t > 1.0e7) {
            // Unreadable: drop the candidate, try the other encoding next frame,
            // and publish nothing rather than a number that is not a measurement.
            if (m.tries < 2) { m.form = (m.form == 1) ? 2 : 1; ++m.tries; }
            else             { m = Meter{}; }
            return 0.0f;
        }

        // First sample of this encoding: remember it, publish nothing yet.
        if (!m.primed) {
            m.primed = true;
            m.last_t = t;
            m.last_ms = now;
            return m.value;
        }

        const double dt = t - m.last_t;
        const double ds = (double)(now - m.last_ms) * 0.001;
        m.last_t = t;
        m.last_ms = now;

        // A clock that does not move, moves backwards, jumps a second at a time
        // (a teleport of the value, not a frame) or reports a rate outside what a
        // client can present is not evidence of a frame rate.
        if (dt > 0.0 && ds > 0.0 && dt < 1.0) {
            const double inst = dt / ds;
            if (inst >= 1.0 && inst <= 2000.0) {
                m.acc_dt += dt;
                m.acc_s += ds;
            }
        }

        // A window of at least a fifth of a second: at 240 fps that is 48 frames,
        // which is enough for the number to be steady without lagging a change.
        if (m.acc_s >= 0.2) {
            const double measured = m.acc_dt / m.acc_s;
            if (measured >= 1.0 && measured <= 2000.0) {
                m.value = (m.value > 0.0f) ? (float)(m.value * 0.4 + measured * 0.6)
                                           : (float)measured;
                m.tries = 0;
            }
            m.acc_dt = 0.0;
            m.acc_s = 0.0;
        }
        return m.value;
    }

    void RunFpsCap() {
        const float want = fpsfield::ClampCap(fps_cap_value);
        const bool on = fps_cap_enabled && want > 0.0f;

        if (!on) {
            if (s_cap.active) Restore();
            return;
        }

        // Re-resolving the scheduler means a pointer read through the process
        // handle; at the feature loop's rate that is a thousand reads a second to
        // learn nothing. Once a second is enough to notice a relaunch.
        const uint64_t tick = platform::NowMs();
        uintptr_t ts = s_cap.ts;
        if (ts == 0 || tick - s_cap.ts_checked >= 1000) {
            ts = FindScheduler();
            s_cap.ts_checked = tick;
        }
        if (!ts) {
            if (s_cap.active) Restore();     // the client went away: nothing to hold
            return;
        }
        if (!Offsets::TaskScheduler::MaxFPS) {
            // Undiscovered (offsets_dynamic on Linux): ts+0 would be the
            // scheduler's vtable — never arm, never write, never log-spam.
            return;
        }
        // An arm that could not classify the field retries every five seconds: a
        // transient read failure at attach time should not disable the feature for
        // the life of the process, and a field that genuinely is neither type should
        // not cost a read per millisecond either.
        const bool retry_unknown =
            s_cap.form == fpsfield::Unknown && tick - s_cap.last_check >= 5000;
        if (ts != s_cap.ts || !s_cap.active || retry_unknown) {
            // First arm, or the client's scheduler was replaced (a relaunch).
            if (s_cap.active) Restore();
            s_cap.ts = ts;
            s_cap.field = ts + Offsets::TaskScheduler::MaxFPS;
            s_cap.active = true;
            s_cap.saved = false;
            s_cap.said_unrecognised = false;
            s_cap.said_reset = false;
            s_cap.said_armed = false;

            int as_int = 0;
            float as_float = 0.0f;
            if (!ReadField(s_cap.field, as_int, as_float)) {
                s_cap.last_check = tick;      // gates the retry above
                LogLine("fps cap: the TaskScheduler field at +0x%llX cannot be read - the "
                        "offset is probably for another client version; nothing written",
                        (unsigned long long)Offsets::TaskScheduler::MaxFPS);
                return;
            }
            s_cap.form = (int)fpsfield::Classify(as_int, as_float);
            if (s_cap.form == fpsfield::Unknown) {
                s_cap.last_check = tick;      // gates the retry above
                if (!s_cap.said_unrecognised) {
                    s_cap.said_unrecognised = true;
                    LogLine("fps cap: TaskScheduler+0x%llX reads neither as an int nor as a "
                            "float frame rate (int %d, float %.4f) - nothing written. The "
                            "widget still shows the client's own FPS",
                            (unsigned long long)Offsets::TaskScheduler::MaxFPS, as_int, as_float);
                }
                return;
            }
            s_cap.saved_bits = (uint32_t)as_int;
            s_cap.saved = true;
            LogLine("fps cap: the field holds %s %.3f; armed at %.0f",
                    s_cap.form == fpsfield::AsInt ? "an int" : "a float",
                    (double)fpsfield::Value((fpsfield::Form)s_cap.form, as_int, as_float),
                    (double)want);
        }

        if (s_cap.form == fpsfield::Unknown) return;

        // Write, then look at what the field holds a moment later. Once a second
        // is enough: the client re-applies its cap on focus/resize, not per frame.
        const uint64_t now = platform::NowMs();
        const bool due = (s_cap.last_check == 0) || (now - s_cap.last_check >= 1000);
        if (!due) return;
        s_cap.last_check = now;

        int want_int = 0;
        float want_float = 0.0f;
        if (!fpsfield::Encode(want, (fpsfield::Form)s_cap.form, want_int, want_float)) return;

        int cur_int = 0;
        float cur_float = 0.0f;
        if (!ReadField(s_cap.field, cur_int, cur_float)) return;
        const float cur = fpsfield::Value((fpsfield::Form)s_cap.form, cur_int, cur_float);

        if (std::fabs(cur - want) < 0.01f) {
            s_cap.written = want;
            return;                       // already ours: no write, no syscall
        }

        const bool ok = (s_cap.form == fpsfield::AsInt)
                            ? write<int>(s_cap.field, want_int)
                            : write<float>(s_cap.field, want_float);
        if (!ok) {
            LogLine("fps cap: the write to TaskScheduler+0x%llX failed",
                    (unsigned long long)Offsets::TaskScheduler::MaxFPS);
            return;
        }
        // Ours, or theirs? The field differing from what we WROTE (not from what we
        // want) is the client putting its own value back; the user moving the slider
        // is us wanting a different number, and must not be reported as a reset.
        const bool client_reset = s_cap.written > 0.0f &&
                                  std::fabs(cur - s_cap.written) > 0.5f;
        if (client_reset && !s_cap.said_reset) {
            s_cap.said_reset = true;
            LogLine("fps cap: the client had put its own value back (read %.0f, wanted %.0f) - "
                    "it re-applies its cap on focus and resize, so the feature re-asserts "
                    "ours once a second rather than fighting it per frame", cur, want);
        }
        s_cap.written = want;

        int back_int = 0;
        float back_float = 0.0f;
        if (!ReadField(s_cap.field, back_int, back_float)) return;
        const float back = fpsfield::Value((fpsfield::Form)s_cap.form, back_int, back_float);
        if (std::fabs(back - want) > 0.01f) {
            LogLine("fps cap: wrote %.0f but the field reads %.0f - the client is overwriting "
                    "it; the write is re-asserted once a second", want, back);
        } else if (!s_cap.said_armed) {
            s_cap.said_armed = true;
            LogLine("fps cap: the client's frame rate is capped at %.0f (it was %.0f); the "
                    "original goes back when the switch is turned off", want, cur);
        }
    }

    void ReleaseFpsCap() { Restore(); }

}  // namespace features
