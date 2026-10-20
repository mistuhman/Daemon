#include <cstdint>
#include <cmath>
#include "flight.h"
#include "flight/stealth.h"
#include "players/humanoid_state.h"
#include "features/movement/mover.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"

// -----------------------------------------------------------------------------
//  Flight  --  PIN VELOCITY. The engine owns position. We never write it.
// -----------------------------------------------------------------------------
//  Three iterations got us here, each killed by a measurement:
//
//  1. POSITION HAMMER (hold-thread re-asserts position + zeroed velocity):
//     "the player rubberbands a bit and then snaps back to how we have our
//     flight". Even after the hold stopped echoing rotation and learned to
//     carry the path velocity, writing POSITION 66 times per step keeps
//     overwriting the engine's own integration mid-step - the assembly
//     transforms, render interpolation and replication history the engine
//     derives from its physics state are invalidated faster than they settle,
//     and every internal re-derive is a visible pop. You cannot out-write the
//     engine on its own output and stay smooth.
//
//  2. VELOCITY FROM THE FEATURE LOOP (the old "velocity-driven flight"):
//     sunk and snapped, because a feature-loop write is 1-2 writes per step,
//     torn mid-solve, and the humanoid damper eats a velocity it only sees
//     once per step.
//
//  3. WHAT THE DEBUG TEST PROVED (RunTest id 10, and infinite jump since):
//     a velocity HAMMERED from the dedicated thread - both motion blocks,
//     thousands of writes a second - pins the velocity, and the ENGINE
//     integrates it into motion that is perfectly smooth (+25 studs in 375ms,
//     monotonic, no revert anywhere in the timeline). Smooth because the
//     engine computes the position itself: everything downstream (assembly,
//     render, replication) is derived from ONE consistent integration instead
//     of fighting ours. And the replicator forwards velocity - the server
//     dead-reckons a plausible path instead of reconciling a teleporting one.
//
//  So flight feeds the same burst machinery infinite jump uses
//  (mover::VelocityBurst): direction x speed, re-fed every 25ms with a 500ms
//  deadline, so it never expires while flying and rides out cache hiccups
//  (which outlast the 25ms feed gap by an order of magnitude). AND THE
//  PHYSICS WORLD'S GRAVITY IS ZEROED while flying: the solver applies gravity
//  to a pinned vy=0 every step and integrates the result before our next
//  write resets it - a constant slow sink no write rate can remove ("i still
//  float down"). With the gravity term gone, no keys held = velocity 0 = a
//  TRUE hover, and motion is exactly the pinned vector. Stop() restores the
//  original gravity (verified before writing) and the fall resumes. A teleport
//  (higher owner) taking the body stops the burst for the duration of its
//  hold, and flight resumes from wherever the teleport landed you - there is
//  no cached position to re-seed anymore, which is the point.
//
//  THE STEALTH PROFILE (flight_stealth, on by default) removes the three things
//  about the above that a game can notice, all of them in features/flight/stealth.h
//  so the arithmetic has Linux cases rather than a feeling:
//
//    * NO GRAVITY WRITE. Zeroing Workspace.Gravity is a GLOBAL, replicated change:
//      every falling object in the map behaves differently for as long as you fly,
//      and a game script that reads workspace.Gravity sees 0. The stealth profile
//      leaves the world alone and instead adds 0.5*g*dt to the pinned vertical
//      velocity - the value that makes one physics step of gravity integrate to
//      zero net displacement - then trims that bias from the measured drift, so a
//      client whose physics step is not 60 Hz still hovers instead of sinking.
//    * A RAMP, NOT A STEP. Velocity approaches the commanded vector at a bounded
//      acceleration (flight_accel), so take-off, turns and stops are curves.
//    * SPEED AND CLIMB CAPS. flight_max_speed bounds the horizontal component and
//      flight_max_climb / flight_max_descent bound vertical separately, because a
//      body climbing at 300 studs/s is the least plausible thing this feature can
//      produce.
//
//  It also WATCHES ITS OWN WRITES: once a second it compares the velocity it pins
//  with the one the assembly holds. Two consecutive misses mean somebody else owns
//  the body (the server took it back), and the feature stops instead of writing
//  into a losing argument - the failure that precedes a rubberband, and sometimes a
//  disconnect.
//
//  NO PlatformStand while flying (see the comment at the claim): a pinned
//  velocity with the controller active is the measured-clean configuration;
//  a limp character tumbles. The burst hammer runs UNPACED - the working
//  pins (debug test, infinite jump) wrote 5-12x/ms; paced to the position
//  hold's 4kHz the damper clawed velocity back between writes and flight
//  "floated down" instead of hovering.
// -----------------------------------------------------------------------------

