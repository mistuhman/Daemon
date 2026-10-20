#pragma once

#include <cstdint>

namespace features {
namespace friends {

// The reference repo's visual/players/friends: a set of players the ESP paints
// green and the player list marks FRIEND.
//
// It is keyed by USER ID, not by address. The reference keys by
// player/model instance addresses, which is only correct until the model is
// rebuilt (a respawn, a place change): the address is a new allocation and the
// mark silently follows the old one around the heap. A user id is the same
// number for the same account for the whole session, which is what "this person
// is my friend" actually means.
//
// The set is small, user-curated, and belongs to the session: it is saved in the
// config like the blacklist, and it survives a respawn because nothing in it is
// an address.
// The config's cap, and the same number the set enforces: a line in a file must
// not be able to grow the set past what a save can write back.
enum { kConfigMax = 1024 };

bool IsFriend(uint32_t user_id);
void SetFriend(uint32_t user_id, bool friended);
void ToggleFriend(uint32_t user_id);
int  Count();
void Clear();

// The saved form, for config.cpp: a comma-separated list of ids, capped.
int          ExportIds(uint32_t* out, int max);   // returns how many were written
void         ImportIds(const uint32_t* ids, int count);   // replaces the set

}  // namespace friends
}  // namespace features
