#include <atomic>
#include <cmath>
#include <mutex>
#include "mover.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"

namespace features {
namespace mover {
namespace {

// Roblox steps its physics/replication pipeline at 60 Hz. Writing the root part
// faster than that does not move you faster - it just means most writes land
// mid-solve and get discarded, and the replicator sees a position history it
// cannot reconcile, which is what the server corrects you for. One write per
// step, on the same cadence the engine consumes them, is the whole fix.
constexpr uint64_t kStepMs = 16;

// -----------------------------------------------------------------------------
//  WRITE THROUGHPUT IS THE HIDDEN VARIABLE
// -----------------------------------------------------------------------------
//  Four runs of the identical continuous-write test, same build, same machine:
//
//    writes/500ms | per 16.7ms step | position held?
//    -------------+-----------------+----------------------------------
//       17127     |      572        | perfectly, flat
//        1248     |       41        | yes, 2 brief glitches
//         810     |       27        | yes
//          88     |        2.9      | NO - fell most of the run
//
//  Same code, ~200x spread, and the run that failed is the one that only managed
//  ~3 writes per physics step. That is the whole explanation for "it works
//  sometimes": we are in a race with the engine's restore, and how often we get
//  to run is not something the feature controls. WriteProcessMemory took ~29us
//  per pair in the fast run and ~5.7ms in the slow one - a syscall whose cost
//  depends on system load, other processes, and the scheduler.
//
//  So a correct implementation cannot just "write in the feature loop and hope".
//  It needs (a) to measure what it is actually achieving, and (b) to not be at
//  the mercy of a 1ms Sleep shared with every other feature.
constexpr int kMinWritesPerStep = 8;   // below this the contest is unreliable

// Rolling throughput, so features and the log can see the real rate.
uint64_t s_wr_t0_ns = 0;    // (ns; was QPC ticks before the platform seam)
int   s_wr_count = 0;
float s_wr_per_ms = 0.0f;
bool  s_wr_warned = false;

uint64_t s_last_ns = 0;     // (ns; QPC frequency belonged to the platform)
float s_dt = 0.0f;

uint64_t s_next_step = 0;
bool  s_step_now = false;

Body s_body{};
Owner s_owner = None;

// Saved state, and the exact address it was saved from. Restoring a value into
// an address that has since been recycled by a respawn is worse than not
// restoring at all, so every save carries its owner address.
uintptr_t s_ps_hum = 0;
bool      s_ps_saved = false;
bool      s_ps_value = false;

uintptr_t s_walk_hum = 0;
bool      s_walk_saved = false;
float     s_walk_value = 16.0f;
bool      s_walk_autorot = true;

uintptr_t s_anchor_prim = 0;
bool      s_anchor_saved = false;
uint8_t   s_anchor_value = 0;

struct Motion {
    float rot[9];
    float pos[3];
    float lin[3];
    float ang[3];
};
static_assert(sizeof(Motion) == 0x48, "a motion block is rot36 + pos12 + lin12 + ang12");

uintptr_t s_last_prim = 0;

// -----------------------------------------------------------------------------
//  The hold thread
// -----------------------------------------------------------------------------
//  Holding a position is a race against the engine's per-step restore, and the
//  slow run lost it at ~3 writes/step. The feature loop cannot fix that on its
//  own: it is one platform::SleepMs(1) thread shared with ESP, aimbot, servers polling and
//  everything else, so a slow pass anywhere steals the writes movement needs.
//
//  So the position hold gets its own thread, at above-normal priority, doing
//  nothing but re-asserting the target. It is the only work on it, so its rate
//  does not depend on how long an ESP pass took, and it keeps writing while the
//  feature loop is busy.
std::mutex s_hold_mutex;    // (was CRITICAL_SECTION; same one-lock-per-iteration shape)
bool   s_hold_active = false;
static bool s_hold_thread_started = false;
Motion s_hold_motion{};
uintptr_t s_hold_prim = 0;


// A hammered-velocity BURST, driven by the hold thread -- the exact mechanism
// RunTest id 10 demonstrates, with the one property that made the test always
// work and every feature version fail: AUTONOMY.
//
// The test reads the primitive ONCE at click, then hammers for 400ms and
// nothing in the world can interrupt it. The feature versions all routed
// through the ownership system instead, and mover::Tick() demolishes every hold
// and owner the moment one cache pass comes back without a local primitive --
// which fetch_local does whenever ANY of its ~20 chained reads fails that pass.
// Under load that is an every-second-or-so event, so each version died within
// milliseconds of firing, every time, and "infinite jump never worked" across
// four different mechanisms while the test beside it always climbed.
//
// So a burst carries its own primitive address, its own velocity and its own
// deadline. The hold thread writes both motion blocks' linear velocity in a
// tight loop until the deadline passes and then stops BY ITSELF. Tick()'s
// respawn guard does not touch it; the feature that fired it decides (from its
// own view of the cache) whether the address is still the character, and can
// kill it early via StopVelocityBurst. The deadline bounds everything: a burst
// nobody tends to extinguishes in at most its own duration.
bool      s_vburst_active = false;
uintptr_t s_vburst_prim = 0;
float     s_vburst_v[3] = {};
uint64_t     s_vburst_deadline = 0;      // GetTickCount clock
std::atomic<int> s_vburst_writes{ 0 };   // achieved by the current burst

// A held CAMERA CFrame (freecam): 12 floats = rotation 9 + position 3, the
// same layout cam::WriteCFrame writes. Also under s_hold_cs.
//
// 6 = CameraType::Scriptable, re-asserted with every CFrame write (see the
// hold thread: the type and the CFrame are ONE unit against the game's camera
// script, which re-asserts Custom on its own RenderStepped).
constexpr int kCamScriptable = 6;
bool      s_camhold_active = false;
uintptr_t s_camhold_addr = 0;
float     s_camhold_cf[12] = {};
uintptr_t s_camhold_ws = 0;        // for the flap guard; 0 = guard disarmed
unsigned  s_cam_flaps = 0;
unsigned  s_cam_flap_total = 0;
uint64_t     s_cam_flap_log_at = 0;
bool      s_cam_flap_logged = false;
// THE RIVAL SET: every camera the pointer has ever flapped to while we held.
// The round-5 guard wrote our pose into a rival ONCE on detection - but the
// user keeps flying, so seconds later that pose is STALE and a frame rendered
// through the rival flips between fresh and stale freecam views ("less
// flickering but still some"), and flips shorter than the 100us sampling
// window rendered untouched. The fix: the current pose is written into EVERY
// rival on EVERY paced iteration, so a flip we never even sample still
// renders the fresh pose. Pruned on write failure (rivals die on respawn).
constexpr int kCamRivalMax = 4;
uintptr_t s_cam_rivals[kCamRivalMax] = {};
int       s_cam_rival_count = 0;
// The round-7 capture identified the enemy for real: a writer re-asserting
// the character-orbit pose 118-159 times per SECOND (per RENDER frame - a
// high-refresh monitor's rate), always 12.4-13.1 studs from the character
// (the follow-cam zoom orbit), leaving type=Scriptable and the subject
// untouched. An orbit writer needs CameraSubject to compute its pose - so
// the counter is to DETACH it (nil subject = the legal cutscene pattern;
// the follow-cam has nothing to orbit). Restore() puts it back.
bool      s_cam_subject_said = false;
bool      s_cam_matrix_logged = false;   // one full-matrix forensic per arm
uint64_t     s_cam_contest_hot_until = 0;
// FEED MODE. The round-10 log settled the fight's physics: the foreign pose
// is ALWAYS exactly 7.5 studs from ours, rotation identical, offset opposite
// the direction of travel - the writer ECHOES our pose at a fixed trailing
// offset, and it re-writes so fast that our 10kHz sampler caught it ~90x/sec
// despite our own continuous writes: a kHz-vs-kHz interleave where the
// renderer coin-flips each frame ("still flickering an inverted version" -
// 7.5 studs behind the path, parallax runs backwards). No write rate wins a
// coin flip. So we do what spectate always did: STOP FIGHTING AND FEED. In
// feed mode we write gentle 30Hz setpoints and let the game's writer render
// the view (a smooth chase-cam ~7.5 studs behind the path we fly). Engages
// after 40 sustained contests (a >=5Hz writer for ~0.5-8s); resets per arm.
// The restore guard: for a short window after freecam hands the camera back,
// the hold thread re-asserts the restored type+subject (round 15: "when you
// turn off freecam you dont return to normal" - the starved default module
// can outlive our restore by a frame).
uintptr_t s_guard_addr = 0;
int       s_guard_type = 0;
uintptr_t s_guard_subject = 0;
uint64_t     s_guard_until = 0;
uint64_t     s_guard_last = 0;

bool      s_cam_feed = false;
int       s_cam_feed_votes = 0;
uint64_t     s_cam_contest_prev_at = 0;
uint64_t s_cam_sync_last_ns = 0;     // echo-sync cap (ns clock). The old cap used
                                      // GetTickCount, whose ~15ms granularity
                                      // silently turned a "2ms" cap into 15ms:
                                      // against a ~120Hz writer (a write every
                                      // ~8ms) we answered every OTHER write -
                                      // whole frames of its MIRRORED pose in
                                      // the field = the round-12 residual
                                      // flicker, and culling/LOD reading the
                                      // mirrored pose = the see-through map
                                      // and players. QPC has real 100ns
                                      // resolution; the cap is now honest.
uint64_t     s_cam_subject_at = 0;       // nil-subject reassert timer in feed mode

// "Flickering an INVERTED version" (round 9) is a matrix-level signature:
// the rival frames carry OUR position with a TRANSFORMED rotation. Name the
// transform numerically: transposed (a view-matrix writer storing the
// inverse), fully negated (180), a single row/column flip (mirrored or
// upside-down view), or a negative determinant (mirror). Anything else comes
// out UNCLASSIFIED with the full matrix logged for hand analysis.
const char* ClassifyCamMatrix(const float f[9], const float o[9])
{
    bool tr = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (fabsf(f[i * 3 + j] - o[j * 3 + i]) > 0.01f) tr = false;
    if (tr) return "TRANSPOSED (view-matrix writer)";
    bool ng = true;
    for (int i = 0; i < 9; ++i) if (fabsf(f[i] + o[i]) > 0.01f) ng = false;
    if (ng) return "NEGATED (180-deg)";
    for (int k = 0; k < 3; ++k) {
        bool okr = true, okc = true;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                if (fabsf(f[i * 3 + j] - ((i == k) ? -o[i * 3 + j] : o[i * 3 + j])) > 0.01f)
                    okr = false;
                if (fabsf(f[i * 3 + j] - ((j == k) ? -o[i * 3 + j] : o[i * 3 + j])) > 0.01f)
                    okc = false;
            }
        if (okr) return "ROW-NEGATED (one basis vector flipped)";
        if (okc) return "COL-NEGATED (one axis flipped)";
    }
    const float det = f[0] * (f[4] * f[8] - f[5] * f[7])
                    - f[1] * (f[3] * f[8] - f[5] * f[6])
                    + f[2] * (f[3] * f[7] - f[4] * f[6]);
    if (det < 0.0f) return "MIRRORED (negative determinant)";
    return "UNCLASSIFIED";
}
// The CONTEST BASELINE: the last CFrame actually in memory. The hold thread
// reads the camera back at high frequency and classifies what it finds
// against this and the desired pose - a value that is neither is another
// writer's, and that is the flicker culprit named with numbers.
float     s_cam_written[12] = {};
unsigned  s_cam_contests = 0;        // contests in the current reporting window
unsigned  s_cam_contest_total = 0;
uint64_t     s_cam_contest_log_at = 0;
bool      s_cam_contest_logged = false;

bool Same12(const float a[12], const float b[12])
{
    for (int i = 0; i < 12; ++i)
        if (fabsf(a[i] - b[i]) > 0.001f) return false;
    return true;
}

// CameraType + CFrame as ONE unit - the write sequence shared by the feature
// thread's inline arm and the hold thread (paced, and unpaced on contest).
bool WriteCamUnit(uintptr_t addr, const float cf[12])
{
    bool ok = write_raw(addr + Offsets::Camera::CameraType,
                        &kCamScriptable, sizeof(kCamScriptable));
    const uintptr_t rot = addr + Offsets::Camera::Rotation;
    if (Offsets::Camera::Rotation + 36 == Offsets::Camera::Position)
        ok = write_raw(rot, cf, 48) && ok;                   // one contiguous CFrame
    else {
        ok = write_raw(rot, cf, 36) && ok;                   // adjacency lost: two writes
        ok = write_raw(addr + Offsets::Camera::Position, cf + 9, 12) && ok;
    }
    return ok;
}
void HoldThread(void*);            // defined below, next to the motion writers

void RestoreAll()
{
    if (s_ps_saved && is_valid_address(s_ps_hum)) {
        write<bool>(s_ps_hum + Offsets::Humanoid::PlatformStand, s_ps_value);
    }
    s_ps_saved = false;
    s_ps_hum = 0;

    if (s_walk_saved && is_valid_address(s_walk_hum)) {
        write<float>(s_walk_hum + Offsets::Humanoid::Walkspeed, s_walk_value);
        write<bool>(s_walk_hum + Offsets::Humanoid::AutoRotate, s_walk_autorot);
    }
    s_walk_saved = false;
    s_walk_hum = 0;

    // The anchor last, and unconditionally: leaving it set is the one failure
    // here that makes the character unplayable rather than merely wrong.
    if (s_anchor_saved && is_valid_address(s_anchor_prim))
        write<uint8_t>(s_anchor_prim + Offsets::Primitive::Flags, s_anchor_value);
    s_anchor_saved = false;
    s_anchor_prim = 0;
}

} // namespace

void Tick()
{
    // Space state, once per iteration - DEFENCE IN DEPTH (see globals.h for
    // the history): the LEVEL comes from GAKS, whose level bit is always
    // truthful about the current instant (and immune to a hook dying
    // silently); PRESSES come from the hook's queue, with the GAKS edge bit
    // as a supplement GATED ON A FRESH DOWN TRANSITION.
    //
    // THE GATE IS THE HOLD FIX. Auto-repeat re-sets the GAKS edge bit the
    // whole time a key is simply held, so an ungated OR turned one real press
    // into an endless stream of phantom ones - each stacking the rise speed
    // and restarting the window, and "hold space" became a continuous climb
    // with no pressing at all. Honouring the bit only when the sampled level
    // was UP at the previous iteration keeps every real use (a fast
    // press+release entirely inside a loop stall leaves the sampled level up
    // on both sides, so the bit still fires) and rejects repeats (held means
    // the level was down at every prior sample - the test can never pass).
    {
        const platform::KeyState sp = platform::SampleKey(platform::key::SPACE);
        const bool level = sp.down;
        g_input_space_down = level;
        const bool queued =
            g_input_space_hook_presses.exchange(0, std::memory_order_relaxed) > 0;
        static bool s_prev_level = false;      // feature-loop-local, single thread
        const bool fresh = sp.pressed_since_last && !s_prev_level;
        s_prev_level = level;
        g_input_space_edge = queued || fresh;
    }

    if (s_last_ns == 0) {
        s_last_ns = platform::NowNs();
        s_dt = 0.0f;
        if (!s_hold_thread_started) {
            // fire-and-forget: the thread outlives any interest in its handle
            s_hold_thread_started = platform::StartThread(HoldThread, nullptr);
        }
    } else {
        const uint64_t now = platform::NowNs();
        double d = double(now - s_last_ns) / 1e9;
        s_last_ns = now;
        if (d < 0.0) d = 0.0;
        if (d > 0.1) d = 0.1;   // a stall is not a 3-second-long movement command
        s_dt = (float)d;
    }

    const uint64_t now_ms = platform::NowMs();
    if (s_next_step == 0 || (int)(now_ms - s_next_step) >= 0) {
        s_step_now = true;
        // Anchor to the deadline, not to now, so the cadence does not drift; but
        // resync if we fell more than a step behind rather than firing a burst.
        s_next_step = ((int)(now_ms - s_next_step) > (int)kStepMs)
                          ? now_ms + kStepMs
                          : s_next_step + kStepMs;
    } else {
        s_step_now = false;
    }

    // Measure what we are ACTUALLY achieving. This is the variable that decided
    // every "works sometimes" report, so it is reported rather than assumed.
    {
        const uint64_t now2 = platform::NowNs();
        if (s_wr_t0_ns == 0) s_wr_t0_ns = now2;
        const double el = double(now2 - s_wr_t0_ns) / 1e9;
        if (el >= 0.5) {
            s_wr_per_ms = (float)(s_wr_count / (el * 1000.0));
            const float per_step = s_wr_per_ms * (float)kStepMs;
            if (s_wr_count > 0 && per_step < (float)kMinWritesPerStep && !s_wr_warned) {
                s_wr_warned = true;
                LogLine("mover: only %.1f position writes per physics step (%.2f/ms). "
                        "Below ~%d the engine wins the restore race often enough to be "
                        "visible as sinking or rubber-banding. Something is starving the "
                        "feature loop - check CPU load and other overlays.",
                        per_step, s_wr_per_ms, kMinWritesPerStep);
            }
            if (per_step >= (float)kMinWritesPerStep) s_wr_warned = false;
            s_wr_count = 0;
            s_wr_t0_ns = now2;
        }
    }

    const cache::LocalPlayerData lp = cache::GetLocalPlayer();
    s_body.valid = lp.valid && is_valid_address(lp.hrp_primitive);
    s_body.primitive = lp.hrp_primitive;
    s_body.humanoid = lp.humanoid_address;
    s_body.pos[0] = lp.x; s_body.pos[1] = lp.y; s_body.pos[2] = lp.z;

    // A REAL respawn hands out a whole new character (a different VALID
    // primitive). Anything we saved belongs to a dead instance, and any owner
    // is driving a body that no longer exists, so both are dropped here.
    //
    // A TRANSIENT MISS IS NOT A RESPAWN. A failed cache pass publishes no
    // primitive for one 16ms window - under load that is an every-second-or-so
    // event, and treating it as a respawn is what collapsed flight mid-air
    // (hold killed, feature stopped, position re-seeded from wherever the fall
    // had got to) and killed every infinite-jump burst on arrival. So a miss
    // keeps the previous address and every hold armed on it; features bound
    // their own "really gone" tolerance (flight: 500ms, infjump: 200ms) and
    // release their holds when it expires, so a genuine detach is still
    // cleaned up.
    const uintptr_t prim = s_body.valid ? s_body.primitive : 0;
    const uintptr_t effective = s_body.valid ? prim : s_last_prim;
    if (effective != s_last_prim) {
        // Stop the position hold FIRST: it writes to a primitive address of its
        // own, and a respawn makes that address someone else's memory.
        // (The velocity BURST is deliberately not touched here - see its comment
        // above. A transiently-failed cache pass publishes prim 0 for one
        // 16ms window, and killing an in-flight burst on that is precisely how
        // "the test works but the feature never does" happened. The burst owns
        // its address and its 400ms clock; the feature that fired it watches
        // for a REAL respawn - a different VALID primitive - and kills it.)
        {
            std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
            s_hold_active = false;
            s_hold_prim = 0;
        }
        RestoreAll();
        s_owner = None;
        s_last_prim = effective;
    }
}

float Dt() { return s_dt; }
bool PhysicsStep() { return s_step_now; }
const Body& Local() { return s_body; }

bool Claim(Owner who)
{
    if (!s_body.valid) return false;
    if (s_owner != None && s_owner > who) return false;
    if (s_owner != who) {
        // The previous owner's saved state is handed back before the new one
        // starts saving over it.
        RestoreAll();
        s_owner = who;
    }
    return true;
}

void Release(Owner who)
{
    if (s_owner != who) return;
    RestoreAll();
    s_owner = None;
}

Owner Current() { return s_owner; }

// -----------------------------------------------------------------------------
//  The motion block
// -----------------------------------------------------------------------------
//  The primitive holds TWO identical motion blocks (rotation, position, linear
//  velocity, angular velocity - 0x48 bytes each, block B exactly 0x48 after
//  block A). See the table in offsets.h.
//
//  This is what was still broken after the pacing fix. The code wrote block A's
//  velocity (0xF8) and both positions, but never block B's velocity (0x140) -
//  and the probe caught 0x140 holding a live value, magnitude 16.0, the
//  character's exact walkspeed. So every tick the engine re-synced our write
//  away from the copy we had left alone. The position test alternating
//  "dY 10.00 / dY 0.00" is that race, sampled.
//
//  So a motion write is one 0x48 buffer written to BOTH blocks - all four
//  quantities, together, atomically per block. Never a single field.
// -----------------------------------------------------------------------------

namespace {



// The layout is DERIVED from the offsets that the live offset service actually
// updates (Rotation and CFrame2), not from the standalone MotionBlock/Stride
// constants - those are not in the dump, so a client update would refresh
// Rotation while leaving them behind, and we would write 0x48 bytes into
// whatever now lives at the old address. Derived, they cannot drift apart.
//
// The relationships are checked once and reported, because writing a motion
// block at a wrong stride is the one mistake here that corrupts unrelated
// fields rather than simply not working.
constexpr uintptr_t kPosOff = 0x24;   // within a motion block
constexpr uintptr_t kLinOff = 0x30;
constexpr uintptr_t kAngOff = 0x3c;

uintptr_t BlockA() { return s_body.primitive + Offsets::Primitive::Rotation; }

uintptr_t Stride()
{
    return Offsets::Primitive::CFrame2 - Offsets::Primitive::Rotation;
}

bool LayoutSane()
{
    static int s_cached = -1;      // -1 unknown, 0 bad, 1 good
    if (s_cached >= 0) return s_cached == 1;

    const uintptr_t rot = Offsets::Primitive::Rotation;
    const uintptr_t stride = Stride();
    bool ok = true;
    // block A's own fields must sit where a rot36+pos12+lin12+ang12 block puts them
    if (Offsets::Primitive::Position != rot + 0x24) ok = false;
    if (Offsets::Primitive::AssemblyLinearVelocity != rot + 0x30) ok = false;
    if (Offsets::Primitive::AssemblyAngularVelocity != rot + 0x3c) ok = false;
    // and block B must be one whole block further on, with the same shape
    if (stride != 0x48) ok = false;
    if (Offsets::Primitive::Position2 != Offsets::Primitive::CFrame2 + 0x24) ok = false;

    s_cached = ok ? 1 : 0;
    if (!ok)
        LogLine("mover: primitive motion-block layout is not the expected 0x48 "
                "pair (rot 0x%llx, pos 0x%llx, lin 0x%llx, ang 0x%llx, cframe2 0x%llx). "
                "Movement features are disabled rather than write at a wrong stride - "
                "re-run the debug probe and update offsets.h.",
                (unsigned long long)rot,
                (unsigned long long)Offsets::Primitive::Position,
                (unsigned long long)Offsets::Primitive::AssemblyLinearVelocity,
                (unsigned long long)Offsets::Primitive::AssemblyAngularVelocity,
                (unsigned long long)Offsets::Primitive::CFrame2);
    return ok;
}

uintptr_t BlockB() { return BlockA() + Stride(); }

bool ReadMotion(Motion& m)
{
    if (!s_body.valid || !LayoutSane()) return false;
    return read_raw(BlockA(), &m, sizeof(m));
}

// Both blocks, whole, A then B. B last because it is the copy the engine
// re-syncs from: if we are interrupted between the two, the one that decides is
// the one already updated.
void WriteMotion(const Motion& m)
{
    if (!s_body.valid || !LayoutSane()) return;
    write_raw(BlockA(), &m, sizeof(m));
    write_raw(BlockB(), &m, sizeof(m));
    ++s_wr_count;
}

// The hold thread's own write: it must not touch s_body, which the feature loop
// owns, so it carries its own primitive address captured under the lock.
//
// A POSITION HOLD WRITES ONLY THE TAIL OF THE BLOCK - position and both
// velocities (block+0x24, 36 bytes) - and NEVER the rotation. The old
// full-block write echoed a rotation captured once per feature tick, so
// between ticks the hammer re-asserted a STALE rotation thousands of times per
// step and fought the engine's own rotation updates (auto-rotate while
// steering, the humanoid controller standing the character up). That fight was
// the visible twitch in the character and the camera while flying - the exact
// mistake WriteVelocity's comment describes for position ("never echo an
// output back"), one field over. Rotation belongs to the engine during a hold;
// we own position and nothing else.
void WriteHoldTailTo(uintptr_t prim_base, const Motion& m)
{
    const uintptr_t a = prim_base + Offsets::Primitive::Rotation + kPosOff;
    const uintptr_t b = a + Stride();
    write_raw(a, &m.pos, sizeof(m.pos) + sizeof(m.lin) + sizeof(m.ang));
    write_raw(b, &m.pos, sizeof(m.pos) + sizeof(m.lin) + sizeof(m.ang));
}

// Pacing for the hammer. Unpaced, this thread spins as fast as it can - on an
// idle core that is 100% of it and tens of thousands of writes a second, which
// is 10-100x more than the race needs (slowest SUCCESSFUL measured hold: ~27
// writes per 16.7ms step) and enough of a CPU burn to cost the GAME render
// frames, which reads as the camera stuttering while flying. Paced to one
// write burst per 250us it still lands ~66 writes per step - 2.4x the slowest
// measured success - for a fraction of the CPU. When WriteProcessMemory is
// slow (the 5.7ms-per-call regime), the natural rate is already below the cap
// and the pacing no-ops.
constexpr int    kWritePeriodHz = 4000;    // one burst per 250us
constexpr uint64_t kWritePeriodNs = 1000000000ull / kWritePeriodHz;
constexpr uint64_t kCamReadNs     = 100000ull;    // the 10kHz contest read

void HoldThread(void*)
{
    // Above normal, not highest: this thread spins, and at HIGHEST it can starve
    // the very game we are writing into on a busy machine.
    platform::BoostThread();   // above normal, not highest - see platform_win
    const uint64_t min_period_ns = kWritePeriodNs;
    // The contest detector reads the camera back 10x per write period: an
    // outside writer survives at most ~100us before we notice and rewrite -
    // it cannot hold a whole rendered frame.
    const uint64_t cam_read_period_ns = kCamReadNs;
    uint64_t last_write_ns = 0;
    for (;;) {
        // ONE critical section per iteration for all three work snapshots.
        // Three separate acquisitions (the old shape) meant three windows per
        // iteration for priority inversion against the UI thread - and the
        // user's round-16 log caught real 30ms hold-thread starvation gaps.
        bool active = false;
        Motion m{};
        uintptr_t prim_base = 0;
        bool camhold = false;
        bool camhold_feed = false;
        uintptr_t camhold_addr = 0;
        uintptr_t camhold_ws = 0;
        float camhold_cf[12] = {};
        uintptr_t cam_rivals[kCamRivalMax] = {};
        int cam_rival_count = 0;
        bool vburst = false;
        float bv[3] = {};
        uintptr_t vburst_prim = 0;
        uint64_t vburst_deadline = 0;
        {
        std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
        active = s_hold_active;
        if (active) { m = s_hold_motion; prim_base = s_hold_prim; }
        if (s_camhold_active) {
            camhold = true;
            camhold_feed = s_cam_feed;
            camhold_addr = s_camhold_addr;
            camhold_ws = s_camhold_ws;
            for (int i = 0; i < 12; ++i) camhold_cf[i] = s_camhold_cf[i];
            for (int i = 0; i < s_cam_rival_count && i < kCamRivalMax; ++i)
                cam_rivals[i] = s_cam_rivals[i];
            cam_rival_count = s_cam_rival_count;
        }
        if (s_vburst_active) {
            vburst = true;
            bv[0] = s_vburst_v[0]; bv[1] = s_vburst_v[1]; bv[2] = s_vburst_v[2];
            vburst_prim = s_vburst_prim;
            vburst_deadline = s_vburst_deadline;
        }
        }

        // The deadline check is NOT paced: a burst must end on time even if the
        // writes themselves are being paced.
        if (vburst && is_valid_address(vburst_prim) &&
            (int)(platform::NowMs() - vburst_deadline) >= 0) {
            { std::lock_guard<std::mutex> hold_lock(s_hold_mutex); s_vburst_active = false; }
            vburst = false;
        }

        const bool hold_work = active && is_valid_address(prim_base);
        const bool burst_work = vburst && is_valid_address(vburst_prim);

        // BURSTS ARE NOT PACED. The working velocity pins (the debug test,
        // infinite jump) achieved 5-12 writes/ms and held perfectly; pacing
        // the burst to the 4kHz position-hold cap let the humanoid damper
        // claw vertical velocity back between writes - flight "floated down"
        // instead of hovering and bounced while steering. Position holds keep
        // the cap: they are indefinite, and 66 writes/step is 2x+ the slowest
        // measured success; bursts are short-lived (400ms deadlines) and need
        // everything the thread can give.
        if (burst_work) {
            const uintptr_t pa = vburst_prim + Offsets::Primitive::Rotation;
            write_raw(pa + kLinOff, bv, 12);
            write_raw(pa + Stride() + kLinOff, bv, 12);
            s_vburst_writes.fetch_add(1, std::memory_order_relaxed);
        }

        // THE CAMERA HOLD RE-ASSERTS CameraType AND the CFrame TOGETHER, every
        // paced iteration. Re-asserting the type only from the feature loop
        // (at ~7Hz) left a hole a RenderStepped camera script walked straight
        // through: it flips CameraType back to Custom, and until our next
        // Take (up to 150ms later) the default camera module writes the
        // character-orbit pose while our hammer writes ours - the reported
        // "camera goes back and forth from your character and back super
        // fast". Type+CFrame written as one unit, the module can hold at most
        // a fraction of a frame. (6 = CameraType::Scriptable, the same enum
        // cam::kScriptable carries; a literal to keep mover free of the
        // camera-drive include.)
        //
        // Its OWN pacing timestamp, not the position hold's: sharing one let
        // an active position hold (a teleport, flight) halve the camera rate.
        static uint64_t s_last_cam_write = 0;
        if (camhold && is_valid_address(camhold_addr)) {
            const uint64_t now = platform::NowNs();
            // HOT MODE: while a writer is actively contesting (a contest
            // confirmed within the last second) the pacing gate opens and
            // the camera write runs at full thread speed - same doctrine as
            // velocity bursts. The round-7 writer lands ~140 writes/sec in
            // the render phase; our ~100us re-win still lost a few frames
            // per second. Unpaced writes shrink its window ~10x.
            const bool cam_hot =
                !camhold_feed && platform::NowMs() < s_cam_contest_hot_until;
            // FEED MODE HAS NO BLIND PACED WRITES. The round-11 log showed
            // what happens with them: a blind write survives in the field
            // until the writer's next pass (~half a frame) and the renderer
            // coin-flips on it - "good until you start moving, then a ton of
            // flickering" (our fresh setpoints vs its 45-stud-lagging echoes,
            // both in the field). Feed-mode writes are ECHO-TRIGGERED only:
            // immediately after each of the writer's own writes, where the
            // renderer samples the pair's LAST value - ours.
            const bool cam_due = !camhold_feed &&
                ((now - s_last_cam_write >= min_period_ns) || cam_hot);
            if (cam_due) {
                // Starvation telemetry first: if THIS thread ever went quiet
                // for milliseconds while the hold was active, the flicker was
                // self-inflicted and the log says so.
                if (s_last_cam_write != 0) {
                    const uint64_t gap_us = (now - s_last_cam_write) / 1000;
                    static uint64_t s_gap_log_at = 0;
                    if (gap_us >= 5000) {
                        const uint64_t t = platform::NowMs();
                        if (t - s_gap_log_at >= 1000) {
                            s_gap_log_at = t;
                            LogLine("camera hold: write gap %lldus - the hold thread starved",
                                    (long long)gap_us);
                        }
                    }
                }
                s_last_cam_write = now;
                WriteCamUnit(camhold_addr, camhold_cf);
                // STARVE THE ORBIT WRITER: it computes its pose from
                // CameraSubject every render frame; a nil subject gives it
                // nothing to orbit (the legal cutscene pattern - Restore()
                // saves and returns the original). Re-asserted every paced
                // iteration in case the game re-points it.
                static const uintptr_t kNilSubject = 0;
                write_raw(camhold_addr + Offsets::Camera::CameraSubject,
                          &kNilSubject, sizeof(kNilSubject));
                // ...and into every rival, so any frame the game renders
                // through one - sampled or not - shows the CURRENT pose.
                // Failed write = dead rival (respawn) = prune it.
                for (int i = 0; i < cam_rival_count && i < kCamRivalMax; ++i) {
                    const uintptr_t rv = cam_rivals[i];
                    if (rv == camhold_addr || !is_valid_address(rv)) continue;
                    if (!WriteCamUnit(rv, camhold_cf)) {
                        { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                        for (int j = i; j < s_cam_rival_count - 1 && j + 1 < kCamRivalMax; ++j)
                            s_cam_rivals[j] = s_cam_rivals[j + 1];
                        s_cam_rivals[s_cam_rival_count - 1] = 0;
                        if (s_cam_rival_count > 0) --s_cam_rival_count; }
                    }
                }
                { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                for (int i = 0; i < 12; ++i) s_cam_written[i] = camhold_cf[i]; }
            }
        } else {
            s_last_cam_write = 0;
        }

        // THE CONTEST DETECTOR. The 500ms feature-loop alarm could only see a
        // writer that STAYS ahead of us; the flicker user report showed a
        // frame-level fight (type stable at Scriptable, no camera swap, no
        // sustained deviation - yet the view snapped to the character and
        // back). So the hold thread now READS THE CAMERA BACK at 10kHz and
        // classifies: (a) the desired pose - a fresh feature update landed,
        // refresh the baseline; (b) the last pose WE wrote while the desired
        // moved on - a pending mouse update: write it NOW, unpaced, so input
        // lands in ~100us; (c) neither - suspect. Confirm with an immediate
        // second read, because a torn read of a concurrent update does not
        // repeat; a real other-writer's value does. Confirmed = rewrite
        // type+CFrame immediately and NAME IT in the log with both poses and
        // a per-second count. An outside writer now wins at most ~100us - a
        // fraction of one rendered frame - instead of the 150ms window the
        // type-pace bug left, or the whole frames a RenderStepped writer
        // could steal between 500ms alarms.
        static uint64_t s_last_cam_read = 0;
        if (camhold && is_valid_address(camhold_addr) &&
            Offsets::Camera::Rotation + 36 == Offsets::Camera::Position) {
            const uint64_t rn = platform::NowNs();
            // In feed mode the read IS the weapon (every detection is an
            // answered write) - sample at 20kHz there, 10kHz otherwise. (The
            // 40kHz experiment of round 15 REGRESSED - the extra syscall+lock
            // load starved the hold thread in visible 30ms gaps; 20kHz/500us
            // is the state the user called "works perfectly".)
            const uint64_t read_period_ns = camhold_feed ? cam_read_period_ns / 2
                                                         : cam_read_period_ns;
            if (rn - s_last_cam_read >= read_period_ns) {
                s_last_cam_read = rn;
                float got[12] = {};
                if (read_raw(camhold_addr + Offsets::Camera::Rotation, got, 48)) {
                    float written[12] = {};
                    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                    for (int i = 0; i < 12; ++i) written[i] = s_cam_written[i]; }
                    if (Same12(got, camhold_cf)) {
                        if (!Same12(written, camhold_cf)) {
                            { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                            for (int i = 0; i < 12; ++i) s_cam_written[i] = camhold_cf[i]; }
                        }
                    } else if (Same12(got, written)) {
                        if (!Same12(written, camhold_cf) && !camhold_feed) {
                            WriteCamUnit(camhold_addr, camhold_cf);
                            { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                            for (int i = 0; i < 12; ++i) s_cam_written[i] = camhold_cf[i]; }
                        }
                    } else if (camhold_feed) {
                        // FEED MODE, ECHO-SYNCED. A foreign value here is one
                        // of the writer's OWN writes just landed (its lagging
                        // echo of our pose - 45 studs behind once we move -
                        // or its mirrored reflection pass). Answer it NOW:
                        // our pose goes in immediately after theirs, and the
                        // renderer samples the LAST value in the field. Rate-
                        // capped at 500us ON QPC (see s_cam_sync_last_ns for
                        // why GetTickCount cannot do this job).
                        const uint64_t sq = platform::NowNs();
                        const uint64_t st = platform::NowMs();
                        if (s_cam_sync_last_ns == 0 ||
                            sq - s_cam_sync_last_ns >= 500000ull) {   // 500us in ns
                            s_cam_sync_last_ns = sq;
                            WriteCamUnit(camhold_addr, camhold_cf);
                            { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                            for (int i = 0; i < 12; ++i) s_cam_written[i] = camhold_cf[i]; }
                        }
                        // The nil-subject reassert moves to a quiet 1s timer
                        // (the paced writes that carried it are gone in feed
                        // mode).
                        if (s_cam_subject_at == 0 || st - s_cam_subject_at >= 1000) {
                            s_cam_subject_at = st;
                            static const uintptr_t kNilFeed = 0;
                            write_raw(camhold_addr + Offsets::Camera::CameraSubject,
                                      &kNilFeed, sizeof(kNilFeed));
                        }
                    } else {
                        float again[12] = {};
                        if (read_raw(camhold_addr + Offsets::Camera::Rotation, again, 48) &&
                            !Same12(again, camhold_cf) && !Same12(again, written)) {
                            WriteCamUnit(camhold_addr, camhold_cf);
                            s_cam_contest_hot_until = platform::NowMs() + 1000;
                            { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                            for (int i = 0; i < 12; ++i) s_cam_written[i] = camhold_cf[i];
                            ++s_cam_contests;
                            ++s_cam_contest_total;
                            }
                            // FEED ENGAGEMENT: 40 sustained contests (each
                            // gap < 200ms, i.e. a >=5Hz writer) = the field
                            // cannot be won by writing. Feed it instead.
                            const uint64_t tn = platform::NowMs();
                            if (s_cam_contest_prev_at == 0 ||
                                tn - s_cam_contest_prev_at > 200)
                                s_cam_feed_votes = 0;
                            s_cam_contest_prev_at = tn;
                            if (++s_cam_feed_votes >= 40 && !s_cam_feed) {
                                s_cam_feed = true;
                                s_cam_contest_hot_until = 0;
                                // Reset the paced timestamp: the next hammer
                                // mode (if ever) must not log a bogus years-
                                // long "write gap" against this stale value.
                                s_last_cam_write = 0;
                                const float ddx = again[9]  - camhold_cf[9];
                                const float ddy = again[10] - camhold_cf[10];
                                const float ddz = again[11] - camhold_cf[11];
                                LogLine("camera: FEED MODE (echo-sync) - the writer "
                                        "re-writes the field every frame (echo trails "
                                        "%.1f studs); our pose now lands right after "
                                        "each of its writes, where the renderer "
                                        "samples it",
                                        sqrtf(ddx * ddx + ddy * ddy + ddz * ddz));
                            }
                            const uint64_t tick = platform::NowMs();
                            if (!s_cam_contest_logged ||
                                tick - s_cam_contest_log_at >= 1000) {
                                s_cam_contest_log_at = tick;
                                s_cam_contest_logged = true;
                                unsigned sec = 0, tot = 0;
                                { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                                sec = s_cam_contests;
                                tot = s_cam_contest_total;
                                s_cam_contests = 0;
                                }
                                // Classification reads, rate-limited so the hot
                                // path never pays for them: the type the writer
                                // left behind, the subject it orbits, and how far
                                // the foreign pose sits from OUR CHARACTER. An
                                // orbit/zoom writer lands ~zoom distance (8-14
                                // studs) from the character; a transition or
                                // cutscene writer lands elsewhere. The first
                                // real capture (user log) showed the foreign
                                // poses CLUSTERED near ground height while we
                                // flew - orbit-shaped - but only ~6 writes in a
                                // whole session (4 events x 2), far from a
                                // 60Hz script: an EVENT-DRIVEN writer. These
                                // fields name it.
                                const int ctype =
                                    read<int>(camhold_addr + Offsets::Camera::CameraType);
                                const uintptr_t subj =
                                    read<uintptr_t>(camhold_addr + Offsets::Camera::CameraSubject);
                                float cpos[3] = {};
                                float cdist = -1.0f;
                                Motion cm{};
                                if (ReadMotion(cm)) {
                                    cpos[0] = cm.pos[0]; cpos[1] = cm.pos[1]; cpos[2] = cm.pos[2];
                                    const float ddx = again[9]  - cpos[0];
                                    const float ddy = again[10] - cpos[1];
                                    const float ddz = again[11] - cpos[2];
                                    cdist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                                }
                                // Rotation delta: the round-8 log showed the
                                // starved writer now writes poses NEAR OURS
                                // (d-from-character 164-202, foreign within
                                // 0.5 studs of our hold) - if its ROTATION
                                // also matches ours, it is re-writing our own
                                // pose (an interpolator/predictor chasing the
                                // target we feed it); a different rotation
                                // means a genuinely independent viewpoint.
                                const float dot = again[2] * camhold_cf[2]
                                                + again[5] * camhold_cf[5]
                                                + again[8] * camhold_cf[8];
                                float rdeg = acosf(fmaxf(-1.0f, fminf(1.0f, dot)))
                                           * 57.29578f;
                                if (!(rdeg >= 0.0f && rdeg <= 180.0f)) rdeg = -1.0f;
                                if (!s_cam_matrix_logged) {
                                    s_cam_matrix_logged = true;
                                    const char* cls =
                                        ClassifyCamMatrix(again, camhold_cf);
                                    // v2: determinant + orthonormality error
                                    // settle mirror-vs-torn-read for good (a
                                    // real mirrored pose is orthonormal with
                                    // det<0; a torn read is neither), and OUR
                                    // rotation goes in the log too so the
                                    // relative transform is computable later.
                                    const float fdet =
                                        again[0] * (again[4] * again[8] - again[5] * again[7])
                                      - again[1] * (again[3] * again[8] - again[5] * again[6])
                                      + again[2] * (again[3] * again[7] - again[4] * again[6]);
                                    float oerr = 0.0f;
                                    for (int i = 0; i < 3; ++i)
                                        for (int j = 0; j < 3; ++j) {
                                            float d = 0.0f;
                                            for (int k2 = 0; k2 < 3; ++k2)
                                                d += again[i * 3 + k2] * again[j * 3 + k2];
                                            d -= (i == j) ? 1.0f : 0.0f;
                                            oerr += fabsf(d);
                                        }
                                    LogLine("camera: FIRST FOREIGN MATRIX - %s | "
                                            "rot=%ddeg pos-delta=(%.1f,%.1f,%.1f) | "
                                            "det=%.3f orthoerr=%.2f | "
                                            "FR=[%.2f %.2f %.2f; %.2f %.2f %.2f; "
                                            "%.2f %.2f %.2f] "
                                            "OURS=[%.2f %.2f %.2f; %.2f %.2f %.2f; "
                                            "%.2f %.2f %.2f]",
                                            cls, (int)(rdeg + 0.5f),
                                            again[9] - camhold_cf[9],
                                            again[10] - camhold_cf[10],
                                            again[11] - camhold_cf[11],
                                            fdet, oerr,
                                            again[0], again[1], again[2],
                                            again[3], again[4], again[5],
                                            again[6], again[7], again[8],
                                            camhold_cf[0], camhold_cf[1], camhold_cf[2],
                                            camhold_cf[3], camhold_cf[4], camhold_cf[5],
                                            camhold_cf[6], camhold_cf[7], camhold_cf[8]);
                                }
                                LogLine("camera: CONTEST - foreign %.1f,%.1f,%.1f "
                                        "(ours %.1f,%.1f,%.1f) rot=%ddeg type=%d "
                                        "subj=0x%llX char=%.1f,%.1f,%.1f d=%.1f; "
                                        "rewrote. %u recent, %u total",
                                        again[9], again[10], again[11],
                                        camhold_cf[9], camhold_cf[10], camhold_cf[11],
                                        (int)(rdeg + 0.5f),
                                        ctype, (unsigned long long)subj,
                                        cpos[0], cpos[1], cpos[2], cdist, sec, tot);
                            }
                        }
                    }
                }
            }
        } else {
            s_last_cam_read = 0;
        }

        // THE FLAP GUARD. Constant flicker with a QUIET CFrame field (the
        // user's round-4 log: 6 contests in a whole session, yet "still
        // constant, no scroll wheel") means the rendered view is not always
        // read from the camera we hold - and the one thing that can alternate
        // per frame INVISIBLY is the Workspace.CurrentCamera pointer flapping
        // inside a single frame: the feature-tick watchdog cannot see
        // A->B->A between its ticks, but the renderer samples the pointer
        // live. A game rendering a viewmodel/scope pass through a second
        // camera does exactly this. So: read the pointer at 10kHz, and on any
        // flap first write OUR pose into the OTHER camera - a frame rendered
        // through it still shows our view - then pin the pointer back and
        // name the flap in the log.
        static uint64_t s_last_ptr_read = 0;
        if (camhold && is_valid_address(camhold_addr) && is_valid_address(camhold_ws)) {
            const uint64_t pn = platform::NowNs();
            if (pn - s_last_ptr_read >= cam_read_period_ns) {
                s_last_ptr_read = pn;
                uintptr_t cur = 0;
                if (read_raw(camhold_ws + Offsets::Workspace::CurrentCamera,
                             &cur, sizeof(cur)) &&
                    cur != camhold_addr && is_valid_address(cur)) {
                    WriteCamUnit(cur, camhold_cf);      // their frame shows OUR pose
                    write_raw(camhold_ws + Offsets::Workspace::CurrentCamera,
                              &camhold_addr, sizeof(camhold_addr));   // pin back
                    // Remember it: from now on every paced write lands in it
                    // too, so the NEXT flip - even one this thread never
                    // samples - renders the fresh pose, not a stale one.
                    bool known = false;
                    bool added = false;
                    {
                        std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                        for (int i = 0; i < s_cam_rival_count; ++i)
                            if (s_cam_rivals[i] == cur) known = true;
                        if (!known && s_cam_rival_count < kCamRivalMax) {
                            s_cam_rivals[s_cam_rival_count++] = cur;
                            added = true;
                        }
                    }
                    if (added) {
                        LogLine("camera: rival camera 0x%llX added to the pose set - "
                                "every write lands in it too",
                                (unsigned long long)cur);
                    }
                    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                    ++s_cam_flaps;
                    ++s_cam_flap_total; }
                    const uint64_t t = platform::NowMs();
                    if (!s_cam_flap_logged || t - s_cam_flap_log_at >= 1000) {
                        s_cam_flap_log_at = t;
                        s_cam_flap_logged = true;
                        unsigned fl = 0, tot = 0;
                        { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                        fl = s_cam_flaps;
                        tot = s_cam_flap_total;
                        s_cam_flaps = 0;
                        }
                        LogLine("camera: CurrentCamera FLAPPED to 0x%llX - our pose "
                                "written into it, pointer pinned back "
                                "(%u recent, %u total)",
                                (unsigned long long)cur, fl, tot);
                    }
                }
            }
        } else {
            s_last_ptr_read = 0;
        }

