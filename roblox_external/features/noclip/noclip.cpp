#include <unordered_map>
#include "noclip.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"
#include "features/movement/mover.h"

namespace features {

    static std::unordered_map<uintptr_t, uint8_t> s_original_flags;
    static uintptr_t s_last_character = 0;

    static void RestoreCollision() {
        if (s_original_flags.empty()) return;
        if (!g_base_address) return;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return;
        instance players = dm.read_service("Players");
        if (!players.is_valid()) return;
        instance local = players.local_player();
        if (!local.is_valid()) return;
        instance character = local.model_instance();
        if (!character.is_valid()) return;

        for (const instance& child : character.get_children()) {
            if (!child.is_valid()) continue;
            uintptr_t prim = read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
            if (!is_valid_address(prim)) continue;
            auto it = s_original_flags.find(child.address);
            if (it != s_original_flags.end()) {
                // Restore CanCollide only, leaving the anchor bit exactly as it
                // is now: mover may legitimately be holding it for a teleport or
                // for flight at this instant, and clearing it here would drop the
                // character out of the sky mid-feature.
                const uint8_t now = read<uint8_t>(prim + Offsets::Primitive::Flags);
                const uint8_t keep = (uint8_t)(now & (uint8_t)Offsets::PrimitiveFlags::Anchored);
                write<uint8_t>(prim + Offsets::Primitive::Flags, (uint8_t)(it->second | keep));
            }
        }
        s_original_flags.clear();
        s_last_character = 0;
    }

    void RunNoclip() {
        if (!noclip_enabled) {
            RestoreCollision();
            return;
        }

        // no keybind set -> noclip whenever enabled; otherwise hold-to-use
        bool key_held = (noclip_keybind == 0) || (platform::IsKeyDown(noclip_keybind));

        if (!key_held) {
            RestoreCollision();
            return;
        }

        if (!g_base_address) return;

        // Once per physics step, not once per platform::SleepMs(1) iteration. This walks the
        // whole character (a class-name read and two flag accesses per part) and
        // then WRITES every one of them; at ~600Hz that is thousands of
        // ReadProcessMemory/WriteProcessMemory calls a second into the same
        // Primitive structs flight and the teleport hold are writing, and it is
        // the traffic that made the other movement features' writes get lost.
        // Collision flags are not something the engine resets every frame, so a
        // 60Hz re-assert is as effective and ~10x cheaper.
        if (!mover::PhysicsStep()) return;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return;
        instance players = dm.read_service("Players");
        if (!players.is_valid()) return;
        instance local = players.local_player();
        if (!local.is_valid()) return;
        instance character = local.model_instance();
        if (!character.is_valid()) return;

        if (character.address != s_last_character) {
            s_original_flags.clear();
            s_last_character = character.address;
        }

        for (const instance& child : character.get_children()) {
            if (!child.is_valid()) continue;
            uintptr_t prim = read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
            if (!is_valid_address(prim)) continue;

            if (s_original_flags.find(child.address) == s_original_flags.end()) {
                // Save the flags WITHOUT the anchor bit. No feature sets Anchored
                // any more (it was measured to have no effect on position and the
                // calls were removed), but a game or another tool can, and noclip
                // must never remember someone else's transient anchor as this
                // part's normal state and restore it later - that is a character
                // frozen in place by a feature that has nothing to do with
                // anchoring. Noclip owns CanCollide and nothing else.
                const uint8_t f = read<uint8_t>(prim + Offsets::Primitive::Flags);
                s_original_flags[child.address] =
                    (uint8_t)(f & ~(uint8_t)Offsets::PrimitiveFlags::Anchored);
            }

            uint8_t flags = read<uint8_t>(prim + Offsets::Primitive::Flags);
            flags &= ~static_cast<uint8_t>(Offsets::PrimitiveFlags::CanCollide);
            write<uint8_t>(prim + Offsets::Primitive::Flags, flags);
        }
    }
}
