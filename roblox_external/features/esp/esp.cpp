#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include "esp.h"
#include "globals.h"
#include "features/players/friends.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"
#include "raycast/world_ray.h"
#include "range.h"
#include "imgui/imgui.h"

namespace features {

    struct Vec2 {
        float x;
        float y;
    };

    struct Vec3 {
        float x;
        float y;
        float z;
    };

    struct Matrix4 {
        float data[16];
    };

    struct Box2D {
        float min_x;        float min_y;

        float max_x;
        float max_y;
        bool valid;
    };

    // ── strokes ───────────────────────────────────────────────────────────────
    //  Every ESP element used to build its lines by stacking 1px AddRectFilled
    //  calls (a black one, a coloured one, another black one for the inner edge).
    //  That is why the ESP could not be made thicker: the width was geometry, not a
    //  setting. These three helpers own the whole vocabulary - halo, outline, line -
    //  so esp_line_width / esp_glow reach everything at once and stay
    //  consistent between a box, a skeleton and a tracer.

    static ImVec4 Mix4(const float* a, const float* b, float t) {
        return ImVec4(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
                      a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t);
    }

    static float EspWidth(float scale = 1.0f) {
        const float w = esp_line_width * scale;
        return w < 0.5f ? 0.5f : w;
    }

    // Scale only the alpha of a packed colour: a glow is the same hue, dimmer, and
    // recomputing it from the float array would lose whatever EspTint decided.
    static ImU32 FadeCol(ImU32 col, float mul) {
        const unsigned int a = (unsigned int)(((col >> 24) & 0xFF) * (mul < 0.0f ? 0.0f : (mul > 1.0f ? 1.0f : mul)));
        return (col & 0x00FFFFFFu) | (a << 24);
    }