        // THE RESTORE GUARD. Active for a short window after freecam hands the
        // camera back: re-assert the restored type+subject at ~100Hz. Never
        // the CFrame - the game owns the pose again. Covers the one frame in
        // which the (all-session-starved) default module might re-assert its
        // own state after our restore.
        if (is_valid_address(s_guard_addr) &&
            (int)(platform::NowMs() - s_guard_until) < 0) {
            const uint64_t gt = platform::NowMs();
            if (s_guard_last == 0 || gt - s_guard_last >= 10) {
                s_guard_last = gt;
                write_raw(s_guard_addr + Offsets::Camera::CameraType,
                          &s_guard_type, sizeof(s_guard_type));
                write_raw(s_guard_addr + Offsets::Camera::CameraSubject,
                          &s_guard_subject, sizeof(s_guard_subject));
            }
        } else {
            s_guard_last = 0;
        }

        if (hold_work) {
            const uint64_t now = platform::NowNs();
            if (now - last_write_ns >= min_period_ns) {
                last_write_ns = now;
                WriteHoldTailTo(prim_base, m);
                { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
                ++s_wr_count;
                }
            }
        }

        if (hold_work || burst_work || camhold) {
            // Yield rather than sleep: this is still a race we have to win, and
            // the measurement showed the failing run managing only ~3 writes per
            // physics step. The pacing above is what keeps the CPU cost sane.
            platform::YieldThread();
        } else {
            last_write_ns = 0;
            platform::SleepMs(2);
        }
    }
}

} // namespace

