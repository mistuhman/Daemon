#include "infinite_jump.h"
#include "features/flight/flight.h"
#include "features/movement/mover.h"
#include "globals.h"
#include "memory.h"
#include "offsets.h"

// -----------------------------------------------------------------------------
//  Infinite jump  --  ONE autonomous hammered-velocity BURST per space press
// -----------------------------------------------------------------------------
//  "make the infinite jump work almost exactly how the debug hammered velocity
//  works because hammered velocity actually works, it increase ur jumppower and
//  you go up and up everytime you click it without having to land, it works
//  perfectly."
//
//  WHAT THE TEST DOES (RunTest id 10), per click of its run button:
//      read the primitive ONCE, then write linear velocity y=100 into BOTH
//      motion blocks in a tight loop for 400ms. Nothing else. Nothing can
//      interrupt it. The session log: every click climbed +29.6..+34.5 studs,
//      grounded or airborne (runs 2-4 never landed between clicks).
//
//  THE SHAPE, per press of space:
//    * a press fires mover::VelocityBurst(prim, {vx, power, vz}, 400) - the
//      hold thread hammers both motion blocks until its own deadline, then
//      stops itself. The window ends on its own clock, never on the key
//      coming up; a new press - mid-air, mid-window - restarts it. Presses
//      chain bursts into a continuous climb without landing.
//    * the only early ends: the clock, the toggle, a REAL respawn (a
//      different VALID primitive), >200ms unresolved (a detach), or flight
//      owning space.
//
//  PRESS DETECTION (the "spam space and only half fire" fix): the feature
//  loop shares a thread with ESP/aimbot/servers and stalls for tens of
//  milliseconds under load - a whole press+release inside a stall is
//  invisible to a level-only poll. So the edge comes from the "pressed since
//  the last call" bit of the ONE shared GetAsyncKeyState call per iteration
//  (mover::Tick -> g_input_space_edge), which survives a stall by
//  construction, OR a plain level transition - whichever fires first. A
//  press that arrives during a cache hiccup (body unresolved) is LATCHED and
//  fired the moment the body resolves (within 150ms), instead of being eaten.
//
//  JUMP POWER, save and RESTORE: "when you turn off infinite jump your jump
//  power still isnt restored" - the humanoid's own JumpPower is written while
//  the feature is on (so a grounded jump agrees with the slider), and when
//  the feature is switched off the ORIGINAL value is written back - held
//  down at 4Hz for 3s, because the server replicates the field and a single
//  restore write loses that race. A respawn is the hard boundary: the saved
//  value belongs to a dead humanoid, and the new one arrives with its own.
//
//  Each burst leaves one rate-limited summary line in the log, so a future
//  failure names itself instead of being reasoned about.
// -----------------------------------------------------------------------------

namespace features {

    namespace {
        // The debug test's loop duration, verbatim: `while (elapsed < 400)`.
        constexpr uint32_t kBoostMs = 400;

        // How long the local player may fail to resolve before a live burst is
        // considered orphaned (a hiccup is 16-32ms; a detach lasts forever).
        constexpr uint32_t kInvalidGraceMs = 200;

        // How long a press that arrived during a hiccup stays latched, waiting
        // for the body to resolve so it can fire. Generous: under load the
        // cache can miss the player for several passes, and dropping a real
        // press to a slow cache pass reads as exactly the spam-miss being
        // fixed here.
        constexpr uint32_t kPendingMs = 250;

        bool      s_space_was_down = false;
        bool      s_bursting = false;      // we fired a burst and watch it
        uintptr_t s_burst_prim = 0;        // the address it is hammering
        uint64_t     s_invalid_since = 0;     // 0 = body was valid last check
        uint64_t     s_pending_until = 0;     // a press is latched until this

        // per-burst bookkeeping for the summary line
        float     s_start_y = 0.0f;
        uint64_t     s_burst_t0 = 0;
        uint64_t     s_last_summary = 0;      // rate limit for the log lines

        // STACKING. A press inside a live 400ms window used to merely restart
        // the clock on a pin already at full power - velocity did not change,
        // nothing could be felt, so a spam read as "only some presses
        // registered" even when every one was detected (the window never
        // expires under spam, so total height grew with TIME, not PRESSES).
        // Now each press during a live window ADDS to the pinned rise speed,
        // capped at 3x the slider: press -> power, press -> 2x, press -> 3x.
        // Every press is instantly perceptible; spam = visibly faster climb.
        float     s_stack_vy = 0.0f;
        unsigned  s_presses = 0;           // presses since the last summary line
        float     s_peak_vy = 0.0f;

