#include "features/camera/camera_drive.h"
#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "game.h"
#include "cache.h"
#include <cmath>
#include <cstring>

namespace features {
namespace cam {
namespace {

instance s_cam{};
uint64_t    s_cam_at = 0;
instance s_ws{};                 // the Workspace walk is expensive; the camera
uint64_t    s_ws_at = 0;            // pointer off it is ONE read
uintptr_t s_taken = 0;
int       s_was_type = 0;
uintptr_t s_was_subject = 0;

// THE IDENTITY WATCHDOG. The user's two-camera question - "does it create
// another camera so there's 2?" - is answerable with data instead of theory:
// every identity change is LOGGED. Some games legitimately swap CurrentCamera
// (respawns, cutscenes, streaming), and the old 400ms cache held freecam on a
// dead camera for up to 400ms per swap - the renderer drawing the NEW camera
// (the game's view) while we hammered the old one. The workspace is cached
// (2s) so resolving CurrentCamera is a single pointer read, done EVERY call:
// a swap is noticed within one tick, and the swap itself names itself.
uintptr_t s_prev_cam_addr = 0;
uint64_t    s_last_swap_log = 0;

} // namespace

uintptr_t WorkspaceAddr()
{
    Current();   // refreshes/maintains the workspace cache as a side effect
    return s_ws.is_valid() ? s_ws.address : 0;
}

instance Current()
{
    const uint64_t now = platform::NowMs();
    instance ws = s_ws;
    if (!ws.is_valid() || (now - s_ws_at) >= 2000) {
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return instance{};
        s_ws = ws;
        s_ws_at = now;
    }
    s_cam = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
    s_cam_at = now;
    if (s_cam.is_valid() && s_cam.address != s_prev_cam_addr) {
        if (is_valid_address(s_prev_cam_addr) &&
            (s_last_swap_log == 0 || (now - s_last_swap_log) > 2000)) {
            s_last_swap_log = now;
            LogLine("camera: Workspace.CurrentCamera changed 0x%llX -> 0x%llX",
                    (unsigned long long)s_prev_cam_addr,
                    (unsigned long long)s_cam.address);
        }
        s_prev_cam_addr = s_cam.address;
    }
    return s_cam;
}

void Take(const instance& cam)
{
    if (!cam.is_valid()) return;
    if (s_taken != cam.address) {
        s_was_type = read<int>(cam.address + Offsets::Camera::CameraType);
        s_was_subject = read<uintptr_t>(cam.address + Offsets::Camera::CameraSubject);
        s_taken = cam.address;
    }
    // Scriptable only. Clearing CameraSubject leaves the camera with nothing to
    // project and is what "bugs out your camera" looked like.
    if (Offsets::Camera::CameraType) {
        write<int>(cam.address + Offsets::Camera::CameraType, kScriptable);
    }

    // VERIFY, do not assume. The reported freecam symptom is "you can rotate the
    // camera around your player but you cannot move it around the map" - and
    // rotating AROUND THE PLAYER is precisely what the default camera module
    // does. It recomputes the camera's position from CameraSubject every frame
    // and leaves orientation to whatever last set it, so our rotation appears to
    // work while every position write is immediately replaced. That is the
    // signature of the camera NOT being in Scriptable mode, even though the
    // write returned true.
    //
    // Two ways that happens: CameraType is not this field / not this enum value,
    // or Roblox's CameraScript re-asserts Custom on its own heartbeat and wins.
    // Both are invisible to a blind write, so read it back and say so once.
    static uintptr_t s_warned_for = 0;
    const int got = read<int>(cam.address + Offsets::Camera::CameraType);
    if (got != kScriptable && s_warned_for != cam.address) {
        s_warned_for = cam.address;
        LogLine("camera: wrote CameraType=%d (Scriptable) at 0x%llx but it reads "
                "back %d. The default camera module is still driving position - "
                "expect the view to orbit your character and ignore movement. "
                "Either the CameraType offset/enum is wrong, or the game's "
                "CameraScript is re-asserting it every frame.",
                kScriptable,
                (unsigned long long)Offsets::Camera::CameraType, got);
    }
}

void Restore(int* wrote_type, uintptr_t* wrote_subject)
{
    if (wrote_type) *wrote_type = 0;
    if (wrote_subject) *wrote_subject = 0;
    if (!s_taken) return;
    instance cam = Current();
    if (cam.is_valid() && cam.address == s_taken) {
        if (s_was_type >= 0 && s_was_type <= 6)
            if (Offsets::Camera::CameraType) {
                write<int>(cam.address + Offsets::Camera::CameraType, s_was_type);
            }
        else
            if (Offsets::Camera::CameraType) {
                write<int>(cam.address + Offsets::Camera::CameraType, 0); // Fixed / custom follow
            }
        if (s_was_subject)
            if (Offsets::Camera::CameraSubject) {
                write<uintptr_t>(cam.address + Offsets::Camera::CameraSubject, s_was_subject);
            }
        else {
            const uintptr_t mine = cache::GetLocalPlayer().humanoid_address;
            if (is_valid_address(mine))
                s_was_subject = mine;
            if (s_was_subject)
                if (Offsets::Camera::CameraSubject) {
                    write<uintptr_t>(cam.address + Offsets::Camera::CameraSubject, s_was_subject);
                }
        }
        if (wrote_type) *wrote_type = (s_was_type >= 0 && s_was_type <= 6) ? s_was_type : 0;
        if (wrote_subject) *wrote_subject = s_was_subject;
    }
    s_taken = 0;
    s_was_type = 0;
    s_was_subject = 0;
}

bool Ours() { return s_taken != 0; }

bool ReadPos(const instance& cam, float out[3])
{
    return cam.is_valid() && read_raw(cam.address + Offsets::Camera::Position, out, 12);
}
bool ReadRot(const instance& cam, float out[9])
{
    return cam.is_valid() && read_raw(cam.address + Offsets::Camera::Rotation, out, 36);
}

void LookVectors(const float rot[9], float fwd[3], float right[3], float up[3])
{
    right[0] = rot[0]; right[1] = rot[3]; right[2] = rot[6];
    up[0]    = rot[1]; up[1]    = rot[4]; up[2]    = rot[7];
    fwd[0]   = -rot[2]; fwd[1]  = -rot[5]; fwd[2]  = -rot[8];
}

void LookAtMatrix(const float from[3], const float to[3], float rot[9])
{
    float fx = to[0] - from[0], fy = to[1] - from[1], fz = to[2] - from[2];
    float m = sqrtf(fx * fx + fy * fy + fz * fz);
    if (m < 1e-4f) { fx = 0; fy = 0; fz = -1; m = 1; }
    fx /= m; fy /= m; fz /= m;
    // RIGHT = FWD x WORLD_UP, then UP = RIGHT x FWD. The old order here
    // (right = world_up x fwd, up = fwd x right) is the SAME left-handed
    // basis bug freecam's Tick had (round 13): it built a det=-1 mirrored
    // rotation - and freecam's handback (round 15) started calling this, so
    // every freecam-OFF parked the camera with a MIRRORED rotation while the
    // subject was also restored nil (nothing to re-derive from): "you dont
    // return to normal", and each on/off cycle poisoned the next session.
    // right x up = -fwd holds for this construction -> det = +1, proper.
    float rx = fy * 0.0f - fz * 1.0f;
    float ry = fz * 0.0f - fx * 0.0f;
    float rz = fx * 1.0f - fy * 0.0f;
    m = sqrtf(rx * rx + ry * ry + rz * rz);
    if (m < 1e-4f) { rx = 1; ry = 0; rz = 0; m = 1; }
    rx /= m; ry /= m; rz /= m;
    // up = right x fwd
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;
    rot[0] = rx; rot[3] = ry; rot[6] = rz;
    rot[1] = ux; rot[4] = uy; rot[7] = uz;
    rot[2] = -fx; rot[5] = -fy; rot[8] = -fz;
}

void WriteCFrame(const instance& cam, const float pos[3], const float rot[9])
{
    if (!cam.is_valid()) return;
    // Rotation (0xd8) and Position (0xfc) are one contiguous CFrame - 0xd8 + 36
    // bytes IS 0xfc - so this is a single 48-byte write. As two writes the
    // renderer could sample the frame between them and draw one composed of the
    // old orientation and the new position, which reads as the view snapping.
    float cf[12];
    for (int i = 0; i < 9; ++i) cf[i] = rot[i];
    cf[9] = pos[0]; cf[10] = pos[1]; cf[11] = pos[2];
    if (Offsets::Camera::Rotation + 36 == Offsets::Camera::Position) {
        if (Offsets::Camera::Rotation) {
            write_raw(cam.address + Offsets::Camera::Rotation, cf, sizeof(cf));
        }
    } else {
        // An offset update could break the adjacency; then it is two writes again,
        // which is worse but still correct.
        if (Offsets::Camera::Rotation) {
            write_raw(cam.address + Offsets::Camera::Rotation, cf, 36);
        }
        if (Offsets::Camera::Position) {
            write_raw(cam.address + Offsets::Camera::Position, cf + 9, 12);
        }
    }
}

void WriteLookAt(const instance& cam, const float pos[3], const float look_at[3])
{
    float rot[9];
    LookAtMatrix(pos, look_at, rot);
    WriteCFrame(cam, pos, rot);
}

} // namespace cam
} // namespace features
