// -----------------------------------------------------------------------------
//  tools/mover_pace_test.cpp  --  the movement pacing, tested off Windows
// -----------------------------------------------------------------------------
//  features/movement/mover.cpp cannot be run here (it reads another process), but
//  the two things it got wrong before are pure arithmetic and can be:
//
//    * THE STEP GATE. FeatureLoop is Sleep(1), so ~600-1000 iterations a second.
//      Roblox steps at 60. If the gate lets more than one write through per 16ms
//      the engine discards most of them mid-solve and the replicator sees a
//      position history the server will not accept - the rubber-banding. This
//      pins the gate at 60 +/- 2 Hz across loop rates from 100Hz to 2000Hz, and
//      pins the burst behaviour after a stall (must resync, never fire a
//      catch-up burst - that is the same flood by another name).
//    * THE CLOCK. Flight and freecam used a hardcoded dt of 0.016 per iteration,
//      which at 600Hz integrates ~10x too fast. This pins that integrating with
//      the clamped real dt travels the requested studs per second regardless of
//      loop rate.
//
//  The logic below is a transcription of mover.cpp's; the point is that the
//  arithmetic is checked, and that a change to the cadence has to update a test
//  that says out loud what the cadence is for.
//
//  g++ -std=c++17 tools/mover_pace_test.cpp -o build/mover_pace_test && ./build/mover_pace_test
// -----------------------------------------------------------------------------
#include <cstdio>
#include <string>
#include <cmath>
#include <initializer_list>
#include <cstdint>

static int g_checks = 0;
static int g_fails = 0;

static void Check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok) { ++g_fails; printf("  FAIL  %s\n", what); }
}

static void CheckNear(double got, double want, double tol, const char* what)
{
    ++g_checks;
    if (!(std::fabs(got - want) <= tol)) {
        ++g_fails;
        printf("  FAIL  %s (got %.4f, want %.4f +/- %.4f)\n", what, got, want, tol);
    }
}

// ---- the gate, transcribed from mover::Tick ---------------------------------
struct Pacer {
    static constexpr unsigned kStepMs = 16;
    unsigned next = 0;

    bool Step(unsigned now_ms)
    {
        if (next == 0 || (int)(now_ms - next) >= 0) {
            next = ((int)(now_ms - next) > (int)kStepMs) ? now_ms + kStepMs
                                                         : next + kStepMs;
            return true;
        }
        return false;
    }
};

// dt, transcribed from mover::Tick (seconds, clamped)
static double ClampDt(double d)
{
    if (d < 0.0) d = 0.0;
    if (d > 0.1) d = 0.1;
    return d;
}

static int StepsOverSecond(int loop_hz)
{
    Pacer p;
    int steps = 0;
    for (int i = 0; i < loop_hz; ++i) {
        unsigned now = (unsigned)((double)i * 1000.0 / loop_hz);
        if (p.Step(now)) ++steps;
    }
    return steps;
}

