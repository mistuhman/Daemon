#include "freecam.h"
#include "features/camera/camera_drive.h"
#include "game.h"
#include "features/movement/mover.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "input_mouse.h"
#include <cmath>

// -----------------------------------------------------------------------------
//  Freecam
// -----------------------------------------------------------------------------
//  Four separate things made this "bug your camera out", and none of them was
//  the CFrame write:
//
//  1. MOUSELOOK READ THE WRONG THING. It used GetCursorPos() deltas. Roblox locks
//     the cursor to the centre of its window every frame while you are in
//     first/third person, so those deltas are the distance from the game's own
//     re-centring, not from your hand: near-zero when still, a large jump the
//     instant anything moves it. The camera therefore sat frozen and then flung
//     itself. It now uses raw mouse input (WM_INPUT, see input_mouse.h), which
//     reports actual device movement before any clipping or re-centring.
//
//  2. THE CAMERA WAS WRITTEN ~600 TIMES A SECOND with a hardcoded dt of 16ms, so
//     the speed slider was ~16x off and the write raced the game's own camera
//     update. Now: real dt from mover, one write per paced step.
//
//  3. THE UP VECTOR COLLAPSED WHEN LOOKING STRAIGHT UP OR DOWN. LookAtMatrix
//     builds right = worldUp x fwd, which is a zero-length cross product at the
//     poles; the old ±1.45rad clamp was not enough margin once the matrix was
//     renormalised, and a degenerate basis is a camera showing garbage. The
//     matrix is now built directly from yaw/pitch, which has no such case, and
//     the pitch clamp is a safety net rather than the fix.
//
//  4. IT NEVER RESTORED CLEANLY. Restore() puts CameraType back, but the
//     character had been standing in the world the whole time still taking
//     WASD from the game (you fly the camera away, your body walks off), and if
//     the character respawned while free-flying, the restore wrote a subject
//     that no longer existed. Freecam now claims the body through mover, which
//     zeroes walkspeed for the duration and hands everything back on release -
//     including automatically on a respawn.
//
//  5. THE CFRAME HAMMER RAN FROM THE FEATURE LOOP - the one platform::SleepMs(1) thread
//     shared with ESP, aimbot and servers, whose rate collapses under load.
//     The camera writes are the same race every other movement write turned
//     out to be: hammered from a quiet thread they hold, paced from a busy one
//     they lose. The CFrame is now re-asserted from the DEDICATED HOLD THREAD
//     (mover::HoldCameraCFrame) and the feature loop only recomputes it.
// -----------------------------------------------------------------------------