namespace features {

    namespace {

        // Feed cadence: direction changes apply within one feed tick, and the
        // inline write in VelocityBurst means each feed also writes once
        // itself. Feeding every feature-loop iteration (600-1000Hz) would just
        // burn syscalls re-asserting an unchanged vector.
        constexpr uint32_t kFeedMs = 25;

        // The burst deadline is deliberately longer than a cache hiccup
        // (16-32ms) and matches flight's own give-up tolerance: while the body
        // is unresolved the burst keeps pinning the last velocity, and if the
        // body never comes back the burst - and flight - both end.
        constexpr uint32_t kBurstMs = 500;

        // How long the local player may fail to resolve before flight gives up
        // (a hiccup lasts 16-32ms; a detach lasts forever).
        constexpr uint32_t kUnresolvedMs = 500;

        // Roblox's default physics step. Only used to SEED the hover bias: the
        // trim measures the real drift and corrects a client that steps at a
        // different rate, so this number being wrong costs a second of settling,
        // not a permanent sink.
        constexpr float kPhysicsStep = 1.0f / 60.0f;

        // The ramp's clock floor: a feature-loop stall (a config save, a resize)
        // must not turn one frame of catch-up into a visible lurch.
        constexpr float kMaxRampDt = 0.1f;

        // Velocity the assembly must hold for the burst to still be ours. 15
        // covers the gravity term (g*dt = 3.3) and the step boundary; 35 % of the
        // command covers a fast mover whose write lands a step late. Together they
        // are tight enough that a body stopped dead is a miss rather than a pass.
        constexpr float kHeldTol = 15.0f;
        constexpr float kHeldFrac = 0.35f;

        instance s_cam{};
        uint64_t    s_cam_at = 0;
        bool     s_active = false;
        bool     s_key_was_down = false;
        uint64_t    s_last_feed = 0;
        uint64_t    s_unresolved_since = 0;

        // ---- the stealth profile's state ----------------------------------
        float    s_vel[3] = { 0, 0, 0 };   // the ramped command (no bias)
        uint64_t s_vel_at = 0;             // when it was last ramped
        float    s_trim = 0.0f;            // measured correction to the hover bias
        bool     s_trim_have = false;
        float    s_trim_y = 0.0f;
        uint64_t s_trim_at = 0;
        uint64_t s_watch_at = 0;           // the write-verification clock
        int      s_watch_miss = 0;
        bool     s_watch_logged = false;
        bool     s_state_logged = false;
        bool     s_profile_logged = false;

        // ---- gravity, zeroed while flying ---------------------------------
        // The hover pin alone loses to arithmetic: the solver applies
        // workspace gravity to our pinned vy=0 EVERY step and integrates the
        // result before our next write resets it - a constant sink of a
        // couple of studs per second no write rate can remove ("i still float
        // down"). Removing gravity from the physics world while flying
        // removes the term entirely: pin 0 = a true hover, pin v = clean
        // motion. Restored on stop (verified against the current value, so a
        // world that changed underneath us is left alone).
        uintptr_t s_world = 0;
        bool      s_grav_saved = false;
        int       s_grav_slot = -1;      // which float of the field is gravity
        float     s_grav_orig = 0.0f;

