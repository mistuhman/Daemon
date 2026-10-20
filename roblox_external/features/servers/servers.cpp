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

std::mutex g_mtx;
View       g_view;                                   // published state, g_mtx-guarded

std::atomic<bool> g_busy{ false };
std::atomic<bool> g_done{ false };
bool    g_want_name = true;        // until the API has answered for this place

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
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_pending_msg, sizeof(g_pending_msg), fmt, ap);
    va_end(ap);
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

void RunJob(int job, uint64_t place_id, std::string job_id)
{
    g_pending_name.clear();
    g_pending_list    = false;
    g_pending_count   = 0;
    g_pending_current = -1;
    g_pending_msg[0]  = '\0';

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
                for (int i = 0; i < n && g_pending_count < kMaxRows; ++i) {
                    Row& d = g_pending_rows[g_pending_count];
                    memcpy(d.id, tmp[i].id, sizeof(d.id));
                    d.playing     = tmp[i].playing;
                    d.max_players = tmp[i].max_players;
                    d.state = (tmp[i].playing >= tmp[i].max_players) ? 2 : 0;
                    if (guid_ok && job_id == tmp[i].id) {
                        g_pending_current = g_pending_count;
                        if (d.state == 2) d.state = 3;
                    }
                    ++g_pending_count;
                }
                g_pending_list = true;          // a real answer: even an empty one

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
                if (job == kName) Say("place name from games.roblox.com");
            } else if (job == kName) {
                if (!err.empty()) Say("place name unavailable: %s", err.c_str());
                else              Say("place name unavailable: %s", perr.c_str());
            }
        } else if (job == kName) {
            if (!err.empty()) Say("place name unavailable: %s", err.c_str());
            else              Say("place name unavailable: %s", perr.c_str());
        }
    }

    g_pending_name = name;
    g_busy.store(false);
    g_done.store(true);
}

// Sends the deeplink to the running Roblox client and watches for a JobId change
// in memory. If the hop succeeds in-instance, no new window opens. Never on the
// thread that renders or writes memory: see LaunchThread below.
void RunLaunch(const std::string& url)
{
    std::string err;
    if (servershttp::TryInInstanceHop(url.c_str(), 5000u, err)) {
        Say("server hop within the same instance");
    } else {
        Say("%s", err.empty() ? "the server hop did not take" : err.c_str());
    }
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
struct JobCtx    { int job; uint64_t place_id; char job_id[40]; };
struct LaunchCtx { char url[512]; };
JobCtx    g_job_ctx{};
LaunchCtx g_launch_ctx{};

void FetchThread(void* p)
{
    const JobCtx c = *static_cast<JobCtx*>(p);
    RunJob(c.job, c.place_id, std::string(c.job_id));
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
    if (g_busy.load()) { Say("a request is already running"); g_done.store(true); return false; }
    uint64_t place_id = 0;
    std::string job_id;
    if (!ReadPlaceFromMemory(place_id, job_id)) {
        Say("not in an experience, so there is no server list to read");
        g_done.store(true);
        return false;
    }
    g_job_ctx.job      = job;
    g_job_ctx.place_id = place_id;
    snprintf(g_job_ctx.job_id, sizeof(g_job_ctx.job_id), "%s", job_id.c_str());
    g_busy.store(true);
    if (!platform::StartThread(FetchThread, &g_job_ctx)) {
        g_busy.store(false); Say("the request thread could not start"); g_done.store(true); return false;
    }                                   // fire and forget: the result comes back via g_done
    return true;
}

bool StartLaunch(const std::string& url)
{
    if (g_busy.load()) { Say("wait for the running request to finish"); g_done.store(true); return false; }
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
    if (index < 0 || index >= v.count) return;
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
    int best = -1, best_room = 0;
    for (int i = 0; i < v.count; ++i) {
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
    if (!v.can_rejoin || v.current < 0 || v.current >= v.count) {
        Say("rejoin needs your server id to match the list - refresh and try again");
        g_done.store(true);
        return;
    }
    StartLaunch(JoinUrl(v.place_id, v.rows[v.current].id));
}

} // namespace servers
} // namespace features
