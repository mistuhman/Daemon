#include "friends.h"
// -----------------------------------------------------------------------------
//  features/players/friends.cpp  --  the set, and nothing else
// -----------------------------------------------------------------------------
//  No memory reads, no thread: the ESP asks `IsFriend(user_id)` while it draws
//  and the player list toggles it. That is the whole feature in the reference
//  too (`set_friend` / `is_friend` / `clear` over an unordered_set), and the one
//  change is the key: user ids instead of instance addresses.
//
//  The set is bounded. A user cannot friend more people than a server holds, but
//  a corrupted config line can ask for a million ids, and a set that grows from
//  a file is a memory leak with a menu attached.
// -----------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace features {
namespace friends {

    namespace {
        // kConfigMax (the header) is the same number the config can serialize, so
        // a set that grows from a file can always be written back out.
        constexpr int kMaxFriends = kConfigMax;
        std::unordered_set<uint32_t> s_ids;
    }  // namespace

    bool IsFriend(uint32_t user_id) {
        if (user_id == 0) return false;
        return s_ids.find(user_id) != s_ids.end();
    }

    void SetFriend(uint32_t user_id, bool friended) {
        if (user_id == 0) return;
        if (friended) {
            if ((int)s_ids.size() >= kMaxFriends) return;
            s_ids.insert(user_id);
        } else {
            s_ids.erase(user_id);
        }
    }

    void ToggleFriend(uint32_t user_id) { SetFriend(user_id, !IsFriend(user_id)); }

    int Count() { return (int)s_ids.size(); }

    void Clear() { s_ids.clear(); }

    int ExportIds(uint32_t* out, int max) {
        if (!out || max <= 0) return 0;
        int n = 0;
        for (uint32_t id : s_ids) {
            if (n >= max) break;
            out[n++] = id;
        }
        // Stable order, so a save that changed nothing does not churn the file.
        std::sort(out, out + n);
        return n;
    }

    void ImportIds(const uint32_t* ids, int count) {
        s_ids.clear();
        if (!ids) return;
        for (int i = 0; i < count && (int)s_ids.size() < kMaxFriends; ++i)
            if (ids[i] != 0) s_ids.insert(ids[i]);
    }

}  // namespace friends
}  // namespace features
