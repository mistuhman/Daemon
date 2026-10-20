// -----------------------------------------------------------------------------
//  tools/offsets_flow_test.cpp  --  RefreshOffsets' decision tree, off Windows
// -----------------------------------------------------------------------------
//   g++ -std=c++17 -O1 -w -I"../roblox_external" -I"tools/win32_stub" \
//       tools/offsets_flow_test.cpp -o build/offsets_flow_test
//   ./build/offsets_flow_test
//
// The network is canned: features::servershttp::Get/GetViaCurl are defined
// here to answer from a script of (url-substring -> reply) pairs, so the real
// RefreshOffsets() walks its whole decision tree - version chain, {VER} URL
// templating, cache staleness, fallback file - without a socket anywhere.
// Scenario A is the user-visible bug this exists for: offsets.imtheo.lol
// behind Cloudflare (both transports fail), GitHub mirrors answering.
// -----------------------------------------------------------------------------
#include "features/servers/servers_http.h"
#include "globals.h"

#include "../roblox_external/offsets_live.cpp"

#include <cstdio>
#include <string>

// offsets_live.cpp reaches platform::ExeDir for its cache paths; the
// canned platform answers with the test's working directory (the flow
// test runs each scenario in its own scratch dir, so ExeDir must not
// point at the real binary's home).
namespace platform {
std::string ExeDir() { return "./"; }
bool Init() { return true; }
} // namespace platform

#include <vector>

#include <unistd.h>
#include <cstdlib>
#include <cstring>

// ---- the canned network ------------------------------------------------------
struct Reply { const char* match; const char* body; bool ok; };
static std::vector<Reply> g_net;
static std::vector<std::string> g_hits;      // every URL asked, with transport

static bool Answer(const char* url, std::string& body, std::string& err)
{
    for (const Reply& r : g_net) {
        if (strstr(url, r.match)) {
            if (!r.ok) { err = std::string("simulated refusal for ") + r.match; return false; }
            body = r.body;
            return true;
        }
    }
    err = "no canned reply";
    return false;
}

namespace features { namespace servershttp {
bool Get(const char* url, std::string& body, std::string& err, const char* ua)
{
    g_hits.push_back(std::string("winhttp ") + url + " [ua=" + (ua ? ua : "default") + "]");
    return Answer(url, body, err);
}
bool GetViaCurl(const char* url, std::string& body, std::string& err, const char* ua)
{
    g_hits.push_back(std::string("curl ") + url + " [ua=" + (ua ? ua : "default") + "]");
    return Answer(url, body, err);
}
bool GetAny(const char* url, std::string& body, std::string& err, const char* ua)
{
    if (Get(url, body, err, ua)) return true;
    std::string e2;
    if (GetViaCurl(url, body, e2, ua)) return true;
    err += "; curl: " + e2;
    return false;
}
bool Open(const char*, std::string&) { return false; }
bool TryInInstanceHop(const char*, unsigned, std::string&) { return false; }
} }