        bool LooksLikeGravity(float v) {
            return (v < -50.0f && v > -600.0f) ||   // (0, -196.2, 0) vector Y
                   (v > 50.0f  && v < 600.0f);      // positive scalar
        }

        // Locate the gravity field without changing it: the physics world may
        // store a scalar (~196.2) or a vector (0, -196.2, 0), so only a slot
        // that READS like gravity is treated as one. Returns the magnitude.
        bool ReadGravity(uintptr_t& world, int& slot, float& magnitude) {
            instance dm = game::ReadDatamodel(g_base_address);
            if (!dm.is_valid()) return false;
            instance ws = dm.read_service("Workspace");
            if (!ws.is_valid()) return false;
            const uintptr_t w = read<uintptr_t>(ws.address + Offsets::Workspace::World);
            if (!is_valid_address(w)) return false;
            float g[3] = {};
            if (!read_raw(w + Offsets::World::Gravity, g, sizeof(g))) return false;
            int found = -1;
            if (LooksLikeGravity(g[1])) found = 1;
            else if (LooksLikeGravity(g[0])) found = 0;
            if (found < 0) return false;
            world = w;
            slot = found;
            magnitude = fabsf(g[found]);
            return true;
        }

        // The gravity the stealth profile compensates against, cached for a
        // second: the field is either static or owned by the gravity feature,
        // and both move slowly. 196.2 is Roblox's default and the value the
        // bias would use in a place that leaves gravity alone.
        float StealthGravity() {
            static uint64_t s_at = 0;
            static float    s_mag = 196.2f;
            const uint64_t now = platform::NowMs();
            if (s_at != 0 && now - s_at < 1000) return s_mag;
            s_at = now;
            uintptr_t w = 0;
            int slot = -1;
            float mag = 0.0f;
            if (ReadGravity(w, slot, mag) && mag > 0.0f) s_mag = mag;
            return s_mag;
        }

        bool ZeroGravity() {
            if (s_grav_saved) return true;
            uintptr_t world = 0;
            int slot = -1;
            float mag = 0.0f;
            if (!ReadGravity(world, slot, mag)) {
                LogLine("flight: gravity field not recognised - flying on the velocity pin "
                        "alone");
                return false;
            }
            float g[3] = {};
            if (!read_raw(world + Offsets::World::Gravity, g, sizeof(g))) return false;
            s_grav_slot = slot;
            s_grav_orig = g[slot];
            s_world = world;
            write<float>(world + Offsets::World::Gravity + (uintptr_t)slot * 4u, 0.0f);
            s_grav_saved = true;
            return true;
        }

        void RestoreGravity() {
            if (!s_grav_saved) return;
            // verify before restoring: if the slot no longer reads ~0, the
            // world changed (place hop, a rebuilt DataModel) and the old
            // address is nobody's to write
            if (is_valid_address(s_world)) {
                float cur = read<float>(s_world + Offsets::World::Gravity +
                                              (uintptr_t)s_grav_slot * 4u);
                if (cur > -1.0f && cur < 1.0f)
                    write<float>(s_world + Offsets::World::Gravity +
                                         (uintptr_t)s_grav_slot * 4u, s_grav_orig);
            }
            s_grav_saved = false;
            s_world = 0;
            s_grav_slot = -1;
        }

        instance GetCamera() {
            const uint64_t now = platform::NowMs();
            if (s_cam.is_valid() && (now - s_cam_at) < 1000) return s_cam;
            instance dm = game::ReadDatamodel(g_base_address);
            if (!dm.is_valid()) return instance{};
            instance ws = dm.read_service("Workspace");
            if (!ws.is_valid()) return instance{};
            s_cam = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
            s_cam_at = now;
            return s_cam;
        }

