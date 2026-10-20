#include "env.h"
// -----------------------------------------------------------------------------
//  features/world/env.cpp  --  Lighting and Terrain, saved and restored
// -----------------------------------------------------------------------------
//  Every field here is an ordinary property of the Lighting service (or of
//  Terrain, for grass), which is why the feature is cheap and why it must be
//  polite: the sky is shared with the place's own scripts. Three rules keep it
//  polite and they are the same three the physics trio uses:
//
//   1. THE ORIGINAL IS READ BEFORE THE FIRST WRITE, and only once per service
//      instance. A backup captured after our own write would restore the
//      feature's value forever.
//   2. A FIELD IS RESTORED WHEN ITS SWITCH GOES OFF, not when the tool exits:
//      "the sky did not change back" is a bug the user sees immediately.
//   3. NOTHING IS WRITTEN UNLESS IT CHANGED (settings hash or a 500ms refresh),
//      because the feature loop runs at ~1kHz and the sky does not.
//
//  Fullbright, ambient and night are SCREEN GRADES drawn by RenderLightingGrade,
//  not engine writes - see the block in RunWorldEnv for why the writes were
//  retired (the user's verdict on all three was "doesn't work", and the dirty
//  byte that would make the renderer re-read them is a guess). Night still wins
//  over fullbright, which wins over ambient.
//
//  The clock write is the big one, and it is the only one whose numbers are
//  computed rather than copied: features/world/sky.h does the solar maths and
//  the gradient tables, and it has Linux cases. This file reads the latitude,
//  writes the eight fields, and nothing else.
// -----------------------------------------------------------------------------
#include <cmath>
#include <cstdint>
#include <cstring>

#include "globals.h"
#include "memory.h"
#include "offsets.h"
#include "game.h"
#include "sky.h"
#include "platform/platform.h"
#include "imgui/imgui.h"

namespace features {
namespace worldenv {

    namespace {

        struct LightingBackup {
            uintptr_t address = 0;
            bool  captured = false;
            float clock_time = 14.0f;
            uint32_t source = 0;
            float light_direction[3] = {};
            float light_color[3] = {};
            float sun[3] = {};
            float moon[3] = {};
            float gradient_top[3] = {};
            float gradient_bottom[3] = {};
            float ambient[3] = {};
            float outdoor_ambient[3] = {};
            float color_shift_top[3] = {};
            float color_shift_bottom[3] = {};
            float fog_start = 0.0f;
            float fog_end = 100000.0f;
            float fog_color[3] = {};
            float exposure = 0.0f;
            float brightness = 2.0f;
            uint8_t shadows = 1;
            float latitude = 0.0f;
        };

        struct TerrainBackup {
            uintptr_t address = 0;
            bool  captured = false;
            float grass = 0.0f;
        };

        struct State {
            uintptr_t lighting = 0;
            uintptr_t terrain = 0;
            uint64_t  resolved_ms = 0;
            LightingBackup lb{};
            TerrainBackup  tb{};
            // Which composed write the client currently holds, so the restore can
            // undo exactly that and nothing else.
            bool    applied_clock = false;
            bool    applied_ambient = false;   // ambient + brightness + shifts
            bool    applied_fog = false;
            bool    applied_exposure = false;
            bool    applied_shadows = false;
            bool    applied_grass = false;
            // What was applied on the previous pass, so turning ONE switch off
            // restores exactly its fields instead of waiting for all of them.
            bool    prev_clock = false;
            bool    prev_ambient = false;
            bool    prev_fog = false;
            bool    prev_exposure = false;
            bool    prev_shadows = false;
            bool    prev_grass = false;
            uint64_t cached_hash = 0;
            uint64_t last_apply_ms = 0;
            bool     said_no_service = false;
            bool     said_latitude = false;
        };
        State s_env;

