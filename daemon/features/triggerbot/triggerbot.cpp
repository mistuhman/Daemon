#include "triggerbot.h"
#include "hitsound/hitsound.h"
// -----------------------------------------------------------------------------
//  features/triggerbot/triggerbot.cpp  --  click when a body is on the crosshair
// -----------------------------------------------------------------------------
//  Ported from jatosm/RobloxExternal-base's aim/autoshoot, which is the piece of
//  that repo this tool was missing. What it does, in order, once per pass:
//
//    1. the key (hold, or a latch - the same two modes the aimbot has);
//    2. every entity the ESP knows about, minus the ones the shared checks
//       reject (blacklist, team, dead, range);
//    3. the crosshair test: the body box the ESP is drawing, OR a radius around
//       the aim part, because at range the box is a few pixels and asking a
//       player to keep the crosshair inside it is asking for a different game;
//    4. line of sight, through the same raycast the ESP and the aimbot use;
//    5. timing: the target must STAY on the crosshair for `delay`, and shots are
//       separated by `cooldown` - the reference's two timers, kept because a
//       triggerbot without them is a full-auto macro;
//    6. one click, produced by platform::ClickMouseLeft().
//
//  The crosshair is the centre of the GAME's client rect, not of the desktop.
//  Those differ in windowed mode, which is the bug the ESP's client->desktop
//  transform exists for - so this asks the platform for the rect and compares
//  against ESP-space pixels, rather than mixing the two coordinate systems.
//
//  DID IT LAND? The honest answer is "we cannot see the server's verdict", and that is
//  what the README row says is unverified. But a cheap consequence IS observable: the
//  health of the body we aimed at. So every shot is followed up 400ms later - long
//  enough for the damage to replicate, short enough that regeneration has not started -
//  and the health before/after is logged. A drop proves the click was accepted and the
//  shot connected; an unchanged value means a miss OR a click the game never took, and
//  the line says both rather than choosing. It speaks on the first shot and then only
//  when the verdict changes, so it cannot flood the log at 60 shots a second.
// -----------------------------------------------------------------------------
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "esp/esp.h"
#include "esp/range.h"
#include "raycast/world_ray.h"
#include "platform/platform.h"

namespace features {

    namespace {

        struct State {
            bool had_target = false;
            uint64_t first_seen_ms = 0;
            uint64_t last_shot_ms = 0;
            bool key_was_down = false;
            bool latched = false;
            int bind_seen = -1;
            bool mode_seen = true;
            unsigned shots = 0;

            // The shot read-back (see the header comment): the body to look at, its
            // health when the click went out, and when to look.
            bool pending = false;
            uintptr_t pending_player = 0;
            float pending_health = 0.0f;
            uint64_t pending_at_ms = 0;
            unsigned pending_index = 0;
            char pending_name[64]{};

            // Speaking only on a change needs the last thing said.
            unsigned verdicts = 0;      // shots that could be judged
            unsigned landed = 0;        // ...of which dropped the target's health
            bool reported_any = false;
            bool last_landed = false;
        };
        State s_state;

        bool KeyActive() {
            if (triggerbot_keybind == 0) return true;
            // Rebinding or flipping the mode drops the latch, exactly like the
            // aimbot: a latch that survives a rebind is a phantom "armed" state.
            if (s_state.bind_seen != triggerbot_keybind || s_state.mode_seen != triggerbot_hold_mode) {
                s_state.bind_seen = triggerbot_keybind;
                s_state.mode_seen = triggerbot_hold_mode;
                s_state.latched = false;
            }
            const bool down = platform::IsKeyDown(triggerbot_keybind);
            if (triggerbot_hold_mode) return down;
            if (down && !s_state.key_was_down) s_state.latched = !s_state.latched;
            s_state.key_was_down = down;
            return s_state.latched;
        }

        void CopyName(char* dst, size_t n, const char* src) {
            size_t i = 0;
            for (; src && i + 1 < n && src[i]; ++i) dst[i] = src[i];
            dst[i] = 0;
        }

