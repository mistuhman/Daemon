#include <cstdint>
#include <cmath>
#include "flight.h"
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

        instance s_cam{};
        uint64_t    s_cam_at = 0;
        bool     s_active = false;
        bool     s_key_was_down = false;
        uint64_t    s_last_feed = 0;
        uint64_t    s_unresolved_since = 0;

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

        bool ZeroGravity() {
            if (s_grav_saved) return true;
            instance dm = game::ReadDatamodel(g_base_address);
            if (!dm.is_valid()) return false;
            instance ws = dm.read_service("Workspace");
            if (!ws.is_valid()) return false;
            const uintptr_t world =
                read<uintptr_t>(ws.address + Offsets::Workspace::World);
            if (!is_valid_address(world)) return false;
            float g[3] = {};
            if (!read_raw(world + Offsets::World::Gravity, g, sizeof(g))) return false;
            // identify the layout before writing anything: the physics world
            // may store a scalar (~196.2) or a vector (0, -196.2, 0). Only a
            // slot that READS like gravity is zeroed, and only that one.
            int slot = -1;
            if (LooksLikeGravity(g[1])) slot = 1;
            else if (LooksLikeGravity(g[0])) slot = 0;
            if (slot < 0) {
                LogLine("flight: gravity field not recognised (%.1f, %.1f, %.1f) "
                        "- flying on the velocity pin alone", g[0], g[1], g[2]);
                return false;
            }
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
            if ((now - s_unresolved_since) > kUnresolvedMs) { Stop(); return; }
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
        s_active = true;
        // The engine's gravity term is removed for the duration (see
        // ZeroGravity): the hover pin cannot beat the solver's own
        // integrate-then-reset arithmetic, so the term is deleted instead.
        ZeroGravity();
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
        // integrates it - we never write position. No keys held pins velocity
        // 0, which is a hover: the pin counteracts gravity (a free pin at 0 is
        // still a pin). This is the same write pattern the debug test proved
        // smooth and infinite jump flies with.
        float v[3] = { 0.0f, 0.0f, 0.0f };
        const float mag = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (mag > 0.0001f) {
            const float k = flight_value / mag;
            v[0] = dir[0] * k; v[1] = dir[1] * k; v[2] = dir[2] * k;
        }

        if (s_last_feed == 0 || (now - s_last_feed) >= kFeedMs) {
            s_last_feed = now;
            mover::VelocityBurst(body.primitive, v, kBurstMs);
        }
    }
}
