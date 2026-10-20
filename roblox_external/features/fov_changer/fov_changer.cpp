#include <cmath>
#include "fov_changer.h"
#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "game.h"

namespace features {

    static instance fov_camera{};
    static uint64_t fov_last_lookup = 0;

    // THE RESTORE. The first version wrote the camera's FOV while the switch was
    // on and never put it back - which is exactly the user's report: "your fov
    // stays at what it is even when you turn it off". The original is now saved
    // per CAMERA INSTANCE (respawns and freecam swap the instance, and restoring
    // into a dead address is a write into freed memory), restored the moment the
    // switch goes off, and restored again on detach.
    static uintptr_t fov_saved_cam = 0;
    static float     fov_saved_value = 0.0f;
    static bool      fov_saved = false;

    static instance GetCamera() {
        uint64_t now = platform::NowMs();
        if (fov_camera.is_valid() && (now - fov_last_lookup) < 1000)
            return fov_camera;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return instance{};
        fov_camera = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
        fov_last_lookup = now;
        return fov_camera;
    }

    void ReleaseFovChanger() {
        if (fov_saved && is_valid_address(fov_saved_cam)) {
            bool ok = false;
            if (Offsets::Camera::FieldOfView)
                ok = write<float>(fov_saved_cam + Offsets::Camera::FieldOfView,
                                         fov_saved_value);
            float back = read<float>(fov_saved_cam + Offsets::Camera::FieldOfView);
            LogLine("fov: restored %.3f rad (%.1f deg) %s", (double)fov_saved_value,
                    (double)(fov_saved_value * 57.2957795f),
                    ok && fabsf(back - fov_saved_value) < 0.001f
                        ? "- read back, ok"
                        : "- read back MISMATCH (the camera may have been replaced)");
        }
        fov_saved = false;
        fov_saved_cam = 0;
    }

    // IMPORTANT: roblox stores FieldOfView internally in RADIANS, not degrees.
    // Writing the raw degree value (e.g. 70) meant writing 70 radians (~4010 deg),
    // which wrapped the projection - that's why the fov went the wrong way and
    // the screen ended up upside down.
    void RunFovChanger() {
        if (!fov_changer_enabled) {
            if (fov_saved) ReleaseFovChanger();
            return;
        }

        instance cam = GetCamera();
        if (!cam.is_valid()) return;

        float current = read<float>(cam.address + Offsets::Camera::FieldOfView);
        if (!(current > 0.0f) || current > 4.0f) return;   // not a sane fov - bail

        // First pass on this camera, or the camera changed under us: remember
        // exactly what the client had BEFORE our first write.
        if (!fov_saved || fov_saved_cam != cam.address) {
            if (fov_saved) ReleaseFovChanger();   // restore the old camera first
            fov_saved = true;
            fov_saved_cam = cam.address;
            fov_saved_value = current;
            LogLine("fov: saved the camera's own value (%.3f rad) before writing", (double)current);
        }

        float deg = fov_value;
        if (deg < 1.0f)   deg = 1.0f;
        if (deg > 120.0f) deg = 120.0f;

        // Two storage forms are possible and they are not interchangeable: >2.3 is
        // degrees (no sane FOV is that many radians), otherwise radians - the form
        // this repo learned the hard way (see above). Classify what we READ, not
        // what we want, so a client that changes form still gets a sane number.
        const float kPi = 3.14159265358979323846f;
        const float want = current > 2.3f ? deg : deg * kPi / 180.0f;

        if (fabsf(current - want) > 0.0005f)
            if (Offsets::Camera::FieldOfView) {
                if (Offsets::Camera::FieldOfView) {
                    write<float>(cam.address + Offsets::Camera::FieldOfView, want);
                }
            }
    }
}