        // The follow-up: was the shot we fired 400ms ago followed by the target losing
        // health? Called every pass, does nothing until its deadline.
        void ReportPendingShot(uint64_t now) {
            if (!s_state.pending || now - s_state.pending_at_ms < 400) return;
            s_state.pending = false;

            const cache::EspEntity* found = nullptr;
            if (auto snap = cache::GetEspSnapshot()) {
                for (const cache::EspEntity& e : *snap) {
                    if (e.player_address != s_state.pending_player) continue;
                    // The cache only reads health when it found a Humanoid; both 0 means
                    // "could not look", which is not evidence of anything.
                    if (e.max_health <= 0.0f) break;
                    found = &e;
                    break;
                }
            }

            const unsigned index = s_state.pending_index;
            if (!found) {
                if (!s_state.reported_any) {
                    s_state.reported_any = true;
                    LogLine("triggerbot: shot %u at %s - the body left the snapshot before the "
                            "400ms read-back, so there is no verdict on whether it landed",
                            index, s_state.pending_name);
                }
                return;
            }

            const bool landed = found->health < s_state.pending_health - 0.01f;
            ++s_state.verdicts;
            if (landed) ++s_state.landed;

            if (s_state.reported_any && landed == s_state.last_landed) return;
            s_state.reported_any = true;
            s_state.last_landed = landed;

            if (landed)
                features::hitsound::Play();   // the reference's misc/hitsound
            if (landed)
                LogLine("triggerbot: shot %u at %s LANDED - health %.0f -> %.0f after 400ms "
                        "(%u of %u judged shots landed)",
                        index, s_state.pending_name, s_state.pending_health, found->health,
                        s_state.landed, s_state.verdicts);
            else
                LogLine("triggerbot: shot %u at %s: health unchanged (%.0f -> %.0f) after 400ms "
                        "- either a miss, or a click the game did not take (SendInput goes to "
                        "the window with focus). %u of %u judged shots landed so far",
                        index, s_state.pending_name, s_state.pending_health, found->health,
                        s_state.landed, s_state.verdicts);
        }

        bool InTriggerRange(const cache::EspEntity& e, const cache::LocalPlayerData& lp) {
            if (!(triggerbot_max_dist > 0.0f)) return true;   // 0 = infinite, the ESP's rule
            float ax = 0, ay = 0, az = 0;
            if (lp.valid) { ax = lp.x; ay = lp.y; az = lp.z; }
            else if (!CameraPosition(ax, ay, az)) return true;   // nothing to measure from
            const float dx = e.root_x - ax, dy = e.root_y - ay, dz = e.root_z - az;
            return features::DrawRangeAllows(sqrtf(dx * dx + dy * dy + dz * dz), triggerbot_max_dist);
        }

        // The aim point: the requested part, read live (a stale position is a shot
        // at where somebody used to be). Falls back to the cached root.
        bool AimPoint(const cache::EspEntity& e, float& wx, float& wy, float& wz) {
            const char* want = triggerbot_part == 0 ? "Head" : "HumanoidRootPart";
            for (size_t i = 0; i < e.primitive_count; ++i) {
                if (strcmp(e.part_names[i], want) != 0) continue;
                const uintptr_t p = e.primitives[i];
                if (!is_valid_address(p)) break;
                float pos[3] = { 0, 0, 0 };
                if (!read_raw(p + Offsets::Primitive::Position, pos, sizeof(pos))) break;
                if (pos[0] == 0.0f && pos[1] == 0.0f && pos[2] == 0.0f) break;
                wx = pos[0]; wy = pos[1]; wz = pos[2];
                return true;
            }
            if (e.root_x == 0.0f && e.root_y == 0.0f && e.root_z == 0.0f) return false;
            wx = e.root_x; wy = e.root_y; wz = e.root_z;
            return true;
        }

        struct Target {
            const cache::EspEntity* entity = nullptr;
            float world_x = 0, world_y = 0, world_z = 0;
        };

