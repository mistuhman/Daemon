// -----------------------------------------------------------------------------
//  features/servers/servers.cpp  --  state, one worker thread, the join links
// -----------------------------------------------------------------------------
//  There is exactly one thing in flight at a time (`g_busy`), and it is always
//  finished by the feature loop, never by the caller: the worker writes the
//  `g_pending_*` slots and raises `g_done`, and Tick() copies those into the View
//  the menu reads under the mutex. That is why nothing here returns a status - a
//  hop is a request that may take two seconds, and the menu must not be the thing
//  that waits for it.
// -----------------------------------------------------------------------------
#include "servers.h"

#include "servers_json.h"      // serversjson::, qualified below: this namespace has
                              // its own row type (Row) and they must not be confused
#include "servers_http.h"

#include "game.h"
#include "globals.h"
#include "memory.h"
#include "offsets.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace features {
namespace servers {
namespace {

constexpr const char* kUniversePath = "https://apis.roblox.com/universes/v1/places/";
constexpr const char* kUniverseTail = "/universe";
constexpr const char* kGamesByUniverse = "https://games.roblox.com/v1/games?universeIds=";
constexpr const char* kServersPath  = "https://games.roblox.com/v1/games/";
constexpr const char* kServersQuery = "/servers/Public?limit=100";

// What the worker was asked to do. kName is the small one the loop fires by itself,
// so the rail's GAME row fills in without anyone pressing a button.
enum Job { kName = 1, kList = 2 };

// One lock for the published View AND the worker's out-tray, which is what makes a
// half-written row impossible to observe. It matters more than it looks: a `Row` copied
// while the worker is still memcpy-ing its id is a 40-byte guid with no NUL in it, and
// every reader downstream (`ShortGuid`'s strlen, the menu's %s) walks off the end. The
// crash this module is famous for was exactly that shape one level up - a row written at
// the wrong index, onto the count that bounds the next frame's loop.
// Rule, therefore: nothing calls Say() while holding g_mtx.
std::mutex g_mtx;
View       g_view;                                   // published state, g_mtx-guarded

std::atomic<bool> g_busy{ false };
std::atomic<bool> g_done{ false };
bool    g_want_name = true;        // until the API has answered for this place

// The hop's scoreboard, g_mtx-guarded: what the client was on when the link went out,
// and how long to wait for memory to say otherwise. See RunLaunch / HopWatch.
bool     g_hop_watch = false;
uint64_t g_hop_from_place = 0;
char     g_hop_from_job[40] = "";
std::chrono::steady_clock::time_point g_hop_until{};

// The worker's out-tray: Tick() copies these into g_view and clears them.
std::string g_pending_name;
Row   g_pending_rows[kMaxRows];
int   g_pending_count   = 0;
bool  g_pending_list    = false;   // did this job read the list at all?
int   g_pending_current = -1;
char  g_pending_msg[256] = "";

uint64_t g_place_id = 0;           // the last place id memory reported
bool     g_have_list = false;
std::chrono::steady_clock::time_point g_listed_at{};
std::chrono::steady_clock::time_point g_next_poll{};

void Say(const char* fmt, ...)
{
    char buf[sizeof(g_pending_msg)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // Formatted outside the lock so a worker mid-message cannot make the render thread
    // wait, and copied inside it so a reader never sees half a sentence.
    std::lock_guard<std::mutex> lk(g_mtx);
    memcpy(g_pending_msg, buf, sizeof(buf));
}

// A gameInstanceId is a guid. Checking the shape before trusting a memory read is
// what keeps a moved DataModel::JobId offset from producing a confident, wrong
// join link: garbage from a stale offset fails this and rejoin stays off.
bool LooksLikeGuid(const std::string& s)
{
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        const char c = s[i];
        const bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
        if (dash) { if (c != '-') return false; }
        else if (!isxdigit((unsigned char)c)) return false;
    }
    return true;
}

// How old a list may be before clicking a row in it stops being worth the risk. A
// public server's job id is good for minutes, not hours: the list is a snapshot of a
// churn, and handing the client an id that has already died does not fail fast - the
// player sits on a window that cannot resolve the instance. The card has shown
// "fetched 8s ago" from the start; this is the same fact with a consequence attached.
// It is generous on purpose: reading a stale row and *being told* is better than a
// refused click, so a fetch is fired and the click is what waits.
constexpr int kMaxJoinAgeSec = 180;

bool Start(int job);             // the entry point below; ListUsable fires a refresh

// False (with the reason in the tray) when the list is too old to join from, after
// asking for a fresh one. `v` is the snapshot the caller already holds. Both branches
// raise g_done by hand, like every other caller-side refusal: the message is written
// from the render thread, and Publish() only copies the tray when it is told to.
bool ListUsable(const View& v)
{
    if (v.age_sec < 0) {
        Say("nothing fetched yet - press REFRESH (or this row) first");
        g_done.store(true);
        return false;
    }
    if (v.age_sec <= kMaxJoinAgeSec) return true;
    Start(kList);       // fire it now, so the next click has something current to use
    char why[160];
    snprintf(why, sizeof(why), "that list is %dm old - a dead server id leaves Roblox "
             "sitting on its join screen, so it is being re-fetched: click again when "
             "it lands", v.age_sec / 60);
    Say("%s", why);
    g_done.store(true);
    return false;
}

// The two things memory knows about where we are. False when the datamodel is not
// there (menu open before the join finished, or after the client exited) or when the
// place id is still zero, which is the home menu rather than an experience.
bool ReadPlaceFromMemory(uint64_t& place_id, std::string& job_id)
{
    place_id = 0;
    job_id.clear();
    if (g_base_address == 0) return false;
    const instance dm = game::ReadDatamodel(g_base_address);
    if (!dm.is_valid()) return false;
    place_id = read<uint64_t>(dm.address + Offsets::DataModel::PlaceId);
    if (place_id == 0) return false;
    job_id = fetchstring(dm.address + Offsets::DataModel::JobId);
    return true;
}

// The installer registers roblox://. The website's https join page is what used
// to open a tab first; this is the same deeplink the site then fires, so the
// running client (or the bootstrapper) takes it without a browser.
std::string JoinUrl(uint64_t place_id, const char* instance_guid)
{
    std::string url = "roblox://experiences/start?placeId=";
    url += std::to_string(place_id);
    if (instance_guid && instance_guid[0]) { url += "&gameInstanceId="; url += instance_guid; }
    return url;
}

// The worker. It runs on its own thread, which is why the memory read belongs HERE and
// not in Start(): the datamodel chain is four dependent reads and a string, and Start()
// is called by the button press, on the thread that renders. Reading through a handle the
// attach loop may be swapping at that moment is not something the render thread should
// ever do on a timer nobody controls.
void RunJob(int job)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_pending_name.clear();
        g_pending_list    = false;
        g_pending_count   = 0;
        g_pending_current = -1;
        g_pending_msg[0]  = '\0';
    }