bool ReadCFrame(float rot[9], float pos[3])
{
    Motion m{};
    if (!ReadMotion(m)) return false;
    if (rot) for (int i = 0; i < 9; ++i) rot[i] = m.rot[i];
    if (pos) for (int i = 0; i < 3; ++i) pos[i] = m.pos[i];
    return true;
}

void WriteCFrame(const float pos[3], const float rot[9])
{
    if (!s_body.valid || !pos) return;
    Motion m{};
    // Read the whole block first, so the fields we are not setting (the rotation
    // the player turned to, and the angular velocity) carry over rather than
    // being zeroed by omission.
    if (!ReadMotion(m)) {
        m.rot[0] = 1.0f; m.rot[4] = 1.0f; m.rot[8] = 1.0f;
    }
    if (rot) for (int i = 0; i < 9; ++i) m.rot[i] = rot[i];
    m.pos[0] = pos[0]; m.pos[1] = pos[1]; m.pos[2] = pos[2];
    // A teleport is a position change with no motion through the space between,
    // so both velocities go to zero with it - in the same write, or the engine
    // integrates the old velocity from the new position and drags us off it.
    m.lin[0] = m.lin[1] = m.lin[2] = 0.0f;
    m.ang[0] = m.ang[1] = m.ang[2] = 0.0f;
    WriteMotion(m);
}