        // FNV-1a over the settings that decide a write. Cheap, and it makes the
        // "settings changed" case immediate instead of up to a refresh late.
        void HashByte(uint64_t& h, uint8_t b) {
            h ^= b;
            h *= 1099511628211ull;
        }
        template <typename T>
        void Hash(T v, uint64_t& h) {
            uint8_t bytes[sizeof(T)];
            std::memcpy(bytes, &v, sizeof(T));
            for (size_t i = 0; i < sizeof(T); ++i) HashByte(h, bytes[i]);
        }

        uint64_t SettingsHash() {
            uint64_t h = 1469598103934665603ull;
            Hash((uint8_t)fullbright_enabled, h);
            Hash(fullbright_value, h);
            Hash((uint8_t)world_ambient_enabled, h);
            for (float c : world_ambient_color) Hash(c, h);
            Hash((uint8_t)world_fog_enabled, h);
            Hash(world_fog_start, h);
            Hash(world_fog_end, h);
            for (float c : world_fog_color) Hash(c, h);
            Hash((uint8_t)world_exposure_enabled, h);
            Hash(world_exposure_value, h);
            Hash((uint8_t)noshadow_enabled, h);
            Hash((uint8_t)nograss_enabled, h);
            Hash((uint8_t)night_enabled, h);
            Hash(night_level, h);
            Hash((uint8_t)world_time_enabled, h);
            Hash(world_time_value, h);
            return h;
        }

        bool AnyOn() {
            return fullbright_enabled || world_ambient_enabled || world_fog_enabled ||
                   world_exposure_enabled || noshadow_enabled || nograss_enabled ||
                   night_enabled || world_time_enabled;
        }

        bool ReadVec3(uintptr_t addr, float out[3]) {
            return read_raw(addr, out, sizeof(float) * 3);
        }
        void WriteVec3(uintptr_t addr, const float c[3]) {
            write_raw(addr, c, sizeof(float) * 3);
        }
        void Copy3(float dst[3], const sky::Vec3& v) {
            dst[0] = v.x; dst[1] = v.y; dst[2] = v.z;
        }

        void Resolve(bool force) {
            const uint64_t now = platform::NowMs();
            if (!force && s_env.resolved_ms != 0 && now - s_env.resolved_ms < 2000) return;
            s_env.resolved_ms = now;
            if (!g_base_address) return;

            instance dm = game::ReadDatamodel(g_base_address);
            if (!dm.is_valid()) return;

            if (s_env.lighting == 0 || !is_valid_address(s_env.lighting)) {
                // The old service is gone (a place change). Anything we wrote to
                // it went with it; the backup must not leak into the new one.
                s_env.lighting = 0;
                s_env.lb = LightingBackup{};
                instance l = dm.read_service("Lighting");
                s_env.lighting = l.address;
            }
            if (s_env.terrain == 0 || !is_valid_address(s_env.terrain)) {
                instance t = dm.read_service("Terrain");
                s_env.terrain = t.address;
                s_env.tb = TerrainBackup{};
            }
        }