    uint64_t place_id = 0;
    std::string job_id;
    if (!ReadPlaceFromMemory(place_id, job_id)) {
        Say("not in an experience, so there is no server list to read");
        g_busy.store(false);
        g_done.store(true);
        return;
    }

    std::string name;

    if (job == kList) {
        std::string body, err;
        const std::string url = std::string(kServersPath) + std::to_string(place_id) + kServersQuery;
        std::string perr;
        serversjson::ServerRow tmp[100];
        if (servershttp::Get(url.c_str(), body, err)) {
            const int n = serversjson::ParseServers(body, tmp, 100, name, perr);
            if (n < 0) {
                Say("the server list did not parse: %s", perr.c_str());
            } else {
                const bool guid_ok = LooksLikeGuid(job_id);
                // Built locally, then swapped in under the lock: the tray is what the
                // menu renders, so it is either the old list or the new one, never a
                // row in the middle of being written.
                Row   rows[kMaxRows];
                int   count = 0, current = -1;
                for (int i = 0; i < n && count < kMaxRows; ++i) {
                    Row& d = rows[count];
                    memcpy(d.id, tmp[i].id, sizeof(d.id));
                    d.playing     = tmp[i].playing;
                    d.max_players = tmp[i].max_players;
                    d.state = (tmp[i].playing >= tmp[i].max_players) ? 2 : 0;
                    if (guid_ok && job_id == tmp[i].id) {
                        current = count;
                        if (d.state == 2) d.state = 3;
                    }
                    ++count;
                }
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    for (int i = 0; i < count; ++i) g_pending_rows[i] = rows[i];
                    g_pending_count   = count;
                    g_pending_current = current;
                    g_pending_list    = true;     // a real answer: even an empty one
                }

                if (g_pending_count == 0)
                    Say("no public servers listed for this place");
                else if (g_pending_count < n)
                    Say("%d servers listed, showing the first %d", n, g_pending_count);
                else
                    Say("%d servers from games.roblox.com, this one marked", n);

                // "rejoin is off" has to say why, because the three reasons need
                // different fixes and only one of them is this program's fault.
                if (!LooksLikeGuid(job_id) && job_id.empty())
                    Say("list is up. memory had no server id, so rejoin is off");
                else if (!LooksLikeGuid(job_id))
                    Say("list is up. the server id in memory is not a guid - "
                        "DataModel::JobId has moved, so rejoin is off");
                else if (g_pending_current < 0)
                    Say("list is up. your server id is not in it - a private or "
                        "reserved server, so rejoin is off");
            }
        } else {
            Say("games.roblox.com: %s", err.c_str());
        }
    }

    // Name: prefer the server list (it sometimes embeds place.name), else the
    // two public endpoints. multiget-place-details is authenticated (9002 without
    // a cookie) and is not used. Place-id → universe-id → games?universeIds=.
    if (name.empty() || job == kName) {
        std::string body, err, perr, nm;
        const std::string uurl = std::string(kUniversePath) + std::to_string(place_id) + kUniverseTail;
        long long universe_id = 0;
        if (servershttp::Get(uurl.c_str(), body, err) &&
            serversjson::ParseUniverseId(body, universe_id, perr)) {
            body.clear(); err.clear();
            const std::string gurl = std::string(kGamesByUniverse) + std::to_string(universe_id);
            if (servershttp::Get(gurl.c_str(), body, err) &&
                serversjson::ParsePlaceName(body, nm, perr)) {
                name = nm;
                // A successful name lookup says nothing on purpose. The footer line is
                // one slot, and this branch runs after the server list was fetched, so
                // announcing "place name from games.roblox.com" here overwrote the
                // actually-useful "list is up" message with a step report. Failures
                // still speak -- below -- because those are the answer, not a step.
            } else if (job == kName) {
                if (!err.empty()) Say("place name unavailable: %s", err.c_str());
                else              Say("place name unavailable: %s", perr.c_str());
            }
        } else if (job == kName) {
            if (!err.empty()) Say("place name unavailable: %s", err.c_str());
            else              Say("place name unavailable: %s", perr.c_str());
        }
    }

    if (!name.empty()) {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_pending_name = name;
    }
    g_busy.store(false);
    g_done.store(true);
}