bool ReadVelocity(float v[3])
{
    if (!v) return false;
    Motion m{};
    if (!ReadMotion(m)) return false;
    v[0] = m.lin[0]; v[1] = m.lin[1]; v[2] = m.lin[2];
    return true;
}

// A VELOCITY write must touch the velocity bytes and NOTHING else.
//
// The previous version read the whole 0x48 block, replaced the velocity, and
// wrote the whole block back - carrying a position that was read up to a step
// earlier. Flight writes velocity every step, so every step it re-asserted a
// stale position over the one the engine had just integrated: a 60Hz position
// snap-back, self-inflicted, in the function whose own comment claimed to avoid
// exactly that. Position is the engine's OUTPUT here and must never be echoed
// back to it.
void WriteVelocity(const float v[3])
{
    if (!s_body.valid || !v || !LayoutSane()) return;
    write_raw(BlockA() + kLinOff, v, 12);
    write_raw(BlockB() + kLinOff, v, 12);
}

void WriteVelocityAndSpin(const float lin[3], const float ang[3])
{
    if (!s_body.valid || !lin || !ang || !LayoutSane()) return;
    // linear (0x30) and angular (0x3c) are adjacent, so this is one 24-byte
    // write per block - and still no position echoed back.
    const float v[6] = { lin[0], lin[1], lin[2], ang[0], ang[1], ang[2] };
    write_raw(BlockA() + kLinOff, v, sizeof(v));
    write_raw(BlockB() + kLinOff, v, sizeof(v));
}