        void Stop() {
            if (s_active) {
                mover::StopVelocityBurst();
                mover::Release(mover::Flight);
            }
            RestoreGravity();                   // the fall resumes WITH gravity
            s_active = false;
            s_unresolved_since = 0;
            s_last_feed = 0;
            s_vel[0] = s_vel[1] = s_vel[2] = 0.0f;
            s_vel_at = 0;
            s_trim = 0.0f;
            s_trim_have = false;
            s_trim_at = 0;
            s_watch_at = 0;
            s_watch_miss = 0;
            s_watch_logged = false;
            s_state_logged = false;
            s_profile_logged = false;
        }

        // Read the body's vertical speed drift while hovering and nudge the bias.
        // Only while hovering: while the player is climbing or diving the drift is
        // the input, not an error.
        void TrimHover(uintptr_t primitive, uint64_t now) {
            const float intend = fabsf(s_vel[0]) + fabsf(s_vel[1]) + fabsf(s_vel[2]);
            if (intend > 0.05f) { s_trim_have = false; s_trim_at = 0; return; }
            if (s_trim_at != 0 && now - s_trim_at < 500) return;
            float pos[3] = {};
            if (!read_raw(primitive + Offsets::Primitive::Position, pos, sizeof(pos))) {
                s_trim_have = false;
                s_trim_at = 0;
                return;
            }
            if (s_trim_have) {
                const float dt = (float)(now - s_trim_at) / 1000.0f;
                if (dt > 0.05f) {
                    const float drift = (pos[1] - s_trim_y) / dt;   // + is creeping up
                    const float base = flights::HoverBias(StealthGravity(), kPhysicsStep);
                    s_trim = flights::TrimBias(s_trim, drift, 0.35f, 0.2f, -base, base);
                }
            }
            s_trim_y = pos[1];
            s_trim_have = true;
            s_trim_at = now;
        }

        void LogProfileLine(float gravity) {
            LogLine("flight: stealth profile - %.0f studs/s cap, %.0f studs/s2 ramp, climb "
                    "%.0f/descend %.0f, gravity %.1f LEFT ALONE (hover bias %.2f)",
                    flight_max_speed, flight_accel, flight_max_climb, flight_max_descent,
                    gravity, flights::HoverBias(gravity, kPhysicsStep));
        }

        void LogProfile(float gravity) {
            if (s_profile_logged) return;
            s_profile_logged = true;
            if (flight_stealth) {
                LogProfileLine(gravity);
            } else {
                LogLine("flight: LEGACY profile - Workspace.Gravity is zeroed while flying and "
                        "velocity steps instantly. That gravity write is a global, replicated "
                        "change and is the most likely thing a game notices; the stealth switch "
                        "keeps the world alone");
            }
        }



    } // namespace

    bool IsFlying() { return s_active; }