        // jump power save/restore (see header comment)
        uintptr_t s_jp_hum = 0;            // the humanoid the save came from
        bool      s_jp_saved = false;
        float     s_jp_orig = 50.0f;
        bool      s_jp_orig_use = true;
        bool      s_jp_was_on = false;
        uint64_t     s_jp_restore_until = 0;
        uint64_t     s_jp_last = 0;

        float ClampPower() {
            float power = infinite_jump_power;
            if (!(power >= 1.0f)) power = 100.0f;      // also catches NaN
            if (power > 500.0f) power = 500.0f;
            return power;
        }

        bool ReadVelYandY(uintptr_t prim, float& vy, float& y) {
            float v[3] = {}, p[3] = {};
            const bool ok_v = read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, v, sizeof(v));
            const bool ok_p = read_raw(prim + Offsets::Primitive::Position, p, sizeof(p));
            vy = v[1]; y = p[1];
            return ok_v && ok_p;
        }

        void LogSummary(const char* how_ended, uint64_t now) {
            // one line per burst, never more than one per 2s: a space-mashing
            // player must not flood the console. The PRESS COUNT is the point:
            // it shows whether detection or the effect is the problem the next
            // time spam "doesn't register" (10 presses here + no extra height
            // = a game-side clamp; 3 presses here = detection missed 7).
            if (s_last_summary != 0 && (now - s_last_summary) <= 2000) return;
            s_last_summary = now;
            float vy = 0.0f, y = 0.0f;
            ReadVelYandY(s_burst_prim, vy, y);
            LogLine("infjump burst %s: %u presses, %lu writes in %ums, "
                    "rise speed peaked %.0f, y %.2f -> %.2f (%+.2f)",
                    how_ended, s_presses, mover::VelocityBurstWrites(),
                    (unsigned)(now - s_burst_t0), s_peak_vy,
                    s_start_y, y, y - s_start_y);
            s_presses = 0;
            s_peak_vy = 0.0f;
        }

        void FireBurst(const mover::Body& body, uint64_t now) {
            // Horizontal captured at press (the test writes x=z=0; keeping
            // ours preserves a running start). y STACKS per press while a
            // window is live - see s_stack_vy.
            float v[3] = {};
            if (!mover::ReadVelocity(v)) { v[0] = v[1] = v[2] = 0.0f; }
            const float power = ClampPower();
            if (!s_bursting) {
                s_stack_vy = power;                // fresh window: the slider
                s_burst_t0 = now;
                s_start_y = body.pos[1];
            } else {
                s_stack_vy += power;               // stacked: every press adds
                const float cap = 3.0f * power;
                if (s_stack_vy > cap) s_stack_vy = cap;
            }
            ++s_presses;
            if (s_stack_vy > s_peak_vy) s_peak_vy = s_stack_vy;
            v[1] = s_stack_vy;
            mover::VelocityBurst(body.primitive, v, kBoostMs);
            s_burst_prim = body.primitive;
            s_bursting = true;
            s_invalid_since = 0;
        }