void PlatformStand(Owner who, bool on)
{
    if (s_owner != who || !is_valid_address(s_body.humanoid)) return;
    if (on) {
        if (!s_ps_saved) {
            s_ps_value = read<bool>(s_body.humanoid + Offsets::Humanoid::PlatformStand);
            s_ps_hum = s_body.humanoid;
            s_ps_saved = true;
        }
        write<bool>(s_body.humanoid + Offsets::Humanoid::PlatformStand, true);
    } else if (s_ps_saved) {
        if (is_valid_address(s_ps_hum))
            write<bool>(s_ps_hum + Offsets::Humanoid::PlatformStand, s_ps_value);
        s_ps_saved = false;
        s_ps_hum = 0;
    }
}

void Anchor(Owner who, bool on)
{
    if (s_owner != who || !s_body.valid) return;
    const uint8_t bit = (uint8_t)Offsets::PrimitiveFlags::Anchored;
    if (on) {
        if (!s_anchor_saved) {
            s_anchor_value = read<uint8_t>(s_body.primitive + Offsets::Primitive::Flags);
            s_anchor_prim = s_body.primitive;
            s_anchor_saved = true;
        }
        write<uint8_t>(s_body.primitive + Offsets::Primitive::Flags,
                       (uint8_t)(s_anchor_value | bit));
    } else if (s_anchor_saved) {
        if (is_valid_address(s_anchor_prim))
            write<uint8_t>(s_anchor_prim + Offsets::Primitive::Flags, s_anchor_value);
        s_anchor_saved = false;
        s_anchor_prim = 0;
    }
}