        void CaptureLighting() {
            if (s_env.lb.captured || !is_valid_address(s_env.lighting)) return;
            const uintptr_t a = s_env.lighting;
            LightingBackup& b = s_env.lb;
            b.address = a;
            b.clock_time = read<float>(a + Offsets::Lighting::ClockTime);
            b.source = read<uint32_t>(a + Offsets::Lighting::Source);
            ReadVec3(a + Offsets::Lighting::LightDirection, b.light_direction);
            ReadVec3(a + Offsets::Lighting::LightColor, b.light_color);
            ReadVec3(a + Offsets::Lighting::SunPosition, b.sun);
            ReadVec3(a + Offsets::Lighting::MoonPosition, b.moon);
            ReadVec3(a + Offsets::Lighting::GradientTop, b.gradient_top);
            ReadVec3(a + Offsets::Lighting::GradientBottom, b.gradient_bottom);
            ReadVec3(a + Offsets::Lighting::Ambient, b.ambient);
            ReadVec3(a + Offsets::Lighting::OutdoorAmbient, b.outdoor_ambient);
            ReadVec3(a + Offsets::Lighting::ColorShift_Top, b.color_shift_top);
            ReadVec3(a + Offsets::Lighting::ColorShift_Bottom, b.color_shift_bottom);
            b.fog_start = read<float>(a + Offsets::Lighting::FogStart);
            b.fog_end = read<float>(a + Offsets::Lighting::FogEnd);
            ReadVec3(a + Offsets::Lighting::FogColor, b.fog_color);
            b.exposure = read<float>(a + Offsets::Lighting::ExposureCompensation);
            b.brightness = read<float>(a + Offsets::Lighting::Brightness);
            b.shadows = read<uint8_t>(a + Offsets::Lighting::GlobalShadows);
            b.latitude = read<float>(a + Offsets::Lighting::GeographicLatitude);
            // A latitude that did not read is 0, not an error: the equator is a
            // legal place and the alternative is refusing the whole clock.
            if (!(b.latitude >= -90.0f) || !(b.latitude <= 90.0f)) b.latitude = 0.0f;
            b.captured = true;
            LogLine("world: captured the lighting (ambient %.2f/%.2f/%.2f, fog %.0f..%.0f, "
                    "brightness %.2f, shadows %s, latitude %.1f)",
                    (double)b.ambient[0], (double)b.ambient[1], (double)b.ambient[2],
                    (double)b.fog_start, (double)b.fog_end, (double)b.brightness,
                    b.shadows ? "on" : "off", (double)b.latitude);
        }

        void CaptureTerrain() {
            if (s_env.tb.captured || !is_valid_address(s_env.terrain)) return;
            TerrainBackup& t = s_env.tb;
            t.address = s_env.terrain;
            t.grass = read<float>(t.address + Offsets::Terrain::GrassLength);
            t.captured = true;
        }

        // Lighting properties are cached by the renderer; flipping the validity
        // byte is what makes it re-read them. The skybox changer does the same
        // for its own fields.
        void MarkLightingDirty() {
            const uintptr_t ve = read<uintptr_t>(g_base_address + Offsets::VisualEngine::Pointer);
            if (!is_valid_address(ve)) return;
            const uintptr_t rv = read<uintptr_t>(ve + Offsets::VisualEngine::RenderView);
            if (!is_valid_address(rv)) return;
            if (Offsets::RenderView::LightingValid)
                if (Offsets::RenderView::LightingValid) {
                    write<uint8_t>(rv + Offsets::RenderView::LightingValid, 0);
                }
        }

        void RestoreClock() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            if (Offsets::Lighting::ClockTime) {
                write<float>(b.address + Offsets::Lighting::ClockTime, b.clock_time);
            }
            if (Offsets::Lighting::Source) {
                write<uint32_t>(b.address + Offsets::Lighting::Source, b.source);
            }
            if (Offsets::Lighting::LightDirection) {
                WriteVec3(b.address + Offsets::Lighting::LightDirection, b.light_direction);
            }
            if (Offsets::Lighting::LightColor) {
                WriteVec3(b.address + Offsets::Lighting::LightColor, b.light_color);
            }
            if (Offsets::Lighting::SunPosition) {
                WriteVec3(b.address + Offsets::Lighting::SunPosition, b.sun);
            }
            if (Offsets::Lighting::MoonPosition) {
                WriteVec3(b.address + Offsets::Lighting::MoonPosition, b.moon);
            }
            if (Offsets::Lighting::GradientTop) {
                WriteVec3(b.address + Offsets::Lighting::GradientTop, b.gradient_top);
            }
            if (Offsets::Lighting::GradientBottom) {
                WriteVec3(b.address + Offsets::Lighting::GradientBottom, b.gradient_bottom);
            }
            MarkLightingDirty();
        }