// ---- helpers -----------------------------------------------------------------
static int g_fail = 0, g_checks = 0;
static void Ok(bool cond, const char* what)
{
    ++g_checks;
    if (!cond) { ++g_fail; printf("  [FAIL] %s\n", what); }
}
static void EqU64(uint64_t got, uint64_t want, const char* what)
{
    ++g_checks;
    if (got != want) { ++g_fail; printf("  [FAIL] %s\n         got:  0x%llx\n         want: 0x%llx\n",
                                        what, (unsigned long long)got, (unsigned long long)want); }
}
static bool LogHas(const char* needle)
{
    for (const std::string& l : g_log_lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
static bool HitHas(const char* needle)
{
    for (const std::string& h : g_hits)
        if (h.find(needle) != std::string::npos) return true;
    return false;
}
static size_t HitIndex(const char* needle)
{
    for (size_t i = 0; i < g_hits.size(); ++i)
        if (g_hits[i].find(needle) != std::string::npos) return i;
    return (size_t)-1;
}

// A scratch dir per scenario, because ExeDir() lands on ".\\" under the stub
// and the cache files (offsets.json / offsets.version) are written there.
static std::string EnterScratchDir()
{
    char tmpl[] = "/tmp/offsets_flow_XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (!d) { perror("mkdtemp"); exit(2); }
    if (chdir(d) != 0) { perror("chdir"); exit(2); }
    return std::string(d);
}
static std::string CachePath(const char* name) { return platform::ExeDir() + name; }

static void PutFile(const char* name, const char* body)
{
    FILE* f = fopen(name, "wb");
    if (!f) { perror("fopen"); exit(2); }
    fputs(body, f);
    fclose(f);
}

// A minimal but real External_Offsets.hpp for the "new" version.
static const char* kHppV2 =
"namespace Offsets {\n"
"    inline std::string ClientVersion = \"version-NEWHASH00000000\";\n"
"    namespace TaskScheduler {\n"
"         inline constexpr uintptr_t Pointer = 0x1234567;\n"
"    }\n"
"    namespace VisualEngine {\n"
"         inline constexpr uintptr_t Pointer = 0x7654321;\n"
"    }\n"
"}\n";

// A minimal imtheo-style JSON for the same version.
static const char* kJsonV2 =
"{\"Roblox Version\":\"version-NEWHASH00000000\",\"Offsets\":{"
"\"TaskScheduler\":{\"Pointer\":1234567},"
"\"VisualEngine\":{\"Pointer\":7654321}}}";

int main()
{
    // ------------------------------------------------------------------
    // SCENARIO A - the bug this fixes: imtheo Cloudflare-blocked on both
    // transports, GitHub mirrors answering, nothing cached, fallback shipped.
    // ------------------------------------------------------------------
    {
        EnterScratchDir();
        g_net.clear(); g_hits.clear(); g_log_lines.clear();
        g_net.push_back({ "imtheo.lol/roblox/version", "", false });      // blocked
        g_net.push_back({ "imtheo.lol/version-", "", false });            // blocked (pinned)
        g_net.push_back({ "imtheo.lol/offsets.json", "", false });        // blocked
        g_net.push_back({ "versions/current.json",
                          "{\"Windows\":\"version-NEWHASH00000000\"}", true });
        g_net.push_back({ "External_Offsets.hpp", kHppV2, true });

        Offsets::TaskScheduler::Pointer = 1;
        Offsets::VisualEngine::Pointer = 1;

        Ok(RefreshOffsets(), "A: refresh succeeds with imtheo blocked");
        EqU64(Offsets::TaskScheduler::Pointer, 0x1234567, "A: TaskScheduler::Pointer from mirror hpp");
        EqU64(Offsets::VisualEngine::Pointer, 0x7654321, "A: VisualEngine::Pointer from mirror hpp");
        Ok(LogHas("live version version-NEWHASH00000000 (via rdd"), "A: version came from the rdd mirror");
        Ok(LogHas("downloaded version-NEWHASH00000000 via offsetmanager (hpp"),
           "A: offsets came from the offsetmanager mirror, hpp format");
        Ok(HitHas("curl https://offsets.imtheo.lol/roblox/version"),
           "A: the blocked host was retried through curl before giving up on it");
        Ok(HitHas("https://offsets.imtheo.lol/version-NEWHASH00000000/offsets.json"),
           "A: the pinned imtheo path was tried (and blocked) before the mirror took over");
        Ok(HitHas("Windows/version-NEWHASH00000000/External_Offsets.hpp"),
           "A: the mirror URL was built from the live version hash");
        // the download landed in the cache for next boot
        {
            std::string cached;
            Ok(ReadFile(CachePath("offsets.json"), cached) && cached.find("namespace Offsets") != std::string::npos,
               "A: the hpp body was cached");
            std::string ver;
            Ok(ReadFile(CachePath("offsets.version"), ver) && ver.find("version-NEWHASH00000000") != std::string::npos,
               "A: the version file was written");
        }
    }

    // ------------------------------------------------------------------
    // SCENARIO B - a normal day: imtheo answers everything. The pinned
    // per-version path serves the dump, so the plain /offsets.json is never
    // asked for, and imtheo sees the UA its own docs send.
    // ------------------------------------------------------------------
    {
        EnterScratchDir();
        g_net.clear(); g_hits.clear(); g_log_lines.clear();
        g_net.push_back({ "imtheo.lol/roblox/version", "version-NEWHASH00000000\n", true });
        g_net.push_back({ "imtheo.lol/version-NEWHASH00000000/offsets.json", kJsonV2, true });
        g_net.push_back({ "imtheo.lol/offsets.json", kJsonV2, true });

        Offsets::TaskScheduler::Pointer = 1;
        Ok(RefreshOffsets(), "B: refresh succeeds via imtheo");
        EqU64(Offsets::TaskScheduler::Pointer, 1234567, "B: TaskScheduler::Pointer from imtheo json");
        Ok(LogHas("live version version-NEWHASH00000000 (via imtheo)"), "B: version via imtheo");
        Ok(LogHas("via imtheo-pinned (json"), "B: offsets via the pinned path, json format");
        Ok(HitIndex("imtheo.lol/version-NEWHASH00000000/offsets.json") != (size_t)-1,
           "B: the pinned path was fetched");
        Ok(HitIndex("imtheo.lol/offsets.json") == (size_t)-1,
           "B: the plain /offsets.json was not needed once pinned answered");
        Ok(HitHas("[ua=OffsetsClient/1.0]"), "B: imtheo saw the documented client UA");
        Ok(!HitHas("curl "), "B: curl never needed when winhttp works");
    }

    // ------------------------------------------------------------------
    // SCENARIO C - the cache is current: no download at all.
    // ------------------------------------------------------------------
    {
        EnterScratchDir();
        PutFile(CachePath("offsets.version").c_str(), "version-NEWHASH00000000\n");
        PutFile(CachePath("offsets.json").c_str(), kJsonV2);
        g_net.clear(); g_hits.clear(); g_log_lines.clear();
        g_net.push_back({ "imtheo.lol/roblox/version", "version-NEWHASH00000000\n", true });

        Offsets::TaskScheduler::Pointer = 1;
        Ok(RefreshOffsets(), "C: refresh succeeds from cache");
        EqU64(Offsets::TaskScheduler::Pointer, 1234567, "C: TaskScheduler::Pointer from cache");
        Ok(LogHas("cache is current (version-NEWHASH00000000)"), "C: cache reported current");
        Ok(!HitHas("offsets.json"), "C: the offsets url was never fetched");
    }

    // ------------------------------------------------------------------
    // SCENARIO D - everything down, fallback file shipped next to the exe.
    // ------------------------------------------------------------------
    {
        EnterScratchDir();
        PutFile(CachePath("offsets_fallback.json").c_str(),
                "{\"Roblox Version\":\"version-FALLBACK0000000\",\"Offsets\":{"
                "\"TaskScheduler\":{\"Pointer\":999}}}");
        g_net.clear(); g_hits.clear(); g_log_lines.clear();

        Offsets::TaskScheduler::Pointer = 1;
        Ok(RefreshOffsets(), "D: refresh succeeds from the bundled fallback");
        EqU64(Offsets::TaskScheduler::Pointer, 999, "D: TaskScheduler::Pointer from fallback");
        Ok(LogHas("using bundled fallback"), "D: fallback reported");
    }

    // ------------------------------------------------------------------
    // SCENARIO E - everything down, nothing anywhere: compiled defaults stay.
    // ------------------------------------------------------------------
    {
        EnterScratchDir();
        g_net.clear(); g_hits.clear(); g_log_lines.clear();

        Offsets::TaskScheduler::Pointer = 1;
        Ok(!RefreshOffsets(), "E: refresh fails honestly");
        EqU64(Offsets::TaskScheduler::Pointer, 1, "E: compiled defaults untouched");
        Ok(LogHas("no json, compiled defaults"), "E: defaults reported");
    }

    // ------------------------------------------------------------------
    // SCENARIO F - version known and NEWER than everything we have: the
    // warning that names silent-wrong-offsets must fire.
    // ------------------------------------------------------------------
    {
        EnterScratchDir();
        PutFile(CachePath("offsets_fallback.json").c_str(),
                "{\"Roblox Version\":\"version-FALLBACK0000000\",\"Offsets\":{"
                "\"TaskScheduler\":{\"Pointer\":999}}}");
        g_net.clear(); g_hits.clear(); g_log_lines.clear();
        // the version sources answer a version nothing else has
        g_net.push_back({ "imtheo.lol/roblox/version", "version-EVENNEWER000000\n", true });

        Ok(RefreshOffsets(), "F: refresh still applies what it has");
        Ok(LogHas("WARNING - applied version-FALLBACK0000000 but the live client is version-EVENNEWER000000"),
           "F: the version-mismatch warning fired");
    }

    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