    static void GlowRect(ImDrawList* d, const ImVec2& a, const ImVec2& b, ImU32 col, float r) {
        if (esp_glow <= 0.001f) return;
        const float pad = EspWidth(2.0f) + 1.0f;
        d->AddRect(ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad),
                   FadeCol(col, 0.16f * esp_glow), r, 0, pad * 0.9f);
        d->AddRect(ImVec2(a.x - pad * 0.5f, a.y - pad * 0.5f), ImVec2(b.x + pad * 0.5f, b.y + pad * 0.5f),
                   FadeCol(col, 0.30f * esp_glow), r, 0, pad * 0.5f);
    }

    static void StrokeRect(ImDrawList* d, const ImVec2& a, const ImVec2& b, ImU32 col, float r) {
        const float w = EspWidth();
        GlowRect(d, a, b, col, r);
        d->AddRect(a, b, col, r, 0, w);
    }

    // Corner brackets: four L's, each a pair of segments. Length is a fraction of the
    // SHORTER side on purpose - a crouched player is a tall thin box, and a fraction
    // of its width is the only definition that keeps the four brackets looking alike.
    static void StrokeCorners(ImDrawList* d, const ImVec2& a, const ImVec2& b, ImU32 col) {
        const float w = EspWidth();
        float bw = b.x - a.x, bh = b.y - a.y;
        float len = (std::min)(bw, bh) * (std::max)(0.05f, (std::min)(0.5f, esp_corner_frac));
        len = (std::min)(len, (std::min)(bw, bh) * 0.5f - 1.0f);
        if (len < 1.0f) len = 1.0f;
        const ImVec2 pts[8][2] = {
            { { a.x, a.y },        { a.x + len, a.y } },
            { { a.x, a.y },        { a.x, a.y + len } },
            { { b.x, a.y },        { b.x - len, a.y } },
            { { b.x, a.y },        { b.x, a.y + len } },
            { { a.x, b.y },        { a.x + len, b.y } },
            { { a.x, b.y },        { a.x, b.y - len } },
            { { b.x, b.y },        { b.x - len, b.y } },
            { { b.x, b.y },        { b.x, b.y - len } },
        };
        if (esp_glow > 0.001f)
            for (auto& seg : pts)
                d->AddLine(seg[0], seg[1], FadeCol(col, 0.28f * esp_glow), w + EspWidth(1.6f));
        for (auto& seg : pts)
            d->AddLine(seg[0], seg[1], col, w);
    }

    static ImU32 Col4(const ImVec4& c, float a_mul = 1.0f) {
        return IM_COL32((int)(c.x * 255.0f), (int)(c.y * 255.0f), (int)(c.z * 255.0f),
                        (int)((c.w * a_mul) * 255.0f));
    }

    static ImU32 Col4(const float* c, float a_mul = 1.0f) {
        return IM_COL32((int)(c[0] * 255.0f), (int)(c[1] * 255.0f),
                        (int)(c[2] * 255.0f), (int)(c[3] * 255.0f * a_mul));
    }
    // Whitelist > friend > team > the feature's own colour. The friend set is
    // keyed by user id (features/players/friends), so it survives a respawn;
    // the name-keyed lists stay name-keyed because a username is what the user
    // types, and the two are deliberately different keys.
    static ImU32 EspTint(uint32_t user_id, const char* player_name, bool teammate,
                         const float* fallback) {
        if (PlayerIsWhitelisted(player_name)) return Col4(whitelist_esp_color);
        if (features::friends::IsFriend(user_id)) return Col4(friends_esp_color);
        if (teammate && team_check) return Col4(team_esp_color);
        return Col4(fallback);
    }

    static bool ReadRaw(uint64_t address, void* buffer, size_t size) {
        return read_raw(address, buffer, size);
    }

    static bool ReadVec2(uint64_t address, Vec2& out) {
        return ReadRaw(address, &out, sizeof(out));
    }

    static bool ReadVec3(uint64_t address, Vec3& out) {
        return ReadRaw(address, &out, sizeof(out));
    }

    static float LengthSq(const Vec3& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    static Vec3 Normalize(const Vec3& v) {
        float len_sq = LengthSq(v);
        if (len_sq <= 0.000001f) return { 0.0f, 0.0f, 0.0f };
        float inv = 1.0f / sqrtf(len_sq);
        return { v.x * inv, v.y * inv, v.z * inv };
    }

    static Vec3 Sub(const Vec3& a, const Vec3& b) {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    static int FindEntityPartIndex(const cache::EspEntity& entity, const char* part_name) {
        for (size_t i = 0; i < entity.primitive_count; ++i) {
            if (strcmp(entity.part_names[i], part_name) == 0) return (int)i;
        }
        return -1;
    }


    static instance cached_camera{};
    static uint64_t last_camera_lookup = 0;

    static instance GetCameraInstance() {
        uint64_t now = platform::NowMs();
        if (cached_camera.is_valid() && (now - last_camera_lookup) < 2000)
            return cached_camera;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return instance{};
        cached_camera = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
        last_camera_lookup = now;
        return cached_camera;
    }

    static float Dot3(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // Build the clip matrix from the CAMERA itself (position/rotation/fov), not
    // from VisualEngine::ViewMatrix. The camera offsets are the ones every other
    // feature uses and are confirmed working, while the ViewMatrix layout on this
    // client was the thing making boxes drift off the character with distance.
    static bool GetCamera(Matrix4& view, Vec2& viewport) {
        instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
        if (!ve.is_valid()) return false;
        if (!ReadVec2(ve.address + Offsets::VisualEngine::Dimensions, viewport)) return false;
        if (viewport.x <= 0.0f || viewport.y <= 0.0f) return false;

        instance cam = GetCameraInstance();
        if (!cam.is_valid()) return false;

        Vec3 pos{};
        float rot[9]{};
        if (!ReadVec3(cam.address + Offsets::Camera::Position, pos)) return false;
        if (!ReadRaw(cam.address + Offsets::Camera::Rotation, rot, sizeof(rot))) return false;

        float fov = read<float>(cam.address + Offsets::Camera::FieldOfView);
        if (fov < 0.1f || fov > 3.0f) fov = 1.2217f;   // ~70 deg fallback

        // camera basis out of the Roblox rotation matrix (column-major: right/up/-forward)
        Vec3 right = {  rot[0],  rot[3],  rot[6] };
        Vec3 up    = {  rot[1],  rot[4],  rot[7] };
        Vec3 fwd   = { -rot[2], -rot[5], -rot[8] };

        float tan_half = tanf(fov * 0.5f);
        float aspect   = viewport.x / viewport.y;
        float scale_x  = 1.0f / (tan_half * aspect);
        float scale_y  = 1.0f / tan_half;

        // column-major matrix matching WorldToScreen's layout:
        //   clip.x = x*m0 + y*m1 + z*m2 + m3  = dot(right, p-pos) * scale_x
        //   clip.y = x*m4 + y*m5 + z*m6 + m7  = dot(up,    p-pos) * scale_y
        //   clip.w = x*m12+ y*m13+ z*m14+ m15 = dot(fwd,   p-pos)
        float* m = view.data;
        m[0]  = right.x * scale_x;  m[1]  = right.y * scale_x;  m[2]  = right.z * scale_x;
        m[3]  = -Dot3(right, pos) * scale_x;
        m[4]  = up.x * scale_y;     m[5]  = up.y * scale_y;     m[6]  = up.z * scale_y;
        m[7]  = -Dot3(up, pos) * scale_y;
        m[8]  = 0.0f;               m[9]  = 0.0f;               m[10] = 0.0f;               m[11] = 0.0f;
        m[12] = fwd.x;              m[13] = fwd.y;              m[14] = fwd.z;
        m[15] = -Dot3(fwd, pos);
        return true;
    }

    // ---------------------------------------------------------------------
    // Roblox renders into its window's CLIENT area, so everything WorldToScreen
    // produces is in client-space pixels. Our ImGui overlay, however, is a
    // full-screen window anchored at the desktop origin (0,0). If the game is
    // windowed (or has a title bar / borders / is on a scaled display) the two
    // coordinate systems differ by the client-area origin, plus a scale factor
    // when the backbuffer Dimensions don't match the client rect in pixels.
    //
    // That offset is CONSTANT in pixels, which is exactly why the bug only
    // looked broken "at long distance": up close the player fills a few hundred
    // pixels so a ~30px vertical shift is barely noticeable, but far away the
    // character is only a handful of pixels tall, so the same 30px shift parks
    // the whole box/name above their head.
    //
    // Convert client-space -> desktop-space before drawing.
    struct OverlayTransform {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float scale_x  = 1.0f;
        float scale_y  = 1.0f;
    };

    static OverlayTransform cached_transform{};
    static uint64_t last_transform_lookup = 0;

    static const OverlayTransform& GetOverlayTransform(const Vec2& viewport) {
        const uint64_t now = platform::NowMs();
        // refresh often enough to follow window drags/resizes without hammering
        // the platform window lookup every projected point.
        if ((now - last_transform_lookup) < 250 && last_transform_lookup != 0)
            return cached_transform;
        last_transform_lookup = now;

        OverlayTransform t{};
        int cx = 0, cy = 0, cw = 0, ch = 0;
        if (platform::GameClientRect(cx, cy, cw, ch)) {
            const float client_w = (float)cw;
            const float client_h = (float)ch;
            t.offset_x = (float)cx;
            t.offset_y = (float)cy;
            // Dimensions is the render target size; if it differs from
            // the client rect (DPI scaling / resolution scaling) map
            // between them instead of assuming 1:1.
            if (viewport.x > 0.0f && viewport.y > 0.0f) {
                t.scale_x = client_w / viewport.x;
                t.scale_y = client_h / viewport.y;
            }
        }
        cached_transform = t;
        return cached_transform;
    }

    static bool WorldToScreen(const Vec3& world, Vec2& out, const Matrix4& view, const Vec2& viewport) {
        const float* m = view.data;
        float w_x = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
        if (w_x < 0.01f) return false;
        float screen_x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
        float screen_y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
        float inv_w = 1.0f / w_x;
        out.x = (viewport.x * 0.5f * screen_x * inv_w) + (viewport.x * 0.5f);
        out.y = -(viewport.y * 0.5f * screen_y * inv_w) + (viewport.y * 0.5f);
        if (out.x != out.x || out.y != out.y) return false;

        // client-space -> overlay/desktop-space
        const OverlayTransform& t = GetOverlayTransform(viewport);
        out.x = out.x * t.scale_x + t.offset_x;
        out.y = out.y * t.scale_y + t.offset_y;
        return true;
    }

    // Canonical Roblox body-part sizes (studs). This client build reports ~0 from
    // Primitive::Size, so we fall back to these instead of letting a zero-sized
    // part collapse onto its centre point - that's what made the box float above
    // the character and shrink as they got further away.
    static void CanonicalPartSizeVec(const char* name, Vec3& out) {
        if (!name) { out = { 1, 1, 1 }; return; }
        // Exact name matches (confirmed via float probes for this client build)
        if (!strcmp(name, "Head"))                        { out = { 2, 1, 1 }; return; }
        if (!strcmp(name, "HumanoidRootPart"))            { out = { 2, 2, 1 }; return; }
        if (!strcmp(name, "Torso") || !strcmp(name, "UpperTorso")) { out = { 2, 2, 1 }; return; }
        if (!strcmp(name, "LowerTorso"))                  { out = { 2, 1, 1 }; return; }
        if (!strcmp(name, "Left Arm")  || !strcmp(name, "Right Arm"))  { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftUpperArm")  || !strcmp(name, "RightUpperArm")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftLowerArm")  || !strcmp(name, "RightLowerArm")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "Left Leg")  || !strcmp(name, "Right Leg"))  { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftUpperLeg")  || !strcmp(name, "RightUpperLeg")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftLowerLeg")  || !strcmp(name, "RightLowerLeg")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftHand") || !strcmp(name, "RightHand")) { out = { 1, 1, 1 }; return; }
        if (!strcmp(name, "LeftFoot") || !strcmp(name, "RightFoot")) { out = { 1, 1, 1 }; return; }
        // Category-based fallback for any unrecognized part name.
        // Roblox R6/R15 body parts follow predictable size patterns.
        // Check for substrings that might appear on parts with unusual names.
        if (name != nullptr) {
            // R15 limb parts
            if (strstr(name, "UpperArm") || strstr(name, "LowerArm")) { out = { 1, 2, 1 }; return; }
            if (strstr(name, "UpperLeg") || strstr(name, "LowerLeg")) { out = { 1, 2, 1 }; return; }
            // Hand/Foot variants
            if (strstr(name, "Hand") || strstr(name, "Foot")) { out = { 1, 1, 1 }; return; }
            // Head variant
            if (strstr(name, "Head")) { out = { 2, 1, 1 }; return; }
            // Torso variants
            if (strstr(name, "Torso") || strstr(name, "UpperTorso") || strstr(name, "LowerTorso")) { out = { 2, 2, 1 }; return; }
        }
        // Default fallback - unit cube (better than zero-sized part collapsing)
        out = { 1, 1, 1 };}

    // The public shape (esp.h): plain floats, so features/raycast can use the same
    // table instead of keeping a second copy that drifts.
    void CanonicalPartSize(const char* name, float& ox, float& oy, float& oz) {
        Vec3 v{};
        CanonicalPartSizeVec(name, v);
        ox = v.x;
        oy = v.y;
        oz = v.z;
    }

    // Wall check: segment-vs-AABB ray against the other players' cached parts.
    // (An external can't call the game's raycast and we don't have the world
    // primitives list, so this is line-of-sight through OTHER PLAYERS only.)
    // ── the draw-distance gate, in ONE place ─────────────────────────────────
    //  This used to be an `if (esp_render_distance > 0.0f && lp.valid)` block
    //  copy-pasted into two of the six render passes. The other four (skeleton,
    //  chams, expanded hitbox, and anything added later) had no gate at all, which
    //  is exactly what "some esp elements never disappear past the set range"
    //  looks like: the box vanished and the skeleton stayed. 0 means infinite.
    //
    //  When the local player has not been read yet the camera is used as the
    //  anchor instead, so the range setting keeps applying instead of silently
    //  switching itself off for a pass.
    static bool RangeAnchor(Vec3& out) {
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (lp.valid) { out = { lp.x, lp.y, lp.z }; return true; }
        instance cam = GetCameraInstance();
        if (cam.is_valid()) {
            Vec3 p{};
            if (ReadVec3(cam.address + Offsets::Camera::Position, p)) { out = p; return true; }
        }
        return false;
    }

    static bool InRenderRangeAt(float x, float y, float z) {
        if (!(esp_render_distance > 0.0f)) return true;   // 0 = infinite, by definition
        Vec3 anchor{};
        if (!RangeAnchor(anchor)) return true;            // nothing to measure from
        const float dx = x - anchor.x;
        const float dy = y - anchor.y;
        const float dz = z - anchor.z;
        // ONE rule, in features/esp/range.h, with test cases on the Linux side:
        // 0/negative/NaN setting = infinite, NaN distance = never drawn.
        return features::DrawRangeAllows(sqrtf(dx * dx + dy * dy + dz * dz), esp_render_distance);
    }

    static bool InRenderRange(const cache::EspEntity& entity) {
        return InRenderRangeAt(entity.root_x, entity.root_y, entity.root_z);
    }


    // Where to put a label when the box cannot be built. The box needs eight
    // projected corners and rejects anything under ~2px; a character at 20-25k
    // studs IS under 2px, and "the name stops drawing at long range" is exactly
    // that rejection. The head is the honest anchor (it is what the name belongs
    // to), the root is the fallback. This is also the reason a range of 0 (infinite)
    // used to look like "nothing draws at all" past a few thousand studs: the box
    // was gone, and the name and the distance went with it.
    static bool LabelPoint(const cache::EspEntity& entity, const Matrix4& view, const Vec2& viewport, Vec2& out) {
        for (size_t i = 0; i < entity.primitive_count; ++i) {
            if (strcmp(entity.part_names[i], "Head") != 0) continue;
            uintptr_t p = entity.primitives[i];
            if (!is_valid_address(p)) break;
            Vec3 pos{};
            if (ReadVec3(p + Offsets::Primitive::Position, pos) && WorldToScreen(pos, out, view, viewport))
                return true;
            break;
        }
        Vec3 root = { entity.root_x, entity.root_y, entity.root_z };
        if (root.x == 0.0f && root.y == 0.0f && root.z == 0.0f) return false;
        return WorldToScreen(root, out, view, viewport);
    }

    static bool LOSClear(const Vec3& from, const Vec3& to, uintptr_t skip_player) {
        if (!wall_check) return true;

        // ONE raycast, shared with the aimbot and the triggerbot, in
        // features/raycast/world_ray.cpp: grid-accelerated over the world sweep,
        // rotation-aware (a ramp is not a box), live re-reads for moving parts,
        // then the other players' bodies. The old copy here tested every box in a
        // flat loop with an axis-aligned approximation and was the reason a wall
        // check on a big map was both slow and wrong at the same time.
        const float from_p[3] = { from.x, from.y, from.z };
        const float to_p[3] = { to.x, to.y, to.z };
        return !worldray::Blocked(from_p, to_p, skip_player);
    }

    static bool ComputeBoxForPrimitives(const cache::EspEntity& entity, const Matrix4& view, const Vec2& viewport, Box2D& out_box) {
        if (entity.primitive_count == 0) {
            out_box.valid = false;
            return false;
        }

        // Build ONE world-space axis-aligned box around the character from each
        // part's centre + its canonical body size, then project that box's 8
        // corners. We deliberately do NOT read Primitive::Size / Primitive::Rotation
        // here: both offsets are unreliable on this client (Size reads ~0, Rotation
        // can be garbage), and applying them is what made the box drift above the
        // character and wobble with distance. Body-part sizes are essentially
        // constant in Roblox, so canonical sizes give a stable, distance-independent
        // box that hugs the character.
        float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
        float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
        bool any = false;

        for (size_t i = 0; i < entity.primitive_count; ++i) {
            uintptr_t primitive = entity.primitives[i];
            if (!is_valid_address(primitive)) continue;
            Vec3 pos{};
            Vec3 size{};
            if (!ReadVec3(primitive + Offsets::Primitive::Position, pos)) continue;
            CanonicalPartSizeVec(entity.part_names[i], size);

            float hx = size.x * 0.5f;
            float hy = size.y * 0.5f;
            float hz = size.z * 0.5f;

            if (pos.x - hx < min_x) min_x = pos.x - hx;
            if (pos.x + hx > max_x) max_x = pos.x + hx;
            if (pos.y - hy < min_y) min_y = pos.y - hy;
            if (pos.y + hy > max_y) max_y = pos.y + hy;
            if (pos.z - hz < min_z) min_z = pos.z - hz;
            if (pos.z + hz > max_z) max_z = pos.z + hz;
            any = true;
        }

        if (!any) {
            out_box.valid = false;
            return false;
        }

        const Vec3 corners[8] = {
            { min_x, min_y, min_z }, { min_x, min_y, max_z },
            { min_x, max_y, min_z }, { min_x, max_y, max_z },
            { max_x, min_y, min_z }, { max_x, min_y, max_z },
            { max_x, max_y, min_z }, { max_x, max_y, max_z },
        };

        bool has_point = false;
        float scr_min_x = 0.0f, scr_min_y = 0.0f, scr_max_x = 0.0f, scr_max_y = 0.0f;
        for (int c = 0; c < 8; ++c) {
            Vec2 pt{};
            if (WorldToScreen(corners[c], pt, view, viewport)) {
                if (!has_point) {
                    scr_min_x = scr_max_x = pt.x;
                    scr_min_y = scr_max_y = pt.y;
                    has_point = true;
                } else {
                    if (pt.x < scr_min_x) scr_min_x = pt.x;
                    if (pt.x > scr_max_x) scr_max_x = pt.x;
                    if (pt.y < scr_min_y) scr_min_y = pt.y;
                    if (pt.y > scr_max_y) scr_max_y = pt.y;
                }
            }
        }

        if (!has_point) {
            out_box.valid = false;
            return false;
        }

        float w = scr_max_x - scr_min_x;
        float h = scr_max_y - scr_min_y;
        if (w <= 1.0f || h <= 1.0f) {
            out_box.valid = false;
            return false;
        }

        out_box.min_x = scr_min_x;
        out_box.min_y = scr_min_y;
        out_box.max_x = scr_max_x;
        out_box.max_y = scr_max_y;
        out_box.valid = true;
        return true;
    }



    static bool ReadPos(uintptr_t primitive, Vec3& out) {
        if (!is_valid_address(primitive)) return false;
        return ReadVec3(primitive + Offsets::Primitive::Position, out);
    }

    // The public projection (esp.h). Waypoints - and anything else that wants to
    // put a marker on a world point - share this rather than re-deriving it, so
    // there is exactly one place where the camera basis, the FOV and the
    // client->desktop transform are agreed on.
    bool ProjectToOverlay(float wx, float wy, float wz, float& out_x, float& out_y) {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return false;
        Vec2 pt{};
        if (!WorldToScreen(Vec3{ wx, wy, wz }, pt, view, viewport)) return false;
        out_x = pt.x;
        out_y = pt.y;
        return true;
    }

    static bool PartToScreen(uintptr_t primitive, const Matrix4& view, const Vec2& viewport, Vec2& out) {
        if (!primitive) return false;
        Vec3 pos{};
        if (!ReadPos(primitive, pos)) return false;
        return WorldToScreen(pos, out, view, viewport);
    }

    // Skeleton lines take esp_line_width too. Deliberately: box and skeleton at two
    // different weights is exactly what makes an ESP look assembled from plugins.
    static void DrawSkeletonLine(ImDrawList* draw, const Vec2& a, const Vec2& b, ImU32 color) {
        if (esp_glow > 0.001f)
            draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), FadeCol(color, 0.22f * esp_glow),
                          EspWidth(2.4f));
        draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, EspWidth());
    }

    static void DrawBone(ImDrawList* draw, uintptr_t from, uintptr_t to, const Matrix4& view, const Vec2& viewport, ImU32 color) {
        if (!from || !to) return;
        Vec2 a{}, b{};
        if (!PartToScreen(from, view, viewport, a)) return;
        if (!PartToScreen(to, view, viewport, b)) return;
        DrawSkeletonLine(draw, a, b, color);
    }

    void RenderAimViewer() {
        if (!aimviewer) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto skeletons_snap = cache::GetSkeletonSnapshot();
        const auto& skeletons = *skeletons_snap;
        if (skeletons.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        for (const cache::SkeletonEntity& skel : skeletons) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(skel.name)) continue;
            if (!skel.head) continue;

            Vec3 head_pos{};
            if (!ReadVec3(skel.head + Offsets::Primitive::Position, head_pos)) continue;

            Vec3 look_direction{ 0.0f, 0.0f, -1.0f };
            bool has_valid_direction = false;

            float head_rot[9]{};
            if (ReadRaw(skel.head + Offsets::Primitive::Rotation, head_rot, sizeof(head_rot))) {
                look_direction = { -head_rot[2], -head_rot[5], -head_rot[8] };
                float len_sq = LengthSq(look_direction);
                if (len_sq > 0.0001f) {
                    look_direction = Normalize(look_direction);
                    has_valid_direction = true;
                }
            }

            if (!has_valid_direction && skel.upper_torso) {
                Vec3 torso_pos{};
                if (ReadVec3(skel.upper_torso + Offsets::Primitive::Position, torso_pos)) {
                    look_direction = Normalize(Sub(head_pos, torso_pos));
                    has_valid_direction = LengthSq(look_direction) > 0.0001f;
                }
            }

            if (!has_valid_direction) continue;

            Vec3 ray_end = {
                head_pos.x + look_direction.x * 40.0f,
                head_pos.y + look_direction.y * 40.0f,
                head_pos.z + look_direction.z * 40.0f
            };

            Vec2 screen_start{};
            Vec2 screen_end{};
            if (!WorldToScreen(head_pos, screen_start, view, viewport)) continue;
            if (!WorldToScreen(ray_end, screen_end, view, viewport)) continue;

            draw->AddLine(
                ImVec2(screen_start.x, screen_start.y),
                ImVec2(screen_end.x, screen_end.y),
                IM_COL32(255, 0, 0, 255),
                2.0f);
        }
    }

    void RenderSkeletonESP() {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto skeletons_snap = cache::GetSkeletonSnapshot();
        const auto& skeletons = *skeletons_snap;
        if (skeletons.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        for (const cache::SkeletonEntity& skel : skeletons) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(skel.name)) continue;
            if (!skel.head || !skel.upper_torso) continue;
            // The skeleton entity carries no cached position, so the gate measures
            // from the head primitive it is about to read anyway.
            {
                Vec3 head_pos{};
                if (ReadVec3(skel.head + Offsets::Primitive::Position, head_pos) &&
                    !InRenderRangeAt(head_pos.x, head_pos.y, head_pos.z)) continue;
            }
            ImU32 color = EspTint(skel.user_id, skel.name, skel.is_teammate, skeleton_color);

            if (skel.is_r15) {
                DrawBone(draw, skel.head, skel.upper_torso, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.lower_torso, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.left_upper_arm, view, viewport, color);
                DrawBone(draw, skel.left_upper_arm, skel.left_lower_arm, view, viewport, color);
                DrawBone(draw, skel.left_lower_arm, skel.left_hand, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.right_upper_arm, view, viewport, color);
                DrawBone(draw, skel.right_upper_arm, skel.right_lower_arm, view, viewport, color);
                DrawBone(draw, skel.right_lower_arm, skel.right_hand, view, viewport, color);
                DrawBone(draw, skel.lower_torso, skel.left_upper_leg, view, viewport, color);
                DrawBone(draw, skel.left_upper_leg, skel.left_lower_leg, view, viewport, color);
                DrawBone(draw, skel.left_lower_leg, skel.left_foot, view, viewport, color);
                DrawBone(draw, skel.lower_torso, skel.right_upper_leg, view, viewport, color);
                DrawBone(draw, skel.right_upper_leg, skel.right_lower_leg, view, viewport, color);
                DrawBone(draw, skel.right_lower_leg, skel.right_foot, view, viewport, color);
            }
            else {
                DrawBone(draw, skel.head, skel.upper_torso, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.left_hand, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.right_hand, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.left_foot, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.right_foot, view, viewport, color);
            }
        }
    }

    // NOTE: points coming out of WorldToScreen are already in overlay/desktop
    // space, so the visible rect has to be transformed the same way rather than
    // compared against the raw render dimensions.
    static bool OnScreen(const Vec2& pt, const Vec2& viewport) {
        const OverlayTransform& t = GetOverlayTransform(viewport);
        float left   = t.offset_x;
        float top    = t.offset_y;
        float right  = t.offset_x + viewport.x * t.scale_x;
        float bottom = t.offset_y + viewport.y * t.scale_y;
        return pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom;
    }

    void RenderChinaHatESP() {
        if (!chinahat) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        ImU32 color = IM_COL32(
            (int)(chinahat_color[0] * 255.0f),
            (int)(chinahat_color[1] * 255.0f),
            (int)(chinahat_color[2] * 255.0f),
            (int)(chinahat_color[3] * 255.0f * 0.42f)
        );

        const float hat_height = 1.0f;
        const float hat_radius = 1.5f;
        const int segments = 48;

        for (const cache::EspEntity& entity : entities) {
            int head_idx = FindEntityPartIndex(entity, "Head");
            if (head_idx < 0) continue;

            uintptr_t head_prim = entity.primitives[head_idx];
            if (!is_valid_address(head_prim)) continue;

            if (!InRenderRange(entity)) continue;

            Vec3 head_pos{};
            if (!ReadVec3(head_prim + Offsets::Primitive::Position, head_pos)) continue;

            Vec3 apex_pos = { head_pos.x, head_pos.y + hat_height + 0.15f, head_pos.z };

            std::vector<Vec3> base_points;
            base_points.reserve(segments);
            for (int i = 0; i < segments; ++i) {
                float angle = (2.0f * 3.14159f * i) / segments;
                base_points.emplace_back(Vec3{
                    head_pos.x + hat_radius * cosf(angle),
                    head_pos.y + 0.2f,
                    head_pos.z + hat_radius * sinf(angle)
                });
            }

            Vec2 apex_screen{};
            if (!WorldToScreen(apex_pos, apex_screen, view, viewport)) continue;

            std::vector<ImVec2> base_screen;
            base_screen.reserve(segments);
            bool any_on_screen = OnScreen(apex_screen, viewport);
            bool all_projected = true;
            for (const auto& point : base_points) {
                Vec2 screen_pos{};
                if (WorldToScreen(point, screen_pos, view, viewport)) {
                    base_screen.emplace_back(screen_pos.x, screen_pos.y);
                    any_on_screen |= OnScreen(screen_pos, viewport);
                } else {
                    all_projected = false;
                    break;
                }
            }
            if (!all_projected || !any_on_screen) continue;

            draw->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

            const float apex_soft_radius = 2.0f;
            for (size_t i = 0; i < segments; ++i) {
                size_t next = (i + 1) % segments;
                ImVec2 apex_offset = ImVec2(
                    apex_screen.x + cosf((2.0f * 3.14159f * i) / segments) * apex_soft_radius,
                    apex_screen.y + sinf((2.0f * 3.14159f * i) / segments) * apex_soft_radius
                );
                draw->AddTriangleFilled(
                    apex_offset,
                    base_screen[i],
                    base_screen[next],
                    color
                );
            }

            ImU32 base_color = IM_COL32(
                (int)(chinahat_color[0] * 255.0f),
                (int)(chinahat_color[1] * 255.0f),
                (int)(chinahat_color[2] * 255.0f),
                (int)(chinahat_color[3] * 255.0f * 0.6f * 0.42f)
            );
            draw->AddConvexPolyFilled(base_screen.data(), segments, base_color);

            ImU32 outline_color = IM_COL32(0, 0, 0, 100);
            for (size_t i = 0; i < segments; ++i) {
                size_t next = (i + 1) % segments;
                draw->AddLine(base_screen[i], base_screen[next], outline_color, 1.2f);
            }

            draw->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);
        }
    }

    static ImFont* GetEspFont() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts && io.Fonts->Fonts.Size > 1)
            return io.Fonts->Fonts[1];
        if (io.Fonts && io.Fonts->Fonts.Size > 0)
            return io.Fonts->Fonts[0];
        return nullptr;
    }

    static void DrawTextWithShadow(ImDrawList* draw, float font_size, const ImVec2& position, ImU32 color, const char* text) {
        if (!draw || !text) return;
        ImFont* font = GetEspFont();
        ImU32 shadow = IM_COL32(0, 0, 0, 255);
        if (font) {
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    if (i == 0 && j == 0) continue;
                    draw->AddText(font, font_size, ImVec2(position.x + i, position.y + j), shadow, text);
                }
            }
            draw->AddText(font, font_size, position, color, text);
        } else {
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    if (i == 0 && j == 0) continue;
                    draw->AddText(nullptr, font_size, ImVec2(position.x + i, position.y + j), shadow, text);
                }
            }
            draw->AddText(nullptr, font_size, position, color, text);
        }
    }

    static void RenderHealthBar(ImDrawList* draw, float x1, float y1, float x2, float y2, float health, float max_health) {
        float health_percent = (max_health > 0.0f) ? (health / max_health) : 0.0f;
        health_percent = (std::max)(0.0f, (std::min)(1.0f, health_percent));

        float box_height = y2 - y1;
        float bar_gap = (std::max)(2.0f, (std::min)(7.0f, box_height * 0.035f));
        float bar_x = x1 - bar_gap;
        // The bar is the one ESP element whose size is not a setting: it is a
        // proportion of the box, so it grows with distance the way the box does. Its
        // WEIGHT, though, comes from esp_line_width like everything else - a 2px box
        // with a 1px bar looks like two features that never met.
        const float bw = (std::max)(2.0f, EspWidth(1.4f));

        ImU32 outline = IM_COL32(0, 0, 0, 220);
        ImU32 background = IM_COL32(45, 45, 45, 220);
        const ImVec2 ha(bar_x, y1), hb(bar_x + bw, y2);

        float fill_height = box_height * health_percent;
        // Use the user-configured healthbar_color when set; otherwise fall back
        // to the classic green-to-red gradient based on health percentage.
        ImU32 bar_color;
        if (healthbar_color[0] != 0.0f || healthbar_color[1] != 1.0f || healthbar_color[2] != 0.0f) {
            bar_color = IM_COL32(
                (int)(healthbar_color[0] * 255.0f),
                (int)(healthbar_color[1] * 255.0f),
                (int)(healthbar_color[2] * 255.0f),
                (int)(healthbar_color[3] * 255.0f));
        } else {
            int r = (int)((1.0f - health_percent) * 255.0f);
            int g = (int)(health_percent * 255.0f);
            bar_color = IM_COL32(r, g, 0, 255);
        }

        if (esp_glow > 0.001f)
            draw->AddRect(ImVec2(ha.x - 2.0f, ha.y - 2.0f), ImVec2(hb.x + 2.0f, hb.y + 2.0f),
                          FadeCol(bar_color, 0.30f * esp_glow), 2.0f, 0, 3.0f);

        draw->AddRectFilled(ha, hb, background, 1.0f);
        draw->AddRectFilled(ImVec2(ha.x, floorf(y2 - fill_height)), ImVec2(hb.x, ceilf(y2)),
                            bar_color, 1.0f);
    }

    static void RenderName(ImDrawList* draw, float x1, float y1, float x2, float y2, const char* name,
                           uint32_t user_id, bool teammate) {
        if (!name || name[0] == '\0') return;
        const float font_size = esp_text_size;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, name) : ImGui::CalcTextSize(name);
        float center_x = (x1 + x2) * 0.5f;
        float x_pos = floorf(center_x - text_size.x * 0.5f + 0.5f);
        float y_pos = floorf(y1 - text_size.y - 2.0f + 0.5f);
        ImU32 col = EspTint(user_id, name, teammate, name_color);
        // A plate, not just a shadow: over a bright skybox the 1px outline is the
        // difference between a name you can read and a smudge, and mode 2 keeps the
        // plate from looking like a second box stacked on the first.
        if (esp_name_plate) {
            const ImVec2 pa(x_pos - 3.0f, y_pos - 1.0f);
            const ImVec2 pb(x_pos + text_size.x + 3.0f, y_pos + text_size.y + 2.0f);
            if (esp_name_plate == 2)
                draw->AddRectFilledMultiColor(pa, pb, IM_COL32(0, 0, 0, 190), IM_COL32(0, 0, 0, 190),
                                              IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
            else
                draw->AddRectFilled(pa, pb, IM_COL32(0, 0, 0, 150), 2.0f);
        }
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, name);
    }

    static void RenderHealthText(ImDrawList* draw, float x1, float y1, float x2, float y2, float health) {
        char buf[32];
        snprintf(buf, sizeof(buf), "[%.0f]", health);
        const float font_size = esp_text_size;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
        float x_pos = x1 - 6.0f - text_size.x;
        if (healthbar) {
            x_pos -= 7.0f;
        }
        float y_pos = floorf(y1 - 3.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(health_text_color[0] * 255.0f),
            (int)(health_text_color[1] * 255.0f),
            (int)(health_text_color[2] * 255.0f),
            (int)(health_text_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, buf);
    }

    static void RenderRigType(ImDrawList* draw, float x1, float y1, float x2, float y2, bool is_r15) {
        const char* text = is_r15 ? "[R15]" : "[R6]";
        const float font_size = esp_text_size;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text) : ImGui::CalcTextSize(text);
        float x_pos = floorf(x2 + 5.0f + 0.5f);
        float y_pos = floorf(y1 - text_size.y + 10.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(rig_type_color[0] * 255.0f),
            (int)(rig_type_color[1] * 255.0f),
            (int)(rig_type_color[2] * 255.0f),
            (int)(rig_type_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, text);
    }

    static void RenderTool(ImDrawList* draw, float x1, float y1, float x2, float y2, const char* tool_name, bool has_distance) {
        char buf[96];
        if (!tool_name || tool_name[0] == '\0') {
            buf[0] = '['; buf[1] = 'N'; buf[2] = 'o'; buf[3] = 'n'; buf[4] = 'e'; buf[5] = ']'; buf[6] = '\0';
        } else {
            buf[0] = '[';
            int i = 1;
            for (const char* p = tool_name; *p && i < 92; ++p) {
                if (*p != '[' && *p != ']') buf[i++] = *p;
            }
            buf[i++] = ']';
            buf[i] = '\0';
        }
        const float font_size = esp_text_size;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
        float center_x = (x1 + x2) * 0.5f;
        float x_pos = floorf(center_x - text_size.x * 0.5f + 0.5f);
        float y_pos = floorf(y2 + (has_distance ? 18.0f : 3.0f) + 0.5f);
        ImU32 col = IM_COL32(
            (int)(tool_color[0] * 255.0f),
            (int)(tool_color[1] * 255.0f),
            (int)(tool_color[2] * 255.0f),
            (int)(tool_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, buf);
    }

    static void RenderDistance(ImDrawList* draw, float x1, float y1, float x2, float y2, float distance) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f studs", distance);
        const float font_size = esp_text_size;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
        float center_x = (x1 + x2) * 0.5f;
        float x_pos = floorf(center_x - text_size.x * 0.5f + 0.5f);
        float y_pos = floorf(y2 + 2.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(distance_color[0] * 255.0f),
            (int)(distance_color[1] * 255.0f),
            (int)(distance_color[2] * 255.0f),
            (int)(distance_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, buf);
    }

    // ------------------------------------------------------------------ chams
    // Two flavours of the same geometry:
    //
    //   BOX CHAMS  - every face of every part's cuboid, filled and unculled. It is
    //                the old behaviour: loud, and the near and far faces overlap
    //                so the figure reads as a coloured smear.
    //   MESH CHAMS - the backfaces are culled against the camera, the surviving
    //                faces are painted far-to-near, and each part gets an edge in
    //                the same colour. The result is a solid silhouette of the
    //                character's actual parts (head, torso, four limbs) that looks
    //                like a mesh rather than a stack of translucent boxes - which
    //                is what "mesh chams" means in every menu that has the row.
    //
    // Both go through the same corner/face tables, so a part that draws in one
    // draws in the other; only the culling, the order and the edges differ.
    static void DrawChamsPass(bool mesh) {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        Vec3 cam{};
        bool have_cam = false;
        {
            instance c = GetCameraInstance();
            if (c.is_valid() && ReadVec3(c.address + Offsets::Camera::Position, cam)) have_cam = true;
        }

        static const int faces[6][4] = {
            {0, 1, 3, 2},   // -X
            {4, 6, 7, 5},   // +X
            {0, 4, 5, 1},   // -Y
            {2, 3, 7, 6},   // +Y
            {0, 2, 6, 4},   // -Z
            {1, 5, 7, 3},   // +Z
        };
        static const int face_axis[6] = { 0, 0, 1, 1, 2, 2 };
        static const float face_sign[6] = { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f };

        struct FaceDraw {
            float depth;
            ImVec2 pts[4];
            ImU32 fill;
            ImU32 edge;
        };
        static thread_local std::vector<FaceDraw> batch;
        batch.clear();

        for (const cache::EspEntity& entity : entities) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(entity.name)) continue;
            if (!InRenderRange(entity)) continue;
            const float* base = mesh ? mesh_chams_color : chams_color;
            ImU32 fill = EspTint(entity.user_id, entity.name, entity.is_teammate, base);
            // A solid-looking volume wants less alpha than translucent boxes, or the
            // overlapping faces turn the figure into a smear anyway.
            if (mesh) {
                fill = (fill & 0x00FFFFFFu) | ((ImU32)((float)((fill >> 24) & 0xFFu) * 0.72f) << 24);
            }
            const ImU32 edge = (fill & 0x00FFFFFFu) | (0xFFu << 24);

            for (size_t i = 0; i < entity.primitive_count; ++i) {
                const uintptr_t prim = entity.primitives[i];
                if (!is_valid_address(prim)) continue;

                struct { float rot[9]; Vec3 pos; } rp{};
                if (!ReadRaw(prim + Offsets::Primitive::Rotation, &rp, sizeof(rp))) continue;

                Vec3 sz;
                CanonicalPartSizeVec(entity.part_names[i], sz);
                const float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;

                const float lx[8] = {-hx, -hx, -hx, -hx, hx, hx, hx, hx};
                const float ly[8] = {-hy, -hy, hy, hy, -hy, -hy, hy, hy};
                const float lz[8] = {-hz, hz, -hz, hz, -hz, hz, -hz, hz};

                Vec3 corners[8];
                for (int c = 0; c < 8; ++c) {
                    corners[c].x = rp.rot[0] * lx[c] + rp.rot[1] * ly[c] + rp.rot[2] * lz[c] + rp.pos.x;
                    corners[c].y = rp.rot[3] * lx[c] + rp.rot[4] * ly[c] + rp.rot[5] * lz[c] + rp.pos.y;
                    corners[c].z = rp.rot[6] * lx[c] + rp.rot[7] * ly[c] + rp.rot[8] * lz[c] + rp.pos.z;
                }

                Vec2 screen[8];
                bool ok[8];
                for (int c = 0; c < 8; ++c)
                    ok[c] = WorldToScreen(corners[c], screen[c], view, viewport);

                for (int f = 0; f < 6; ++f) {
                    const int a = faces[f][0], b = faces[f][1], ci = faces[f][2], d = faces[f][3];
                    if (!ok[a] || !ok[b] || !ok[ci] || !ok[d]) continue;

                    if (mesh && have_cam) {
                        // World normal: the axis column of the rotation, signed.
                        const int ax = face_axis[f];
                        float nx = rp.rot[0 + ax], ny = rp.rot[3 + ax], nz = rp.rot[6 + ax];
                        // A zero-length axis is a garbage read, not a wall: draw it.
                        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                        if (nl > 1e-4f) {
                            nx *= face_sign[f] / nl; ny *= face_sign[f] / nl; nz *= face_sign[f] / nl;
                            const float fx = (corners[a].x + corners[b].x + corners[ci].x + corners[d].x) * 0.25f;
                            const float fy = (corners[a].y + corners[b].y + corners[ci].y + corners[d].y) * 0.25f;
                            const float fz = (corners[a].z + corners[b].z + corners[ci].z + corners[d].z) * 0.25f;
                            const float vx = cam.x - fx, vy = cam.y - fy, vz = cam.z - fz;
                            if (nx * vx + ny * vy + nz * vz <= 0.0f) continue;   // backface
                        }
                    }

                    FaceDraw fd;
                    fd.fill = fill;
                    fd.edge = edge;
                    fd.pts[0] = ImVec2(screen[a].x, screen[a].y);
                    fd.pts[1] = ImVec2(screen[b].x, screen[b].y);
                    fd.pts[2] = ImVec2(screen[ci].x, screen[ci].y);
                    fd.pts[3] = ImVec2(screen[d].x, screen[d].y);
                    const float dx = fd.pts[0].x - (float)viewport.x * 0.5f;
                    const float dy = fd.pts[0].y - (float)viewport.y * 0.5f;
                    fd.depth = dx * dx + dy * dy;
                    batch.push_back(fd);
                }
            }
        }

        if (mesh) {
            // Far faces first, so a near limb covers a far one instead of being
            // XOR-ed with it. Depth is squared screen distance from the centre,
            // which is monotonic with distance for a perspective projection.
            std::sort(batch.begin(), batch.end(),
                      [](const FaceDraw& l, const FaceDraw& r) { return l.depth > r.depth; });
        }

        for (const FaceDraw& fd : batch) {
            draw->AddConvexPolyFilled(fd.pts, 4, fd.fill);
            if (mesh) draw->AddPolyline(fd.pts, 4, fd.edge, ImDrawFlags_Closed, 1.0f);
        }
    }

    void RenderChams() {
        if (!chams_enabled) return;
        DrawChamsPass(false);
    }

    void RenderMeshChams() {
        if (!mesh_chams) return;
        DrawChamsPass(true);
    }

    void RenderExpandedHitbox() {
        if (!render_expanded_hitbox) return;
        if (!hitbox_expander_enabled) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        ImU32 color = IM_COL32(255, 50, 50, 255);

        const float size = hitbox_expander_value;
        const float hx = size * 0.5f;
        const float hy = size * 0.5f;
        const float hz = size * 0.5f;

        static const int faces[6][4] = {
            {0, 1, 3, 2},
            {4, 6, 7, 5},
            {0, 4, 5, 1},
            {2, 3, 7, 6},
            {0, 2, 6, 4},
            {1, 5, 7, 3},
        };

        for (const cache::EspEntity& entity : entities) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(entity.name)) continue;
            if (!InRenderRange(entity)) continue;
            if (!entity.character_address) continue;

            instance character{ entity.character_address };
            if (!character.is_valid()) continue;

            uintptr_t hrp_prim = 0;
            for (const instance& child : character.get_children()) {
                if (!child.is_valid()) continue;
                if (child.get_name() != "HumanoidRootPart") continue;
                hrp_prim = read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
                break;
            }

            if (!is_valid_address(hrp_prim)) continue;

            struct { float rot[9]; Vec3 pos; } rp{};
            if (!ReadRaw(hrp_prim + Offsets::Primitive::Rotation, &rp, sizeof(rp))) continue;

            float lx[8] = {-hx, -hx, -hx, -hx, hx, hx, hx, hx};
            float ly[8] = {-hy, -hy, hy, hy, -hy, -hy, hy, hy};
            float lz[8] = {-hz, hz, -hz, hz, -hz, hz, -hz, hz};

            Vec3 corners[8];
            for (int c = 0; c < 8; ++c) {
                corners[c].x = rp.rot[0] * lx[c] + rp.rot[1] * ly[c] + rp.rot[2] * lz[c] + rp.pos.x;
                corners[c].y = rp.rot[3] * lx[c] + rp.rot[4] * ly[c] + rp.rot[5] * lz[c] + rp.pos.y;
                corners[c].z = rp.rot[6] * lx[c] + rp.rot[7] * ly[c] + rp.rot[8] * lz[c] + rp.pos.z;
            }

            Vec2 screen[8];
            bool ok[8];
            for (int c = 0; c < 8; ++c)
                ok[c] = WorldToScreen(corners[c], screen[c], view, viewport);

            for (int f = 0; f < 6; ++f) {
                int a = faces[f][0], b = faces[f][1], ci = faces[f][2], d = faces[f][3];
                if (!ok[a] || !ok[b] || !ok[ci] || !ok[d]) continue;
                draw->AddLine(ImVec2(screen[a].x, screen[a].y), ImVec2(screen[b].x, screen[b].y), color, 1.5f);
                draw->AddLine(ImVec2(screen[b].x, screen[b].y), ImVec2(screen[ci].x, screen[ci].y), color, 1.5f);
                draw->AddLine(ImVec2(screen[ci].x, screen[ci].y), ImVec2(screen[d].x, screen[d].y), color, 1.5f);
                draw->AddLine(ImVec2(screen[d].x, screen[d].y), ImVec2(screen[a].x, screen[a].y), color, 1.5f);
            }
        }
    }

    // A tracer is one line, and the entire design question is which end of it the
    // player is on: FEET by default, because the bottom of the box is where a Roblox
    // character actually stands, and a line to the middle of the box reads as pointing
    // at a floating torso. The origin is a screen anchor rather than the local
    // player's projected position for the same reason - the local player has no
    // on-screen position at all in first person, and a crosshair origin would put the
    // whole point of a tracer (it survives when the target leaves the screen) at risk
    // of being in the way of aiming.
    static void RenderTracer(ImDrawList* draw, float x1, float y1, float x2, float y2,
                             const cache::EspEntity& entity, const cache::LocalPlayerData& lp) {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        if (ds.x <= 1.0f || ds.y <= 1.0f) return;

        // Studs, the unit the rest of the ESP culls in, so the two caps can be
        // reasoned about together. This one only hides tracers: the box stays.
        if (esp_tracer_max_dist > 0.0f && lp.valid) {
            const float dx = entity.root_x - lp.x;
            const float dy = entity.root_y - lp.y;
            const float dz = entity.root_z - lp.z;
            if (sqrtf(dx * dx + dy * dy + dz * dz) > esp_tracer_max_dist) return;
        }

        float ox = ds.x * 0.5f, oy = ds.y;
        switch (esp_tracer_origin) {
        case 1:  ox = 0.0f;            break;   // bottom left
        case 2:  ox = ds.x;            break;   // bottom right
        case 3:  oy = ds.y * 0.5f;      break;   // screen centre
        default: break;                         // 0: bottom centre
        }

        const float cx = floorf((x1 + x2) * 0.5f) + 0.5f;
        float ty = y2;                                     // feet
        if      (esp_tracer_attach == 1) ty = floorf((y1 + y2) * 0.5f) + 0.5f;  // centre
        else if (esp_tracer_attach == 2) ty = y1;                                // head

        const ImU32 col = esp_tracer_box_color
                            ? EspTint(entity.user_id, entity.name, entity.is_teammate, esp_tracer_color)
                            : Col4(esp_tracer_color);
        const float w = esp_tracer_width < 0.5f ? 0.5f : esp_tracer_width;

        if (esp_glow > 0.001f)
            draw->AddLine(ImVec2(ox, oy), ImVec2(cx, ty), FadeCol(col, 0.25f * esp_glow),
                          w + EspWidth(1.5f));
        draw->AddLine(ImVec2(ox, oy), ImVec2(cx, ty), col, w);
        // A dot at the anchor: several tracers share a bottom-centre origin, and
        // without it they read as one arrowhead rather than as a fan of targets.
        draw->AddCircleFilled(ImVec2(ox, oy), w + 1.0f, col, 12);
    }

    bool CameraPosition(float& out_x, float& out_y, float& out_z) {
        instance cam = GetCameraInstance();
        if (!cam.is_valid()) return false;
        Vec3 p{};
        if (!ReadVec3(cam.address + Offsets::Camera::Position, p)) return false;
        out_x = p.x; out_y = p.y; out_z = p.z;
        return true;
    }

    bool EntityScreenBox(const cache::EspEntity& entity, float& x1, float& y1, float& x2, float& y2) {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return false;
        Box2D box{};
        ComputeBoxForPrimitives(entity, view, viewport, box);
        if (!box.valid) {
            Vec2 lp{};
            if (!LabelPoint(entity, view, viewport, lp)) return false;
            const float w = 3.0f, h = 5.0f;
            box.min_x = lp.x - w * 0.5f;
            box.max_x = lp.x + w * 0.5f;
            box.min_y = lp.y - h * 0.5f;
            box.max_y = lp.y + h * 0.5f;
        }
        x1 = box.min_x; y1 = box.min_y;
        x2 = box.max_x; y2 = box.max_y;
        return true;
    }

    // ── headdot ─────────────────────────────────────────────────────────────
    // The reference's visual/headdot: a disc on every head. Everything hard about
    // it is already in this file - the projection (client -> OVERLAY space, not raw
    // client pixels), the range gate, the filters - so it is a short pass. The one
    // thing it does not copy from the reference is the distance scaling: this draws
    // on the overlay, which is screen space, and a marker that shrinks with distance
    // is a second meaning for one slider.
    void RenderHeadDot() {
        if (!headdot_enabled) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        const float r = headdot_thickness < 1.0f ? 1.0f : headdot_thickness;
        const ImU32 fill = IM_COL32((int)(headdot_color[0] * 255.0f),
                                    (int)(headdot_color[1] * 255.0f),
                                    (int)(headdot_color[2] * 255.0f),
                                    (int)(headdot_color[3] * 255.0f));
        const ImU32 edge = IM_COL32((int)(headdot_outline_color[0] * 255.0f),
                                    (int)(headdot_outline_color[1] * 255.0f),
                                    (int)(headdot_outline_color[2] * 255.0f),
                                    (int)(headdot_outline_color[3] * 255.0f));

        for (const cache::EspEntity& entity : entities) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(entity.name)) continue;
            if (team_check && entity.is_teammate) continue;
            if (dead_check && entity.IsDead()) continue;
            if (!InRenderRange(entity)) continue;

            const int head_idx = FindEntityPartIndex(entity, "Head");
            if (head_idx < 0) continue;
            const uintptr_t head_prim = entity.primitives[head_idx];
            if (!is_valid_address(head_prim)) continue;

            Vec3 head{};
            if (!ReadVec3(head_prim + Offsets::Primitive::Position, head)) continue;
            Vec2 pt{};
            if (!WorldToScreen(head, pt, view, viewport)) continue;

            const ImVec2 c(pt.x, pt.y);
            if (headdot_fill && headdot_outline)
                draw->AddCircleFilled(c, r + 1.0f, edge, 24);   // outline = a bigger disc under it
            if (headdot_fill)
                draw->AddCircleFilled(c, r, fill, 24);
            else if (headdot_outline)
                draw->AddCircle(c, r, edge, 24, 1.5f);
        }
    }

    // ── off-screen indicators ────────────────────────────────────────────────
    // The reference's visual/oof. The part worth explaining is the direction: a
    // player the game is not showing has no projected point to aim an arrow at, so
    // the direction comes from the CLIP-space vector, with both components negated
    // when the point is behind the camera (w < 0). That negation is the whole trick
    // - without it, the indicator for somebody directly behind you points the wrong
    // way, which is the classic version of this bug. The arrow then sits on a ring
    // around the view's centre, pushed inside the screen edges by a margin: an arrow
    // drawn past the bezel is an arrow nobody sees.
    void RenderOof() {
        if (!oof_enabled) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        const OverlayTransform& t = GetOverlayTransform(viewport);
        const float left   = t.offset_x;
        const float top    = t.offset_y;
        const float right  = t.offset_x + viewport.x * t.scale_x;
        const float bottom = t.offset_y + viewport.y * t.scale_y;
        const float cx = (left + right) * 0.5f;
        const float cy = (top + bottom) * 0.5f;
        const float margin = 30.0f;
        const float radius = oof_radius < 4.0f ? 4.0f : oof_radius;

        const ImU32 col = IM_COL32((int)(oof_color[0] * 255.0f), (int)(oof_color[1] * 255.0f),
                                   (int)(oof_color[2] * 255.0f), (int)(oof_color[3] * 255.0f));
        const ImU32 edge = IM_COL32((int)(oof_outline_color[0] * 255.0f),
                                    (int)(oof_outline_color[1] * 255.0f),
                                    (int)(oof_outline_color[2] * 255.0f),
                                    (int)(oof_outline_color[3] * 255.0f));

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();

        for (const cache::EspEntity& entity : entities) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(entity.name)) continue;
            if (team_check && entity.is_teammate) continue;
            if (dead_check && entity.IsDead()) continue;
            if (!InRenderRange(entity)) continue;
            if (entity.root_x == 0.0f && entity.root_y == 0.0f && entity.root_z == 0.0f) continue;

            // The head when it is readable, the root otherwise: the indicator is
            // about where the player IS, and the head is the point the ESP already
            // trusts out at range.
            Vec3 world = { entity.root_x, entity.root_y, entity.root_z };
            const int head_idx = FindEntityPartIndex(entity, "Head");
            if (head_idx >= 0 && is_valid_address(entity.primitives[head_idx])) {
                Vec3 head{};
                if (ReadVec3(entity.primitives[head_idx] + Offsets::Primitive::Position, head))
                    world = head;
            }

            const float* m = view.data;
            const float clip_w = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
            const float clip_x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
            const float clip_y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
            const float denom = (fabsf(clip_w) < 0.001f) ? 0.001f : clip_w;
            float ndc_x = clip_x / denom;
            float ndc_y = clip_y / denom;
            const bool behind = clip_w < 0.1f;
            if (behind) { ndc_x = -ndc_x; ndc_y = -ndc_y; }

            const float on_x = cx + ndc_x * (right - left) * 0.5f;
            const float on_y = cy - ndc_y * (bottom - top) * 0.5f;
            if (!behind && on_x >= left && on_x <= right && on_y >= top && on_y <= bottom)
                continue;   // the game is showing them: no indicator needed

            // Aim at where the point IS when it projected (a player just off the
            // left edge gets an arrow pointing left, not at the map diagonal); a
            // player behind the camera has no such point, and the flank is the only
            // information there is.
            float dx = behind ? ndc_x * (right - left) * 0.5f : (on_x - cx);
            float dy = behind ? -ndc_y * (bottom - top) * 0.5f : (on_y - cy);
            float len = sqrtf(dx * dx + dy * dy);
            if (!(len > 0.001f)) continue;
            dx /= len;
            dy /= len;

            // THE RING, not a rectangle. The old placement clamped the arrow's
            // centre to the viewport's edges with a margin, so a diagonal player
            // was pulled onto a box corner: the radius changed with the angle and
            // the arrow visibly left the circle the setting describes. The ring is
            // an ellipse (the viewport is not square) whose radii are the radius
            // setting, shrunk when the window is too small to hold it and by the
            // arrow's own half-size so the GLYPH stays inside it, never just the
            // centre. dx/dy is a unit vector, so (cx + dx*rx, cy + dy*ry) is on
            // that ellipse by construction - there is no position left to clamp.
            const float sz = oof_size < 2.0f ? 2.0f : oof_size;
            const float half_glyph = sz * (oof_arrow_style == 0 ? 1.0f : 0.8f);
            float ring_x = (right - left) * 0.5f - margin - half_glyph;
            float ring_y = (bottom - top) * 0.5f - margin - half_glyph;
            if (ring_x > radius) ring_x = radius;
            if (ring_y > radius) ring_y = radius;
            // A legibility floor that can never exceed the setting: at a radius
            // under 10 the floor is half the radius, so "the arrows stay on the
            // ring" is true even at the smallest slider value.
            const float floor_r = radius > 10.0f ? 8.0f : radius * 0.5f;
            if (ring_x < floor_r) ring_x = floor_r;
            if (ring_y < floor_r) ring_y = floor_r;
            const float ex = cx + dx * ring_x;
            const float ey = cy + dy * ring_y;

            const float ang = atan2f(dy, dx);
            const float ca = cosf(ang), sa = sinf(ang);
            const float px = -sa, py = ca;

            if (oof_arrow_style == 1) {
                // chevron: a thin V, the least obtrusive of the three
                const float H = sz * 1.6f;
                const float W = H * 0.76f;
                const ImVec2 pts[3] = {
                    ImVec2(ex + ca * (H * 0.5f), ey + sa * (H * 0.5f)),
                    ImVec2(ex - ca * (H * 0.5f) + px * (W * 0.5f), ey - sa * (H * 0.5f) + py * (W * 0.5f)),
                    ImVec2(ex - ca * (H * 0.5f) - px * (W * 0.5f), ey - sa * (H * 0.5f) - py * (W * 0.5f)),
                };
                draw->AddPolyline(pts, 3, col, ImDrawFlags_Closed, 2.0f);
            } else if (oof_arrow_style == 2) {
                // triangle: the loudest, and the default for that reason
                const float H = sz * 1.6f;
                const float W = H * 0.76f;
                const ImVec2 pts[3] = {
                    ImVec2(ex + ca * (H * 0.5f), ey + sa * (H * 0.5f)),
                    ImVec2(ex - ca * (H * 0.5f) + px * (W * 0.5f), ey - sa * (H * 0.5f) + py * (W * 0.5f)),
                    ImVec2(ex - ca * (H * 0.5f) - px * (W * 0.5f), ey - sa * (H * 0.5f) - py * (W * 0.5f)),
                };
                draw->AddTriangleFilled(pts[0], pts[1], pts[2], col);
                if (oof_outline) draw->AddPolyline(pts, 3, edge, ImDrawFlags_Closed, 1.0f);
            } else {
                // arrow: a 7-point head with a tail (the reference's default shape)
                const float H = sz * 2.0f;
                const float W = H * 0.76f;
                const ImVec2 pts[7] = {
                    ImVec2(ex + ca * (H * 0.5f), ey + sa * (H * 0.5f)),
                    ImVec2(ex - ca * (H * 0.15f) + px * (W * 0.5f), ey - sa * (H * 0.15f) + py * (W * 0.5f)),
                    ImVec2(ex - ca * (H * 0.32f) + px * (W * 0.5f), ey - sa * (H * 0.32f) + py * (W * 0.5f)),
                    ImVec2(ex - ca * (H * 0.5f) + px * (W * 0.28f), ey - sa * (H * 0.5f) + py * (W * 0.28f)),
                    ImVec2(ex - ca * (H * 0.5f) - px * (W * 0.28f), ey - sa * (H * 0.5f) - py * (W * 0.28f)),
                    ImVec2(ex - ca * (H * 0.32f) - px * (W * 0.5f), ey - sa * (H * 0.32f) - py * (W * 0.5f)),
                    ImVec2(ex - ca * (H * 0.15f) - px * (W * 0.5f), ey - sa * (H * 0.15f) - py * (W * 0.5f)),
                };
                draw->AddConvexPolyFilled(pts, 7, col);
                if (oof_outline) draw->AddPolyline(pts, 7, edge, ImDrawFlags_Closed, 1.0f);
            }

            // Labels ride just inside the arrow, pulled back toward the centre and
            // clamped, so a corner arrow's label stays on screen.
            if (oof_names || oof_distance) {
                char text[128] = { 0 };
                float dist = 0.0f;
                if (lp.valid) {
                    const float ax = entity.root_x - lp.x;
                    const float ay = entity.root_y - lp.y;
                    const float az = entity.root_z - lp.z;
                    dist = sqrtf(ax * ax + ay * ay + az * az);
                }
                if (oof_names && entity.name[0] != '\0')
                    snprintf(text, sizeof(text), "%s", entity.name);
                if (oof_distance && dist > 0.0f) {
                    // Build the separator INTO the second snprintf rather than appending
                    // with strncat: strncat is one of the CRT functions MSVC flags as
                    // deprecated (C4996), and a hand-bounded copy of its semantics is
                    // the same code with none of the argument. The minus-one on `used`
                    // and the room check keep the terminator inside the buffer.
                    const size_t used = strlen(text);
                    if (used + 1 < sizeof(text)) {
                        char d[40];
                        snprintf(d, sizeof(d), used > 0 ? " %.0f" : (oof_names ? "%.0f" : "%.0f studs"),
                                 dist);
                        snprintf(text + used, sizeof(text) - used, "%s", d);
                    }
                }
                if (text[0] != '\0') {
                    ImFont* font = GetEspFont();
                    const float font_size = esp_text_size;
                    const ImVec2 ts = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text)
                                           : ImVec2(0, 0);
                    float tx = ex - dx * 6.0f - ts.x * 0.5f;
                    float ty = ey - dy * 6.0f - ts.y * 0.5f;
                    if (tx < left + 2.0f) tx = left + 2.0f;
                    if (tx + ts.x > right - 2.0f) tx = right - 2.0f - ts.x;
                    if (ty < top + 2.0f) ty = top + 2.0f;
                    if (ty + ts.y > bottom - 2.0f) ty = bottom - 2.0f - ts.y;
                    DrawTextWithShadow(draw, font_size, ImVec2(tx, ty), col, text);
                }
            }
        }
    }

    void RenderESP() {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();

        Vec3 cam_pos{};
        bool have_cam = false;
        if (wall_check) {
            instance cam = GetCameraInstance();
            if (cam.is_valid() && ReadVec3(cam.address + Offsets::Camera::Position, cam_pos))
                have_cam = true;
        }

        for (const cache::EspEntity& entity : entities) {
            if (blacklist_hides_esp && PlayerIsBlacklisted(entity.name)) continue;
            if (!InRenderRange(entity)) continue;

            // Wall check: a CLEAR line to the head OR to the feet counts as visible.
            // A single cam->root ray reports "not visible" for a player behind a
            // waist-high wall whose head is in the open -- and the old code tested
            // the feet, which is the worse of the two points for that.
            if (wall_check && have_cam &&
                !(entity.root_x == 0.0f && entity.root_y == 0.0f && entity.root_z == 0.0f)) {
                Vec3 root = { entity.root_x, entity.root_y, entity.root_z };
                bool clear = LOSClear(cam_pos, root, entity.player_address);
                if (!clear) {
                    for (size_t i = 0; i < entity.primitive_count && !clear; ++i) {
                        if (strcmp(entity.part_names[i], "Head") != 0) continue;
                        uintptr_t hp = entity.primitives[i];
                        if (!is_valid_address(hp)) break;
                        Vec3 head{};
                        if (ReadVec3(hp + Offsets::Primitive::Position, head))
                            clear = LOSClear(cam_pos, head, entity.player_address);
                        break;
                    }
                }
                if (!clear) continue;
            }

            Box2D box{};
            ComputeBoxForPrimitives(entity, view, viewport, box);
            // `degenerate` = the projection collapsed (tiny or partly off-screen).
            // The box below is inflated to a legible marker so the label stack,
            // the tracer and the box all still have somewhere to live; the fill
            // and the real stroke are skipped for it.
            bool degenerate = !box.valid;
            if (degenerate) {
                Vec2 lp{};
                if (!LabelPoint(entity, view, viewport, lp)) continue;
                const float w = 3.0f, h = 5.0f;
                box.min_x = lp.x - w * 0.5f;
                box.max_x = lp.x + w * 0.5f;
                box.min_y = lp.y - h * 0.5f;
                box.max_y = lp.y + h * 0.5f;
                box.valid = true;
            }

            float x1 = floorf(box.min_x);
            float y1 = floorf(box.min_y);
            float x2 = floorf(box.max_x);
            float y2 = floorf(box.max_y);

            // Fill first, then the stroke: the outline has to sit on top of its own
            // fill or a translucent box fill eats the corners.
            if (box_esp) {
                ImU32 col = EspTint(entity.user_id, entity.name, entity.is_teammate, box_esp_color);
                const ImVec2 ba(x1, y1), bb(x2, y2);

                if (box_fill && !degenerate) {
                    const ImU32 top = Col4(box_fill_top);
                    const ImU32 bot = Col4(box_fill_bottom);
                    if (box_fill_gradient && box_fill_gradient_rotate) {
                        // The two stops swap along the diagonal as time runs. Cheap and
                        // readable; a real rotating gradient would need a shader, and
                        // this overlay has no GPU pass of its own.
                        const float t = (float)ImGui::GetTime() * 2.0f;
                        const float k = (sinf(t) + 1.0f) * 0.5f;
                        draw->AddRectFilledMultiColor(ba, bb, Col4(Mix4(box_fill_top, box_fill_bottom, k)),
                                                      Col4(Mix4(box_fill_bottom, box_fill_top, k)),
                                                      Col4(Mix4(box_fill_top, box_fill_bottom, k)),
                                                      Col4(Mix4(box_fill_bottom, box_fill_top, k)));
                    } else if (box_fill_gradient) {
                        draw->AddRectFilledMultiColor(ba, bb, top, top, bot, bot);
                    } else {
                        draw->AddRectFilled(ba, bb, top);
                    }
                }

                if (box_esp_type == 1) StrokeCorners(draw, ba, bb, col);
                else                  StrokeRect(draw, ba, bb, col, 0.0f);
            }

            if (esp_tracer)
                RenderTracer(draw, x1, y1, x2, y2, entity, lp);

            if (healthbar)
                RenderHealthBar(draw, x1, y1, x2, y2, entity.health, entity.max_health);
            if (health_text)
                RenderHealthText(draw, x1, y1, x2, y2, entity.health);
            if (name)
                RenderName(draw, x1, y1, x2, y2, entity.name, entity.user_id, entity.is_teammate);

            if (distance && lp.valid) {
                float dx = entity.root_x - lp.x;
                float dy = entity.root_y - lp.y;
                float dz = entity.root_z - lp.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                RenderDistance(draw, x1, y1, x2, y2, dist);
            }
            if (rig_type)
                RenderRigType(draw, x1, y1, x2, y2, entity.is_r15);
            if (tool_esp)
                RenderTool(draw, x1, y1, x2, y2, entity.tool_name, distance && lp.valid);
        }
    }
}
