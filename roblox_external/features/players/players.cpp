#include <cmath>
#include <cstdio>
#include <cstring>

#include "players.h"
#include "features/movement/mover.h"

#include "globals.h"
#include "memory.h"
#include "game.h"
#include "cache.h"
#include "offsets.h"

namespace features {
namespace {

struct Pose
{
    bool  ok = false;
    float pos[3] = {};
    float rot[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
    char  name[40] = "";
};

bool ReadTargetPose(uint32_t user_id, Pose* out)
{
    const cache::EspSnapshot ents = cache::GetEspSnapshot();
    if (!ents || ents->empty()) return false;

    const cache::EspEntity* hit = nullptr;
    for (const cache::EspEntity& e : *ents) {
        if (e.user_id == user_id) { hit = &e; break; }
    }
    if (!hit) return false;

    const char* nm = hit->display_name[0] ? hit->display_name : hit->name;
    // %.39s makes the 40-byte destination provably sufficient: snprintf was
    // already truncating safely, this just states the bound the warning wants.
    snprintf(out->name, sizeof(out->name), "%.39s", nm && nm[0] ? nm : "unknown");

    int root = -1;
    for (size_t i = 0; i < hit->primitive_count; ++i) {
        if (strcmp(hit->part_names[i], "HumanoidRootPart") == 0) { root = (int)i; break; }
    }

    if (root >= 0 && is_valid_address(hit->primitives[root])) {
        const uintptr_t prim = hit->primitives[root];
        const bool rp = read_raw(prim + Offsets::Primitive::Position, out->pos, sizeof(out->pos));
        const bool rr = read_raw(prim + Offsets::Primitive::Rotation, out->rot, sizeof(out->rot));
        if (rp && rr) { out->ok = true; return true; }
    }

    if (hit->root_x != 0.0f || hit->root_y != 0.0f || hit->root_z != 0.0f) {
        out->pos[0] = hit->root_x;
        out->pos[1] = hit->root_y;
        out->pos[2] = hit->root_z;
        out->ok = true;
        return true;
    }
    return false;
}

instance   s_cam{};
uint64_t      s_cam_at = 0;

uintptr_t  s_was_subject  = 0;
uintptr_t  s_subject_cam  = 0;
uintptr_t  s_offset_hum   = 0;
float      s_offset_written[3] = { 0.0f, 0.0f, 0.0f };
float      s_offset_smooth[3]  = { 0.0f, 0.0f, 0.0f };
uintptr_t  s_hum_cached = 0;
uint64_t      s_hum_at = 0;
uintptr_t  s_hum_char = 0;
int        s_miss = 0;

instance CameraInstance()
{
    const uint64_t now = platform::NowMs();
    if (s_cam.is_valid() && (now - s_cam_at) < 1000) return s_cam;

    instance dm = game::ReadDatamodel(g_base_address);
    if (!dm.is_valid()) return instance{};
    instance ws = dm.read_service("Workspace");
    if (!ws.is_valid()) return instance{};
    s_cam = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
    s_cam_at = now;
    return s_cam;
}

uintptr_t FindHumanoid(uintptr_t character_address)
{
    if (!is_valid_address(character_address)) return 0;
    instance ch{};
    ch.address = character_address;
    for (const instance& c : ch.get_children()) {
        if (!c.is_valid()) continue;
        if (c.get_class_name() == "Humanoid") return c.address;
    }
    return 0;
}

bool FindTarget(uint32_t user_id, uintptr_t* character, char* name, size_t name_sz)
{
    const cache::EspSnapshot ents = cache::GetEspSnapshot();
    if (!ents || ents->empty()) return false;
    for (const cache::EspEntity& e : *ents) {
        if (e.user_id != user_id) continue;
        if (character) *character = e.character_address;
        if (name && name_sz) {
            const char* nm = e.display_name[0] ? e.display_name : e.name;
            // Dynamic precision = room left for NUL: the copy is now provably
            // bounded by name_sz (same truncation snprintf already did, but the
            // runtime size left -Wformat-truncation unable to see the bound).
            snprintf(name, name_sz, "%.*s", (int)(name_sz - 1), nm && nm[0] ? nm : "them");
        }
        return true;
    }
    return false;
}

void AttachSubject(const instance& cam, uintptr_t humanoid)
{
    if (s_subject_cam == 0)
        s_was_subject = read<uintptr_t>(cam.address + Offsets::Camera::CameraSubject);
    if (Offsets::Camera::CameraSubject) {
        write<uintptr_t>(cam.address + Offsets::Camera::CameraSubject, humanoid);
    }
    s_subject_cam = cam.address;
}

void ApplyOffset(uintptr_t humanoid)
{
    const float want[3] = { 0.0f,
                            spectate_height   > 0.0f ? spectate_height   : 0.0f,
                            spectate_distance > 0.0f ? spectate_distance : 0.0f };
    // Ease the offset so a slider twitch or a respawn does not yank the camera.
    s_offset_smooth[0] += (want[0] - s_offset_smooth[0]) * 0.14f;
    s_offset_smooth[1] += (want[1] - s_offset_smooth[1]) * 0.14f;
    s_offset_smooth[2] += (want[2] - s_offset_smooth[2]) * 0.14f;
    const float* off = s_offset_smooth;
    if (s_offset_hum == humanoid &&
        fabsf(off[0] - s_offset_written[0]) < 0.02f &&
        fabsf(off[1] - s_offset_written[1]) < 0.02f &&
        fabsf(off[2] - s_offset_written[2]) < 0.02f)
        return;
    if (Offsets::Humanoid::CameraOffset) {
        write_raw(humanoid + Offsets::Humanoid::CameraOffset, off, 12);
    }
    s_offset_hum = humanoid;
    s_offset_written[0] = off[0]; s_offset_written[1] = off[1]; s_offset_written[2] = off[2];
}

void DetachSubject()
{
    if (s_offset_hum && (s_offset_written[0] || s_offset_written[1] || s_offset_written[2])) {
        const float zero[3] = { 0.0f, 0.0f, 0.0f };
        if (Offsets::Humanoid::CameraOffset) {
            write_raw(s_offset_hum + Offsets::Humanoid::CameraOffset, zero, sizeof(zero));
        }
    }
    s_offset_hum = 0;
    s_offset_written[0] = s_offset_written[1] = s_offset_written[2] = 0.0f;
    s_offset_smooth[0] = s_offset_smooth[1] = s_offset_smooth[2] = 0.0f;
    s_hum_cached = 0;
    s_hum_char = 0;
    s_miss = 0;

    if (s_subject_cam == 0) return;
    s_subject_cam = 0;
    if (!g_base_address) { s_was_subject = 0; return; }

    const instance cam = CameraInstance();
    if (!cam.is_valid()) { s_was_subject = 0; return; }
    const uintptr_t mine = cache::GetLocalPlayer().humanoid_address;
    const uintptr_t back = is_valid_address(mine) ? mine : s_was_subject;
    if (Offsets::Camera::CameraSubject) {
        if (back) write<uintptr_t>(cam.address + Offsets::Camera::CameraSubject, back);
    }
    s_was_subject = 0;
}

} // namespace

void StopSpectate(const char* reason)
{
    DetachSubject();
    if (!spectate_enabled && !reason) return;
    spectate_enabled = false;
    spectate_user_id = 0;
    if (reason) {
        snprintf(players_status_msg, sizeof(players_status_msg), "Stopped watching - %s", reason);
        LogLine("spectate stopped: %s", reason);
    }
}

void StartSpectate(uint32_t user_id)
{
    if (!user_id) {
        snprintf(players_status_msg, sizeof(players_status_msg), "Pick a player first");
        return;
    }
    uintptr_t ch = 0;
    char nm[40] = "";
    if (!FindTarget(user_id, &ch, nm, sizeof(nm))) {
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "That player is not in the cache yet - try again in a second");
        return;
    }
    if (!FindHumanoid(ch)) {
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "%s has no Humanoid to follow - they may be respawning", nm);
        return;
    }
    if (!CameraInstance().is_valid()) {
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "No CurrentCamera to re-point - the game is still loading");
        return;
    }
    spectate_user_id = user_id;
    spectate_enabled = true;
    s_hum_cached = 0;
    s_miss = 0;
    snprintf(players_status_msg, sizeof(players_status_msg),
             "Watching %s - the camera is following them now", nm);
    LogLine("spectating %s", nm);
}

