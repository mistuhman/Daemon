#pragma once
#include <cstdint>

// -----------------------------------------------------------------------------
//  features/movement/mover  --  one owner for the local character's physics
// -----------------------------------------------------------------------------
//  Every movement feature used to write the same four fields (the two CFrames,
//  AssemblyLinearVelocity, Humanoid::PlatformStand, Primitive::Flags) on its own
//  schedule, each with its own save/restore. That is the actual source of the
//  rubber-banding:
//
//    1. THE LOOP IS NOT 60 Hz.  FeatureLoop() is `platform::SleepMs(1)`, so it runs 500-1000
//       times a second, while Roblox steps physics/replication at 60. Flight and
//       freecam integrated position with a hardcoded `dt = 0.016f` *per loop
//       iteration*, so they moved ~16x faster than the slider said. 70 studs/s
//       became ~1100 studs/s, which the engine resolves by snapping you back.
//    2. WRITES TORE.  ~16 writes landed inside every physics step, several of
//       them mid-solve, and flight's push wrote rotation and position as separate
//       write-syscall calls with a read in between - so the engine could see
//       frame N's rotation with frame N+1's position.
//    3. NOBODY OWNED ANYTHING.  Teleport anchored the root part, noclip saved and
//       restored Primitive::Flags around it, flight set PlatformStand and infinite
//       jump froze the humanoid state machine - each restoring what it thought it
//       had saved, in an interleaving that left the character anchored, ragdolled
//       or state-machine-less after the feature was switched off.
//
//  So: one tick function, one real clock, one paced write rate, one save/restore,
//  and a priority so that at most one feature drives the body at a time.
//
//  Call Tick() once at the top of FeatureLoop, before any feature runs.
// -----------------------------------------------------------------------------

