// -----------------------------------------------------------------------------
//  tools/servers_json_test.cpp  --  the server-hop parser, off Windows
// -----------------------------------------------------------------------------
//   g++ -std=c++17 -O1 -w -I"../roblox_external" tools/servers_json_test.cpp -o build/servers_json_test
//   ./build/servers_json_test
//
// Fixtures are the documented bodies of games.roblox.com's two endpoints, kept
// close to verbatim: nested `players` array, null members, an escaped quote and an
// emoji in a name, `"playing"` as a string in one variant, and the shapes that must
// be REJECTED rather than shrugged off (truncated body, no data array, an entry
// without a player count).
// -----------------------------------------------------------------------------
#include "features/servers/servers_json.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace features::serversjson;

static int g_fail = 0, g_checks = 0;
static void Ok(bool cond, const char* what)
{
    ++g_checks;
    if (!cond) { ++g_fail; printf("  [FAIL] %s\n", what); }
}
static void EqStr(const std::string& got, const char* want, const char* what)
{
    ++g_checks;
    if (got != want) { ++g_fail; printf("  [FAIL] %s\n         got:  \"%s\"\n         want: \"%s\"\n",
                                        what, got.c_str(), want); }
}

// A realistic page: two servers, nested player lists, a full one, a linked place.
static const char* kServers =
    "{\"previousPageCursor\":null,"
    "\"nextPageCursor\":\"eyJvZmZzZXQiOjEwMH0\","
    "\"data\":["
      "{\"fqdn\":null,\"id\":\"6a5f1c2e-0b47-4c1a-9f3d-7d2e5b1a0c99\","
      "\"maxPlayers\":40,\"players\":["
        "{\"id\":\"261\",\"username\":null,\"displayName\":\"b\\u00e9a \\ud83d\\ude00\"},"
        "{\"id\":\"1\",\"username\":\"Builderman\",\"displayName\":\"Builder\\\"man\\\"\"}"
      "],\"playing\":12,\"serverType\":\"Public\",\"visitedPlayers\":30,"
      "\"place\":{\"id\":2585160604,\"name\":\"Build a Boat for Treasure\"},"
      "\"linkedPlaceId\":null},"
      "{\"id\":\"11111111-2222-3333-4444-555555555555\","
      "\"maxPlayers\":40,\"players\":[],\"playing\":40,\"serverType\":\"Public\","
      "\"visitedPlayers\":100000,\"place\":{\"id\":2585160604,"
      "\"name\":\"Build a Boat for Treasure\"},\"linkedPlaceId\":null},"
      "{\"id\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\","
      "\"maxPlayers\":6,\"players\":[],\"playing\":1,\"serverType\":\"Public\","
      "\"place\":{\"id\":999,\"name\":\"Other place in this game\"},\"linkedPlaceId\":888}"
    "]}";

static int g_fail_local_dummy = 0;   // keeps -Wunused quiet if a check is edited out