// -----------------------------------------------------------------------------
//  The teleport hold
// -----------------------------------------------------------------------------
//  THE SURVIVAL TEST SETTLED THIS, and it overturned my first change.
//
//  Writing +25 studs and sampling for 500ms gave, every single run:
//
//      t+  0ms  A.y 28.10 (off  +0.00)  vel.y 0.00     <- the write landed
//      t+ 50ms  A.y  3.09 (off -25.00)  vel.y 0.07     <- fully reverted
//      t+100ms  A.y  3.10 (off -25.00)  vel.y 0.00     <- and stayed
//
//  Read the VELOCITY column. If the character had fallen 25 studs it would have
//  taken ~505ms and arrived at ~99 studs/s. It reverted inside 50ms at v=0.07.
//  Nothing fell. The position was RECOMPUTED, not integrated - and it came back
//  to 3.10 exactly, the value it had before the write.
//
//  That is the solver rebuilding the root part's position from its assembly
//  every step, which is what an UNANCHORED part means. The write is not being
//  raced and it is not being rejected by the server (a server correction takes
//  a round trip - 100ms at best, and shows a velocity). It is simply being
//  overwritten by the local physics step, immediately, forever. No write rate,
//  ordering, or field combination beats that, which is why nothing I tried in
//  the last three attempts worked.
//
//  Anchoring the primitive takes the part OUT of the solver, so our position is
//  the one that stands. The original code did exactly this and I removed it in
//  my first change, having blamed it for the rubber-banding on reasoning rather
//  than measurement. That was the regression. It is back, with the two problems
//  that made it look guilty actually fixed:
//
//    * it is TRANSIENT - anchored only for the settle window, never left on
//      (an anchored character cannot walk);
//    * it is OWNED by mover, so noclip - which writes the same Flags byte - can
//      no longer clobber the saved value or be clobbered by it. That
//      interleaving is what left characters stuck or non-colliding, and it was
//      the real complaint behind "anchoring is bad", not the anchor itself.
//
//  The hold is paced (position IS an engine output; re-writing it 600x/sec is
//  pointless) and short: long enough for the client to settle and replicate the
//  new position, then the anchor comes off and normal physics resume.
// -----------------------------------------------------------------------------
namespace {
struct RootHold {
    bool  on = false;
    uint64_t until = 0;
    uint64_t start = 0;
    bool  anchored = false;
    float dest[3] = {};
    float from[3] = {};
};
RootHold s_hold;

// WHY THIS IS ANCHORED NOW. The movement layer's own notes settle it: an
// unanchored root is owned by the solver, which recomputes its position from the
// assembly every step - so a position write is a race, and the winner is
// whoever writes 66 times per step. Racing the solver was the whole design here
// (400ms of hammering), and the user still reported "click tp doesn't work":
// winning the race for 400ms and then letting go is a jump the client never
// adopts, so it re-settles at the old spot. mover::Anchor exists for exactly
// this and says so: "Anchoring hands ownership to us for the duration - that is
// what made teleports work in the first place." So the hold anchors the root
// for its first phase, writes the destination while the solver is out of the
// way, then unanchors and keeps writing into a now-correct assembly.
constexpr uint64_t kAnchorMs = 250;    // solver held off while the position lands
constexpr uint64_t kHoldMs   = 700;    // total hold, including the settle below
constexpr uint64_t kSettleMs = 220;    // unanchored writes after the anchor lifts

// A cache hiccup lasts 16-32ms; the hold must ride those out (see
// TickTeleportHold). Only a SUSTAINED absence ends it early.
constexpr uint64_t kInvalidEndsMs = 250;
uint64_t s_invalid_since = 0;

bool PushRoot(const float dest[3])
{
    // Routed through the hold thread for the same reason flight is: the write
    // rate the feature loop achieves swings ~200x with system load, and at the
    // low end the engine wins the restore race. That is the reported "you need
    // to spam the waypoint before it finally works" - each click was a coin
    // flip on whether we out-wrote the engine for long enough.
    // The hold writes position + zeroed velocities only; rotation stays the
    // engine's (see WriteHoldTailTo).
    return mover::HoldPosition(mover::Teleport, dest);
}

void EndHold()
{
    if (!s_hold.on) return;
    // The anchor MUST come off before the claim is released: mover::Release
    // restores every saved value, but an anchor left by a caller that stopped
    // ticking would outlive the tool's ownership of the body.
    if (s_hold.anchored) { mover::Anchor(mover::Teleport, false); s_hold.anchored = false; }
    mover::ReleaseHold(mover::Teleport);
    mover::PlatformStand(mover::Teleport, false);
    mover::Release(mover::Teleport);
    // Say where the character ACTUALLY ended up. "Did the click move me" was
    // answerable only by looking at the screen, and a silent revert looked
    // exactly like a click that never fired.
    const mover::Body& b = mover::Local();
    if (b.valid) {
        const float dx = b.pos[0] - s_hold.dest[0];
        const float dy = b.pos[1] - s_hold.dest[1];
        const float dz = b.pos[2] - s_hold.dest[2];
        const float off = sqrtf(dx * dx + dy * dy + dz * dz);
        const float mx = b.pos[0] - s_hold.from[0];
        const float my = b.pos[1] - s_hold.from[1];
        const float mz = b.pos[2] - s_hold.from[2];
        const float moved = sqrtf(mx * mx + my * my + mz * mz);
        LogLine("teleport: %.1f studs from the start, %.1f studs from the target (%s)",
                (double)moved, (double)off,
                off <= 2.0f ? "arrived" : "the client put it back");
    }
    s_hold.on = false;
    s_invalid_since = 0;
}
} // namespace