bool IsAnchored() { return s_anchor_saved; }

float WritesPerMs() { return s_wr_per_ms; }

bool HoldPosition(Owner who, const float pos[3], const float vel[3])
{
    if (s_owner != who || !s_body.valid || !pos) return false;
    Motion m{};
    if (!ReadMotion(m)) return false;   // could not read: DO NOT report armed
    m.pos[0] = pos[0]; m.pos[1] = pos[1]; m.pos[2] = pos[2];
    // The hold's VELOCITY is caller policy, and it is not a detail:
    //
    //  ZERO (a teleport) says "no motion through the space skipped" - right
    //  for a jump, wrong for flight. Flight measured the wrongness as
    //  rubberbanding: with velocity pinned at 0 while position moves, the
    //  replicator forwards "standing still" every tick, the server's copy of
    //  the character stays BEHIND ours, and each replication restore yanks the
    //  character back along the flight path until our next write snaps it
    //  forward - visible as rubberband-then-snap, worse the faster you fly.
    //
    //  THE FLIGHT VELOCITY (vel = path direction x speed) does two jobs at
    //  once: the engine's own integration between our writes moves the
    //  character exactly along our path (nothing to snap back from), and the
    //  replicator forwards a velocity the server can dead-reckon with, so the
    //  restore target TRACKS us instead of lagging us.
    if (vel) {
        m.lin[0] = vel[0]; m.lin[1] = vel[1]; m.lin[2] = vel[2];
    } else {
        m.lin[0] = m.lin[1] = m.lin[2] = 0.0f;
    }
    m.ang[0] = m.ang[1] = m.ang[2] = 0.0f;

    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    s_hold_motion = m;
    s_hold_prim = s_body.primitive;
    s_hold_active = true;
    }

    // Write once inline too, so the hold starts on this iteration rather than
    // whenever the thread next wakes. Tail only - the rotation in m is carried
    // for nothing and written by nobody (see WriteHoldTailTo).
    const uintptr_t a = s_body.primitive + Offsets::Primitive::Rotation + kPosOff;
    write_raw(a, &m.pos, sizeof(m.pos) + sizeof(m.lin) + sizeof(m.ang));
    write_raw(a + Stride(), &m.pos, sizeof(m.pos) + sizeof(m.lin) + sizeof(m.ang));
    return true;
}