// Hands the deeplink to the registered protocol and returns. Nothing else.
//
// This used to call servershttp::TryInInstanceHop, which opened the link and then sat
// reading the datamodel chain every 100ms for five seconds looking for a JobId change.
// Three things were wrong with that, in ascending order of annoyance: it made this thread
// the second one in the process doing chained memory reads (the feature loop is the
// designed one); it held `g_busy` for the whole five seconds, so the card said FETCHING
// and REFRESH refused while nothing was being fetched; and the answer it waited for
// arrived - if at all - as a change in the very state Tick() already reads once a second,
// which is where the comparison now happens (see HopWatch in Tick).
void RunLaunch(const std::string& url)
{
    // What we believe the client is on right now, as the baseline Tick() compares
    // against. Read here, on a worker, so the button press stays read-free.
    uint64_t from_place = 0;
    std::string from_job;
    ReadPlaceFromMemory(from_place, from_job);

    std::string err;
    if (!servershttp::Open(url.c_str(), err)) {
        Say("%s", err.empty() ? "the client refused the link" : err.c_str());
        g_busy.store(false);
        g_done.store(true);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_hop_from_place = from_place;
        snprintf(g_hop_from_job, sizeof(g_hop_from_job), "%s", from_job.c_str());
        g_hop_until = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        g_hop_watch = true;
    }
    Say("link sent to the client - watching for the server id to change");
    g_busy.store(false);
    g_done.store(true);
}