namespace features {
namespace mover {

// Higher wins. A claim by an equal or higher owner takes the body over.
enum Owner : int {
    None     = 0,
    Freecam  = 1,   // does not drive the body, only freezes it
    Jump     = 2,
    Flight   = 3,
    Teleport = 4,
};

struct Body {
    bool      valid     = false;
    uintptr_t primitive = 0;   // HumanoidRootPart's Primitive
    uintptr_t humanoid  = 0;
    float     pos[3]{};
};

// Refreshes the clock, the body, and drops every saved value if the character
// changed (a respawn hands out new instances - restoring into the old ones is
// how PlatformStand got left on).
void Tick();

// Real seconds since the previous Tick(), clamped to [0, 0.1]. The clamp is for
// the case that matters: a stall (attach, alt-tab, a slow read) must not turn
// into one enormous integration step, which is a teleport the engine rejects.
float Dt();

// True on at most one tick per paced physics interval.
//
// *** DO NOT GATE POSITION WRITES ON THIS. *** Measured, 4 runs of the
// continuous-write test: hammering both motion blocks flat-out held the
// character at the target for the full 500ms (1248-1302 writes), while a single
// write reverted within 50ms every time. The engine restores position from a
// master copy every step, so holding a position is a CONTINUOUS contest - one
// write per 16ms loses it, and pacing was the single biggest mistake in this
// codebase. The two glitch samples in those runs (vel.y -11.44 and -58.86, both
// recovering by the next sample) are the engine briefly winning the race and
// being overwritten again, which is exactly the fight we want to be winning.
//
// It survives only for things that are genuinely per-step and idempotent.
bool PhysicsStep();

const Body& Local();

bool Claim(Owner who);
void Release(Owner who);
Owner Current();

// The primitive holds TWO identical 0x48 motion blocks (rotation, position,
// linear velocity, angular velocity - see the table in offsets.h). Every one of
// these reads/writes the block WHOLE, and writes it to BOTH copies: the engine
// re-syncs from the second one, so updating a single field in a single block is
// reverted within a step. That is what the "dY 10.00 / dY 0.00" alternation in
// the position write test was.
bool ReadCFrame(float rot[9], float pos[3]);
// Sets position (and rotation, unless rot is null) and zeroes BOTH velocities:
// a teleport has no motion through the space it skipped.
void WriteCFrame(const float pos[3], const float rot[9]);
bool ReadVelocity(float v[3]);
// Sets linear velocity, leaving position exactly as read - a velocity write must
// not re-assert a stale position against the engine's own integration.
void WriteVelocity(const float v[3]);
void WriteVelocityAndSpin(const float lin[3], const float ang[3]);

// Saved once, restored by Release()/respawn/detach. Only the current owner may
// set these.
void PlatformStand(Owner who, bool on);
void FreezeWalk(Owner who, bool on);

// Primitive::Flags ANCHORED, saved and restored like the rest.
//
// This is the one the survival test settled. An unanchored root part is owned by
// the engine's solver: it recomputes position from the assembly every step, so a
// position write is reverted within ~50ms no matter how it is written or how
// often. Anchoring hands ownership to us for the duration - that is what made
// teleports work in the first place, and removing it was my mistake.
//
// It must be transient. A permanently anchored character cannot walk, and the
// noclip feature writes the same byte, which is why it belongs here (one owner,
// one save) instead of in each feature.
void Anchor(Owner who, bool on);
bool IsAnchored();

// Measured position-write throughput (writes/ms), averaged over the last ~500ms.
// Holding a position is a race against the engine's per-step restore, and the
// rate we achieve varies by ~200x with system load - which is precisely why the
// movement features worked in some sessions and not others. Exposed so the UI
// can show it instead of leaving it to guesswork.
float WritesPerMs();

// Hold a world position until ReleaseHold. The write is re-asserted from a
// DEDICATED THREAD, not from the feature loop.
//
// Measured across four identical runs, position writes varied ~200x (17127 down
// to 88 per 500ms) purely with system load, and the run that achieved only ~3
// writes per physics step visibly lost the position. The feature loop is one
// platform::SleepMs(1) thread shared with ESP, aimbot and network polling, so a slow pass
// anywhere steals exactly the writes movement needs. A hold that must win a
// per-step race cannot be scheduled behind all of that.
// Returns true when the hold actually armed (body valid, motion block
// readable). A silent no-op return here is how a teleport used to "succeed"
// while nothing was armed - callers must treat false as failure.
// `vel` is the velocity written alongside the position (both motion blocks,
// continuously). nullptr = zero - right for a teleport (no motion through the
// space skipped). FLIGHT must pass its path velocity: a zero-velocity hold
// while moving makes the replicator report "standing still", so the server's
// copy lags behind and every restore yanks the character back along the path
// until the next write snaps it forward - the measured rubberband-then-snap.
bool HoldPosition(Owner who, const float pos[3], const float vel[3] = nullptr);
void ReleaseHold(Owner who);

// Hold a CAMERA CFrame (rotation 9 + position 3, exactly cam::WriteCFrame's
// 12-float layout) re-asserted from the dedicated hold thread. Freecam used
// to hammer its CFrame from the feature loop - the one platform::SleepMs(1) thread shared
// with ESP, aimbot and servers - so under load the write rate collapsed, the
// game's camera module re-asserted between our writes, and the view stuttered
// or orbited the character. Same cure as every other movement write: the hold
// thread owns the race. Paced with the position holds (indefinite by nature).
// Re-armed with a fresh CFrame every feature tick; ReleaseCameraHold on stop.
// workspace_address (optional) arms the FLAP GUARD: the hold thread watches
// Workspace.CurrentCamera at 10kHz and, if the game flips it to another
// camera even for one frame, writes our pose into THAT camera and pins the
// pointer back. Pass cam::WorkspaceAddr().
void HoldCameraCFrame(uintptr_t camera_address, const float cf[12],
                      uintptr_t workspace_address = 0);
// True while the camera hold decided the field is unwinnable and is feeding
// 30Hz setpoints to the game's writer instead of hammering (the rendered view
// is then the writer's echo of our setpoint, expected to sit a few studs off).
bool CameraHoldFeeding();

// After freecam hands the camera back: re-assert the RESTORED type+subject
// (never the CFrame - the game owns the pose again) for a short window, so a
// last frame from the starved default camera module cannot leave the camera
// Scriptable/subjectless after our restore.
void GuardCameraFields(uintptr_t camera_address, int type, uintptr_t subject,
                       uint32_t timeout_ms);
void ReleaseCameraHold();

// Hold a DOWNWARD-SPEED CAP on a primitive until ReleaseFallHold.
//
// A single velocity clamp from the feature loop is a coin flip: the humanoid
// damper rewrites vertical velocity every physics step, and a lone write only
// survives if it lands just after a step boundary - which is why no-fall
// "did nothing" while every hammered write in this file works. The hold thread
// re-reads the linear velocity every paced iteration and rewrites the DOWNWARD
// component only, so horizontal motion and the engine's own integration are
// untouched: this is a floor on fall speed, not a velocity pin.
//
// `cap` is studs/s (>= 0). 0 holds the downward component AT ZERO - a true
// hover, the setting the feature's slider calls "no fall". A cap < 0 (or an
// invalid primitive) releases. The hold carries its own
// primitive address and survives the cache's transient misses; call it every
// pass to keep the address current (a respawn re-arms it on the new body).
// Hold the body ABOVE a world Y, from the hold thread.
//
// This is what "no fall" actually needs. A velocity clamp cannot express it: the
// solver integrates gravity into the POSITION every step, and clamping the
// velocity only changes how fast the position falls - which is why the feature
// could only ever be a terminal velocity, and why "no fall still doesn't work"
// was a correct reading of it. Pinning the position's Y at the hold thread's
// ~66-writes-per-physics-step rate is the same mechanism that makes a teleport
// hold work, and it is unconditional: below `y`, the body is put back.
//
// The caller owns the anchor: it moves the anchor down with the body while the
// body is walking (so slopes and stairs still work) and freezes it when a real
// fall starts.
void HoldVertical(uintptr_t primitive, float y);
void ReleaseVertical();
unsigned long VerticalHoldWrites();

void HoldFallSpeed(uintptr_t primitive, float cap);
void ReleaseFallHold();
// Clamps the hold thread has applied since the process started (for the log).
unsigned long FallHoldWrites();

// Fire a hammered-velocity BURST on a specific primitive -- the debug test
// (RunTest id 10) as a mechanism, made structural.
//
// A single velocity write is a coin flip: two identical runs from the same spot
// gave "climbed -0.01" and "climbed +25.24", because the humanoid controller
// damps vertical velocity every step and a lone write survives only if it lands
// just after a step boundary. Every hammered run climbed cleanly instead
// (+30-34 studs in 375ms, velocity pinned at the written value).
//
// THE CRITICAL PROPERTY: the burst is AUTONOMOUS. The caller hands over a
// primitive address, a velocity and a duration; the hold thread then writes the
// linear velocity into BOTH motion blocks in a tight loop until the deadline
// passes, and stops BY ITSELF. Nothing else in the mover can tear it down --
// specifically, mover::Tick()'s respawn guard does NOT touch it.
//
// That guard is why a burst has to work this way at all. Tick() compares the
// cache's local primitive against the last one it saw and demolishes every hold
// and owner when they differ -- including when a cache pass simply FAILS (any
// one of ~20 chained reads in fetch_local hiccups and it publishes
// hrp_primitive = 0 for that pass). Those hiccups happen every second or so
// under load, so anything routed through ownership dies within milliseconds of
// starting, nearly every time. That is why every previous infinite jump
// mechanism "never worked" while the debug test -- which captures the primitive
// once at click and then ignores the world -- always did. A burst that survives
// must be equally indifferent: it carries its own primitive address and its own
// clock, and the policy (is that address still THE character?) belongs to the
// feature that fired it, not to the cache's per-pass mood.
//
// Deadline semantics: the clock starts when this is called; call it again to
// restart the clock (the mid-air re-fire -- the debug test's button clicked
// again mid-run). PINS velocity for the duration, so the rise is linear; when
// the window ends the engine integrates gravity from whatever speed is left.
void VelocityBurst(uintptr_t primitive, const float v[3], uint32_t duration_ms);
// Restart the clock on the LIVE burst, keeping its primitive and velocity.
void ExtendVelocityBurst(uint32_t duration_ms);
// Kill the live burst now (feature toggled off, or the character it was fired
// at is gone for good).
void StopVelocityBurst();
bool VelocityBurstActive();
// Writes achieved by the current/last burst, for the log.
unsigned long VelocityBurstWrites();

// SPIN THE BODY. Writes an angular velocity to both motion blocks from the hold
// thread, unpaced, until ReleaseSpin(). This is the physics a fling is made of:
// a character spun at a few hundred rad/s sweeps its limbs through everything
// nearby and the solver hands that energy to whatever it touches, so other
// players get launched out of the way. Linear velocity is left alone - spinning
// must not pin you in place or stop a jump.
//
// `angular` is rad/s in world axes. Unpaced on purpose (see the burst note): the
// humanoid damper rewrites angular velocity every step, so a paced spin decays
// between writes. Called every feature-loop pass, like the other holds.
void HoldSpin(uintptr_t primitive, const float angular[3]);
void ReleaseSpin();
bool SpinActive();

} // namespace mover
} // namespace features
