#pragma once
// -----------------------------------------------------------------------------
//  features/servers  --  hop to another server, or rejoin this one
// -----------------------------------------------------------------------------
//  WHY A BROWSER LINK RATHER THAN A MEMORY WRITE
//  ---------------------------------------------
//  There is no memory location whose value makes the client move to another
//  server. Joining is `TeleportService`'s job, and it is a Lua call: an external
//  with no injection cannot make it. What an external CAN do is hand Roblox the
//  same deeplink the website would fire at the client:
//
//      roblox://experiences/start?placeId=<placeId>&gameInstanceId=<guid>
//
//  ShellExecute hands that to the registered protocol (the running player, or
//  the bootstrapper) with no browser tab. The hop sends the deeplink to the
//  running client which teleports in-place (no new window). The list request
//  goes from your own IP, unauthenticated - both list
//  endpoints are public, so no cookie is read and nothing of yours is sent.
//
//  WHAT IS PROVED RATHER THAN HOPEDED
//  ----------------------------------
//  Which of the listed servers is *ours* comes from `DataModel::JobId` (0x118).
//  That read is only trusted if it looks like a guid (36 hex digits, dashes in the
//  four places) AND matches one of the ids the API returned; otherwise the list
//  still shows, "this server" marks nothing and REJOIN stays disabled with the
//  reason in its place. A stale offset produces an honest "unknown", never a link
//  to a server that is not yours.
//
//  The experience name comes from this module too, because nothing in memory holds
//  it: the datamodel's instance name is the DataModel *kind* ("Ugc", "Game"), which
//  is why the rail used to say "Ugc". `View::place_name` is empty until the API
//  answers, and the rail shows no row rather than a wrong one.
//
//  The requests run on their own thread; `Tick()` reaps them. The menu only ever
//  reads a copy under the lock.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace features {
namespace servers {

enum { kMaxRows = 24 };        // one page of the API is 100; the card shows this many

struct Row
{
    char id[40];        // the gameInstanceId, a guid string
    int  playing     = 0;
    int  max_players = 0;
    int  state       = 0;   // 0 another server, 1 this one, 2 full, 3 both
};

struct View
{
    bool busy        = false;   // a request or a launch is in flight
    int  count       = 0;
    int  current     = -1;      // index of the row that is ours, -1 = not known
    Row  rows[kMaxRows];

    char msg[256]    = "";      // what the last request did, or why it could not
    char place_name[128] = "";  // "" until games.roblox.com answers for this place
    char place[24]   = "";      // place id, formatted, from memory
    uint64_t place_id = 0;
    bool can_rejoin  = false;   // only when `current` is known
    int  age_sec     = -1;      // how old the list is; -1 = never fetched
    bool name_from_api = false; // so the UI can say where the name came from
};

// Copies the state out under the lock. Not `const View&`: the caller must not hold
// a reference into memory a worker thread is writing.
void SnapshotInto(View& out);

// Call every feature-loop tick, ungated. While nothing is pending this reads two
// pointers at most once a second, which is what notices a place change (a hop that
// worked, or a rejoin) and drops the list that belonged to the old one.
void Tick();

// Fetch the server list, and the place name if it is still unknown. Returns false
// immediately (with the reason in View::msg) when not attached or already busy.
bool Refresh();

// Opens the join link for one of the listed rows, for the emptiest server that is
// not this one, or for this one (Rejoin). All three report into View::msg.
void JoinRow(int index);
void HopToOpenest();
void Rejoin();

} // namespace servers
} // namespace features