void TickTeleportHold()
{
    if (!s_hold.on) return;
    if ((int)(platform::NowMs() - s_hold.until) >= 0) { EndHold(); return; }
    // A TRANSIENT miss does not end the hold. The cache publishes "no local
    // player" for one 16ms window whenever any of its ~20 chained reads fails a
    // pass - under load that is an every-second-or-so event, and ending a
    // 400ms teleport hold on the first one is how click-tp and waypoints came
    // to "not work" while player tp (same write path, luckier timing) did.
    // The hold thread keeps hammering the armed destination through the miss;
    // only a SUSTAINED absence (>250ms - a real respawn or detach) gives up.
    if (!mover::Local().valid) {
        if (s_invalid_since == 0) s_invalid_since = platform::NowMs();
        if (platform::NowMs() - s_invalid_since > kInvalidEndsMs) EndHold();
        return;
    }
    s_invalid_since = 0;
    // The claim can be lost without us (a respawn drops every saved value and
    // the owner). Ending through EndHold - not by clearing the flag - is what
    // makes sure the ANCHOR is lifted: an anchored root left behind is a
    // character that cannot walk, and that is the one failure this feature must
    // never cause.
    if (mover::Current() != mover::Teleport) { EndHold(); return; }
    mover::PlatformStand(mover::Teleport, true);
    // Phase switch: anchor held while the destination lands, then released so
    // the solver rebuilds the assembly from where the part now is (and starts
    // reporting that position to the server). The hold keeps writing through
    // the release, so the first solver step after the anchor lifts already
    // reads the destination rather than the old spot.
    const uint64_t age = platform::NowMs() - s_hold.start;
    if (s_hold.anchored && age >= kAnchorMs) {
        mover::Anchor(mover::Teleport, false);
        s_hold.anchored = false;
    }
    PushRoot(s_hold.dest);
    (void)kSettleMs;   // documents the remaining hold; kHoldMs-kAnchorMs covers it
}

