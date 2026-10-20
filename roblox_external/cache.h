#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <mutex>
#include <memory>

namespace cache {
    static constexpr size_t k_esp_primitive_capacity = 64;

    struct EspEntity {
        uintptr_t primitives[64]{};
        char part_names[64][24]{};   // longest body part name is 16 chars
        uintptr_t part_addresses[64]{};
        size_t primitive_count = 0;
        uintptr_t character_address = 0;
        uintptr_t player_address = 0;
        uint32_t user_id = 0;
        uintptr_t team_address = 0;
        // True when this player's Team instance is the local player's Team.
        // The cache no longer DELETES teammates (the ESP tints them in
        // team_esp_color instead); consumers decide: the aimbot skips them
        // when team_check is on, the ESP colours them.
        bool is_teammate = false;
        bool is_r15 = false;
        float root_x = 0, root_y = 0, root_z = 0;
        float health = 0, max_health = 0;
        // One definition of "dead" for the whole tool (the players list labels with
        // it, the aimbot filters on it). The `max_health` half is the important part:
        // the cache only reads health when it FOUND a Humanoid, so a player whose
        // humanoid could not be read this pass has both at 0 -- and calling that dead
        // would let a transient read failure drop an aimbot lock or hide a live
        // player. Unknown is not dead; it is unknown.
        bool IsDead() const { return max_health > 0.0f && health <= 0.0f; }
        char name[64]{};
        char display_name[64]{};
        char tool_name[64]{};
    };

    struct SkeletonEntity {
        uintptr_t head = 0;
        uintptr_t upper_torso = 0;
        uintptr_t lower_torso = 0;
        uintptr_t left_hand = 0, right_hand = 0;
        uintptr_t left_foot = 0, right_foot = 0;
        uintptr_t left_upper_arm = 0, left_lower_arm = 0;
        uintptr_t right_upper_arm = 0, right_lower_arm = 0;
        uintptr_t left_upper_leg = 0, left_lower_leg = 0;
        uintptr_t right_upper_leg = 0, right_lower_leg = 0;
        uintptr_t player_address = 0;
        uintptr_t team_address = 0;
        bool is_teammate = false;   // see EspEntity::is_teammate
        bool is_r15 = false;
        char name[64]{};
    };

    struct LocalPlayerData {
        bool valid = false;
        float x = 0, y = 0, z = 0;
        uintptr_t hrp_primitive = 0;
        // The BasePart instance itself, not its Primitive. The authority scan
        // needs it: if the position we write into the primitive is being copied
        // back from somewhere, the part is the first place to look.
        uintptr_t hrp_address = 0;
        uintptr_t humanoid_address = 0;
        char name[64]{};   // account username (the LocalPlayer instance's name)
    };

    // Zero-copy snapshots. The cache thread publishes a new immutable vector each
    // pass; readers hold a shared_ptr so the data stays alive while they use it.
    // (The old by-value getters deep-copied ~2.7KB per player on every call, from
    // 9 call sites per frame.)
    using EspSnapshot  = std::shared_ptr<const std::vector<EspEntity>>;
    using SkelSnapshot = std::shared_ptr<const std::vector<SkeletonEntity>>;

    // World geometry for the wall check: axis-aligned boxes (center + half
    // extents) for every world primitive that is not a character part and is
    // visible to raycasts (CanQuery). Refreshed by a dedicated slow sweep
    // thread while wall_check is on -- a big map takes hundreds of
    // milliseconds to sweep and must never stall the 16ms cache pass.
    struct WorldBox {
        float cx = 0, cy = 0, cz = 0;
        float hx = 0, hy = 0, hz = 0;
    };
    using WorldSnapshot = std::shared_ptr<const std::vector<WorldBox>>;

    EspSnapshot  GetEspSnapshot();
    SkelSnapshot GetSkeletonSnapshot();
    WorldSnapshot GetWorldBoxes();   // empty when undetected / wall check never ran

    // Dumps the physics-world primitive container (raw qwords, auto-detection
    // verdict, sample primitives) to the log. For pinning the container
    // layout by hand when auto-detection fails. Debug page -> world probe.
    void WorldProbeLog();

    std::mutex& GetMutex();
    std::vector<EspEntity> GetEspEntities();
    std::vector<SkeletonEntity> GetSkeletonEntities();
    LocalPlayerData GetLocalPlayer();
    void StartThread();
    void StopThread();
}