// Threads go through platform::StartThread, which on Windows is CreateThread -
// NOT std::thread, because the project keeps $(ProjectDir) on the include path, so
// its own process.h SHADOWS the CRT's <process.h>, and MSVC's <thread> includes
// <process.h> for _beginthreadex. std::thread therefore fails as C2039 + C3861
// *inside* VC/tools/.../include/thread, in a file nobody can edit (HANDOFF §9 has
// the rename that would end it). Exactly one job is ever in flight, which is also
// what makes handing a worker a pointer to a static context safe - the next Start
// cannot overwrite it until this one has published and cleared.
struct JobCtx    { int job; };
struct LaunchCtx { char url[512]; };
JobCtx    g_job_ctx{};
LaunchCtx g_launch_ctx{};

void FetchThread(void* p)
{
    const int job = static_cast<JobCtx*>(p)->job;
    RunJob(job);
}

void LaunchThread(void* p)
{
    const LaunchCtx c = *static_cast<LaunchCtx*>(p);
    RunLaunch(std::string(c.url));
}

// false, with the reason in the tray, when there is nothing to ask about or a request
// is already running. Callers that cannot report a bool go through Publish() like any
// other message.
bool Start(int job)
{
    // No memory reads here, and only two atomic loads: this is called from the render
    // thread by the button. The worker decides whether there is anything to ask about
    // and says so in the tray if there is not.
    if (g_busy.load()) { Say("a request is already running"); g_done.store(true); return false; }
    g_job_ctx.job = job;
    g_busy.store(true);
    if (!platform::StartThread(FetchThread, &g_job_ctx)) {
        g_busy.store(false); Say("the request thread could not start"); g_done.store(true); return false;
    }                                   // fire and forget: the result comes back via g_done
    return true;
}

bool StartLaunch(const std::string& url)
{
    if (g_busy.load()) { Say("wait for the running request to finish"); g_done.store(true); return false; }
    if (url.empty())   { Say("there was no link to send"); g_done.store(true); return false; }
    // Measure before copying. A link clipped to fit the context buffer is not a cosmetic
    // problem: it is a well-formed roblox:// URL pointing at half a place id, which is the
    // one outcome this module's header promises it will not produce - a confident, wrong
    // join. Nothing can reach 512 today (place id + guid is ~110 chars), so this is a
    // tripwire for whoever adds a longer link, and it refuses out loud instead.
    const int need = snprintf(nullptr, 0, "%s", url.c_str());
    if (need < 0 || need >= (int)sizeof(g_launch_ctx.url)) {
        Say("the join link was too long for the hand-off - refusing to send half of it");
        g_done.store(true);
        return false;
    }
    snprintf(g_launch_ctx.url, sizeof(g_launch_ctx.url), "%s", url.c_str());
    g_busy.store(true);
    if (!platform::StartThread(LaunchThread, &g_launch_ctx)) {
        g_busy.store(false); Say("the shell hand-off could not start"); g_done.store(true); return false;
    }
    return true;
}

void Publish()
{
    if (!g_done.load()) return;
    g_done.store(false);
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_pending_name.empty()) {
        snprintf(g_view.place_name, sizeof(g_view.place_name), "%s", g_pending_name.c_str());
        g_view.name_from_api = true;
        g_want_name = false;
    }
    // Only a reply that parsed replaces the list. A fetch that failed leaves the
    // rows standing - stale, but labelled - because clearing them would claim the
    // place has no servers, which is a different and wrong fact.
    if (g_pending_list) {
        g_view.count   = g_pending_count;
        g_view.current = g_pending_current;
        for (int i = 0; i < g_pending_count; ++i) g_view.rows[i] = g_pending_rows[i];
        g_view.can_rejoin = (g_pending_current >= 0);
        g_listed_at = std::chrono::steady_clock::now();
        g_have_list = true;
    }
    if (g_pending_msg[0]) {
        snprintf(g_view.msg, sizeof(g_view.msg), "%s", g_pending_msg);
        g_pending_msg[0] = '\0';
    }
    g_view.busy = g_busy.load();
}

} // namespace

void SnapshotInto(View& out)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    out = g_view;
    out.busy     = g_busy.load();
    out.place_id = g_view.place_id;
    if (g_view.place_id != 0)
        snprintf(out.place, sizeof(out.place), "%llu", (unsigned long long)g_view.place_id);
    else
        out.place[0] = '\0';
    out.age_sec = g_have_list
        ? (int)std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - g_listed_at).count()
        : -1;
}

