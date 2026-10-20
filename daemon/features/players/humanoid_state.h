#pragma once

// -----------------------------------------------------------------------------
//  features/players/humanoid_state.h  --  what the Humanoid says it is doing
// -----------------------------------------------------------------------------
//  The humanoid state is cheap to read (one enum behind HumanoidState, one int at
//  HumanoidStateID inside it) and it is the closest thing an external has to the
//  game's own opinion of the local body. Three features want it, so it lives here
//  as a pure rule with Linux cases instead of being re-derived in each caller:
//
//    * flight watches it for a state a script did not choose (Ragdoll, FallingDown,
//      GettingUp, Seated): a server that takes the body back, or a hitbox check
//      tripping something, often shows up here BEFORE the position starts to
//      rubberband - and a body in Ragdoll while we pin a velocity is a picture the
//      game will not draw for a standing character.
//    * the aimbot and the triggerbot want "is this body actually upright and
//      alive", which the health field alone does not answer (a corpse keeps the
//      Humanoid for a moment, a ragdoll is alive and on the floor).
//
//  The values are the HumanoidStateType enum in document order. Only the ones a
//  tool cares about are named; unknown values are "other", because a client
//  version that renumbers the enum must not make a feature mistake a corpse for a
//  player.
// -----------------------------------------------------------------------------

namespace features {
namespace hstate {

    enum Id : int {
        Physics   = 0,
        Running   = 8,
        RunningNoPhysics = 9,
        Strafing  = 10,
        StrafingNoPhysics = 11,
        Climbing  = 12,
        Seated    = 13,
        Frozen    = 14,
        Flying    = 15,
        Landed    = 16,
        Jumping   = 18,
        Freefall  = 20,
        FallingDown = 22,
        Ragdoll   = 23,
        GettingUp = 24,
        // A corpse's state. The client is never told this one, but a memory read
        // after death can return it, and it must never count as upright.
        HumanoidDeath = 31,
    };

    // A state a character is only in because something happened to it, rather than
    // because the player did it. This is the set flight reports and the aimbot does
    // not aim through: a body in any of these is not a body that is playing.
    inline bool IsKnockedDown(int id) {
        return id == Ragdoll || id == FallingDown || id == GettingUp || id == Seated;
    }

    // Upright enough to be aimed at (and shot at) as a character. Ragdoll and the
    // knocked-down set are out; so is the death state, which the client never sets
    // but a memory read can return.
    inline bool IsUpright(int id) {
        switch (id) {
            case Physics: case Running: case RunningNoPhysics:
            case Strafing: case StrafingNoPhysics: case Climbing:
            case Flying: case Landed: case Jumping: case Freefall:
                return true;
            default:
                return false;
        }
    }

    // Is this a value the enum could actually hold? Used to reject a garbage read
    // before it is treated as a state at all - a failed read must read as
    // "unknown", never as "Frozen" or "Ragdoll" (which would make flight stop for
    // a read hiccup).
    inline bool Plausible(int id) {
        return id >= 0 && id <= 31;
    }

}  // namespace hstate
}  // namespace features