int main()
{
    ServerRow rows[8];
    std::string name, err;

    // ---- the happy page -----------------------------------------------------
    int n = ParseServers(kServers, rows, 8, name, err);
    Ok(n == 3, "three servers parsed");
    EqStr(err, "", "no error on a good reply");
    EqStr(name, "Build a Boat for Treasure", "place name taken from the first entry");
    if (n == 3) {
        EqStr(rows[0].id, "6a5f1c2e-0b47-4c1a-9f3d-7d2e5b1a0c99", "row 0 id");
        Ok(rows[0].playing == 12 && rows[0].max_players == 40, "row 0 counts");
        Ok(rows[1].playing == 40 && rows[1].max_players == 40, "full server reads full");
        EqStr(rows[2].id, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "row 2 id");
        Ok(rows[2].playing == 1 && rows[2].max_players == 6, "row 2 counts");
    }

    // a row must survive a `players` array full of objects: brace matching, not
    // "next }", is what keeps entries apart.
    Ok(strstr(rows[0].id, "6a5f") == rows[0].id, "nested players[] did not eat the id");

    // ---- an empty universe list is a state, not a failure -------------------
    n = ParseServers("{\"previousPageCursor\":null,\"nextPageCursor\":null,\"data\":[]}",
                     rows, 8, name, err);
    Ok(n == 0 && err.empty(), "empty data array = no servers, no error");

    // ---- shapes that must be refused loudly ---------------------------------
    name.clear();
    n = ParseServers("{\"error\":true,\"message\":\"Too Many Requests\"}", rows, 8, name, err);
    Ok(n == -1 && !err.empty(), "an error reply is reported, not read as empty");

    n = ParseServers("{\"data\":[{\"id\":\"x\",\"maxPlayers\":40,\"pl", rows, 8, name, err);
    Ok(n == -1 && err.find("cut off") != std::string::npos, "truncated body says so");

    // an entry with no count is dropped rather than shown as 0/0, because the hop
    // button picks the biggest gap; a page of only those is still an empty list.
    n = ParseServers("{\"data\":[{\"id\":\"only-id-no-counts\",\"maxPlayers\":40}]}",
                     rows, 8, name, err);
    Ok(n == 0 && err.empty(), "an uncountable row is dropped, not faked");

    // ---- the id must fit; saying so beats truncating a guid ---------------
    std::string huge = std::string("{\"data\":[{\"id\":\"") + std::string(60, 'a') +
                       "\",\"playing\":1,\"maxPlayers\":2}]}";
    n = ParseServers(huge, rows, 8, name, err);
    Ok(n == -1 && err.find("longer") != std::string::npos, "an oversized id is refused");

    // ---- escaped quotes, an accent, an emoji --------------------------------
    {
        const std::string j = "{\"name\":\"say \\\"hi\\\"\"}";
        std::string out;
        Ok(Str(j, 0, j.size(), "name", out), "escaped quote parsed");
        EqStr(out, "say \"hi\"", "escaped quote decoded");
    }
    // "caf\u00e9 \ud83d\ude00" -> c a f 0xC3 0xA9 SP 0xF0 0x9F 0x98 0x80
    {
        std::string nm;
        ParsePlaceName("[{\"placeId\":1,\"name\":\"caf\\u00e9 \\ud83d\\ude00\"}]", nm, err);
        const std::string want = std::string("caf") + "\xc3\xa9" + " " + "\xf0\x9f\x98\x80";
        EqStr(nm, want.c_str(), "accent + surrogate pair -> UTF-8");
    }
    {
        std::string out;
        const std::string cut = "{\"name\":\"unterminated";
        const std::string other = "{\"other\":\"x\"}";
        Ok(!Str(cut, 0, cut.size(), "name", out), "unterminated string refused");
        Ok(!Str(other, 0, other.size(), "name", out), "missing key refused");
        // `"maxPlayers"` must not be matched by a search for `"Players"`-like keys:
        // the search is for the quoted key, so a prefix must not hit.
        long long v = 0;
        const std::string near = "{\"notplaying\":5}";
        Ok(!Num(near, 0, near.size(), "playing", v), "a longer key is not the key");
    }

    // ---- a string number is still a number ---------------------------------
    n = ParseServers("{\"data\":[{\"id\":\"g\",\"playing\":\"3\",\"maxPlayers\":\"10\"}]}",
                     rows, 8, name, err);
    Ok(n == 1 && rows[0].playing == 3 && rows[0].max_players == 10, "quoted numbers accepted");

    // ---- place name: old details array AND the public games?universeIds= envelope
    {
        std::string nm;
        Ok(ParsePlaceName("[{\"placeId\":2585160604,\"name\":\"Adopt Me!\",\"description\":\"x\","
                          "\"isRootPlace\":true,\"price\":null}]", nm, err), "name from place details");
        EqStr(nm, "Adopt Me!", "name is the name, not Ugc");
        Ok(ParsePlaceName("{\"data\":[{\"id\":1,\"name\":\"Adopt Me!\",\"rootPlaceId\":2}]}", nm, err) &&
           nm == "Adopt Me!", "name from games?universeIds=");
        Ok(ParsePlaceName("[{\"description\":\"name\",\"name\":\"Real\"}]", nm, err) && nm == "Real",
           "the key is matched whole, not as a substring");
        Ok(!ParsePlaceName("[]", nm, err) && !err.empty(), "an empty reply says so");
        long long uid = 0;
        Ok(ParseUniverseId("{\"universeId\":994732206}", uid, err) && uid == 994732206,
           "universe id from apis.roblox.com");
        Ok(!ParseUniverseId("{\"error\":\"no\"}", uid, err) && !err.empty(),
           "a universe reply without an id is refused");
    }

    // ---- the row label: head + tail, never a silently cut guid --------------
    {
        char out[20];
        Ok(ShortGuid("6a5f1c2e-0b47-4c1a-9f3d-7d2e5b1a0c99", out, sizeof(out)), "a guid fits a row");
        EqStr(out, "6a5f1c..0c99", "shortened to head and tail");
        Ok(ShortGuid("short", out, sizeof(out)) && std::string(out) == "short", "a short id is kept whole");
        char tiny[8];
        Ok(!ShortGuid("6a5f1c2e-0b47-4c1a-9f3d-7d2e5b1a0c99", tiny, sizeof(tiny)),
           "a buffer too small is refused, not half-written");
        Ok(tiny[0] == '\0', "and leaves nothing to misread");
    }

    (void)g_fail_local_dummy;
    if (g_fail) { printf("%d of %d checks FAILED\n", g_fail, g_checks); return 1; }
    printf("all ok  (%d checks)\n", g_checks);
    return 0;
}
