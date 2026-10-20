#include <cmath>
#include <cstdarg>
#include <cstring>
#include "click_teleport.h"
#include "tp_math.h"
#include "features/players/players.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"

namespace features {

    static instance ct_cached_camera{};
    static uint64_t ct_last_camera_lookup = 0;
    static bool ct_key_held = false;
    static uint64_t ct_last_jump = 0;
    static bool ct_unarmed_logged = false;
    static bool ct_menu_logged = false;
    // "I clicked and nothing happened" is the only symptom of ten different causes,
    // so every exit path says which one it was. This runs on a press, not every
    // frame, so it cannot flood the 200-line ring buffer.
    static void Refuse(const char* fmt, ...)
    {
        char buf[192];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        LogLine("click-tp: %s", buf);
    }

    static instance GetCamera() {
        uint64_t now = platform::NowMs();
        if (ct_cached_camera.is_valid() && (now - ct_last_camera_lookup) < 1000)
            return ct_cached_camera;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance workspace = dm.read_service("Workspace");
        if (!workspace.is_valid()) return instance{};
        ct_cached_camera = read<instance>(workspace.address + Offsets::Workspace::CurrentCamera);
        ct_last_camera_lookup = now;
        return ct_cached_camera;
    }

    // Teleport to wherever the mouse cursor is pointing.
    //
    // The ray is the exact inverse of the projection ESP draws with (tp_math.h,
    // unit-tested in glass-obsidian/tools/tp_math_test.cpp), so "the pixel under the
    // cursor" means the same thing here as it does in the box on screen. What the
    // click then *lands on* is the part a fixed distance never got right, so the
    // destination is solved in this order:
    //
    //   1. a player under the cursor  -> their feet, lifted by "land above by".
    //      Clicking a person and getting thrown past them felt broken, and the
    //      server list already makes "jump to them" a first-class thing to want.
    //   2. the standing plane under you (root - about half a character) -> that
    //      plane is as close to "the floor" as an external gets without a raycast;
    //      the root plane was the old bug, since in first person it sits right at
    //      eye level and the solve collapses onto the camera.
    //   3. nothing below the cursor at all (a click at the sky) -> the range cap
    //      along the ray, which is the only honest reading of "that way".
    void RunClickTeleport() {
        if (!click_teleport_enabled) {
            // Off means off - but a bind press while off is the one way to end up
            // clicking and getting nothing with the switch forgotten, and silence is
            // not an answer. Say it once, then stay quiet until it is armed again.
            const int trig = click_teleport_keybind ? click_teleport_keybind : platform::key::LBUTTON;
            const bool down = platform::IsKeyDown(trig);
            if (down && !ct_key_held && !ct_unarmed_logged) {
                LogLine("click-tp: not armed. The movement page's CLICK TELEPORT switch is "
                        "off; a bind on its own does not enable the feature.");
                ct_unarmed_logged = true;
            }
            ct_key_held = down;
            return;
        }
        ct_unarmed_logged = false;

        // The bind is the trigger when there is one, left mouse button otherwise
        // ("click where you want to go" is the point of the feature). This used to
        // be hardcoded to platform::key::LBUTTON: click_teleport_keybind was saved, loaded and
        // displayed by the menu but read by nothing, so binding it did nothing.
        const int trig = click_teleport_keybind ? click_teleport_keybind : platform::key::LBUTTON;

        // A click that belongs to the UI must not move the latch either, or the
        // click used to close the menu would eat the first real click after it.
        // Note it SYNCs the latch to the key instead of clearing it: clearing it
        // meant that holding the bind over the menu and letting go afterwards fired a
        // jump the moment the menu closed.
        const bool down_now = platform::IsKeyDown(trig);
        if (g_menu_open.load(std::memory_order_relaxed)) {
            // A click while the menu is open belongs to the menu - but that
            // used to be a SILENT swallow, and "click tp does nothing" with a
            // silent log had exactly this hole in it. Say it once per arm.
            if (down_now && !ct_key_held && !ct_menu_logged) {
                ct_menu_logged = true;
                LogLine("click-tp: the menu is open, clicks belong to it - close "
                        "the menu and click again");
            }
            ct_key_held = down_now;
            return;
        }
        ct_menu_logged = false;

        // An edge, not a level: one jump per press. The low bit of GetAsyncKeyState
        // ("pressed since the last call") is also honoured, because a short click on
        // a mouse button can start and finish between two polls of this loop and a
        // transition test would miss it outright. Other code polls the same bit, so
        // it is a bonus path here and not the primary one.
        const bool edge = down_now && !ct_key_held;
        const bool sticky = platform::KeyPressed(trig) != 0;
        ct_key_held = down_now;
        if (!edge && !sticky) return;

        const uint64_t now = platform::NowMs();
        if (now - ct_last_jump < 200) return;      // a double-click is one jump
        ct_last_jump = now;

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (!lp.valid) return Refuse("no local player cached - load into a game first");
        if (!is_valid_address(lp.hrp_primitive)) return Refuse("local HumanoidRootPart primitive is not resolved");

        instance cam = GetCamera();
        if (!cam.is_valid()) return Refuse("Workspace.CurrentCamera unreadable - camera offsets stale?");

        // ONE READ for the whole CFrame. The freecam war proved the default
        // camera module rewrites this field EVERY frame; the old two separate
        // reads (position, then rotation) could catch position from one frame
        // and rotation from the NEXT - a ray bent between where you look and
        // where the camera is, and a teleport that lands somewhere else than
        // the pixel you clicked. Rotation (0xd8) + 36 == Position (0xfc), so
        // one 48-byte read is atomic-with-respect-to-us: both halves from the
        // same frame, always.
        float camcf[12] = {};
        float* rot = camcf;
        float* campos = camcf + 9;
        if (Offsets::Camera::Rotation + 36 == Offsets::Camera::Position) {
            if (!read_raw(cam.address + Offsets::Camera::Rotation, camcf, sizeof(camcf)))
                return Refuse("Camera::CFrame (48B single read) failed");
        } else {
            if (!read_raw(cam.address + Offsets::Camera::Rotation, rot, 36))
                return Refuse("Camera::Rotation read failed");
            if (!read_raw(cam.address + Offsets::Camera::Position, campos, 12))
                return Refuse("Camera::Position read failed");
        }
        for (int i = 0; i < 9; ++i)
            if (!(rot[i] > -1e6f && rot[i] < 1e6f)) return Refuse("Camera::Rotation is garbage");

        // stored in radians, not degrees
        const float fov_rad = read<float>(cam.address + Offsets::Camera::FieldOfView);

        // viewport dimensions straight from the renderer
        instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
        if (!ve.is_valid()) return Refuse("VisualEngine pointer unread");
        float dims[2] = {};
        if (!read_raw(ve.address + Offsets::VisualEngine::Dimensions, dims, sizeof(dims)))
            return Refuse("VisualEngine::Dimensions read failed");

        // the viewport is roblox's CLIENT area - if the game is windowed, the desktop
        // cursor position is offset from it, which threw the ray off. CursorClient
        // converts (and reports failure when the game window cannot be found).
        int cursor_x = 0, cursor_y = 0;
        if (!platform::CursorClient(cursor_x, cursor_y)) return Refuse("cursor/window read failed");

        const tpmath::Ray ray = tpmath::ScreenRay(campos, rot, fov_rad, dims[0], dims[1],
                                                  (float)cursor_x, (float)cursor_y);
        if (!ray.ok) return Refuse("no ray: %s", ray.reason);

        const float land = tp_land_height > 0.0f ? tp_land_height : 0.0f;
        float t = tpmath::Clamp(click_teleport_distance, 4.0f, 500.0f);
        bool on_player = false;
        tpmath::Vec3 target{};

        // 1. a player under the cursor
        {
            const cache::EspSnapshot ents = cache::GetEspSnapshot();
            float best = 1e30f;
            int hit_i = -1;
            if (ents) {
                for (size_t i = 0; i < ents->size(); ++i) {
                    const cache::EspEntity& e = (*ents)[i];
                    // a character's canonical box, centred on the cached root: no
                    // reads, and the local player is not in this snapshot anyway
                    const float ctr[3] = { e.root_x, e.root_y, e.root_z };
                    const float half[3] = { 1.2f, 2.6f, 1.2f };
                    float th = 0.0f;
                    if (!tpmath::HitAabb(ray, ctr, half, th)) continue;
                    if (th > 0.5f && th < best) { best = th; hit_i = (int)i; }
                }
            }
            if (hit_i >= 0) {
                const cache::EspEntity& e = (*ents)[hit_i];
                target = { e.root_x, e.root_y + land, e.root_z };
                t = best;
                on_player = true;
            }
        }

        // 2. the standing plane, or 3. nothing below the cursor at all -> the cap
        if (!on_player) {
            // ~2.6 studs from an HRP centre to the feet of a standing rig. Not exact
            // for every avatar: the "land above by" slider absorbs the difference, and
            // arriving slightly high only costs a short fall, which is the failure mode
            // that does not put you inside the floor.
            const float plane_y = lp.y - 2.6f;
            float tp = 0.0f;
            const bool ground = tpmath::HitPlaneY(ray, plane_y, tp);
            if (ground) t = tpmath::Clamp(tp, 2.0f, t);
            target = { campos[0] + ray.d.x * t,
                       campos[1] + ray.d.y * t + (ground ? land : 0.0f),
                       campos[2] + ray.d.z * t };
            if (!ground)
                t = tpmath::Clamp(click_teleport_distance, 4.0f, 500.0f);
        }

        if (!(target.x == target.x && target.y == target.y && target.z == target.z)) {
            Refuse("destination came out non-finite, nothing written");
            return;
        }
        if (std::fabs(target.x - lp.x) < 0.5f && std::fabs(target.z - lp.z) < 0.5f &&
            std::fabs(target.y - lp.y) < 0.5f) {
            Refuse("destination is where you already stand (%.0f,%.0f,%.0f): the cursor "
                   "is not over anywhere worth going", target.x, target.y, target.z);
            return;
        }

        if (!WriteRootTo(target.x, target.y, target.z, on_player ? "cursor (player)" : "cursor")) {
            // WriteRootTo explains itself in players_status_msg (a UI string
            // on the players page) - which the click-tp user never sees. A
            // silent return here was indistinguishable from "the click did
            // nothing"; say the reason in the LOG too.
            Refuse("could not write: %s", players_status_msg);
            return;
        }

        LogLine("click-tp: jumped %.0f studs to %.0f,%.0f,%.0f (%s)",
                t, target.x, target.y, target.z, on_player ? "player under cursor"
                                                            : "ground plane");
    }
}