    void RunFlight() {
        const uint64_t now = platform::NowMs();

        if (!flight_enabled) {
            Stop();
            s_key_was_down = false;
            return;
        }

        const bool key_down = (flight_keybind == 0) ||
                              (platform::IsKeyDown(flight_keybind));
        bool want = s_active;
        if (flight_keybind == 0)      want = true;
        else if (flight_hold_mode)    want = key_down;
        else if (key_down && !s_key_was_down) want = !s_active;
        s_key_was_down = key_down;

        if (!want) { Stop(); return; }

        const mover::Body& body = mover::Local();

        // A transient cache miss does not stop flight: the burst's own 500ms
        // deadline keeps the last velocity pinned through it. Only a sustained
        // absence (a detach, or a respawn mid-flight - which changes the
        // primitive and therefore the address the burst is hammering) ends it.
        if (!body.valid) {
            if (!s_active) return;              // cannot START without a body
            if (s_unresolved_since == 0) s_unresolved_since = now;
            if ((now - s_unresolved_since) > kUnresolvedMs) {
                // The line to correlate a disconnect with: what was running when the
                // body stopped resolving. The profile is the only thing that differs
                // between a session that survives and one that does not.
                LogLine("flight: the local player stopped resolving after %llums of flight "
                        "(stealth=%s, cap %.0f, ramp %.0f, climb %.0f). If the client "
                        "disconnected, this is the line to correlate it with",
                        (unsigned long long)(now - s_unresolved_since),
                        flight_stealth ? "on" : "off", flight_max_speed, flight_accel,
                        flight_max_climb);
                Stop();
                return;
            }
            return;                             // riding it out: burst still live
        }
        s_unresolved_since = 0;

        // Losing the body to a higher owner (a teleport) stops the burst for
        // the duration of its hold - our pinned velocity would fight its
        // position writes. Flight re-claims and resumes when the hold ends.
        if (!mover::Claim(mover::Flight)) {
            if (mover::VelocityBurstActive() && mover::Current() >= mover::Teleport)
                mover::StopVelocityBurst();
            s_active = false;
            return;
        }
        const bool was_active = s_active;
        s_active = true;
        // Stealth: the world's gravity is READ (for the bias), never written.
        // Legacy: it is zeroed for the duration, the old behaviour, kept behind
        // the switch because it is what makes the hover `exactly` still.
        const float gravity = flight_stealth ? StealthGravity() : 0.0f;
        if (flight_stealth) RestoreGravity();
        else ZeroGravity();
        if (!was_active) LogProfile(gravity);
        // NO PlatformStand here, deliberately. It was right for the old
        // position-hold flight (a frozen body being moved), but with a pinned
        // velocity it makes the character LIMP - a ragdoll being dragged by
        // its root part, tumbling off every bump it touches. That was "you
        // just bounce around". Infinite jump pins velocity through the same
        // blocks with the humanoid controller fully active and no PlatformStand,
        // and it is the one mechanism measured clean - so the controller stays
        // on, the character stays stood, and the hammer simply out-writes the
        // controller's damping (the damper sees our velocity once per step at
        // most; the thread re-asserts it thousands of times).

        float look[3] = { 0, 0, -1 }, right[3] = { 1, 0, 0 };
        instance cam = GetCamera();
        if (cam.is_valid()) {
            float rot[9] = {};
            if (read_raw(cam.address + Offsets::Camera::Rotation, rot, sizeof(rot))) {
                look[0] = -rot[2]; look[1] = -rot[5]; look[2] = -rot[8];
                right[0] = rot[0]; right[1] = rot[3]; right[2] = rot[6];
            }
        }

        float dir[3] = {};
        // Steering is gated on TEXT CAPTURE, not on the menu being open: typing
        // in a keybind box or the config name field must not fly you around,
        // but an open menu must not freeze the controls either.
        const bool text = g_menu_text_input.load(std::memory_order_relaxed);
        if (!text) {
            auto add = [&](const float v[3], float sc) {
                dir[0] += v[0] * sc; dir[1] += v[1] * sc; dir[2] += v[2] * sc;
            };
            if (platform::IsKeyDown('W')) add(look, 1.0f);
            if (platform::IsKeyDown('S')) add(look, -1.0f);
            if (platform::IsKeyDown('D')) add(right, 1.0f);
            if (platform::IsKeyDown('A')) add(right, -1.0f);
            // the shared sample, not a second GetAsyncKeyState call: any call
            // on the key consumes the edge latch infinite jump's press
            // detection depends on (see globals.h)
            if (g_input_space_down) dir[1] += 1.0f;
            if (platform::IsKeyDown(platform::key::LCONTROL) ||
                platform::IsKeyDown(platform::key::LSHIFT)) dir[1] -= 1.0f;
        }

        // THE WHOLE FEATURE: velocity = direction x speed, pinned. The engine
        // integrates it - we never write position.
        float target[3] = { 0.0f, 0.0f, 0.0f };
        const float mag = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (mag > 0.0001f) {
            const float k = flight_value / mag;
            target[0] = dir[0] * k; target[1] = dir[1] * k; target[2] = dir[2] * k;
        }
        if (flight_stealth) {
            flights::ClampHorizontal(target, flight_max_speed);
            flights::ClampVertical(target, flight_max_climb, flight_max_descent);
        }

        // The commanded vector: instant in the legacy profile, ramped in the
        // stealth one. The ramp's clock is the real elapsed time, floored and
        // capped so a feature-loop stall cannot turn into a lurch.
        float dt = s_vel_at == 0 ? 0.0f : (float)(now - s_vel_at) / 1000.0f;
        if (!(dt > 0.0f) || dt > kMaxRampDt) dt = kMaxRampDt;
        s_vel_at = now;
        if (flight_stealth) {
            if (s_last_feed == 0) { s_vel[0] = target[0]; s_vel[1] = target[1]; s_vel[2] = target[2]; }
            else flights::Approach(s_vel, target, flight_accel, dt);
            TrimHover(body.primitive, now);
        } else {
            s_vel[0] = target[0]; s_vel[1] = target[1]; s_vel[2] = target[2];
        }

        // THE STATE WATCH. A body in Ragdoll / FallingDown / GettingUp / Seated while
        // we pin a velocity is a picture the engine will not draw for a character that
        // is playing, and a server that has taken the body back often shows it here
        // BEFORE the position starts to rubberband. Reported once per flight, with the
        // state id: it is the earliest observable half of the same failure the write
        // watch sees later.
        if (flight_stealth && !s_state_logged) {
            const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
            const uintptr_t hum = lp.valid ? lp.humanoid_address : 0;
            if (is_valid_address(hum)) {
                const uintptr_t sm = read<uintptr_t>(hum + Offsets::Humanoid::HumanoidState);
                if (is_valid_address(sm)) {
                    const int id = read<int>(sm + Offsets::Humanoid::HumanoidStateID);
                    if (hstate::Plausible(id) && hstate::IsKnockedDown(id)) {
                        s_state_logged = true;
                        LogLine("flight: the Humanoid reports state %d (a knockdown) while we "
                                "pin a velocity - the game is animating a body that is not "
                                "standing, which usually means it has taken the character back",
                                id);
                    }
                }
            }
        }

        // The pinned vector: the command, plus - in stealth - the bias that makes
        // one physics step of gravity integrate to zero displacement. Gravity is
        // still there; the body is held against it, which is what a flying body
        // looks like.
        float v[3] = { s_vel[0], s_vel[1], s_vel[2] };
        if (flight_stealth) {
            const float base = flights::HoverBias(gravity, kPhysicsStep);
            float bias = base + s_trim;
            if (bias < 0.0f) bias = 0.0f;
            if (bias > base * 2.0f + 0.5f) bias = base * 2.0f + 0.5f;
            v[1] += bias;
        }

        // THE WRITE WATCH. Once a second, read back what the assembly actually
        // holds. This is DIAGNOSTIC, not a kill switch: a collision, a ladder or a
        // seat can hold the body still while we fly into it, and stopping there
        // would be a bug the user blames on the feature. What it does is say, once
        // per flight, that the body stopped obeying - which is what having the
        // network owner revoked looks like, and the line a disconnect report needs.
        if (flight_stealth && !s_watch_logged &&
            (s_watch_at == 0 || (now - s_watch_at) > 1000)) {
            s_watch_at = now;
            float got[3] = {};
            if (mover::ReadVelocity(got) &&
                !flights::VelocityHeld(v, got, kHeldTol, kHeldFrac)) {
                if (++s_watch_miss >= 3) {
                    s_watch_logged = true;
                    LogLine("flight: the assembly has not held the pinned velocity for 3 checks "
                            "(want %.0f,%.0f,%.0f, read %.0f,%.0f,%.0f). If you are also "
                            "rubberbanding, the server has taken the body back - that is a "
                            "step before a kick, and continuing to fly is what makes it worse",
                            v[0], v[1], v[2], got[0], got[1], got[2]);
                }
            } else {
                s_watch_miss = 0;
            }
        }

        if (s_last_feed == 0 || (now - s_last_feed) >= kFeedMs) {
            s_last_feed = now;
            mover::VelocityBurst(body.primitive, v, kBurstMs);
        }
    }
}