        void RestoreAmbient() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            if (Offsets::Lighting::Ambient) {
                WriteVec3(b.address + Offsets::Lighting::Ambient, b.ambient);
            }
            if (Offsets::Lighting::OutdoorAmbient) {
                WriteVec3(b.address + Offsets::Lighting::OutdoorAmbient, b.outdoor_ambient);
            }
            if (Offsets::Lighting::ColorShift_Top) {
                WriteVec3(b.address + Offsets::Lighting::ColorShift_Top, b.color_shift_top);
            }
            if (Offsets::Lighting::ColorShift_Bottom) {
                WriteVec3(b.address + Offsets::Lighting::ColorShift_Bottom, b.color_shift_bottom);
            }
            if (Offsets::Lighting::GradientTop) {
                WriteVec3(b.address + Offsets::Lighting::GradientTop, b.gradient_top);
            }
            if (Offsets::Lighting::GradientBottom) {
                WriteVec3(b.address + Offsets::Lighting::GradientBottom, b.gradient_bottom);
            }
            if (Offsets::Lighting::Brightness) {
                write<float>(b.address + Offsets::Lighting::Brightness, b.brightness);
            }
            MarkLightingDirty();
        }

        void RestoreFog() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            if (Offsets::Lighting::FogStart) {
                write<float>(b.address + Offsets::Lighting::FogStart, b.fog_start);
            }
            if (Offsets::Lighting::FogEnd) {
                write<float>(b.address + Offsets::Lighting::FogEnd, b.fog_end);
            }
            if (Offsets::Lighting::FogColor) {
                WriteVec3(b.address + Offsets::Lighting::FogColor, b.fog_color);
            }
            MarkLightingDirty();
        }

        void RestoreExposure() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            if (Offsets::Lighting::ExposureCompensation) {
                write<float>(b.address + Offsets::Lighting::ExposureCompensation, b.exposure);
            }
        }

        void RestoreShadows() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            if (Offsets::Lighting::GlobalShadows) {
                write<uint8_t>(b.address + Offsets::Lighting::GlobalShadows, b.shadows);
            }
        }

        void RestoreGrass() {
            const TerrainBackup& t = s_env.tb;
            if (!t.captured || !is_valid_address(t.address)) return;
            if (Offsets::Terrain::GrassLength) {
                write<float>(t.address + Offsets::Terrain::GrassLength, t.grass);
            }
        }

        void RestoreEverything() {
            RestoreClock();
            RestoreAmbient();
            RestoreFog();
            RestoreExposure();
            RestoreShadows();
            RestoreGrass();
            s_env.applied_clock = s_env.applied_ambient = s_env.applied_fog = false;
            s_env.applied_exposure = s_env.applied_shadows = s_env.applied_grass = false;
            s_env.lb = LightingBackup{};
            s_env.tb = TerrainBackup{};
        }

        void ApplyClock() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            const sky::Clock c = sky::ClockAt(world_time_value, b.latitude);
            if (!c.valid) {
                if (!s_env.said_latitude) {
                    s_env.said_latitude = true;
                    LogLine("world: clock %.2f is not an hour - nothing written", (double)world_time_value);
                }
                return;
            }
            const uintptr_t a = b.address;
            float top[3], bottom[3], lc[3], sun[3], moon[3], dir[3];
            Copy3(top, c.gradient_top);
            Copy3(bottom, c.gradient_bottom);
            Copy3(lc, c.light_color);
            Copy3(sun, c.sun);
            Copy3(moon, c.moon);
            Copy3(dir, c.light_direction);
            if (Offsets::Lighting::ClockTime) {
                write<float>(a + Offsets::Lighting::ClockTime, c.clock_time);
            }
            if (Offsets::Lighting::GradientTop) {
                WriteVec3(a + Offsets::Lighting::GradientTop, top);
            }
            if (Offsets::Lighting::GradientBottom) {
                WriteVec3(a + Offsets::Lighting::GradientBottom, bottom);
            }
            if (Offsets::Lighting::LightColor) {
                WriteVec3(a + Offsets::Lighting::LightColor, lc);
            }
            if (Offsets::Lighting::SunPosition) {
                WriteVec3(a + Offsets::Lighting::SunPosition, sun);
            }
            if (Offsets::Lighting::MoonPosition) {
                WriteVec3(a + Offsets::Lighting::MoonPosition, moon);
            }
            if (Offsets::Lighting::LightDirection) {
                WriteVec3(a + Offsets::Lighting::LightDirection, dir);
            }
            if (Offsets::Lighting::Source) {
                write<uint32_t>(a + Offsets::Lighting::Source, (uint32_t)c.source);
            }
            s_env.applied_clock = true;
            if (!s_env.said_latitude) {
                s_env.said_latitude = true;
                LogLine("world: clock set to %.2f h (sun %.2f, source %s, latitude %.1f)",
                        (double)c.clock_time, (double)c.sun.y,
                        c.source == 0 ? "sun" : "moon", (double)b.latitude);
            }
        }

        void ApplyFrame() {
            const LightingBackup& b = s_env.lb;
            if (!b.captured || !is_valid_address(b.address)) return;
            const uintptr_t a = b.address;

            // NIGHT / FULLBRIGHT / AMBIENT ARE A SCREEN GRADE NOW.
            //
            // The engine-side version wrote Ambient / OutdoorAmbient / Brightness.
            // The user's verdict on all three was "doesn't work", and the likely
            // reason is the renderer's dirty byte (RenderView::LightingValid is a
            // GUESS - no dump publishes it), so the writes land but the renderer
            // never re-reads them. A grade cannot be gated by an unknown offset:
            // it is drawn by the overlay, over the game, in the same frame the
            // switch flips. It is also exactly reversible, because there is
            // nothing stored in the client to restore.
            //
            //   night      darkens, blue-shifted
            //   fullbright lifts the blacks - reads as gamma, not as a white sheet
            //   ambient    tints
            //
            // Priority is night > fullbright > ambient, the same order the old
            // composed write used.
            // (The three switches are read by RenderLightingGrade, which the render
            // loop calls; ApplyFrame only owns the fields still written engine-side.)

            if (world_fog_enabled) {
                float start = world_fog_start;
                float end = world_fog_end;
                if (!(start >= 0.0f)) start = 0.0f;
                if (!(end > start)) end = start + 1.0f;
                if (Offsets::Lighting::FogStart) {
                    write<float>(a + Offsets::Lighting::FogStart, start);
                }
                if (Offsets::Lighting::FogEnd) {
                    write<float>(a + Offsets::Lighting::FogEnd, end);
                }
                float c[3] = { world_fog_color[0], world_fog_color[1], world_fog_color[2] };
                if (Offsets::Lighting::FogColor) {
                    WriteVec3(a + Offsets::Lighting::FogColor, c);
                }
                s_env.applied_fog = true;
                MarkLightingDirty();
            }
            if (world_exposure_enabled) {
                float e = world_exposure_value;
                if (!(e >= -3.0f)) e = -3.0f;
                if (e > 3.0f) e = 3.0f;
                if (Offsets::Lighting::ExposureCompensation) {
                    write<float>(a + Offsets::Lighting::ExposureCompensation, e);
                }
                s_env.applied_exposure = true;
            }
            if (noshadow_enabled) {
                if (Offsets::Lighting::GlobalShadows) {
                    write<uint8_t>(a + Offsets::Lighting::GlobalShadows, 0);
                }
                s_env.applied_shadows = true;
            }
            if (nograss_enabled && is_valid_address(s_env.terrain)) {
                if (Offsets::Terrain::GrassLength) {
                    write<float>(s_env.terrain + Offsets::Terrain::GrassLength, 0.0f);
                }
                s_env.applied_grass = true;
            }
            if (world_time_enabled) ApplyClock();
        }

    }  // namespace

    void ReleaseWorldEnv() {
        RestoreEverything();
        s_env = State{};
    }

    // The screen grade for fullbright / ambient / night. Called from the render
    // loop BEFORE the ESP, so the ESP is drawn on top of it rather than tinted by
    // it; the menu is a window and is painted after the background list anyway.
    void RenderLightingGrade() {
        if (!night_enabled && !fullbright_enabled && !world_ambient_enabled) return;

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 sz = io.DisplaySize;
        if (sz.x < 4.0f || sz.y < 4.0f) return;
        ImDrawList* d = ImGui::GetBackgroundDrawList();
        if (!d) return;

        int a = 0, r = 0, g = 0, b = 0;
        if (night_enabled) {
            float level = night_level;
            if (!(level >= 0.0f)) level = 0.0f;
            if (level > 1.0f) level = 1.0f;
            // Dark and cold: the same 0.09/0.09/0.12 the ambient write used,
            // expressed as an overlay.
            r = 6; g = 9; b = 26;
            a = (int)(level * 190.0f);
        } else if (fullbright_enabled) {
            float bright = fullbright_value;
            if (!(bright >= 1.0f)) bright = 1.0f;
            if (bright > 5.0f) bright = 5.0f;
            r = g = b = 255;
            a = (int)(14.0f + (bright - 1.0f) * 26.0f);   // 14 .. 118
        } else {
            float c[3] = { world_ambient_color[0], world_ambient_color[1], world_ambient_color[2] };
            for (int i = 0; i < 3; ++i) {
                if (!(c[i] >= 0.0f)) c[i] = 0.0f;
                if (c[i] > 1.0f) c[i] = 1.0f;
            }
            r = (int)(c[0] * 255.0f);
            g = (int)(c[1] * 255.0f);
            b = (int)(c[2] * 255.0f);
            a = (int)(world_ambient_color[3] > 0.0f ? world_ambient_color[3] * 255.0f : 90.0f);
        }
        if (a <= 0) return;
        d->AddRectFilled(ImVec2(0.0f, 0.0f), sz, IM_COL32(r, g, b, a));
    }

    void RunWorldEnv() {
        if (!g_base_address) {
            if (AnyOn() || s_env.lighting) ReleaseWorldEnv();
            return;
        }

        const bool any = AnyOn();
        if (!any) {
            if (s_env.lighting || s_env.terrain) ReleaseWorldEnv();
            return;
        }

        Resolve(false);
        if (!is_valid_address(s_env.lighting)) {
            if (!s_env.said_no_service) {
                s_env.said_no_service = true;
                LogLine("world: the Lighting service did not resolve - nothing applied "
                        "(the skybox changer is the same service)");
            }
            return;
        }
        s_env.said_no_service = false;
        CaptureLighting();
        if (nograss_enabled && is_valid_address(s_env.terrain)) CaptureTerrain();

        // A switch that went off since the last pass gets its fields back NOW,
        // not when every switch is off: "the fog did not come back" is visible.
        const bool amb_mode = night_enabled || fullbright_enabled || world_ambient_enabled;
        if (s_env.prev_ambient && !amb_mode) RestoreAmbient();
        if (s_env.prev_clock && !world_time_enabled) RestoreClock();
        if (s_env.prev_fog && !world_fog_enabled) RestoreFog();
        if (s_env.prev_exposure && !world_exposure_enabled) RestoreExposure();
        if (s_env.prev_shadows && !noshadow_enabled) RestoreShadows();
        if (s_env.prev_grass && !nograss_enabled) RestoreGrass();

        const uint64_t now = platform::NowMs();
        const uint64_t h = SettingsHash();
        const bool changed = h != s_env.cached_hash;
        if (!changed && s_env.last_apply_ms != 0 && now - s_env.last_apply_ms < 500) return;
        s_env.cached_hash = h;
        s_env.last_apply_ms = now;

        ApplyFrame();
        s_env.prev_ambient = amb_mode;
        s_env.prev_clock = world_time_enabled;
        s_env.prev_fog = world_fog_enabled;
        s_env.prev_exposure = world_exposure_enabled;
        s_env.prev_shadows = noshadow_enabled;
        s_env.prev_grass = nograss_enabled;
    }

}  // namespace worldenv
}  // namespace features