void VelocityBurst(uintptr_t primitive, const float v[3], uint32_t duration_ms)
{
    if (!is_valid_address(primitive) || !v) return;
    // Inline write first, so the burst starts on this call rather than whenever
    // the hold thread next wakes - the test's first loop iteration. Identical
    // addresses to the hold thread's hammer and to the debug test: rotation +
    // 0x30 in both motion blocks, unconditionally - the test works without a
    // layout audit and so does its thread form.
    const uintptr_t pa = primitive + Offsets::Primitive::Rotation;
    write_raw(pa + kLinOff, v, 12);
    write_raw(pa + Stride() + kLinOff, v, 12);
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    s_vburst_prim = primitive;
    s_vburst_v[0] = v[0]; s_vburst_v[1] = v[1]; s_vburst_v[2] = v[2];
    s_vburst_deadline = platform::NowMs() + duration_ms;
    s_vburst_active = true;
    }
    s_vburst_writes.exchange(1, std::memory_order_relaxed);
}

void ExtendVelocityBurst(uint32_t duration_ms)
{
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    if (s_vburst_active)
        s_vburst_deadline = platform::NowMs() + duration_ms;
    }
}

void StopVelocityBurst()
{
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    s_vburst_active = false;
    s_vburst_prim = 0;
    }
}