        bool FindTarget(Target& out) {
            auto snap = cache::GetEspSnapshot();
            if (!snap || snap->empty()) return false;
            const cache::LocalPlayerData& lp = cache::GetLocalPlayer();

            int cx = 0, cy = 0, cw = 0, ch = 0;
            if (!platform::GameClientRect(cx, cy, cw, ch) || cw <= 0 || ch <= 0) return false;
            const float cross_x = (float)cx + (float)cw * 0.5f;
            const float cross_y = (float)cy + (float)ch * 0.5f;

            float best_score = 1e30f;
            for (const cache::EspEntity& e : *snap) {
                if (PlayerIsBlacklisted(e.name)) continue;
                if (team_check && e.is_teammate) continue;
                if (dead_check && e.IsDead()) continue;
                if (!InTriggerRange(e, lp)) continue;

                float wx = 0, wy = 0, wz = 0;
                if (!AimPoint(e, wx, wy, wz)) continue;
                float sx = 0, sy = 0;
                if (!features::ProjectToOverlay(wx, wy, wz, sx, sy)) continue;

                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                const bool have_box = EntityScreenBox(e, x1, y1, x2, y2);
                const bool in_box = have_box &&
                                    cross_x >= x1 && cross_x <= x2 && cross_y >= y1 && cross_y <= y2;
                const float dx = sx - cross_x, dy = sy - cross_y;
                const float dist = sqrtf(dx * dx + dy * dy);
                if (!in_box && dist > triggerbot_fov) continue;

                if (triggerbot_visible_check) {
                    float camx = 0, camy = 0, camz = 0;
                    if (CameraPosition(camx, camy, camz)) {
                        const float from[3] = { camx, camy, camz };
                        const float to[3] = { wx, wy, wz };
                        if (worldray::Blocked(from, to, e.player_address)) continue;
                    }
                }

                // Inside the box beats inside the radius: it is the stronger
                // evidence, and among equals the closest to the crosshair wins.
                const float score = (in_box ? 0.0f : 1000.0f) + dist;
                if (score >= best_score) continue;
                best_score = score;
                out.entity = &e;
                out.world_x = wx;
                out.world_y = wy;
                out.world_z = wz;
            }
            return out.entity != nullptr;
        }

    }  // namespace

    void RunTriggerbot() {
        if (!triggerbot_enabled) {
            s_state = State{};
            return;
        }
        // The read-back comes first: it is about the shot fired a moment ago and must
        // not be skipped because the key was released or the menu took focus.
        const uint64_t pass_now = platform::NowMs();
        ReportPendingShot(pass_now);
        // Never fire while a text field in the menu has focus: the key that arms
        // the triggerbot is usually also a letter.
        if (g_menu_text_input) return;
        if (!KeyActive()) {
            s_state.had_target = false;
            return;
        }

        // 8ms poll - the cadence the reference used, and an order of magnitude
        // finer than the timers below (which are in ms).
        static uint64_t s_last_poll = 0;
        const uint64_t now = platform::NowMs();
        if (s_last_poll != 0 && now - s_last_poll < 8) return;
        s_last_poll = now;

        Target target{};
        if (!FindTarget(target)) {
            s_state.had_target = false;
            return;
        }

        if (!s_state.had_target) {
            s_state.had_target = true;
            s_state.first_seen_ms = now;
            return;   // the delay starts now: no shot on the frame of first sight
        }

        const uint64_t on_target_ms = now - s_state.first_seen_ms;
        const uint64_t since_shot_ms =
            s_state.last_shot_ms == 0 ? UINT64_MAX : now - s_state.last_shot_ms;
        const float delay = triggerbot_delay_ms < 0.0f ? 0.0f : triggerbot_delay_ms;
        const float cooldown = triggerbot_cooldown_ms < 60.0f ? 60.0f : triggerbot_cooldown_ms;
        if (on_target_ms < (uint64_t)delay) return;
        if (since_shot_ms < (uint64_t)cooldown) return;

        if (triggerbot_chance < 100.0f) {
            const int roll = rand() % 1000;
            if ((float)roll >= triggerbot_chance * 10.0f) {
                // A missed roll still consumes the cooldown, or "70% chance"
                // becomes "always, one pass later".
                s_state.last_shot_ms = now;
                return;
            }
        }

        platform::ClickMouseLeft();
        s_state.last_shot_ms = now;
        ++s_state.shots;

        // Arm the read-back with the body we just shot at. The name is copied because
        // the snapshot this entity points into may be replaced before the deadline, and
        // the arm is skipped when the health was not readable in the first place: "no
        // verdict" is not the same as "the body left".
        if (target.entity && target.entity->max_health > 0.0f) {
            s_state.pending = true;
            s_state.pending_player = target.entity->player_address;
            s_state.pending_health = target.entity->health;
            s_state.pending_at_ms = now;
            s_state.pending_index = s_state.shots;
            CopyName(s_state.pending_name, sizeof(s_state.pending_name), target.entity->name);
        }
    }

    unsigned TriggerbotShotsFired() { return s_state.shots; }

}  // namespace features