namespace features {
namespace {

bool  s_on = false;
bool  s_key_was = false;
float s_pos[3] = {};
float s_yaw = 0.0f, s_pitch = 0.0f;

// WHICH camera we are driving, and whether someone else is moving it.
// The identity watchdog (cam::Current) logs every CurrentCamera swap; this
// tracks the address we armed so a swap mid-flight re-seeds the pose from the
// NEW camera instead of hammering a dead one while the renderer draws the
// game's view - the "back and forth super fast" when a game flaps cameras.
uintptr_t s_cam_addr = 0;
uint64_t     s_check_at = 0;
int       s_dev_streak = 0;

// THE SCRIPT HUNT (round 12). The enemy is identified but not located: a
// per-frame writer that mirrors one axis of our pose and trails it 7.5 studs
// - culling reads that mirrored pose, which is the see-through map/players.
// If it is a game LocalScript (custom camera scripts are the usual suspect
// and respect nothing, not even Scriptable), it lives in the containers
// below and we can DISABLE it by instance once named. This pass only lists
// script-class instances (name + class + container), once per app session -
// zero risk, and the log picks the culprit.
void LogCameraScriptInventory()
{
    static bool s_done = false;
    if (s_done) return;
    instance dm = game::ReadDatamodel(g_base_address);
    if (!dm.is_valid()) return;          // game still loading; retry next toggle
    s_done = true;
    LogLine("freecam: script hunt - inventory follows");
    int logged = 0;
    const char* services[] = { "Players", "Workspace", "StarterPlayer", "StarterGui" };
    for (const char* svc_name : services) {
        instance svc = dm.read_service(svc_name);
        if (!svc.is_valid()) continue;
        instance roots[2] = { svc, instance{} };
        if (strcmp(svc_name, "Players") == 0) {
            instance lp = svc.local_player();
            if (lp.is_valid()) roots[1] = lp;   // PlayerScripts hangs off the player
        }
        for (const instance& root : roots) {
            if (!root.is_valid()) continue;
            for (const instance& c : root.get_children()) {
                if (logged >= 25) goto done;
                const std::string cls = c.get_class_name();
                if (cls.find("Script") != std::string::npos) {
                    LogLine("freecam hunt: %s '%s' (under %s)",
                            cls.c_str(), c.get_name().c_str(),
                            root.get_name().c_str());
                    ++logged;
                }
                for (const instance& g : c.get_children()) {
                    if (logged >= 25) goto done;
                    const std::string gcls = g.get_class_name();
                    if (gcls.find("Script") != std::string::npos) {
                        LogLine("freecam hunt: %s '%s' (under %s '%s')",
                                gcls.c_str(), g.get_name().c_str(),
                                cls.c_str(), c.get_name().c_str());
                        ++logged;
                    }
                }
            }
        }
    }
done:
    if (logged == 0)
        LogLine("freecam hunt: no script-class instances in the scanned "
                "containers - the camera writer is ENGINE-side");
    else
        LogLine("freecam hunt: %d script-class instances listed", logged);
}

void Stop()
{
    if (s_on) {
        LogLine("freecam: off");
        mover::ReleaseCameraHold();     // stop hammering before handing back
        int was_type = 0;
        uintptr_t was_subject = 0;
        cam::Restore(&was_type, &was_subject);
        // The saved subject can be 0 (nil): if freecam's FIRST Take ran while
        // the game was still assigning the camera module's subject, or the
        // previous handback fell in the same hole, 0 is what it saved - and
        // restoring nil leaves the module with NOTHING to orbit ("you dont
        // return to normal"). By Stop time the character certainly exists:
        // resolve the humanoid FRESH and use that.
        if (was_subject == 0) {
            const uintptr_t hum = cache::GetLocalPlayer().humanoid_address;
            if (is_valid_address(hum)) {
                was_subject = hum;
                instance cam_now = cam::Current();
                if (cam_now.is_valid())
                    if (Offsets::Camera::CameraSubject) {
                        write<uintptr_t>(cam_now.address + Offsets::Camera::CameraSubject,
                                         was_subject);
                    }
                LogLine("freecam: saved subject was nil - restored the local "
                        "Humanoid instead (0x%llX)",
                        (unsigned long long)was_subject);
            } else {
                LogLine("freecam: saved subject was nil AND no local Humanoid "
                        "yet - the game may need a respawn to re-aim its camera");
            }
        }
        // HANDBACK v2 (round 15: "when you turn off freecam you dont return to
        // normal"). Restore puts the FIELDS back, but the camera's pose is
        // still wherever freecam left it in the sky - and the default module
        // spent the whole session starved (subject nil), so it may not snap
        // back on its own. Place the camera back behind the character at the
        // last freecam yaw, LOOKING AT the character: a sane third-person
        // pose for the module (or the renderer) to start from.
        {
            instance camera = cam::Current();
            const mover::Body& b = mover::Local();
            if (camera.is_valid() && b.valid) {
                const float cy = cosf(s_yaw), sy = sinf(s_yaw);
                const float cp = cosf(s_pitch);
                const float fwd[3] = { sy * cp, sinf(s_pitch), cy * cp };
                const float pos[3] = { b.pos[0] - fwd[0] * 11.0f,
                                       b.pos[1] + 5.0f,
                                       b.pos[2] - fwd[2] * 11.0f };
                const float at[3]  = { b.pos[0], b.pos[1] + 2.0f, b.pos[2] };
                cam::WriteLookAt(camera, pos, at);
            }
        }
        // And guard the restored fields for 600ms: if the starved module
        // writes one more frame of its own state after the restore, the guard
        // puts type+subject right back (never the CFrame - the game owns the
        // pose again).
        {
            instance camera = cam::Current();
            if (camera.is_valid()) {
                mover::GuardCameraFields(camera.address, was_type, was_subject, 600);
                // Verification, in the log, every time: what actually stuck.
                float p[3] = {};
                cam::ReadPos(camera, p);
                LogLine("freecam: handed back - type=%d subj=0x%llX pos=%.1f,%.1f,%.1f",
                        read<int>(camera.address + Offsets::Camera::CameraType),
                        (unsigned long long)read<uintptr_t>(
                            camera.address + Offsets::Camera::CameraSubject),
                        p[0], p[1], p[2]);
            }
        }
        mover::FreezeWalk(mover::Freecam, false);
        mover::Release(mover::Freecam);
        input::DropRawMouse();
    }
    s_on = false;
}

} // namespace

bool FreecamActive() { return s_on; }

void RunFreecam()
{
    // Spectate also owns the camera; two features writing CameraType/CFrame is
    // its own kind of broken, and spectate is the more specific request.
    if (!freecam_enabled || spectate_enabled) {
        Stop();
        s_key_was = false;
        return;
    }

    const bool key_down = (freecam_keybind == 0) ||
                          (platform::IsKeyDown(freecam_keybind));
    bool want = s_on;
    if (freecam_keybind == 0) want = true;
    else if (key_down && !s_key_was) want = !s_on;
    s_key_was = key_down;
    if (!want) { Stop(); return; }

    instance camera = cam::Current();
    if (!camera.is_valid()) { Stop(); return; }

    // The game swapped the camera instance under us. Follow it: re-seed the
    // pose from the new camera (below, via the normal start path) so the view
    // does not jump to stale coordinates, and keep flying. The swap itself
    // was already logged by the watchdog.
    if (s_on && s_cam_addr != 0 && camera.address != s_cam_addr)
        s_on = false;

    if (!s_on) {
        // Start exactly where the game's camera is, so switching in does not jump.
        if (!cam::ReadPos(camera, s_pos)) {
            const mover::Body& b = mover::Local();
            s_pos[0] = b.pos[0]; s_pos[1] = b.pos[1] + 2.0f; s_pos[2] = b.pos[2];
        }
        float rot[9] = {};
        if (cam::ReadRot(camera, rot)) {
            float fwd[3], r[3], u[3];
            cam::LookVectors(rot, fwd, r, u);
            const float len = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
            if (len > 0.0001f) {
                fwd[0] /= len; fwd[1] /= len; fwd[2] /= len;
                s_yaw = atan2f(fwd[0], fwd[2]);
                s_pitch = asinf(fmaxf(-1.0f, fminf(1.0f, fwd[1])));
            }
        }
        // Discard whatever motion accumulated before we started listening, or the
        // camera snaps on the first frame.
        input::DropRawMouse();
        s_cam_addr = camera.address;
        s_dev_streak = 0;
        s_check_at = 0;
        s_on = true;
        LogLine("freecam: on (camera 0x%llX)", (unsigned long long)camera.address);
        LogCameraScriptInventory();
    }

    // The body is claimed but not driven: freecam is the lowest priority owner, so
    // a teleport or flight can take it away, and losing it is not a reason to stop
    // flying the camera.
    if (mover::Claim(mover::Freecam))
        mover::FreezeWalk(mover::Freecam, true);

    const bool menu = g_menu_open.load(std::memory_order_relaxed);

    // Mouse look, RIGHT-BUTTON-GATED - the Studio-freecam convention the user
    // asked for ("use mouse button 2 to turn it, like most free cams do"): the
    // pointer stays free for the game and the OS while you fly; hold the right
    // button and the mouse turns the view. Deltas are still consumed every
    // loop iteration (not only on a step) and simply DISCARDED while the gate
    // is closed - TakeRawMouse is read-and-zero, so nothing accumulates and a
    // press never releases a stored jump.
    float mdx = 0.0f, mdy = 0.0f;
    const bool raw = input::TakeRawMouse(mdx, mdy);
    const bool look = raw && !menu && platform::IsKeyDown(platform::key::RBUTTON);
    if (look) {
        constexpr float kSens = 0.0022f;   // radians per mouse count
        s_yaw += mdx * kSens;
        s_pitch -= mdy * kSens;
        // 89 degrees. Past this the look vector and world up are parallel and any
        // basis built from them is degenerate.
        constexpr float kMaxPitch = 1.5533f;
        if (s_pitch > kMaxPitch) s_pitch = kMaxPitch;
        if (s_pitch < -kMaxPitch) s_pitch = -kMaxPitch;
        // Keep yaw bounded so it cannot drift into float ranges where the
        // trig loses precision over a long session.
        constexpr float kTwoPi = 6.2831853f;
        if (s_yaw > kTwoPi) s_yaw -= kTwoPi;
        if (s_yaw < -kTwoPi) s_yaw += kTwoPi;
    }

    const float cy = cosf(s_yaw), sy = sinf(s_yaw);
    const float cp = cosf(s_pitch), sp = sinf(s_pitch);
    const float fwd[3]   = { sy * cp, sp, cy * cp };
    // THE MIRROR BUG, FOUND (round 13): the old basis here was right=(cy,0,-sy)
    // with up = fwd x right - which makes right x up = +fwd, i.e. a LEFT-handed
    // (reflected, det=-1) rotation matrix. Every freecam frame ever written was
    // an improper CFrame, and Roblox's DEFAULT CameraModule (the hunt found it:
    // StarterPlayer.PlayerModule.CameraModule) re-derived a proper det=+1
    // rotation from it every render frame - the fight we have been losing was
    // the engine CORRECTING us, and the "inverted version" frames were our own
    // mirrored matrix rendering. Proper frame: right = fwd x world_up (so
    // right x up = -fwd = the look axis, det = +1), up in closed form - exact,
    // orthonormal, no cross that can vanish.
    const float right[3] = { -cy, 0.0f, sy };
    const float up[3]    = { -sy * sp, cp, -cy * sp };
    // And a guard so this can never regress silently: the written matrix must
    // be proper. det = right . (up x (-fwd)) - the closed forms cancel to +-1,
    // so this check is exact.
    {
        const float bx = -fwd[0], by = -fwd[1], bz = -fwd[2];
        const float ux[] = { up[1] * bz - up[2] * by,
                             up[2] * bx - up[0] * bz,
                             up[0] * by - up[1] * bx };
        const float det = right[0] * ux[0] + right[1] * ux[1] + right[2] * ux[2];
        static bool s_said_bad = false, s_said_ok = false;
        if (det < 0.99f && !s_said_bad) {
            s_said_bad = true;
            LogLine("freecam: rotation matrix is IMPROPER (det=%.3f) - the view "
                    "would mirror-flicker; fix the basis in Tick", det);
        } else if (det > 0.99f && !s_said_ok) {
            s_said_ok = true;
            LogLine("freecam: rotation matrix proper (det=+1)");
        }
    }

    // KEYBOARD is gated on TEXT CAPTURE, not menu-open (the policy infinite
    // jump learned): the menu starts open, and "WASD does nothing until you
    // close it" reads as freecam being broken - you can fly with the menu up;
    // only a text field actually eating the keyboard freezes movement. The
    // MOUSE stays menu-gated below because with the menu open the mouse is
    // navigating the menu, not looking around - and look is additionally
    // right-button-gated (see the mouse-look block above).
    float dir[3] = {};
    const bool text = g_menu_text_input.load(std::memory_order_relaxed);
    if (!text) {
        auto add = [&](const float v[3], float s) {
            dir[0] += v[0] * s; dir[1] += v[1] * s; dir[2] += v[2] * s;
        };
        if (platform::IsKeyDown('W')) add(fwd, 1.0f);
        if (platform::IsKeyDown('S')) add(fwd, -1.0f);
        if (platform::IsKeyDown('D')) add(right, 1.0f);
        if (platform::IsKeyDown('A')) add(right, -1.0f);
        // the shared sample, not another GetAsyncKeyState call on the key (see
        // globals.h: space belongs to the single poll in mover::Tick)
        if (g_input_space_down) dir[1] += 1.0f;
        if (platform::IsKeyDown(platform::key::LCONTROL)) dir[1] -= 1.0f;
    }

    // Real elapsed time, so freecam_speed means studs per second on any machine
    // and at any loop rate.
    const float dt = mover::Dt();
    const float mag = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (mag > 0.001f && dt > 0.0f) {
        const float boost = platform::IsKeyDown(platform::key::LSHIFT) ? 2.4f : 1.0f;
        const float s = freecam_speed * boost * dt / mag;
        s_pos[0] += dir[0] * s;
        s_pos[1] += dir[1] * s;
        s_pos[2] += dir[2] * s;
    }

    if (!(s_pos[0] == s_pos[0] && s_pos[1] == s_pos[1] && s_pos[2] == s_pos[2])) {
        // A NaN here is unrecoverable and shows as a black/garbage view, so bail
        // rather than write it. Restarting re-reads the game's own camera.
        LogLine("freecam: position went non-finite - handing the camera back");
        Stop();
        return;
    }

    // cam::Take saves the original type/subject (for Restore), claims at
    // start, and does the read-back verification for the log. The ongoing
    // FIGHT - re-asserting CameraType and the CFrame against the game's
    // camera script - belongs to the hold thread, which writes type+CFrame as
    // one unit every paced iteration (see mover::HoldCameraCFrame). Pacing
    // the type HERE was the flicker: a 150ms window in which the game's
    // RenderStepped had flipped Custom and the default module fought our
    // hammer - "camera goes back to your character and back super fast".
    cam::Take(camera);

    float rot[9];
    rot[0] = right[0]; rot[3] = right[1]; rot[6] = right[2];
    rot[1] = up[0];    rot[4] = up[1];    rot[7] = up[2];
    // Roblox's CFrame look vector is -Z.
    rot[2] = -fwd[0];  rot[5] = -fwd[1];  rot[8] = -fwd[2];
    float cf[12];
    for (int i = 0; i < 9; ++i) cf[i] = rot[i];
    cf[9] = s_pos[0]; cf[10] = s_pos[1]; cf[11] = s_pos[2];
    mover::HoldCameraCFrame(camera.address, cf, cam::WorkspaceAddr());
    s_cam_addr = camera.address;

    // THE DEVIATION ALARM: if the camera's position keeps reading somewhere we
    // did not write, another writer is winning frames against the hammer, and
    // that deserves NUMBERS in the log rather than a shrug - "still happening"
    // then arrives with evidence attached. (A one-off deviation is a sampling
    // race; two consecutive ~500ms checks is a real fight.)
    const uint64_t now = platform::NowMs();
    if (mover::CameraHoldFeeding()) {
        s_check_at = 0;      // feed mode: the field is the writer's echo of our
    }                        // setpoint - an offset is expected, not an alarm
    else if (s_check_at == 0 || (now - s_check_at) >= 500) {
        s_check_at = now;
        float rp[3] = {};
        if (cam::ReadPos(camera, rp)) {
            const float dx = rp[0] - s_pos[0], dy = rp[1] - s_pos[1], dz = rp[2] - s_pos[2];
            if (sqrtf(dx * dx + dy * dy + dz * dz) > 2.0f) {
                if (++s_dev_streak >= 2) {
                    s_dev_streak = 0;
                    LogLine("freecam: another writer is moving the camera - reads "
                            "%.1f,%.1f,%.1f, we wrote %.1f,%.1f,%.1f. A game "
                            "camera script is fighting the hammer",
                            rp[0], rp[1], rp[2], s_pos[0], s_pos[1], s_pos[2]);
                }
            } else {
                s_dev_streak = 0;
            }
        }
    }
}

} // namespace features