bool WriteRootTo(float x, float y, float z, const char* label)
{
    const mover::Body& body = mover::Local();
    if (!body.valid) {
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "No root part to write to - respawn, then try again");
        return false;
    }
    if (s_hold.on) EndHold();
    if (!mover::Claim(mover::Teleport)) {
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "Something else is driving the character right now - try again");
        return false;
    }

    const float dest[3] = { x, y, z };
    // ANCHOR, not the old "no anchor" note. The survival test that note cites
    // held the flag for 500ms and saw the position revert - but it wrote ONCE and
    // then let the solver step. Here the flag is set and the hold thread keeps
    // writing the destination 4000 times a second underneath it, so there is no
    // step at which the solver recomputes from the old assembly. The unanchor in
    // TickTeleportHold is what hands the new position back to the solver.
    mover::PlatformStand(mover::Teleport, true);
    mover::Anchor(mover::Teleport, true);
    // If the arm did not take (motion block unreadable this instant), say so
    // instead of reporting success over a hold that will never write - a
    // silently failed arm is indistinguishable from "click tp does nothing".
    if (!PushRoot(dest)) {
        EndHold();
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "Could not arm the teleport write - try again");
        return false;
    }
    s_hold.on = true;
    s_hold.start = platform::NowMs();
    s_hold.until = s_hold.start + kHoldMs;
    s_hold.anchored = true;
    s_hold.dest[0] = dest[0]; s_hold.dest[1] = dest[1]; s_hold.dest[2] = dest[2];
    s_hold.from[0] = body.pos[0]; s_hold.from[1] = body.pos[1]; s_hold.from[2] = body.pos[2];

    (void)label;
    snprintf(players_status_msg, sizeof(players_status_msg), "teleported");
    return true;
}

bool TeleportToWorld(float x, float y, float z, const char* label)
{
    return WriteRootTo(x, y + tp_land_height, z, label);
}

bool TeleportToPlayer(uint32_t user_id)
{
    Pose p;
    if (!ReadTargetPose(user_id, &p)) {
        snprintf(players_status_msg, sizeof(players_status_msg),
                 "That player left the server (or never resolved)");
        return false;
    }
    return TeleportToWorld(p.pos[0], p.pos[1], p.pos[2], p.name);
}

void RunSpectate()
{
    if (!spectate_enabled) {
        if (s_subject_cam) DetachSubject();
        return;
    }
    if (!g_base_address) { StopSpectate("the game closed"); return; }

    const instance cam = CameraInstance();
    if (!cam.is_valid()) return;

    uintptr_t ch = 0;
    if (!FindTarget(spectate_user_id, &ch, nullptr, 0)) {
        if (++s_miss > 45) StopSpectate("they left the server");
        return;
    }
    s_miss = 0;

    const uint64_t now = platform::NowMs();
    uintptr_t hum = s_hum_cached;
    if (ch != s_hum_char || !is_valid_address(hum) || (now - s_hum_at) > 350) {
        hum = FindHumanoid(ch);
        s_hum_cached = hum;
        s_hum_char = ch;
        s_hum_at = now;
    }
    if (!hum) return;
    AttachSubject(cam, hum);
    ApplyOffset(hum);
}

} // namespace features