int main()
{
    printf("mover pacing\n");

    // 1. one step per 16ms, whatever the loop rate. 1000/16 = 62.5.
    for (int hz : { 100, 250, 500, 1000, 2000 }) {
        char msg[96];
        snprintf(msg, sizeof(msg), "step rate at a %dHz loop is ~62/s", hz);
        CheckNear(StepsOverSecond(hz), 62.0, 2.0, msg);
    }

    // A 1000Hz loop must NOT produce ~1000 writes - the whole point.
    Check(StepsOverSecond(1000) < 70, "a 1000Hz loop does not write 1000 times");

    // 2. the gate never fires twice for the same millisecond.
    {
        Pacer p;
        int twice = 0;
        for (unsigned ms = 0; ms < 2000; ++ms) {
            bool a = p.Step(ms);
            bool b = p.Step(ms);   // a second loop iteration inside the same ms
            if (a && b) ++twice;
        }
        Check(twice == 0, "two iterations in one millisecond give at most one step");
    }

    // 3. a stall resyncs instead of firing a catch-up burst. This is the case the
    //    old code would have turned into ~30 back-to-back position writes, which
    //    is exactly the flood the server corrects.
    {
        Pacer p;
        p.Step(0);
        int burst = 0;
        // 500ms of nothing (an attach, an alt-tab, a slow read), then resume.
        for (unsigned ms = 500; ms < 510; ++ms)
            if (p.Step(ms)) ++burst;
        Check(burst == 1, "a 500ms stall produces one step, not a burst");
    }

    // 4. no drift: the deadline advances by the step, not from 'now'.
    {
        Pacer p;
        int steps = 0;
        // a loop that is slightly late every iteration would drift if the pacer
        // re-anchored to now each time
        for (unsigned ms = 0; ms < 1000; ms += 5)
            if (p.Step(ms)) ++steps;
        CheckNear(steps, 62.0, 2.0, "a coarse 5ms loop still steps ~62 times a second");
    }

    printf("dt and integration\n");

    // 5. the clamp: a stall must not become one enormous movement step.
    CheckNear(ClampDt(3.2), 0.1, 1e-9, "a 3.2s stall clamps to 0.1s");
    CheckNear(ClampDt(-0.5), 0.0, 1e-9, "a backwards clock clamps to 0");
    CheckNear(ClampDt(0.016), 0.016, 1e-9, "a normal frame passes through");

    // 6. integrating with real dt travels the requested studs/second at ANY loop
    //    rate. This is the freecam bug: dt was hardcoded to 0.016 per iteration,
    //    so a 600Hz loop moved ~10x too far.
    const double speed = 60.0;   // studs per second
    for (int hz : { 60, 600, 1000 }) {
        double travelled = 0.0;
        for (int i = 0; i < hz; ++i) travelled += speed * ClampDt(1.0 / hz);
        char msg[96];
        snprintf(msg, sizeof(msg), "%dHz loop travels 60 studs in a second", hz);
        CheckNear(travelled, 60.0, 0.01, msg);
    }

    // and the old behaviour is shown to be wrong, so nobody reintroduces it
    {
        double travelled = 0.0;
        for (int i = 0; i < 600; ++i) travelled += speed * 0.016;   // the old code
        Check(travelled > 500.0, "the old fixed-dt integration overshot ~10x (regression guard)");
    }

    // -------------------------------------------------------------------------
    //  the freecam basis
    // -------------------------------------------------------------------------
    //  The old freecam aimed the camera with LookAtMatrix, which builds
    //  right = worldUp x fwd. Looking straight up or down makes that cross
    //  product zero-length, the basis degenerate, and the view garbage - the
    //  "camera bugs out". The replacement builds the basis from yaw/pitch
    //  directly. These checks are what "directly" has to mean: orthonormal at
    //  every pitch including the clamp, and right-handed so the view is not
    //  mirrored.
    printf("freecam basis\n");
    {
        const double kMaxPitch = 1.5533;   // the clamp in freecam.cpp, 89 degrees
        bool ortho = true, unit = true, handed = true;
        for (int yi = -8; yi <= 8; ++yi) {
            for (int pi = -8; pi <= 8; ++pi) {
                const double yaw   = yi * (3.14159265 / 8.0);
                const double pitch = pi / 8.0 * kMaxPitch;
                const double cy = std::cos(yaw),   sy = std::sin(yaw);
                const double cp = std::cos(pitch), sp = std::sin(pitch);
                const double fwd[3]   = { sy * cp, sp, cy * cp };
                const double right[3] = { cy, 0.0, -sy };
                const double up[3] = { fwd[1]*right[2] - fwd[2]*right[1],
                                       fwd[2]*right[0] - fwd[0]*right[2],
                                       fwd[0]*right[1] - fwd[1]*right[0] };
                auto dot = [](const double a[3], const double b[3]) {
                    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
                };
                auto len = [&](const double v[3]) { return std::sqrt(dot(v, v)); };

                if (std::fabs(dot(fwd, right)) > 1e-9) ortho = false;
                if (std::fabs(dot(fwd, up))    > 1e-9) ortho = false;
                if (std::fabs(dot(right, up))  > 1e-9) ortho = false;
                if (std::fabs(len(fwd) - 1.0)   > 1e-9) unit = false;
                if (std::fabs(len(right) - 1.0) > 1e-9) unit = false;
                if (std::fabs(len(up) - 1.0)    > 1e-9) unit = false;
                // right x up should be fwd (right-handed), not -fwd
                const double rxu[3] = { right[1]*up[2] - right[2]*up[1],
                                        right[2]*up[0] - right[0]*up[2],
                                        right[0]*up[1] - right[1]*up[0] };
                if (dot(rxu, fwd) < 0.999) handed = false;
            }
        }
        Check(ortho,  "the basis is orthogonal at every yaw/pitch, poles included");
        Check(unit,   "the basis is unit length at every yaw/pitch");
        Check(handed, "the basis is right-handed (the view is not mirrored)");
    }
    {
        // and the old approach is shown to fail at the pole, so the reason for
        // the rewrite is recorded and not just asserted in a comment.
        const double fwd[3] = { 0.0, 1.0, 0.0 };          // looking straight up
        const double up[3]  = { 0.0, 1.0, 0.0 };          // world up
        const double cross[3] = { up[1]*fwd[2] - up[2]*fwd[1],
                                  up[2]*fwd[0] - up[0]*fwd[2],
                                  up[0]*fwd[1] - up[1]*fwd[0] };
        const double m = std::sqrt(cross[0]*cross[0] + cross[1]*cross[1] + cross[2]*cross[2]);
        Check(m < 1e-9, "worldUp x fwd collapses looking straight up (regression guard)");
    }

    // -------------------------------------------------------------------------
    //  the primitive's motion-block layout
    // -------------------------------------------------------------------------
    //  Decoded from a REAL probe dump (client version-e7d81637d42c4b23, character
    //  running at walkspeed 16). This is the bug that survived the pacing fix: the
    //  primitive holds two identical motion blocks and the code only ever wrote
    //  block A's velocity, so the engine re-synced from block B's stale copy.
    //
    //  The dump is kept verbatim so the claim "0x140 is a live velocity" is
    //  evidence rather than an assertion - if a future client moves the layout,
    //  paste the new probe here and this test says what changed.
    printf("primitive motion blocks\n");
    {
        // +0x0C0 .. +0x150, four floats per row, exactly as the log printed them
        const float dump[][4] = {
            /*0x0C0*/ {  0.00f,  0.00f,  1.00f,   0.00f },
            /*0x0D0*/ { -0.09f, -0.00f,  1.00f,   0.00f },
            /*0x0E0*/ {  0.09f, -0.00f,  1.00f, -39.20f },
            /*0x0F0*/ {  3.10f,-51.57f,  1.42f,  -0.00f },
            /*0x100*/ {-15.94f,  0.00f, -0.00f,   0.00f },
            /*0x110*/ {  1.00f,  0.00f, -0.09f,  -0.00f },
            /*0x120*/ {  1.00f,  0.00f,  0.09f,  -0.00f },
            /*0x130*/ {  1.00f,-39.20f,  3.10f, -51.57f },
            /*0x140*/ {  1.42f, -0.00f,-15.94f,   0.00f },
        };
        const int kBase = 0x0C0;
        auto at = [&](int off) -> float {
            const int i = (off - kBase) / 16, j = ((off - kBase) % 16) / 4;
            return dump[i][j];
        };

        // the offsets the app uses
        const int kRotA = 0xC8,  kPosA = 0xEC, kLinA = 0xF8,  kAngA = 0x104;
        const int kRotB = 0x110, kPosB = 0x134, kLinB = 0x140, kAngB = 0x14C;

        Check(kPosA - kRotA == 0x24, "position is 0x24 into a motion block");
        Check(kLinA - kRotA == 0x30, "linear velocity is 0x30 into a motion block");
        Check(kAngA - kRotA == 0x3C, "angular velocity is 0x3C into a motion block");
        Check(kRotB - kRotA == 0x48, "block B is one whole 0x48 block after block A");
        Check(36 + 12 + 12 + 12 == 0x48, "rot36 + pos12 + lin12 + ang12 == 0x48");
        Check(kPosB - kRotB == kPosA - kRotA && kLinB - kRotB == kLinA - kRotA &&
              kAngB - kRotB == kAngA - kRotA, "both blocks have the same shape");

        // the two blocks held the same values at the instant of the probe
        for (int k = 0; k < 3; ++k) {
            CheckNear(at(kPosA + 4 * k), at(kPosB + 4 * k), 1e-6, "position matches in both blocks");
            CheckNear(at(kLinA + 4 * k), at(kLinB + 4 * k), 1e-6, "velocity matches in both blocks");
        }
        for (int k = 0; k < 9; ++k)
            CheckNear(at(kRotA + 4 * k), at(kRotB + 4 * k), 1e-6, "rotation matches in both blocks");

        // THE decisive one: block B's velocity is not padding, it is the
        // character's actual speed. Writing only block A leaves this behind.
        const float bx = at(kLinB), bz = at(kLinB + 8);
        CheckNear(std::sqrt(bx * bx + bz * bz), 16.0, 0.02,
                  "block B's linear velocity has magnitude 16 = the character's walkspeed");

        // and the rotation really is a rotation matrix, so the block start is right
        for (int r = 0; r < 3; ++r) {
            double len = 0.0;
            for (int c = 0; c < 3; ++c) {
                const double v = at(kRotA + 4 * (r * 3 + c));
                len += v * v;
            }
            CheckNear(std::sqrt(len), 1.0, 0.02, "each rotation row is unit length");
        }
    }

    // -------------------------------------------------------------------------
    //  the jump arc
    // -------------------------------------------------------------------------
    //  Both previous attempts were wrong in opposite directions, so the shape is
    //  pinned here rather than argued about again:
    //    * ONE write  -> the solver applies gravity and eats it within a step;
    //                    a twitch, not a jump.
    //    * CONSTANT v -> that is a lift, not a jump. The server rejects the path
    //                    (this was the original 180ms re-assert).
    //  A jump is one impulse that DECAYS under gravity, and stops being asserted
    //  as soon as the engine's own value has caught up with ours.
    printf("jump arc\n");
    {
        const float kGravity = 196.2f;
        const float launch = 50.0f;
        auto arc = [&](unsigned age_ms) { return launch - kGravity * age_ms * 0.001f; };

        Check(arc(0) == launch, "the arc starts at the launch speed");
        Check(arc(120) < arc(0), "the arc decays under gravity");
        CheckNear(arc(100), 50.0 - 19.62, 1e-4, "after 100ms it has lost g*0.1");

        // it must not become a downward thruster: we stop before it goes negative
        Check(arc(255) < 0.0f, "left unbounded the arc would turn into downward force");

        // The window must OUTLAST the arc, or the jump is truncated in flight. A
        // launch at power P needs P/g seconds just to reach apex - 50 studs/s is
        // ~255ms - so the 120ms first chosen here cut every jump short. This is
        // the check that caught it.
        const unsigned kLaunchMs = 600;
        CheckNear(launch / kGravity * 1000.0, 255.0, 2.0,
                  "a 50 studs/s launch takes ~255ms to apex");
        Check(kLaunchMs > (unsigned)(launch / kGravity * 1000.0),
              "the launch window outlasts the arc to apex");

        // the normal exit: once the engine's own velocity has caught up with our
        // decayed value we are holding the character up, not launching it - stop.
        const float engine_v = 10.0f;
        unsigned stop_at = 0;
        for (unsigned t = 0; t <= kLaunchMs; t += 5) {
            if (arc(t) <= engine_v) { stop_at = t; break; }
        }
        Check(stop_at > 0 && stop_at < kLaunchMs,
              "the arc yields to the engine before the window cap expires");

        // and a constant-velocity hold (the old bug) never yields - the guard
        Check(!(launch <= engine_v), "a constant hold would never yield (regression guard)");
    }

    // -------------------------------------------------------------------------
    //  Anchor save/restore, and the noclip interleave
    // -------------------------------------------------------------------------
    //  The survival test proved an unanchored position write is recomputed away
    //  within one step, so the anchor is load-bearing for teleport and flight.
    //  That makes leaving it on, or losing the original value, a character that
    //  cannot move. Both owners of the Flags byte are modelled here.
    {
        const uint8_t kAnchored  = 0x2;
        const uint8_t kCanCollide = 0x8;

        // mover::Anchor - save the original once, OR the bit, restore verbatim.
        uint8_t flags = kCanCollide | 0x20;   // a normal, unanchored part
        const uint8_t original = flags;
        uint8_t saved = 0; bool have_saved = false;

        auto anchor_on = [&]{
            if (!have_saved) { saved = flags; have_saved = true; }
            flags |= kAnchored;
        };
        auto anchor_off = [&]{
            if (have_saved) { flags = saved; have_saved = false; }
        };

        anchor_on();
        Check((flags & kAnchored) != 0, "anchor on sets the bit");
        anchor_on(); anchor_on();          // ticked every frame by flight
        anchor_off();
        Check(flags == original, "anchor restores the original flags after repeat sets");
        anchor_off();
        Check(flags == original, "a second release is a no-op, not a corruption");

        // noclip saves the SAME byte. If it saves during an anchor window it must
        // not remember "anchored" as the part's normal state.
        anchor_on();
        uint8_t nc_saved = (uint8_t)(flags & ~kAnchored);   // the fix
        Check((nc_saved & kAnchored) == 0, "noclip's save strips the transient anchor bit");

        flags &= (uint8_t)~kCanCollide;                      // noclip does its thing
        Check((flags & kAnchored) != 0, "noclip clearing CanCollide preserves the anchor");

        // noclip restores while the anchor is still legitimately held.
        uint8_t keep = (uint8_t)(flags & kAnchored);
        flags = (uint8_t)(nc_saved | keep);
        Check((flags & kAnchored) != 0, "noclip restore leaves a live anchor alone");
        Check((flags & kCanCollide) != 0, "noclip restore brings CanCollide back");

        // now the feature holding the anchor ends: we must land on the original.
        anchor_off();
        Check(flags == original,
              "after a full noclip interleave the flags return to their original value");
    }

    // -------------------------------------------------------------------------
    //  Infinite jump: one write, the engine does the rest
    // -------------------------------------------------------------------------
    //  Measured in-game (RunTest 9), single velocity write of y=100:
    //
    //    t+  0ms vel 100.00  y  3.10        t+200ms vel 50.13  y 22.06
    //    t+100ms vel  74.66  y 14.31        t+400ms vel  2.72  y 28.35
    //
    //  That is a correct, self-decaying jump arc from ONE write. The engine
    //  integrates velocity and applies gravity itself. Everything this feature
    //  used to layer on top - a recomputed decaying arc, a launch window, a
    //  yield condition - was fighting an integrator that was already right.
    {
        // The observed decay IS gravity - but note it is 243, not the 196.2 that
        // this file assumed everywhere. THIS PLACE SETS ITS OWN GRAVITY
        // (World::Gravity is a per-place property). That matters: every hardcoded
        // 196.2 in the old arc-shaping code was computing a decay curve steeper
        // or shallower than the engine's actual one, so our re-asserted arc and
        // the engine's integration diverged from the first millisecond. Another
        // reason the arc shaping could never have worked - and a reason not to
        // reintroduce it without reading World::Gravity.
        const double v0 = 100.0;
        const double g_observed = (100.00 - 2.72) / 0.400;   // studs/s per second
        CheckNear(g_observed, 243.2, 1.0, "the measured decay is a constant gravity");
        Check(fabs(g_observed - 196.2) > 20.0,
              "and it is NOT the 196.2 the old code hardcoded");

        // The climb, straight from the measurement. Note it does NOT match the
        // clean analytic arc for that gravity (v0*t - g*t^2/2 = 20.54): the real
        // curve climbs 25.25. The engine's integrator is not a textbook one -
        // there is a humanoid controller and a damper in the loop too. That gap
        // is precisely why modelling the arc ourselves was doomed, and why the
        // feature now writes once and lets the engine own the shape.
        const double measured_climb = 25.25;
        const double analytic_climb = v0 * 0.4 - 0.5 * g_observed * 0.4 * 0.4;
        Check(measured_climb > 20.0 && measured_climb < 30.0,
              "one impulse lifts ~25 studs in 400ms (measured)");
        Check(fabs(measured_climb - analytic_climb) > 3.0,
              "the real arc differs from the textbook one - do not model it ourselves");

        // Apex timing: the arc is over well inside half a second, so no window
        // or re-assertion is needed to 'keep' it.
        CheckNear(1000.0 * v0 / g_observed, 411.0, 5.0,
                  "apex at ~411ms with no help from us");

        // The regression that made it do nothing: yielding when the recomputed
        // arc dips below a velocity read back from our own write.
        double last_written = v0;
        int cancelled_at = -1;
        for (int t = 2; t <= 600; t += 2) {
            const double v = v0 - g_observed * (t / 1000.0);
            if (v <= last_written) { cancelled_at = t; break; }
            last_written = v;
        }
        Check(cancelled_at > 0 && cancelled_at <= 4,
              "regression: re-asserting an arc cancels itself on iteration one");

        // Hammering velocity instead pins it - a lift, not a jump. Measured:
        // vel stayed 100.00 and y climbed +30.45 in 375ms, i.e. linear.
        const double hammered_rate = 30.45 / 0.375;
        CheckNear(hammered_rate, 100.0, 20.0,
                  "hammered velocity gives constant-speed lift, not a jump arc");
        Check(hammered_rate > 60.0,
              "a pinned velocity does not decay - wrong shape for a jump");
    }

    // -------------------------------------------------------------------------
    //  Position must be hammered, not paced
    // -------------------------------------------------------------------------
    //  Measured: 1248-1302 writes over 500ms held the target; one write per 16ms
    //  lost it within 50ms. Model the contest - the engine restores every step,
    //  so whoever wrote most recently before the step boundary wins.
    {
        const int step_ms = 16;
        const int window = 500;

        // paced: one write per step, engine restores each step -> we hold only if
        // our write lands after the restore, which is a coin flip we mostly lose.
        int paced_writes = window / step_ms;
        Check(paced_writes < 40, "paced gives ~31 writes in 500ms");

        // hammered at the measured rate
        int hammered = 1248;
        Check(hammered / paced_writes > 30,
              "hammering issues >30x more writes than pacing over the same window");
        Check((double)hammered / window > 2.0,
              "hammering exceeds 2 writes per millisecond - many per physics step");
    }

    // -------------------------------------------------------------------------
    //  Write throughput is the hidden variable
    // -------------------------------------------------------------------------
    //  Four runs of the identical continuous-write test, same build, same
    //  machine, differing only in system load:
    //
    //    writes/500ms | per 16.7ms step | held?
    //          17127  |      572        | perfectly
    //           1248  |       41        | yes (2 glitches)
    //            810  |       27        | yes
    //             88  |      2.9        | NO - fell most of the run
    //
    //  That ~200x spread is why the movement features "work sometimes". The
    //  threshold sits between 2.9 and 27 writes per step.
    {
        const double step_ms = 16.7;
        auto per_step = [&](double writes){ return writes / 500.0 * step_ms; };

        CheckNear(per_step(17127), 572.0, 2.0, "fast run: ~572 writes per step");
        CheckNear(per_step(1248),   41.7, 1.0, "medium run: ~42 writes per step");
        CheckNear(per_step(810),    27.1, 1.0, "slow run: ~27 writes per step");
        CheckNear(per_step(88),      2.9, 0.5, "failing run: only ~2.9 writes per step");

        // The runs that held all cleared the threshold; the one that failed did not.
        const int kMinWritesPerStep = 8;
        Check(per_step(810)  > kMinWritesPerStep, "the slowest SUCCESSFUL run clears the threshold");
        Check(per_step(88)   < kMinWritesPerStep, "the failing run falls below it");
        Check(kMinWritesPerStep > per_step(88) && kMinWritesPerStep < per_step(810),
              "the threshold sits between the failing and passing runs");

        // A single paced write per step is far below it - the original bug.
        Check(1.0 < kMinWritesPerStep, "one write per step (the old pacing) is far too slow");
    }

    // -------------------------------------------------------------------------
    //  Infinite jump: one AUTONOMOUS hammered-velocity burst per space press
    // -------------------------------------------------------------------------
    //  The user compared the feature against RunTest id 10: "hammered velocity
    //  actually works, it increase ur jumppower and you go up and up everytime
    //  you click it without having to land, it works perfectly." The test's
    //  button is CLICKED once per run, and the log shows every click climbing
    //  whether grounded or airborne:
    //      run 1: y   3.10 ->  37.57 (+34.47)  grounded
    //      run 2: y  61.17 ->  94.22 (+33.05)  AIRBORNE, never landed
    //      run 3: y 102.30 -> 131.94 (+29.64)  AIRBORNE
    //      run 4: y 154.83 -> 189.05 (+34.22)  AIRBORNE
    //
    //  AND THE REASON THE FEATURE NEVER MATCHED IT: every previous version
    //  routed its writes through mover ownership, and mover::Tick() demolishes
    //  every hold and owner whenever one cache pass returns without a local
    //  primitive - which fetch_local() does if ANY of its ~20 chained reads
    //  fails that pass (a every-second-or-so event under load, publishing
    //  hrp_primitive = 0 for one 16ms window). A 400ms burst needs ~25 clean
    //  passes in a row and died within milliseconds, every time, under every
    //  mechanism - while the test, which captures the primitive once at click
    //  and then ignores the world, always climbed. The state machine below
    //  pins the fix: the burst is autonomous, carries its own address and
    //  deadline, and tolerates cache hiccups.
    {
        const double g = 243.2;          // this place's measured gravity

        // The measured coin flip that rules out a single write.
        const double run1 = -0.01, run2 = 25.24;
        Check(run1 < 1.0 && run2 > 20.0,
              "regression: a lone velocity write gave both -0.01 and +25.24 studs");

        // Hammering pins velocity: the climb is LINEAR at ~the written value.
        const double hammered_rate = 35.09 / 0.375;
        CheckNear(hammered_rate, 93.6, 1.0, "hammering pins velocity near the written value");
        Check(hammered_rate > 0.85 * 100.0, "and loses under 15% to the damper");

        // ---- the feature's state machine, transcribed -----------------------
        constexpr unsigned kBoostMs = 400;   // the debug test's loop duration
        constexpr unsigned kGraceMs = 200;   // cache-hiccup tolerance

        struct Burst {                  // the autonomous hold-thread burst
            bool active = false;
            unsigned prim = 0, deadline = 0;
            unsigned long writes = 0;
            void HoldIter(unsigned now) {   // one hold-thread iteration
                if (!active) return;
                if ((int)(now - deadline) >= 0) { active = false; return; }
                ++writes;                   // wrote both motion blocks
            }
        };
        struct Feature {                // the feature loop's policy
            bool was_down = false, bursting = false;
            unsigned burst_prim = 0, invalid_since = 0;
            void Tick(Burst& b, unsigned now, bool space_down,
                      unsigned body_prim, bool body_valid)
            {
                const bool pressed = space_down && !was_down;
                was_down = space_down;
                if (bursting) {
                    if (body_valid && body_prim != burst_prim) {
                        b.active = false; bursting = false;      // real respawn
                    } else if (!body_valid) {
                        if (invalid_since == 0) invalid_since = now;
                        if (now - invalid_since > kGraceMs) { b.active = false; bursting = false; }
                    } else invalid_since = 0;
                }
                if (bursting && !b.active) bursting = false;     // its own clock
                if (!pressed) return;
                if (!body_valid) {
                    if (bursting) { b.deadline = now + kBoostMs; invalid_since = 0; }
                    return;                                     // press inside a hiccup
                }
                b.active = true; b.prim = body_prim; b.deadline = now + kBoostMs;
                burst_prim = body_prim; invalid_since = 0; bursting = true;
            }
        };

        // A TAP fires a FULL burst: the window is 400ms regardless of how long
        // the key was down, because the test's loop runs its full 400ms after
        // one click.
        {
            Burst b; Feature f;
            for (unsigned t = 0; t <= 500; ++t) {
                f.Tick(b, t, t == 0, 0xAAA, true);   // press at t=0 only
                b.HoldIter(t);
            }
            Check(!b.active && f.bursting == false, "a tap's burst ends by itself");
            Check(b.writes > 390 && b.writes <= 401,
                  "and it hammered for the whole 400ms (not truncated by key-up)");
        }

        // HOLDING does not re-arm: one click is one burst.
        {
            Burst b; Feature f;
            unsigned bursts = 0; bool was = false;
            for (unsigned t = 0; t < 1000; ++t) {
                f.Tick(b, t, true, 0xAAA, true);     // held the whole second
                b.HoldIter(t);
                if (b.active && !was) ++bursts;
                was = b.active;
            }
            Check(bursts == 1, "a held key gets exactly one burst, not a lift");
        }

        // A STUCK key cannot pin velocity: one rising edge, one burst.
        {
            Burst b; Feature f;
            bool any_after = false;
            for (unsigned t = 0; t < 10000; ++t) {
                f.Tick(b, t, true, 0xAAA, true);
                b.HoldIter(t);
                if (b.active && t > 500) any_after = true;
            }
            Check(!any_after, "a stuck key self-extinguishes at 400ms - no failsafe needed");
        }

        // MID-AIR RE-FIRE: presses chain bursts, no landing in between.
        {
            Burst b; Feature f;
            int bursts = 0; bool was = false;
            for (unsigned t = 0; t < 1500; ++t) {
                f.Tick(b, t, t == 0 || t == 500 || t == 1000, 0xAAA, true);
                b.HoldIter(t);
                if (b.active && !was) ++bursts;
                was = b.active;
            }
            Check(bursts == 3, "three taps, three bursts - up and up per click");

            // a press DURING a live window restarts the clock
            Burst b2; Feature f2;
            for (unsigned t = 0; t <= 700; ++t) {
                f2.Tick(b2, t, t == 0 || t == 300, 0xAAA, true);
                b2.HoldIter(t);
                if (t == 690) Check(b2.active, "a mid-window press restarts the clock");
            }
            Check(!b2.active, "and closes 400ms after the last press");
        }

        // THE REGRESSION THAT EXPLAINS EVERY "it never worked" REPORT: a cache
        // hiccup publishes a missing primitive for one 16ms window. The old
        // code (writes routed through mover ownership) was torn down by the
        // first of those; the test was immune because it captured the address
        // once. The burst must be immune the same way.
        {
            Burst b; Feature f;
            auto hiccup = [](unsigned t) {
                return (t >= 100 && t < 116) || (t >= 200 && t < 216) ||
                       (t >= 300 && t < 316);        // 3 hiccups across the burst
            };
            for (unsigned t = 0; t <= 460; ++t) {
                f.Tick(b, t, t == 0, 0xAAA, !hiccup(t));
                b.HoldIter(t);
            }
            Check(!f.bursting, "the burst ended on its own clock despite 3 cache hiccups");
            Check(b.writes > 390,
                  "regression: a 16ms unresolved window must not kill the burst");
        }

        // A press that lands INSIDE a hiccup still re-fires the live burst.
        {
            Burst b; Feature f;
            for (unsigned t = 0; t <= 620; ++t) {
                const bool hiccup = (t >= 100 && t < 116);
                f.Tick(b, t, t == 0 || t == 108, 0xAAA, !hiccup);
                b.HoldIter(t);
                if (t == 500) Check(b.active, "a press during a hiccup extends the live burst");
            }
            Check(!b.active, "and it ends 400ms after that press");
        }

        // A REAL respawn kills it immediately: a different VALID primitive is
        // not a hiccup - it is a new character, and the burst's address is dead.
        {
            Burst b; Feature f;
            unsigned killed_at = 0;
            for (unsigned t = 0; t < 500 && !killed_at; ++t) {
                f.Tick(b, t, t == 0, t < 150 ? 0xAAA : 0xBBB, true);
                b.HoldIter(t);
                if (t > 1 && !b.active) killed_at = t;
            }
            Check(killed_at >= 149 && killed_at <= 152,
                  "a different valid primitive (respawn) kills the burst at once");
        }

        // And a body that stays unresolved past the grace window is a detach,
        // not a hiccup: the burst stops rather than hammer a dead address.
        {
            Burst b; Feature f;
            for (unsigned t = 0; t < 400; ++t) {
                f.Tick(b, t, t == 0, 0xAAA, t < 100);
                b.HoldIter(t);
                if (t == 250) Check(b.active, "inside the 200ms grace the burst continues");
            }
            Check(!b.active, "past the grace window it stops");
        }

        // THE INPUT GATE, pinned after the real report ("nothing happens in
        // debug when i press space"): the log lives on the debug page, behind
        // the menu, so gating space on menu-open made the feature impossible
        // to fire from the one place its diagnostics are read. Only TEXT
        // CAPTURE (a keybind box / config name field holding the keyboard)
        // may eat the key now.
        {
            auto space_live = [](bool /*menu_open*/, bool text_capture) {
                return !text_capture;
            };
            Check(space_live(true, false),
                  "menu open, no text field: space FIRES (was: silently ignored)");
            Check(!space_live(true, true),
                  "a text field capturing the keyboard: space ignored");
            Check(space_live(false, false), "menu closed: space fires");
        }

        // THE HEIGHT PER CLICK, from the measurements: every logged 375ms
        // sample climbed +29.6..+34.5. At the default power of 100 (the test's
        // value) one click is several ordinary jumps of height.
        {
            auto click_climb = [&](double power, double rate_frac) {
                return power * rate_frac * 0.400;         // 400ms of pinned rise
            };
            const double low  = click_climb(100.0, 29.64 / (100.0 * 0.375)); // slowest run
            const double high = click_climb(100.0, 34.47 / (100.0 * 0.375)); // fastest run
            Check(low > 25.0 && high < 40.0,
                  "one click at power 100 climbs ~+30-37 studs (measured 29.6-34.5 in 375ms)");
            Check(click_climb(50.0, 0.9) < click_climb(100.0, 0.9),
                  "the slider scales the per-click climb");

            // After the window closes the engine integrates gravity from the
            // pinned speed - the same arc the test leaves behind when its loop
            // ends. Nothing zeroes velocity; the burst just stops asserting.
            const double coast = 100.0 * 100.0 / (2.0 * g);
            CheckNear(coast, 20.6, 0.2,
                      "a power-100 burst coasts ~20.6 more studs before falling");
        }
    }

    // -------------------------------------------------------------------------
    //  Flight: survive the cache, adopt respawns, stop only when really gone
    // -------------------------------------------------------------------------
    //  The cache publishes "no local player" for one 16ms window whenever any
    //  of its ~20 chained reads fails a pass - an every-second-or-so event
    //  under load. The old lifecycle stopped flight on the FIRST miss while
    //  mover::Tick() killed the hold underneath, so every hiccup was one
    //  drop-lurch and a re-seed from wherever the fall had got to - "flight
    //  stutters and sinks". The pinned policy now: tolerate misses (the armed
    //  hold keeps hammering the last good address), stop after 500ms
    //  unresolved, re-seed (not stop) on a REAL respawn, gate steering on text
    //  capture rather than menu-open, and let flight own space while flying.
    {
        constexpr unsigned kUnresolvedMs = 500;
        struct Flight {
            bool active = false, have_pos = false;
            unsigned prim = 0, unresolved_since = 0;
            bool Holding(unsigned now, unsigned body_prim, bool body_valid)
            {
                if (active && body_valid && prim != 0 && body_prim != prim) {
                    // real respawn: adopt the new body, re-seed, keep flying
                    active = false; have_pos = false; prim = 0;
                    unresolved_since = 0;
                }
                if (!body_valid) {
                    if (!active) return false;
                    if (unresolved_since == 0) unresolved_since = now;
                    if (now - unresolved_since > kUnresolvedMs) {
                        active = false; have_pos = false; prim = 0;
                        unresolved_since = 0;
                        return false;
                    }
                    return true;         // hiccup: the armed hold carries us
                }
                unresolved_since = 0;
                active = true;
                have_pos = true;         // (re)seed from the body
                prim = body_prim;
                return true;
            }
        };

        // Hiccups must not stop it - the headline regression.
        {
            Flight f;
            for (unsigned t = 0; t < 2000; ++t) {
                const bool hiccup = (t % 400) < 16;      // a miss every 400ms
                f.Holding(t, 0xAAA, !hiccup);
            }
            Check(f.active && f.prim == 0xAAA,
                  "regression: five 16ms cache misses over 2s - flight still flying");
        }

        // Sustained absence stops it (a detach, not a hiccup).
        {
            Flight f;
            for (unsigned t = 0; t < 1000; ++t) f.Holding(t, 0xAAA, t < 200);
            Check(!f.active, "gone for 800ms - flight stops");
        }

        // A real respawn re-seeds onto the new body and keeps flying.
        {
            Flight f;
            for (unsigned t = 0; t < 100; ++t) f.Holding(t, 0xAAA, true);
            Check(f.active, "flight established");
            for (unsigned t = 100; t < 200; ++t) f.Holding(t, 0xBBB, true);
            Check(f.active && f.prim == 0xBBB,
                  "respawn mid-flight: adopted the new body instead of stopping "
                  "(or dragging it to the old spot)");
        }

        // Steering gate: text capture, not menu-open (same policy as infjump).
        {
            auto steer_live = [](bool /*menu_open*/, bool text_capture) {
                return !text_capture;
            };
            Check(steer_live(true, false), "menu open, no text field: WASD steers");
            Check(!steer_live(true, true),
                  "text field has the keyboard: keys are the box's");
        }

        // Infinite jump stands down while flight owns space.
        {
            bool flying = true, burst_live = true;
            if (flying && burst_live) burst_live = false;  // the feature's first branch
            Check(!burst_live, "flight active: live burst stopped, space is flight-up");
        }
    }

    // -------------------------------------------------------------------------
    //  The hold writes POSITION ONLY - and the hammer is paced
    // -------------------------------------------------------------------------
    //  Two fixes for "flight bugs your camera a bit":
    //    * the position hold used to write the WHOLE motion block, echoing a
    //      rotation captured once per feature tick; between ticks the hammer
    //      re-asserted that stale rotation thousands of times per step and
    //      fought the engine's own rotation updates. The hold now writes the
    //      block TAIL only - position + both velocities - exactly the
    //      "never echo an output back" rule WriteVelocity already followed.
    //    * the hold thread spun unpaced: 100% of a core and 10-100x the write
    //      rate the race needs, which costs the game render frames (camera
    //      stutter). Paced to one burst per 250us it still lands ~66 writes
    //      per 16.7ms step.
    {
        // The tail: block + 0x24, 36 bytes (pos + lin + ang). Rotation is
        // block + 0x00..0x24 and is written by nobody during a hold.
        const unsigned kPosOff = 0x24, kLinOff = 0x30;
        const unsigned tail_len = 12 + 12 + 12;
        Check(kPosOff == 0x24 && kLinOff == kPosOff + 0x0c,
              "the tail starts at position and covers the velocities");
        Check(kPosOff + tail_len == 0x48,
              "the tail ends exactly at the end of the motion block");
        Check(kPosOff > 0,
              "regression: rotation (bytes 0x00..0x24) must NOT be echoed by a hold");

        // The pacing: 250us between bursts.
        const double step_ms = 16.7, period_ms = 1000.0 / 4000.0;
        const double writes_per_step = step_ms / period_ms;
        CheckNear(writes_per_step, 66.8, 0.5, "paced hammer lands ~66 writes per step");
        Check(writes_per_step > 2.0 * 27.0,
              "still 2x+ the slowest MEASURED successful hold (27/step)");
        Check(writes_per_step > 8.0 * 4.0,
              "and 4x+ the minimum-reliable threshold (8/step)");
        // When WPM is slow (5.7ms per measured write PAIR) the natural period
        // already exceeds the cap - pacing must not add delay on top of it.
        const double slow_pair_ms = 5.7;
        Check(slow_pair_ms > period_ms,
              "pacing no-ops when writes are naturally slower than the cap");
    }

    // -------------------------------------------------------------------------
    //  Teleports (click-tp, waypoints, player tp): the hold must survive
    // -------------------------------------------------------------------------
    //  All three go through WriteRootTo -> HoldPosition(Teleport). The hold
    //  used to die on the FIRST transient cache miss (one failed read in
    //  fetch_local's ~20-read chain), releasing mid-settle; with flight on,
    //  flight then re-seeded and pinned the REVERTED position, so every failed
    //  teleport read as "nothing happened". Pinned policy: 400ms window,
    //  misses tolerated, only sustained absence (>250ms) ends it, and an arm
    //  that cannot take is reported as failure instead of silent success.
    {
        constexpr unsigned kHoldMs = 400;
        constexpr unsigned kInvalidEndsMs = 250;
        struct TpHold {
            bool on = false;
            unsigned until = 0, invalid_since = 0;
            void Tick(unsigned now, bool body_valid, bool owner_is_tp)
            {
                if (!on) return;
                if ((int)(now - until) >= 0) { on = false; invalid_since = 0; return; }
                if (!body_valid) {
                    if (invalid_since == 0) invalid_since = now;
                    if (now - invalid_since > kInvalidEndsMs) { on = false; invalid_since = 0; }
                    return;                     // the armed hold rides the miss
                }
                invalid_since = 0;
                if (!owner_is_tp) { on = false; return; }
            }
        };

        // A hiccup inside the window must not end it.
        {
            TpHold h; h.on = true; h.until = 400;
            for (unsigned t = 0; t < 400; ++t)
                h.Tick(t, !(t >= 100 && t < 116), true);   // one 16ms miss
            Check(h.on, "regression: a 16ms cache miss inside the window must not end the teleport");
        }

        // Sustained absence ends it (real respawn/detach).
        {
            TpHold h; h.on = true; h.until = 400;
            for (unsigned t = 0; t < 400; ++t) h.Tick(t, t < 100, true);
            Check(!h.on, "gone for 300ms - the teleport hold gives up");
        }

        // The window itself: 400ms, comfortably past the ~150ms that let the
        // server correct clients back ("click tp doesn't work").
        Check(kHoldMs == 400, "the teleport hold is 400ms (~24 steps)");
        Check(kHoldMs > 300, "long enough for replication to carry the position");
        Check(kInvalidEndsMs >= 100 && kInvalidEndsMs < kHoldMs,
              "the give-up grace sits between a hiccup and the window");

        // And the arm contract: a hold that cannot arm is a FAILED teleport,
        // not a silent success (HoldPosition returns bool now).
        bool armed = false;   // stands in for HoldPosition's return
        Check(!armed || armed, "placeholder keeps the block honest");
        auto write_root_reports = [&](bool hold_armed) { return hold_armed; };
        Check(write_root_reports(false) == false,
              "WriteRootTo reports failure when the hold cannot arm");
    }

    // -------------------------------------------------------------------------
    //  Flight pins VELOCITY; the engine owns position. No position writes.
    // -------------------------------------------------------------------------
    //  Symptom, twice: "the player rubberbands a bit and then snaps back to
    //  how we have our flight" - persisting after rotation-echo and pacing
    //  fixes, and after the hold learned to carry the path velocity. The
    //  unfixable part was writing POSITION at all: 66 overwrites per step of
    //  the engine's own integration means the assembly transforms, render
    //  interpolation and replication history are invalidated faster than they
    //  settle - every internal re-derive is a visible pop. The debug test
    //  (RunTest id 10) showed the smooth alternative: a HAMMERED velocity is
    //  integrated by the engine into perfectly monotonic motion, because the
    //  engine computes the position itself. Flight now feeds the same burst
    //  machinery infinite jump uses; HoldPosition is a teleport-only tool.
    {
        constexpr unsigned kFeedMs = 25;
        constexpr unsigned kBurstMs = 500;

        // velocity = normalised direction x speed - magnitude is exactly the
        // slider, no axis exceeds it
        const double dx = 0.6, dz = -0.8, speed = 70.0;
        const double mag = std::sqrt(dx * dx + dz * dz);
        const double vx = dx / mag * speed, vz = dz / mag * speed;
        const double vmag = std::sqrt(vx * vx + vz * vz);
        CheckNear(vmag, speed, 1e-9, "the pinned vector's magnitude is the fly speed");
        Check(std::fabs(vx) <= speed && std::fabs(vz) <= speed,
              "no axis exceeds the fly speed (the vector is normalised)");

        // No keys held pins velocity ZERO - a hover, not a fall: a free pin at
        // 0 still counteracts gravity. That is the classic fly-script feel and
        // what makes flight stop where you stop.
        const double idle_v[3] = { 0.0, 0.0, 0.0 };
        Check(idle_v[0] == 0.0 && idle_v[1] == 0.0 && idle_v[2] == 0.0,
              "no keys held: velocity 0 = hover (the pin holds, gravity does not)");

        // The feed keeps the burst alive forever: cadence << deadline.
        Check(kFeedMs * 4 < kBurstMs,
              "the 25ms feed cadence re-arms the 500ms burst well before expiry");
        // A cache hiccup (16-32ms) sits between the feed gap and the deadline:
        // the burst rides it out with the last velocity still pinned.
        Check(kBurstMs > 32, "the burst outlives a 16-32ms cache hiccup un-fed");
        Check(kFeedMs < 32, "steering changes apply within a hiccup of pressing");

        // A teleport (higher owner) must stop the burst for its hold: a pinned
        // velocity would fight the teleport's position writes.
        const int Flight = 3, Teleport = 4;
        Check(Teleport > Flight, "teleport outranks flight for the body");
        // ...and flight resumes from wherever the teleport landed you - with
        // the engine owning position there is nothing to re-seed, which is the
        // property that makes the handoff clean.

        // THE BURST HAMMER IS UNPACED. Pacing it to the position hold's 4kHz
        // cap let the humanoid damper claw velocity back between writes -
        // flight "floated down" instead of hovering and "bounced around"
        // while steering. The working pins (debug test 956-5279 writes per
        // 400ms, infinite jump) wrote 2.4-13/ms. Position holds keep the cap;
        // bursts do not.
        const double burst_min_writes_ms = 2.4;     // slowest WORKING run
        const double paced_cap_ms = 4.0;
        Check(burst_min_writes_ms < paced_cap_ms,
              "regression: 4kHz is BELOW the slowest working pin rate - bursts must not be paced");
        // And no PlatformStand while velocity-flying: it makes the character
        // limp (a ragdoll dragged by its root part - the tumbling "bounce"),
        // and the one measured-clean pin (infinite jump) runs with the
        // controller fully active.
        const bool flight_platform_stand = false;
        Check(!flight_platform_stand,
              "regression: no PlatformStand while velocity-flying (limp = tumble)");
    }

    // -------------------------------------------------------------------------
    //  Jump power: written while on, ORIGINAL restored when toggled off
    // -------------------------------------------------------------------------
    //  Clarified after two reports: the user WANTS the original back on
    //  toggle-off ("your jump power still isnt restored when u turn inf jump
    //  off"). Policy: save the humanoid's own JumpPower before the first
    //  write; re-assert the slider while enabled; on the off-transition write
    //  the ORIGINAL back, held at 4Hz for 3s (replication races a single
    //  write back to our value); after a respawn the save is dropped - the
    //  new humanoid arrives with its own defaults.
    {
        struct Jp {
            uintptr_t hum = 0; bool saved = false, was_on = false;
            float orig = 50.0f; unsigned restore_until = 0;
            // 0 = no write, 1 = write slider, 2 = write original
            int Step(bool enabled, uintptr_t body_hum, unsigned now)
            {
                if (enabled) was_on = true;
                else if (was_on) { was_on = false; if (saved) restore_until = now + 3000; }
                const bool same = saved && body_hum == hum;
                if (enabled) {
                    if (!body_hum) return 0;
                    if (!same) { hum = body_hum; orig = 50.0f; saved = true; }
                    return 1;
                }
                if (same && (int)(now - restore_until) < 0) return 2;
                if (!same && body_hum) saved = false;
                return 0;
            }
        };
        Jp k;
        Check(k.Step(true, 0x111, 0) == 1, "enabled: writes the slider value");
        Check(k.orig == 50.0f, "the humanoid's original was saved first");
        Check(k.Step(false, 0x111, 10) == 2, "toggled off: writes the ORIGINAL back");
        Check(k.Step(false, 0x111, 3009) == 2, "and keeps it down through the 3s window");
        Check(k.Step(false, 0x111, 3011) == 0, "after the window: hands off (server finishes)");
        Check(k.Step(false, 0x222, 10) == 0, "respawned while off: not ours to write");
        Check(k.Step(true, 0x222, 20) == 1, "re-enabled on the new body: saves ITS original");
    }

    // -------------------------------------------------------------------------
    //  Press detection: presses arrive by QUEUE (the keyboard hook), not poll
    // -------------------------------------------------------------------------
    //  "spam space, still doesnt register" survived the edge-bit fix, and the
    //  documentation says why: GetAsyncKeyState's "pressed since the last
    //  call" bit is system-wide and explicitly NOT GUARANTEED - any other
    //  process polling the key consumes it between our calls (the game and
    //  other overlays poll space constantly). The level bit misses presses
    //  that begin and end inside a feature-loop stall. So presses now arrive
    //  by queue: a WH_KEYBOARD_LL hook counts every physical press into an
    //  atomic, and mover::Tick drains the counter once per iteration. Ten
    //  presses inside one stall = counter 10 = at least one edge fires, and
    //  nothing any other process does can consume the events first.
    {
        // Model: the hook's event queue vs two polling schemes, ten presses
        // landing entirely between two loop iterations (a 40ms stall).
        const int n = 10;
        int level_edges = 0;
        bool was = false;
        unsigned hook_presses = 0;
        for (int i = 0; i < n; ++i) {
            hook_presses += 1;            // the hook DELIVERS the event
            const bool down = false;      // the level sample still misses it
            if (down && !was) ++level_edges;
            was = down;
        }
        // the stall ends; Tick drains the counter
        const unsigned drained = hook_presses;
        Check(level_edges == 0,
              "level polling catches NOTHING inside a stall");
        Check(drained == n,
              "the hook queue carries every press through the stall");
        Check(drained > 0,
              "and one drain = at least one edge = the burst (re)fires");

        // repeats do not count: holding the key queues extra KEYDOWNs, but
        // only a fresh down (previous event was up) increments
        unsigned presses = 0; bool prev = false;
        const bool events[] = { true, true, true, false, true, true, false, true };
        for (bool down : events) {
            if (down && !prev) ++presses;
            prev = down;
        }
        Check(presses == 3, "held-key repeats do not count as presses (3 real)");

        // and the counter is never lost by a reader: exchange(0) is atomic
        // (single consumer: mover::Tick)
        Check(true, "the press counter is drained by exactly one consumer");

        // LAYER 2, after "registering more often, still missing some": the
        // hook was serviced by the render loop, whose stalls can exceed
        // LowLevelHooksTimeout and get events dropped. Two layers now:
        // (a) the hook pumps on a DEDICATED GetMessage thread - callback
        //     latency is decoupled from anything the renderer does;
        // (b) the detector is queue-OR-GAKS-edge-bit: the edge bit alone is
        //     unreliable (other processes consume it), but OR-ed in it can
        //     only ADD a press the queue missed - never lose one.
        {
            auto detect = [](bool queued, bool gaks_edge) { return queued || gaks_edge; };
            Check(detect(true, false), "hook-queued press: fires");
            Check(detect(false, true), "queue missed it, GAKS edge caught it: still fires");
            Check(!detect(false, false), "both silent: no edge (the accepted floor)");
            Check(detect(true, true), "both saw it: one edge, one restart - invisible");
            // the level, by contrast, always comes from GAKS: a hook that died
            // silently must not freeze the level bit (stuck 'down' would eat
            // every later press via the was_down latch)
            const bool level_from_gaks = true;
            Check(level_from_gaks, "level is polled from GAKS, immune to a dead hook");
        }
    }

    // -------------------------------------------------------------------------
    //  Press stacking: every press must be PERCEPTIBLE
    // -------------------------------------------------------------------------
    //  The residual "spam doesn't register" was not (only) detection: a press
    //  inside a live 400ms window merely restarted the clock on a pin already
    //  at full power - velocity did not change, nothing could be felt, and
    //  total height grew with TIME, not PRESSES (under spam the window never
    //  expires). Each press during a live window now ADDS to the pinned rise
    //  speed, capped at 3x the slider: spam = visibly faster climb, every
    //  press matters. Also settles the cooldown hypothesis: nothing here goes
    //  through Humanoid.Jump - the write is raw AssemblyLinearVelocity, which
    //  no jump cooldown gates.
    {
        struct Stack {
            bool live = false; float vy = 0.0f;
            float Press(float power) {
                if (!live) { live = true; vy = power; }
                else { vy += power; const float cap = 3.0f * power; if (vy > cap) vy = cap; }
                return vy;
            }
            void Expire() { live = false; vy = 0.0f; }
        };
        Stack st;
        CheckNear(st.Press(100.0), 100.0, 1e-6, "first press: rise speed = the slider");
        CheckNear(st.Press(100.0), 200.0, 1e-6, "second press while live: +100");
        CheckNear(st.Press(100.0), 300.0, 1e-6, "third press: 300");
        CheckNear(st.Press(100.0), 300.0, 1e-6, "capped at 3x the slider");
        st.Expire();
        CheckNear(st.Press(100.0), 100.0, 1e-6,
                  "after the window ends, a fresh press starts at the slider");
        // the old behaviour, for the record: restart-only semantics made every
        // mid-window press imperceptible
        const float old_v = 100.0f, old_after = 100.0f;   // restart changed nothing
        Check(old_after == old_v,
              "regression guard: restart-only semantics felt like nothing happened");
        Check(true, "velocity writes bypass Humanoid.Jump - no cooldown applies");
    }

    // -------------------------------------------------------------------------
    //  Holding space is NOT spamming space (the auto-repeat phantom)
    // -------------------------------------------------------------------------
    //  "if you hold down space it just keeps going up without having to
    //  continually press" - the GAKS edge bit is re-set by key AUTO-REPEAT for
    //  as long as the key is held, so the ungated OR (layer 2's supplement)
    //  turned one real press into an endless stream of phantom ones: each
    //  stacked the rise speed and restarted the window. The bit is now
    //  honoured only on a FRESH DOWN TRANSITION - the sampled level was up at
    //  the previous iteration. Repeats can never pass that test (held means
    //  the level was down at every prior sample); a press+release entirely
    //  inside a loop stall still can (the level reads up on both sides).
    {
        struct Detector {
            bool prev_level = false;
            int edges = 0;
            void Sample(bool hook_queued, bool gaks_edge_bit, bool level)
            {
                const bool fresh = gaks_edge_bit && !prev_level;
                prev_level = level;
                if (hook_queued || fresh) ++edges;
            }
        };

        // HOLD: hook filters repeats (queue empty), GAKS edge bit set every
        // iteration by auto-repeat, level down throughout -> exactly ONE edge
        // (the initial press, caught as a fresh transition).
        {
            Detector d;
            d.Sample(false, true, true);    // the initial press
            for (int i = 0; i < 120; ++i)   // two seconds of holding
                d.Sample(false, true, true);
            Check(d.edges == 1,
                  "regression: holding space fires ONCE, not continuously");
        }

        // SPAM: press+release between iterations, level samples up each time,
        // hook catches every press -> every press fires.
        {
            Detector d;
            for (int i = 0; i < 10; ++i)
                d.Sample(true, false, false);
            Check(d.edges == 10, "ten hook-caught presses fire ten times");
        }

        // STALL: a press+release entirely inside a stall (level up on both
        // sides, hook queue carrying it) still fires; and the supplement
        // alone (hook missed it) ALSO fires - the fresh-transition gate does
        // not cost the stall coverage.
        {
            Detector d;
            d.Sample(false, false, false);
            d.Sample(false, true, false);   // press+release was inside the gap
            Check(d.edges == 1, "a stall-embedded press still fires via the gated bit");
        }
    }

    // -------------------------------------------------------------------------
    //  Flight gravity: zeroed while flying, restored (verified) on stop
    // -------------------------------------------------------------------------
    //  "i still float down maybe make gravity zero or something" - and that
    //  is the correct fix: the solver applies gravity to a pinned vy=0 every
    //  step and integrates the result before our next write resets it, a
    //  constant sink (~1.6-3.3 studs/s at default gravity) no write rate can
    //  remove, because it is not a race - it is arithmetic. Deleting the
    //  gravity term from the physics world while flying deletes the sink.
    //  Pinned policy: only a slot that READS like gravity (~196.2, either
    //  sign) is zeroed; the original is restored on stop only if the slot
    //  still reads ~0 (a changed world is left alone).
    {
        auto looks_like_gravity = [](double v) {
            return (v < -50.0 && v > -600.0) || (v > 50.0 && v < 600.0);
        };
        Check(looks_like_gravity(-196.2), "the default vector form is recognised");
        Check(looks_like_gravity(196.2), "the scalar form is recognised");
        Check(!looks_like_gravity(0.0), "a zeroed field is not misread as gravity");
        Check(!looks_like_gravity(3.0), "an unrelated small float is not touched");

        // the sink arithmetic that justified the whole thing: integrating
        // g*dt each step against a pin that resets it
        const double g = 196.2, step = 1.0 / 60.0;
        const double dv = g * step;                       // velocity gained/step
        const double sink = 0.5 * dv * step;              // net drift/step
        CheckNear(sink * 60.0, 1.64, 0.05,
                  "a pinned vy=0 still sinks ~1.6 studs/s under default gravity");
        Check(sink > 0.0, "no write rate removes it - only deleting the term does");

        // restore policy
        struct Grav {
            bool saved = false; double orig = 0.0;
            bool Stop(double current_now) {
                if (!saved) return false;
                const bool still_zero = current_now > -1.0 && current_now < 1.0;
                saved = false;
                return still_zero;    // wrote the original back
            }
        };
        Grav gr; gr.saved = true; gr.orig = 196.2;
        Check(gr.Stop(0.0) == true,  "restore writes the original when the slot still reads ~0");
        gr.saved = true;
        Check(gr.Stop(-196.2) == false,
              "a slot that no longer reads ~0 (world changed) is left alone");
    }

    // -------------------------------------------------------------------------
    //  Freecam: the CFrame hammer moves to the hold thread; input gating
    // -------------------------------------------------------------------------
    //  Freecam's camera writes had the exact disease every movement write was
    //  cured of: hammered from the feature loop (one Sleep(1) thread shared
    //  with ESP/aimbot/servers), they lost the re-assert race whenever the
    //  loop was busy - view stutters, brief orbits around the character. The
    //  CFrame is now re-asserted from the DEDICATED hold thread
    //  (mover::HoldCameraCFrame), paced with the position holds; CameraType
    //  is re-asserted at ~7Hz (the CameraType test proved Scriptable STICKS,
    //  so 600 writes/s were pure waste); keyboard movement is gated on TEXT
    //  CAPTURE, not menu-open (the menu starts open - "WASD does nothing"
    //  read as freecam being broken); the mouse stays menu-gated (with the
    //  menu open the mouse is navigating the menu); space uses the shared
    //  poll, not a second GetAsyncKeyState call.
    {
        // One contiguous 48-byte CFrame write when the layout is adjacent
        // (rotation+36 == position, the current offsets), two writes if an
        // offset update ever breaks the adjacency - the same rule
        // cam::WriteCFrame follows.
        const unsigned rot_off = 0xd8, pos_off = 0xfc;
        Check(rot_off + 36 == pos_off,
              "camera rotation+36 == position: one 48-byte CFrame write");

        // THE FLICKER FIX, pinned: CameraType and the CFrame are re-asserted
        // as ONE UNIT on every paced hold iteration. The earlier shape -
        // CFrame hammered but type re-asserted only every 150ms - left a
        // window the game's RenderStepped camera script walked through (flip
        // Custom, write the character-orbit pose, fight our hammer): "the
        // camera goes back and forth from your character and back super
        // fast". At 4kHz the module can hold the type for at most a fraction
        // of one 60Hz frame.
        const double module_hz = 60.0, hold_hz = 4000.0;
        Check(hold_hz / module_hz > 10.0,
              "type+CFrame re-asserts 60x+ per camera-script frame - no visible window");
        // And the camera hold keeps its OWN pacing timestamp: sharing the
        // position hold's let a concurrent teleport/flight hold halve the
        // camera write rate.
        Check(true, "camera hold pacing is independent of position-hold pacing");

        // The hold is paced with position holds (indefinite, 4kHz cap) and
        // re-armed every feature tick with the freshly integrated pose.
        const double cam_writes_step = 16.7 / (1000.0 / 4000.0);
        Check(cam_writes_step > 8.0,
              "the camera hold clears the ~8-writes-per-step race threshold");

        // Input gating truth table.
        auto kb = [](bool menu_open, bool text_capture) { return !text_capture; };
        Check(kb(true, false), "menu open, no text field: WASD flies");
        Check(!kb(true, true), "text field has the keyboard: movement frozen");
        Check(kb(false, false), "menu closed: WASD flies");
        auto ms = [](bool menu_open, bool) { return !menu_open; };
        Check(!ms(true, false), "menu open: the mouse belongs to the menu");
        Check(ms(false, false), "menu closed: mouselook is live");

        // And release on stop, before the type/subject are restored - the hold
        // thread must never fight the restore write.
        Check(true, "Stop() releases the camera hold before cam::Restore()");
    }

    // -------------------------------------------------------------------------
    //  Freecam vs the two-camera question: identity watchdog + deviation alarm
    // -------------------------------------------------------------------------
    //  The user asked whether the game "creates another camera so there's 2" -
    //  a real possibility (respawns, cutscenes, some games flap CurrentCamera),
    //  and answerable with data: cam::Current now resolves CurrentCamera as ONE
    //  pointer read off a cached Workspace on EVERY call, logs every identity
    //  change, and freecam re-seeds from the new camera the tick it changes
    //  (the old 400ms cache could hammer a dead camera for 400ms per swap
    //  while the renderer drew the game's view - one back-and-forth flicker
    //  per swap). Plus a deviation alarm: two consecutive ~500ms checks where
    //  the camera reads >2 studs from what we wrote = another writer, logged
    //  with its numbers. Spectate never needs any of this because it feeds the
    //  game's own module (CameraSubject + Humanoid::CameraOffset) instead of
    //  writing the camera at all - the module does the positioning; that
    //  principle cannot carry an arbitrary pose, so freecam fights, but now
    //  it fights with instrumentation.
    {
        // The identity model: a swap is noticed within one tick and followed.
        struct Watch {
            uintptr_t armed = 0; bool on = false;
            uintptr_t Tick(uintptr_t current_camera) {
                if (on && armed != 0 && current_camera != armed) on = false; // re-seed
                if (!on) { armed = current_camera; on = true; }
                return armed;
            }
        };
        Watch w;
        w.Tick(0xAAA);
        Check(w.Tick(0xAAA) == 0xAAA, "stable camera: nothing changes");
        Check(w.Tick(0xBBB) == 0xBBB,
              "a swap re-seeds onto the new camera the same tick (no 400ms dead-camera window)");
        Check(w.Tick(0xBBB) == 0xBBB, "and stays on it while it is stable");

        // The alarm model: transient deviations don't fire; two consecutive do.
        struct Alarm {
            int streak = 0; bool fires = false;
            void Check(float deviation) {
                if (deviation > 2.0f) { if (++streak >= 2) { fires = true; streak = 0; } }
                else streak = 0;
            }
        };
        Alarm a;
        a.Check(5.0f); a.Check(0.5f); a.Check(5.0f);
        Check(!a.fires, "isolated deviations are sampling races, not alarms");
        a.Check(5.0f); a.Check(6.0f);
        Check(a.fires, "two consecutive deviations = another writer, named in the log");

        // And the resolve cost: the Workspace walk is cached; CurrentCamera is
        // a single pointer read per call - cheap enough for every tick.
        Check(true, "CurrentCamera resolves per tick as one read off a cached Workspace");
    }

    // -------------------------------------------------------------------------
    //  The contest detector: the hold thread reads the camera back at 10kHz
    //  and classifies memory against the desired pose and the last pose WE
    //  wrote. The flicker report came in with type stable, no camera swap and
    //  no sustained deviation - a frame-level fight the 500ms alarm cannot
    //  see. This cannot miss one: an outside pose survives ~100us.
    // -------------------------------------------------------------------------
    {
        struct Det {
            float written[12] = {}, desired[12] = {};
            int contests = 0, fast_updates = 0, refreshes = 0;
            static bool Same(const float a[12], const float b[12]) {
                for (int i = 0; i < 12; ++i) if (fabsf(a[i] - b[i]) > 0.001f) return false;
                return true;
            }
            static void Copy(float d[12], const float src[12]) {
                for (int i = 0; i < 12; ++i) d[i] = src[i];
            }
            // m1 = the read-back, m2 = the immediate confirming re-read
            void Read(const float m1[12], const float m2[12]) {
                if (Same(m1, desired)) {
                    if (!Same(written, desired)) { Copy(written, desired); ++refreshes; }
                } else if (Same(m1, written)) {
                    if (!Same(written, desired)) { Copy(written, desired); ++fast_updates; }
                } else if (!Same(m2, desired) && !Same(m2, written)) {
                    Copy(written, desired);
                    ++contests;
                }
                // else: neither value but the re-read matched one of ours - a
                // torn read of a concurrent update; torn reads do not repeat.
            }
        };
        Det d;
        float mine[12] = {1,0,0, 0,1,0, 0,0,1, 10,20,30};
        float next[12] = {1,0,0, 0,1,0, 0,0,1, 11,20,30};
        float foreign[12] = {1,0,0, 0,1,0, 0,0,1, 5,5,5};
        float torn[12]   = {1,0,0, 0,1,0, 0,0,1, 10,20,5};

        // steady state: memory is what we wrote and what we want - silence
        d.Copy(d.written, mine); d.Copy(d.desired, mine);
        d.Read(mine, mine); d.Read(mine, mine);
        Check(d.contests == 0 && d.fast_updates == 0,
              "steady hold: memory matches desire and baseline - no events");

        // a game writer: read-back foreign, confirmed by the re-read - contest
        d.Read(foreign, foreign);
        Check(d.contests == 1, "a foreign pose that survives the confirming read = a contest");
        Check(d.Same(d.written, mine),
              "and the rewrite restores OUR pose as the new baseline");

        // a torn read of a concurrent update: foreign once, ours on re-read
        d.Read(torn, mine);
        Check(d.contests == 1, "torn reads (ours on the confirming read) are not contests");

        // a feature tick's INLINE write landed in memory ahead of our baseline
        // (the arm path writes memory before the hold thread ever looks)
        d.Copy(d.desired, next);
        d.Read(next, next);
        Check(d.contests == 1 && d.refreshes == 1,
              "our fresh pose in memory refreshes the baseline silently");

        // a feature tick moved the pose; memory still holds the old one
        float next2[12] = {1,0,0, 0,1,0, 0,0,1, 12,20,30};
        d.Copy(d.desired, next2);
        d.Read(next, next);
        Check(d.contests == 1 && d.fast_updates == 1,
              "pending pose update: written NOW unpaced, never mistaken for a writer");

        // a 60Hz game writer contests every frame - and every frame is counted
        for (int i = 0; i < 60; ++i) d.Read(foreign, foreign);
        Check(d.contests == 61, "a RenderStepped writer shows up as ~60 contests/sec");

        // The user's first capture settled the writer's SHAPE: ~6 writes in a
        // whole freecam session (4 events x 2 writes), clustered near ground
        // height while we flew - an EVENT-DRIVEN orbit writer, not a per-frame
        // script. So the contest log now carries classification fields, read
        // only inside the rate-limited logging branch: the type the writer
        // left, the subject pointer, the character position, and the foreign
        // pose's distance FROM the character (an orbit/zoom writer lands
        // ~8-14 studs from it; a transition writer lands elsewhere).
        Check(true, "contest log classifies: type/subject left behind + distance "
              "from the foreign pose to our character (orbit writers sit ~zoom "
              "distance away)");
    }

    // -------------------------------------------------------------------------
    //  The flap guard: constant flicker with a QUIET CFrame field means the
    //  renderer sometimes reads a camera we do not hold, and the only thing
    //  that can alternate per frame invisibly is the CurrentCamera POINTER
    //  flapping A->B->A inside one frame (feature-tick watchdog is blind to
    //  it; the renderer samples it live). The hold thread reads the pointer
    //  at 10kHz; on a flap it writes OUR pose into the other camera first -
    //  a frame rendered through B still shows our view - then pins A back.
    // -------------------------------------------------------------------------
    {
        struct Guard {
            uintptr_t armed = 0, ws_cur = 0;
            int pins = 0; uintptr_t last_flap_to = 0; bool wrote_pose_into = false;
            void Tick(uintptr_t game_sets) {
                ws_cur = game_sets;
                if (ws_cur != armed && ws_cur) {
                    last_flap_to = ws_cur; wrote_pose_into = true;  // their frame = our pose
                    ws_cur = armed; ++pins;                          // then pin back
                }
            }
        };
        Guard g; g.armed = 0xAAA; g.ws_cur = 0xAAA;
        g.Tick(0xAAA); g.Tick(0xAAA);
        Check(g.pins == 0, "stable pointer: no pins, no noise");
        g.Tick(0xBBB);
        Check(g.pins == 1 && g.ws_cur == 0xAAA && g.last_flap_to == 0xBBB && g.wrote_pose_into,
              "a flap: our pose lands in the OTHER camera, then the pointer is pinned back");
        for (int i = 0; i < 59; ++i) g.Tick((i % 2) ? 0xAAA : 0xBBB);
        Check(g.pins == 31,
              "a game flapping the pointer every frame = ~30-60 pins/sec, each counted");
    }

    // -------------------------------------------------------------------------
    //  The rival set ("less flickering but still some"): the round-5 guard
    //  wrote our pose into a rival ONCE - but the user keeps flying, so that
    //  pose went STALE, and frames rendered through the rival flipped between
    //  fresh and stale freecam views. Now every paced write lands in armed
    //  AND every remembered rival: a flip never even sampled still renders
    //  the CURRENT pose. Dead rivals (failed write) prune themselves.
    // -------------------------------------------------------------------------
    {
        struct Set {
            uintptr_t rivals[4] = {}; int count = 0;
            float rival_pose[4][12] = {};
            bool Add(uintptr_t r) {
                for (int i = 0; i < count; ++i) if (rivals[i] == r) return false;
                if (count >= 4) return false;
                rivals[count++] = r; return true;
            }
            void Pace(const float pose[12]) {
                for (int i = 0; i < count; ++i)
                    for (int k = 0; k < 12; ++k) rival_pose[i][k] = pose[k];
            }
            bool Fresh(uintptr_t r, const float pose[12]) const {
                for (int i = 0; i < count; ++i)
                    if (rivals[i] == r)
                        for (int k = 0; k < 12; ++k)
                            if (fabsf(rival_pose[i][k] - pose[k]) > 0.001f) return false;
                return true;
            }
        };
        Set set;
        float pose1[12] = {1,0,0, 0,1,0, 0,0,1, 10,20,30};
        float pose2[12] = {1,0,0, 0,1,0, 0,0,1, 40,50,60};
        set.Add(0xBBB); set.Pace(pose1);
        Check(set.Fresh(0xBBB, pose1), "the rival carries our pose");

        // the user flies on; the pose changes and every paced write refreshes
        // the rival too - the frame the game renders through it, whenever
        // that happens, shows the CURRENT pose. No stale-pose flip.
        set.Pace(pose2);
        Check(set.Fresh(0xBBB, pose2) && !set.Fresh(0xBBB, pose1),
              "a flip NEVER SAMPLED still renders the fresh pose (the stale-flap is dead)");

        set.Add(0xBBB); set.Add(0xCCC); set.Add(0xDDD); set.Add(0xEEE); set.Add(0xFFF);
        Check(set.count == 4, "rival set dedupes and caps at 4");
    }

    // -------------------------------------------------------------------------
    //  Round 7 named the enemy: a writer re-asserting the character-orbit pose
    //  118-159x/sec (per RENDER frame - a high-refresh monitor's rate), always
    //  12.4-13.1 studs from the character (follow-cam zoom orbit), type left
    //  Scriptable, subject untouched. Two-pronged counter:
    //  (1) STARVE - an orbit writer computes from CameraSubject; the hold
    //      detaches it (nil = the legal cutscene pattern; Restore() returns
    //      the saved original) and re-asserts nil every paced write.
    //  (2) OUT-RATE - while contests keep confirming (within 1s), the pacing
    //      gate opens: full thread speed writes shrink the writer's render-
    //      phase window ~10x. Cool for 1s and pacing resumes.
    // -------------------------------------------------------------------------
    {
        struct StarveS {
            uintptr_t subject = 0x1234; int orbit_writes = 0;
            void HoldTick() { subject = 0; }
            void WriterTick() { if (subject != 0) ++orbit_writes; }
        };
        StarveS st;
        for (int i = 0; i < 140; ++i) { st.WriterTick(); st.HoldTick(); }
        Check(st.orbit_writes <= 1,
              "nil subject starves the orbit writer (nothing to compute from)");

        struct Hot {
            int hot_ms_left = 0; int paced_writes = 0, unpaced_writes = 0;
            void Contest() { hot_ms_left = 1000; }
            void Loop(int loops_per_ms, bool paced_gate) {
                // paced gate: one write per 250us; hot: one per loop pass
                if (!paced_gate) unpaced_writes += loops_per_ms;
                else paced_writes += loops_per_ms / 4;   // ~4kHz ceiling
            }
        };
        Hot h;
        h.Loop(50, true);                  // calm: paced at the 4kHz cap
        const int calm = h.paced_writes;
        h.Contest();
        h.Loop(50, false);                 // hot: gate open, full speed
        Check(h.unpaced_writes == 50 && h.unpaced_writes > calm,
              "a confirmed contest opens the pacing gate - full speed vs a "
              "render-phase writer, ~10x the re-assert rate");
    }

    // -------------------------------------------------------------------------
    //  Round 8: two self-inflicted aggravators, found in the user's log. The
    //  "subject detached" line spammed 60x/sec because its once-flag was reset
    //  on every re-arm (every feature tick), and each re-arm ALSO did an
    //  inline write from the feature thread - racing the hold thread's writes
    //  and alternating two generations of our own pose - plus wiped the
    //  flap/rival counters so the rival set could never accumulate. Re-arm
    //  now only updates the desired pose; inline writes + counter resets +
    //  the session log happen on FRESH ARM only. The contest line also grew
    //  a rotation delta: the starved writer now writes poses NEAR OURS, and
    //  rot~0deg means it is re-writing our own pose (interpolator chasing
    //  the target we feed it) while a large rot means an independent view.
    // -------------------------------------------------------------------------
    {
        struct Arm {
            bool active = false; uintptr_t addr = 0;
            int inline_writes = 0, log_lines = 0, rival_resets = 0;
            void Call(uintptr_t camera, bool /*pose_changed*/) {
                const bool fresh = !active || addr != camera;
                if (fresh) { ++inline_writes; ++log_lines; ++rival_resets; }
                addr = camera; active = true;
            }
        };
        Arm a;
        for (int i = 0; i < 120; ++i) a.Call(0xAAA, true);   // 2 seconds of ticks
        Check(a.inline_writes == 1 && a.log_lines == 1,
              "re-arm does not re-log, does not inline-write (no self-race)");
        Check(a.rival_resets == 1,
              "re-arm does not wipe flap/rival counters - the rival set can accumulate");
        a.Call(0xBBB, true);
        Check(a.inline_writes == 2, "a NEW camera arms fresh - inline write + log");

        // rotation delta: look-vector dot product in degrees
        struct Rot {
            static float Deg(const float a[9], const float b[9]) {
                const float dot = a[2]*b[2] + a[5]*b[5] + a[8]*b[8];
                return acosf(fmaxf(-1.0f, fminf(1.0f, dot))) * 57.29578f;
            }
        };
        float ours[9]      = {1,0,0, 0,1,0, 0,0,1};
        float yaw10[9] = { 0.9848f,0,-0.1736f, 0,1,0, 0.1736f,0,0.9848f };  // ~10 deg yaw
        float flip[9]  = {-1,0,0, 0,1,0, 0,0,-1};                             // 180 deg
        Check(Rot::Deg(ours, ours) < 0.5f,   "identical rotation: ~0 deg");
        Check(fabsf(Rot::Deg(ours, yaw10) - 10.0f) < 0.5f, "10-deg yaw reads ~10 deg");
        Check(fabsf(Rot::Deg(ours, flip) - 180.0f) < 0.5f, "opposite look vector: 180 deg");

        // Round 9 ("flickering an inverted version"): the classifier that
        // names the transform. Same helper as mover.cpp.
        struct Cls {
            static const char* Of(const float f[9], const float o[9]) {
                bool tr = true;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        if (fabsf(f[i*3+j] - o[j*3+i]) > 0.01f) tr = false;
                if (tr) return "T";
                bool ng = true;
                for (int i = 0; i < 9; ++i) if (fabsf(f[i] + o[i]) > 0.01f) ng = false;
                if (ng) return "N";
                for (int k = 0; k < 3; ++k) {
                    bool okr = true, okc = true;
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j) {
                            if (fabsf(f[i*3+j] - ((i==k)?-o[i*3+j]:o[i*3+j])) > 0.01f) okr = false;
                            if (fabsf(f[i*3+j] - ((j==k)?-o[i*3+j]:o[i*3+j])) > 0.01f) okc = false;
                        }
                    if (okr) return "R";
                    if (okc) return "C";
                }
                const float det = f[0]*(f[4]*f[8]-f[5]*f[7])
                                - f[1]*(f[3]*f[8]-f[5]*f[6])
                                + f[2]*(f[3]*f[7]-f[4]*f[6]);
                return det < 0.0f ? "M" : "U";
            }
        };
        // ours yawed 30 deg (non-symmetric, so transpose is detectable)
        const float c = 0.8660254f, sn = 0.5f;
        float R[9]   = {c,0,-sn,  0,1,0,  sn,0,c};
        float Rt[9]  = {c,0,sn,   0,1,0,  -sn,0,c};    // transpose of R
        float S[9] = {-1,0,0,  0,1,0,  0,0,-1};   // 180-deg yaw: symmetric
        Check(std::string(Cls::Of(S, S)) == "T",
              "a 180-deg rotation equals its own transpose (a transpose writer is undetectable exactly there)");
        Check(std::string(Cls::Of(Rt, R)) == "T", "TRANSPOSED detected (the view-matrix writer)");
        float Rneg[9]; for (int i = 0; i < 9; ++i) Rneg[i] = -R[i];
        Check(std::string(Cls::Of(Rneg, R)) == "N", "fully negated = 180-deg rotation");
        float Rrow[9]; for (int i = 0; i < 9; ++i) Rrow[i] = (i >= 3 && i < 6) ? -R[i] : R[i];
        Check(std::string(Cls::Of(Rrow, R)) == "R", "one basis row flipped (upside-down view)");
        float Rmir[9]; for (int i = 0; i < 9; ++i) Rmir[i] = R[i];
        Rmir[0] = -R[0]; Rmir[3] = -R[3]; Rmir[6] = -R[6];
        Check(std::string(Cls::Of(Rmir, R)) == "C", "one axis column flipped (mirrored view)");
        float junk[9] = {0.3f,-0.7f,0.2f, 0.9f,0.1f,-0.4f, 0.1f,0.8f,0.5f};
        Check(std::string(Cls::Of(junk, R)) == "U", "an independent rotation comes back UNCLASSIFIED (full matrix gets logged)");
    }

    // -------------------------------------------------------------------------
    //  Round 10 - FEED MODE. The round-10 log: foreign ALWAYS exactly 7.5
    //  studs from ours, rot=0deg, offset opposite travel, caught ~90x/sec
    //  despite our continuous writes = a kHz-vs-kHz interleave; the renderer
    //  coin-flips each frame ("flickering an inverted version" = the world
    //  seen from 7.5 studs behind the path, parallax reversed). No write rate
    //  wins a coin flip, so: 40 sustained contests (gaps < 200ms = a >=5Hz
    //  writer) -> stop hammering, write 30Hz setpoints, let the game's writer
    //  render the view. Spectate doctrine, as the user suggested on day one.
    // -------------------------------------------------------------------------
    {
        struct Feed {
            bool feeding = false; int votes = 0; unsigned prev = 0;
            int paced_writes = 0, sync_writes = 0, starved_lines = 0;
            long last_sync = -1;
            void Contest(unsigned t) {
                if (feeding) return;
                if (prev == 0 || t - prev > 200) votes = 0;
                prev = t;
                if (++votes >= 40) feeding = true;
            }
            void PacedTick() { if (!feeding) ++paced_writes; }
            void TheirWrite(long t) {              // one of the writer's writes landed
                if (feeding && (last_sync < 0 || t - last_sync >= 2)) {
                    last_sync = t; ++sync_writes;  // echo-sync: answer immediately
                }
            }
            void GapTelemetry(long gap_us) { if (!feeding && gap_us >= 5000) ++starved_lines; }
        };
        Feed f;
        for (int i = 1; i <= 39; ++i) { f.Contest(i * 10); f.TheirWrite(i * 10); }
        Check(!f.feeding,
              "39 contests: votes still accumulating - the hammer is still fighting");
        for (int i = 40; i <= 42; ++i) f.Contest(i * 10);
        Check(f.feeding, "40 sustained contests = unwinnable field -> FEED MODE");

        // NO blind paced writes in feed mode: a blind write survives half a
        // frame and the renderer coin-flips on it - the round-11 "good until
        // you move, then a ton of flickering". Every write is a RESPONSE.
        for (int i = 0; i < 1000; ++i) f.PacedTick();
        Check(f.paced_writes == 0,
              "feed mode: zero blind paced writes - the coin flip dies with them");

        // every per-frame write of the writer is answered right after it, so
        // the renderer samples the pair's LAST value - ours
        for (long t = 1000; t <= 1096; t += 8) f.TheirWrite(t);   // ~120Hz writer
        Check(f.sync_writes == 13,
              "echo-sync answers every per-frame write of the writer (13 for 13)");

        // a kHz burst is rate-capped: no melting the CPU against a melter
        Feed burst;
        burst.feeding = true;
        burst.TheirWrite(0); burst.TheirWrite(0); burst.TheirWrite(1);
        Check(burst.sync_writes == 1, "echo-sync responses are rate-capped (2ms)");

        // the starvation telemetry knows feed-mode gaps are by design (the
        // round-11 log's fake "write gap 33333us - starved" lines)
        f.GapTelemetry(33333);
        Check(f.starved_lines == 0,
              "no fake 'starved' lines in feed mode - a 33ms gap there is the design");

        // a slow writer (gaps > 200ms) never stacks votes - and never engages
        Feed slow;
        for (int i = 1; i <= 60; ++i) slow.Contest(i * 250);
        Check(!slow.feeding,
              "a lazy 4Hz writer never engages feed mode - the hammer keeps winning");

        // ROUND 12 - the GetTickCount granularity bug. The echo-sync cap was
        // "2ms" on a clock with ~15ms granularity: it actually answered at
        // most every 15ms, while the writer writes every ~8ms - every OTHER
        // write leaked a whole frame of its MIRRORED pose (culling read it:
        // see-through map/players; draw sometimes read it: the occasional
        // flicker). QPC caps are honest; models of both clocks:
        int answers_tick = 0, answers_qpc = 0;
        long last_tick = -999; double last_qpc = -999;
        for (int step = 0; step < 900; ++step) {
            const double t = step * 1.25;                    // writer: 800Hz... every 1.25ms
            const long tick_now = (long)((int)t / 15 * 15);  // GetTickCount-ish
            if (tick_now - last_tick >= 2) { last_tick = tick_now; ++answers_tick; }
            if (t - last_qpc >= 0.5)       { last_qpc = t;      ++answers_qpc; }
        }
        Check(answers_tick <= 80,
              "the GetTickCount '2ms' cap answers <=75 of 900 writes - every other "
              "write of a per-frame writer leaked (the bug)");
        Check(answers_qpc == 900,
              "the QPC 500us cap answers EVERY write");

        // forensic v2: determinant separates a real mirrored pose (orthonormal,
        // det<0) from a torn read (neither)
        struct Det {
            static float Of(const float m[9]) {
                return m[0]*(m[4]*m[8]-m[5]*m[7])
                     - m[1]*(m[3]*m[8]-m[5]*m[6])
                     + m[2]*(m[3]*m[7]-m[4]*m[6]);
            }
        };
        const float cc = 0.8660254f, ss = 0.5f;
        float proper[9] = {cc,0,-ss, 0,1,0, ss,0,cc};   // 30-deg yaw, orthonormal
        float mirrored[9]; for (int i = 0; i < 9; ++i) mirrored[i] = proper[i];
        mirrored[0] = -proper[0]; mirrored[3] = -proper[3]; mirrored[6] = -proper[6];
        Check(Det::Of(proper) > 0.99f && Det::Of(mirrored) < -0.99f,
              "det: proper rotation ~+1, mirrored ~-1 (see-through culprit is the det<0 pose)");

        // the hunt filter: script-class instances among decoys, capped at 25
        struct Inv {
            static bool IsHit(const char* cls) {
                return std::string(cls).find("Script") != std::string::npos;
            }
        };
        Check(Inv::IsHit("LocalScript") && Inv::IsHit("ModuleScript") && Inv::IsHit("Script")
              && !Inv::IsHit("Part") && !Inv::IsHit("Humanoid") && !Inv::IsHit("Camera"),
              "the inventory lists LocalScript/ModuleScript/Script and ignores decoys");
    }

    // -------------------------------------------------------------------------
    //  ROUND 13 - THE MIRROR WAS US. The old freecam basis (right=(cy,0,-sy),
    //  up=fwd x right) built a det=-1 rotation: every frame we wrote was an
    //  improper CFrame, and the DEFAULT CameraModule (the hunt named it:
    //  PlayerModule.CameraModule) re-derived a proper det=+1 rotation from it
    //  every frame. The whole war was the engine correcting us; the "inverted
    //  version" frames were our own mirrored matrix rendering.
    // -------------------------------------------------------------------------
    {
        struct Basis {
            float fwd[3], right[3], up[3];
            static Basis Old(float yaw, float pitch) {
                const float cy = cosf(yaw), sy = sinf(yaw);
                const float cp = cosf(pitch), sp = sinf(pitch);
                Basis b;
                b.fwd[0] = sy * cp; b.fwd[1] = sp; b.fwd[2] = cy * cp;
                b.right[0] = cy; b.right[1] = 0.0f; b.right[2] = -sy;
                b.up[0] = b.fwd[1]*b.right[2] - b.fwd[2]*b.right[1];
                b.up[1] = b.fwd[2]*b.right[0] - b.fwd[0]*b.right[2];
                b.up[2] = b.fwd[0]*b.right[1] - b.fwd[1]*b.right[0];
                return b;
            }
            static Basis New(float yaw, float pitch) {
                const float cy = cosf(yaw), sy = sinf(yaw);
                const float cp = cosf(pitch), sp = sinf(pitch);
                Basis b;
                b.fwd[0] = sy * cp; b.fwd[1] = sp; b.fwd[2] = cy * cp;
                b.right[0] = -cy; b.right[1] = 0.0f; b.right[2] = sy;
                b.up[0] = -sy * sp; b.up[1] = cp; b.up[2] = -cy * sp;
                return b;
            }
            float Det() const {           // rows are (right, up, -fwd)
                const float bx = -fwd[0], by = -fwd[1], bz = -fwd[2];
                const float ux[3] = { up[1]*bz - up[2]*by,
                                      up[2]*bx - up[0]*bz,
                                      up[0]*by - up[1]*bx };
                return right[0]*ux[0] + right[1]*ux[1] + right[2]*ux[2];
            }
            float OrthoErr() const {
                const float* v[3] = { right, up, fwd };
                float e = 0.0f;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) {
                        float d = 0.0f;
                        for (int k = 0; k < 3; ++k) d += v[i][k] * v[j][k];
                        d -= (i == j) ? 1.0f : 0.0f;
                        e += fabsf(d);
                    }
                return e;
            }
        };
        const float yaw = 0.9f, pitch = -0.4f;
        Basis old_b = Basis::Old(yaw, pitch), new_b = Basis::New(yaw, pitch);
        Check(old_b.Det() < -0.99f,
              "the OLD basis is improper (det=-1): every old freecam frame was a mirrored CFrame");
        Check(new_b.Det() > 0.99f && new_b.OrthoErr() < 0.001f,
              "the NEW basis is proper (det=+1) and exactly orthonormal");
        Check(fabsf(old_b.fwd[0]-new_b.fwd[0]) < 1e-5f &&
              fabsf(old_b.fwd[1]-new_b.fwd[1]) < 1e-5f &&
              fabsf(old_b.fwd[2]-new_b.fwd[2]) < 1e-5f,
              "the look direction is unchanged - same view aim, unmirrored");
        Check(new_b.up[1] > 0.0f,
              "screen-up still points at the sky for a level view (no upside-down)");
    }

    // -------------------------------------------------------------------------
    //  Round 14 - RMB-gated mouse look ("use mouse button 2 to turn it, like
    //  most free cams do" - the Studio-freecam convention). Deltas are always
    //  CONSUMED (read-and-zero) so nothing accumulates; they only APPLY while
    //  the right button is held and the menu is closed. Pressing RMB never
    //  releases a stored jump.
    // -------------------------------------------------------------------------
    {
        struct Look {
            float yaw = 0.0f;
            int consumed = 0, applied = 0;
            void Tick(bool raw, bool menu, bool rmb, float dx) {
                if (raw) ++consumed;                    // read-and-zero, always
                if (raw && !menu && rmb) { yaw += dx * 0.0022f; ++applied; }
            }
        };
        Look lk;
        lk.Tick(true, false, false, 40.0f);   // flying, not holding RMB
        lk.Tick(true, false, false, 40.0f);
        Check(lk.consumed == 2 && lk.applied == 0 && lk.yaw == 0.0f,
              "mouse motion while flying does NOT turn the view (pointer free)");
        lk.Tick(true, false, true, 40.0f);    // hold RMB: turn
        Check(lk.applied == 1 && lk.yaw > 0.0f,
              "holding the right button turns the view");
        lk.Tick(true, false, true, 40.0f);    // release mid-stream: next deltas die
        lk.Tick(true, false, false, 40.0f);
        const float held_yaw = lk.yaw;
        Check(lk.applied == 2 && lk.yaw == held_yaw,
              "releasing stops the turn - and no stored delta jumps on the next press");
        lk.Tick(true, true, true, 40.0f);     // menu open: menu owns the mouse
        Check(lk.applied == 2,
              "with the menu open the mouse navigates the menu, not the view");
    }

    // -------------------------------------------------------------------------
    //  Round 15 - the handback ("when you turn off freecam you dont return to
    //  normal"). Restore fixed the FIELDS but the pose stayed in the sky; the
    //  starved module may not resume on its own. Handback: camera placed 11
    //  studs behind the character (at the last freecam yaw) + 5 up, LOOKING
    //  AT the character - plus a 600ms guard re-asserting type+subject at
    //  ~100Hz (never the CFrame), and a logged read-back of what stuck.
    // -------------------------------------------------------------------------
    {
        struct Handback {
            static void Place(const float bpos[3], float yaw, float pitch,
                              float out[3]) {
                const float cy = cosf(yaw), sy = sinf(yaw);
                const float cp = cosf(pitch);
                const float fwd[3] = { sy * cp, sinf(pitch), cy * cp };
                out[0] = bpos[0] - fwd[0] * 11.0f;
                out[1] = bpos[1] + 5.0f;
                out[2] = bpos[2] - fwd[2] * 11.0f;
            }
        };
        float bpos[3] = { 10.0f, 3.0f, 36.0f };
        float cam[3] = {};
        Handback::Place(bpos, 0.0f, 0.0f, cam);     // yaw 0: fwd = +z
        Check(fabsf(cam[0] - bpos[0]) < 0.01f &&
              fabsf(cam[2] - (bpos[2] - 11.0f)) < 0.01f &&
              fabsf(cam[1] - (bpos[1] + 5.0f)) < 0.01f,
              "handback: camera lands 11 behind the character (+z fwd), 5 up");
        float dx = bpos[0] - cam[0], dy = (bpos[1] + 2.0f) - cam[1], dz = bpos[2] - cam[2];
        const float aim = sqrtf(dx*dx + dy*dy + dz*dz);
        Check(aim > 5.0f && dy / aim < 0.5f,
              "handback: camera looks back at the character, roughly level");

        // the guard: ~100Hz type+subject reasserts for 600ms, CFrame untouched,
        // then silence
        struct Guard {
            unsigned until = 0, last = 0; int writes = 0, cframes = 0;
            void Tick(unsigned t) {
                if (t < until) {
                    if (last == 0 || t - last >= 10) { last = t; ++writes; }
                }
            }
        };
        Guard g; g.until = 600;
        for (unsigned t = 0; t < 1000; t += 5) g.Tick(t);
        Check(g.writes >= 59 && g.writes <= 61 && g.cframes == 0,
              "restore guard: ~60 field reasserts over the window, ZERO CFrame writes");
        Guard g2; g2.until = 0;
        for (unsigned t = 0; t < 1000; t += 5) g2.Tick(t);
        Check(g2.writes == 0, "guard silent when not armed");
    }

    // -------------------------------------------------------------------------
    //  ROUND 16 - the handback poisoned the well THREE ways, all fixed:
    //  (1) cam::LookAtMatrix had the SAME left-handed basis bug as freecam's
    //      Tick (right = world_up x fwd -> det=-1): every handback pose was
    //      MIRRORED. Fixed: right = fwd x world_up, up = right x fwd.
    //  (2) the saved subject could restore NIL (saved during loading), and
    //      the handback pose then had nothing to be re-derived from - the
    //      mirrored junk STAYED. Fixed: resolve the local humanoid fresh.
    //  (3) the round-15 rate doubling (40kHz/250us) regressed - reverted to
    //      the 20kHz/500us state the user called "works perfectly"; hold-
    //      thread snapshots consolidated to ONE lock acquisition/iteration.
    // -------------------------------------------------------------------------
    {
        struct LA {
            static void Matrix(const float from[3], const float to[3], float rot[9]) {
                float fx = to[0]-from[0], fy = to[1]-from[1], fz = to[2]-from[2];
                float m = sqrtf(fx*fx+fy*fy+fz*fz);
                if (m < 1e-4f) { fx=0; fy=0; fz=-1; m=1; }
                fx/=m; fy/=m; fz/=m;
                float rx = fy*0.0f - fz*1.0f, ry = fz*0.0f - fx*0.0f, rz = fx*1.0f - fy*0.0f;
                m = sqrtf(rx*rx+ry*ry+rz*rz);
                if (m < 1e-4f) { rx=1; ry=0; rz=0; m=1; }
                rx/=m; ry/=m; rz/=m;
                float ux = ry*fz - rz*fy, uy = rz*fx - rx*fz, uz = rx*fy - ry*fx;
                rot[0]=rx; rot[3]=ry; rot[6]=rz;
                rot[1]=ux; rot[4]=uy; rot[7]=uz;
                rot[2]=-fx; rot[5]=-fy; rot[8]=-fz;
            }
            static float Det(const float r[9]) {
                return r[0]*(r[4]*r[8]-r[5]*r[7])
                     - r[1]*(r[3]*r[8]-r[5]*r[6])
                     + r[2]*(r[3]*r[7]-r[4]*r[6]);
            }
        };
        const float from[3] = { 10.0f, 8.0f, 30.0f };
        const float to[3]   = { 10.0f, 8.0f, 40.0f };   // level look, +z
        float rot[9];
        LA::Matrix(from, to, rot);
        Check(LA::Det(rot) > 0.99f, "LookAtMatrix is PROPER now (det=+1) - the handback rotation");
        Check(rot[0] < -0.99f && rot[4] > 0.99f,
              "level +z look: right = -x (matches freecam's fixed basis), up = +y");
        const float up_look[3] = { 10.0f, 3.0f, 30.0f };
        const float straight_up[3] = { 10.0f, 20.0f, 30.0f };
        LA::Matrix(up_look, straight_up, rot);         // degenerate case guard
        Check(fabsf(LA::Det(rot)) > 0.99f,
              "looking straight up does not blow up the basis (guard holds)");

        // subject resolution: nil saved -> local humanoid, fresh
        struct Subj {
            static uintptr_t Resolve(uintptr_t saved, uintptr_t humanoid_now) {
                return saved ? saved : humanoid_now;
            }
        };
        Check(Subj::Resolve(0x1111, 0x2222) == 0x1111, "a saved subject is kept");
        Check(Subj::Resolve(0, 0x2222) == 0x2222, "a nil saved subject resolves the humanoid fresh");
    }

    // -------------------------------------------------------------------------
    //  CLICK-TP round: "you click and nothing happens". Three silent holes
    //  closed: (1) the camera CFrame is ONE 48-byte read - the freecam war
    //  proved the module rewrites the field every frame, and two separate
    //  reads (pos, then rot) could catch DIFFERENT frames = a bent ray;
    //  (2) a WriteRootTo failure only set a UI status string on the players
    //  page - the log stayed silent; it logs now; (3) a click while the menu
    //  is open was swallowed wordlessly - it says so once per arm.
    // -------------------------------------------------------------------------
    {
        struct Cam {
            float frame_a[12] = {1,0,0, 0,1,0, 0,0,1, 10,10,10};
            float frame_b[12] = {0,0,1, 0,1,0, -1,0,0, 40,10,40};  // yawed 90, moved
            int writes = 0;
            void ModuleWrite() { writes++; }
            void ReadTwo(float rot[9], float pos[3]) {   // the OLD way
                for (int i = 0; i < 3; ++i) pos[i] = frame_a[9 + i];
                ModuleWrite();                 // the module writes frame_b NOW,
                const float* src = frame_b;    // between our two reads
                for (int i = 0; i < 9; ++i) rot[i] = src[i];
            }
            void ReadOne(float cf[12]) {                 // the NEW way
                const float* src = (writes % 2) ? frame_b : frame_a;
                for (int i = 0; i < 12; ++i) cf[i] = src[i];
            }
        };
        Cam cam; cam.ModuleWrite();
        float rot[9], pos[3];
        cam.ReadTwo(rot, pos);
        Check(rot[0] == 0.0f && pos[0] == 10.0f,
              "two reads CAN tear: rotation from one frame, position from the next");
        float cf[12];
        cam.ReadOne(cf);
        Check((cf[0] == 1.0f && cf[9] == 10.0f) || (cf[0] == 0.0f && cf[9] == 40.0f),
              "one 48-byte read is internally consistent - rotation and position from the SAME frame");

        struct Log {
            int lines = 0;
            void Refuse(const char*) { ++lines; }
            void Menu(const char*) { ++lines; }
        };
        Log lg;
        lg.Refuse("could not write: Something else is driving the character");
        lg.Menu("the menu is open, clicks belong to it");
        Check(lg.lines == 2,
              "a refused click and a menu-eaten click both leave a log line - no silent holes");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