bool VelocityBurstActive()
{
    bool a = false;
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex); a = s_vburst_active; }
    return a;
}

unsigned long VelocityBurstWrites()
{
    return (unsigned long)s_vburst_writes.load(std::memory_order_relaxed);
}

void HoldCameraCFrame(uintptr_t camera_address, const float cf[12],
                       uintptr_t workspace_address)
{
    if (!is_valid_address(camera_address) || !cf) return;
    // FRESH-ARM GATING. HoldCameraCFrame is called every feature tick, and
    // the round-8 log showed two self-inflicted aggravators when re-arm did
    // everything the first arm did: (1) the INLINE WRITE from this thread
    // raced the hold thread's paced/hot writes - two generations of our own
    // pose alternating in memory = micro-jitter that is entirely OURS; and
    // (2) the per-arm state reset wiped the flap/rival counters and the
    // once-per-session log flag every tick (the "subject detached" spam that
    // drowned the user's log - 60 lines/sec). Now: re-arm only updates the
    // desired pose under the lock; counters reset and the inline write (and
    // subject detach) happen ONLY on a fresh arm, where the inline write
    // still matters - the hold thread may be parked in platform::SleepMs(2) and the hold
    // must start NOW.
    bool fresh_arm = false;
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    fresh_arm = !s_camhold_active || s_camhold_addr != camera_address;
    s_camhold_addr = camera_address;
    for (int i = 0; i < 12; ++i) s_camhold_cf[i] = cf[i];
    for (int i = 0; i < 12; ++i) s_cam_written[i] = cf[i];
    s_camhold_ws = workspace_address;
    if (fresh_arm) {
        s_cam_subject_said = false;
        s_cam_matrix_logged = false;
        s_cam_contest_hot_until = 0;
        s_cam_feed = false;
        s_cam_feed_votes = 0;
        s_cam_contest_prev_at = 0;
        s_cam_sync_last_ns = 0;
        s_cam_subject_at = 0;
        s_cam_flaps = 0;
        s_cam_flap_total = 0;
        s_cam_flap_logged = false;
        s_cam_contests = 0;
        s_cam_contest_total = 0;
        s_cam_contest_logged = false;
        s_cam_contest_log_at = 0;
        for (int i = 0; i < kCamRivalMax; ++i) s_cam_rivals[i] = 0;
        s_cam_rival_count = 0;
    }
    s_camhold_active = true;
    }
    if (fresh_arm) {
        WriteCamUnit(camera_address, cf);
        static const uintptr_t kNilSubjectArm = 0;
        write_raw(camera_address + Offsets::Camera::CameraSubject,
                  &kNilSubjectArm, sizeof(kNilSubjectArm));
        if (!s_cam_subject_said) {
            s_cam_subject_said = true;
            LogLine("camera: subject detached while freecam holds (saved for "
                    "restore) - starves the follow-cam orbit writer");
        }
    }
}

void GuardCameraFields(uintptr_t camera_address, int type, uintptr_t subject,
                       uint32_t timeout_ms)
{
    if (!is_valid_address(camera_address)) return;
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    s_guard_addr = camera_address;
    s_guard_type = type;
    s_guard_subject = subject;
    s_guard_until = platform::NowMs() + timeout_ms;
    s_guard_last = 0;
    }
}

bool CameraHoldFeeding()
{
    bool f = false;
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex); f = s_cam_feed; }
    return f;
}

void ReleaseCameraHold()
{
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    s_camhold_active = false;
    s_camhold_addr = 0;
    s_camhold_ws = 0;      // disarms the flap guard with the hold
    for (int i = 0; i < kCamRivalMax; ++i) s_cam_rivals[i] = 0;
    s_cam_rival_count = 0;
    s_cam_subject_said = false;
    s_cam_matrix_logged = false;
    s_cam_contest_hot_until = 0;
    s_cam_feed = false;
    s_cam_feed_votes = 0;
    s_cam_contest_prev_at = 0;
    s_cam_sync_last_ns = 0;
    s_cam_subject_at = 0;
    }
}

void ReleaseHold(Owner who)
{
    if (s_owner != who && s_owner != None) return;
    { std::lock_guard<std::mutex> hold_lock(s_hold_mutex);
    s_hold_active = false;
    s_hold_prim = 0;
    }
}

void FreezeWalk(Owner who, bool on)
{
    if (s_owner != who || !is_valid_address(s_body.humanoid)) return;
    if (on) {
        if (!s_walk_saved) {
            s_walk_value = read<float>(s_body.humanoid + Offsets::Humanoid::Walkspeed);
            s_walk_autorot = read<bool>(s_body.humanoid + Offsets::Humanoid::AutoRotate);
            if (!(s_walk_value > 0.0f && s_walk_value < 1000.0f)) s_walk_value = 16.0f;
            s_walk_hum = s_body.humanoid;
            s_walk_saved = true;
        }
        write<float>(s_body.humanoid + Offsets::Humanoid::Walkspeed, 0.0f);
    } else if (s_walk_saved) {
        if (is_valid_address(s_walk_hum)) {
            write<float>(s_walk_hum + Offsets::Humanoid::Walkspeed, s_walk_value);
            write<bool>(s_walk_hum + Offsets::Humanoid::AutoRotate, s_walk_autorot);
        }
        s_walk_saved = false;
        s_walk_hum = 0;
    }
}

} // namespace mover
} // namespace features