        void TickJumpPower(const mover::Body& body, uint64_t now) {
            const uintptr_t hum = body.valid ? body.humanoid : 0;

            // the off-transition arms the restore window
            if (infinite_jump_enabled) {
                s_jp_was_on = true;
            } else if (s_jp_was_on) {
                s_jp_was_on = false;
                if (s_jp_saved) s_jp_restore_until = now + 3000;
            }

            const bool same = s_jp_saved && hum == s_jp_hum && is_valid_address(s_jp_hum);

            if (!infinite_jump_enabled) {
                // RESTORE: write the original back, kept down at 4Hz for 3s so
                // replication cannot race it back to our value. After the
                // window (or a respawn) hands off entirely - the server's own
                // copy is the original, so it finishes the job anyway.
                if (same && (int)(now - s_jp_restore_until) < 0) {
                    if ((now - s_jp_last) >= 250) {
                        s_jp_last = now;
                        if (Offsets::Humanoid::JumpPower) {
                            write<float>(s_jp_hum + Offsets::Humanoid::JumpPower, s_jp_orig);
                        }
                        if (Offsets::Humanoid::UseJumpPower) {
                            write<bool>(s_jp_hum + Offsets::Humanoid::UseJumpPower, s_jp_orig_use);
                        }
                    }
                } else if (!same && hum != 0) {
                    s_jp_saved = false;    // respawned while off: not ours
                    s_jp_hum = 0;
                }
                return;
            }

            if (!is_valid_address(hum)) return;
            if (!same) {
                // first write on this humanoid (or a new one after respawn):
                // remember ITS original before we touch it
                float orig = read<float>(hum + Offsets::Humanoid::JumpPower);
                if (!(orig > 0.0f && orig < 1000.0f)) orig = 50.0f;
                s_jp_orig = orig;
                s_jp_orig_use = read<bool>(hum + Offsets::Humanoid::UseJumpPower);
                s_jp_hum = hum;
                s_jp_saved = true;
            }
            if ((now - s_jp_last) >= 250) {
                s_jp_last = now;
                if (Offsets::Humanoid::JumpPower) {
                    write<float>(hum + Offsets::Humanoid::JumpPower, ClampPower());
                }
                if (Offsets::Humanoid::UseJumpPower) {
                    write<bool>(hum + Offsets::Humanoid::UseJumpPower, true);
                }
            }
        }
    }

    void RunInfiniteJump() {
        const uint64_t now = platform::NowMs();
        const mover::Body& body = mover::Local();

        // Jump power first, unconditionally: it is independent of the burst
        // mechanic and must run even during the flight stand-down below.
        TickJumpPower(body, now);

        // While flight is active, space is flight's rise key - and a pinned
        // vy would fight its velocity pin. Stand down and swallow the edge so
        // turning flight off mid-press doesn't fire a leftover burst.
        if (IsFlying()) {
            if (s_bursting) {
                mover::StopVelocityBurst();
                LogSummary("stopped (flight took over)", now);
                s_bursting = false;
            }
            s_space_was_down = g_input_space_down;
            s_pending_until = 0;
            return;
        }

        if (!infinite_jump_enabled) {
            if (mover::VelocityBurstActive()) {
                mover::StopVelocityBurst();
                LogLine("infjump: burst stopped - feature toggled off");
            }
            s_bursting = false;
            s_space_was_down = false;
            s_invalid_since = 0;
            s_pending_until = 0;
            return;
        }

        // Only a TEXT FIELD eats space (keybind capture, config name) - not an
        // open menu. The log lives on the debug page behind the menu, and the
        // menu-open gate is what made "nothing happens when i press space".
        const bool text = g_menu_text_input.load(std::memory_order_relaxed);
        const bool down = !text && g_input_space_down;

        // THE EDGE: level transition OR the sticky bit from the shared single
        // poll (g_input_space_edge - set when a press happened since the
        // previous iteration, even one that started AND finished inside a
        // loop stall). Whichever fires first wins; both consume nothing extra
        // because the poll happens once, in mover::Tick.
        const bool pressed = !text && ((down && !s_space_was_down) || g_input_space_edge);
        s_space_was_down = down;

        // ---- watch the live burst (the hold thread owns the hammering) -------
        if (s_bursting) {
            if (body.valid && body.primitive != s_burst_prim) {
                // a DIFFERENT VALID primitive is the signature of a real
                // respawn: the address we are hammering is a dead character's.
                mover::StopVelocityBurst();
                LogSummary("stopped (character changed)", now);
                s_bursting = false;
            } else if (!body.valid) {
                // unresolved: a cache hiccup (16-32ms, tolerate) or a real
                // disappearance (kills it after the grace window)
                if (s_invalid_since == 0) s_invalid_since = now;
                if ((now - s_invalid_since) > kInvalidGraceMs) {
                    mover::StopVelocityBurst();
                    LogSummary("stopped (player unresolved)", now);
                    s_bursting = false;
                }
            } else {
                s_invalid_since = 0;
            }

            // ended on its own clock - the normal, healthy exit
            if (s_bursting && !mover::VelocityBurstActive()) {
                LogSummary("ended", now);
                s_bursting = false;
            }
        }

        // ---- a latched press fires the moment the body resolves -------------
        if (s_pending_until) {
            if ((int)(now - s_pending_until) > 0) {
                s_pending_until = 0;                     // expired, press dropped
            } else if (body.valid) {
                s_pending_until = 0;
                FireBurst(body, now);
                return;
            }
        }

        if (!pressed) return;

        // ---- fire / re-fire / latch -----------------------------------------
        if (body.valid) {
            FireBurst(body, now);
            return;
        }

        // A press that lands inside a cache hiccup: extend the live burst on
        // its own address, or latch this press to fire when the body resolves
        // - either way it is not silently eaten (that was half the "spam and
        // only half fire" misses, the loop stalls were the other half).
        if (s_bursting) {
            mover::ExtendVelocityBurst(kBoostMs);
            s_invalid_since = 0;
        } else {
            s_pending_until = now + kPendingMs;
            if (s_last_summary == 0 || (now - s_last_summary) > 2000) {
                s_last_summary = now;
                LogLine("infjump: press during a cache hiccup - latched, firing "
                        "when the player resolves");
            }
        }
    }
}