void Tick()
{
    Publish();

    // Once a second is plenty to notice that the place changed - which is what a
    // successful hop looks like from here - and the name request is fired from the
    // same poll, so a join fills the rail in without a button press.
    const auto now = std::chrono::steady_clock::now();
    if (now < g_next_poll) return;
    g_next_poll = now + std::chrono::seconds(1);

    uint64_t place_id = 0;
    std::string job_id;
    const bool have = ReadPlaceFromMemory(place_id, job_id);
    const bool changed = have != (g_place_id != 0) || (have && place_id != g_place_id);
    if (have) g_place_id = place_id;

    // HopWatch: the only evidence that a hop took is memory saying the client is now on
    // a different job (same client, in-instance) or in a different place (it opened its
    // own window). Both are visible here for free, because this already reads the
    // datamodel once a second; RunLaunch used to spin its own 5s poll for the same fact,
    // on a second thread, with the request slot held open while it did.
    const char* hop_msg = nullptr;
    if (g_hop_watch) {
        const bool place_moved = have && g_hop_from_place != 0 &&
                                 place_id != g_hop_from_place;
        const bool job_moved   = have && g_hop_from_job[0] != '\0' &&
                                 !job_id.empty() && job_id != g_hop_from_job;
        if (place_moved)
            hop_msg = "the client opened a different place, so the link took - but not "
                      "in-instance, which is what a dead or full server looks like";
        else if (job_moved)
            hop_msg = "in-instance hop: the same client, a different server";
        else if (now >= g_hop_until)
            hop_msg = "still on the same server a few seconds later. a listed server can "
                      "already be gone, and Roblox stays on its join screen rather than "
                      "saying so - REFRESH and take a row that is closer in time";
    }

    if (changed) {
        g_place_id = have ? place_id : 0;
        std::lock_guard<std::mutex> lk(g_mtx);
        // Everything in the View belongs to the place that just closed.
        g_view = View{};
        g_view.place_id = g_place_id;
        g_have_list = false;
        g_want_name = true;
        if (have)
            snprintf(g_view.msg, sizeof(g_view.msg), "%s",
                     "new place - press REFRESH for this one's servers");
    }

    if (hop_msg) {
        // Written straight into the View rather than through Say(): the block above may
        // have just reset it for a new place, and the hop's answer belongs to *this*
        // state, so it has to land after that reset instead of racing it from a tray.
        std::lock_guard<std::mutex> lk(g_mtx);
        snprintf(g_view.msg, sizeof(g_view.msg), "%s", hop_msg);
        g_hop_watch = false;
    }

    if (g_want_name && !g_busy.load() && have) Start(kName);
}

bool Refresh()
{
    return Start(kList);
}

void JoinRow(int index)
{
    View v;
    SnapshotInto(v);
    // Both bounds, not just the one the data gives: `v.count` is written by a worker
    // thread and read by this one after a lock, but the array's size is the UI's own
    // promise, and a row that cannot exist must not be reachable by an index.
    if (index < 0 || index >= v.count || index >= kMaxRows) return;
    if (!ListUsable(v)) return;
    StartLaunch(JoinUrl(v.place_id, v.rows[index].id));
}

void HopToOpenest()
{
    View v;
    SnapshotInto(v);
    if (v.count < 1) {
        Say("refresh the list first - there is nothing to pick from");
        g_done.store(true);
        return;
    }
    // Same age rule as a row click: HOP is the one that trusts a count read minutes ago
    // the most, because "most room" is also "most likely to have filled up since".
    if (!ListUsable(v)) return;
    int best = -1, best_room = 0;
    for (int i = 0; i < v.count && i < kMaxRows; ++i) {
        if (i == v.current) continue;                       // not back to ourselves
        const int room = v.rows[i].max_players - v.rows[i].playing;
        if (room < 1) continue;                             // full
        if (room > best_room) { best_room = room; best = i; }
    }
    if (best < 0) { Say("every listed server is full"); g_done.store(true); return; }
    StartLaunch(JoinUrl(v.place_id, v.rows[best].id));
}

void Rejoin()
{
    View v;
    SnapshotInto(v);
    if (!v.can_rejoin || v.current < 0 || v.current >= v.count || v.current >= kMaxRows) {
        Say("rejoin needs your server id to match the list - refresh and try again");
        g_done.store(true);
        return;
    }
    if (!ListUsable(v)) return;
    StartLaunch(JoinUrl(v.place_id, v.rows[v.current].id));
}

} // namespace servers
} // namespace features
