#pragma once
// -----------------------------------------------------------------------------
//  features/servers/servers_json.h  --  reading the two Roblox web replies
// -----------------------------------------------------------------------------
//  Header-only, and deliberately free of Windows and of memory reads, because this
//  is the part of server hopping that CAN be tested without a game, a GPU or a
//  network: glass-obsidian/tools/servers_json_test.cpp runs it over fixtures copied
//  from the documented replies. Everything about the *shape* of these answers is
//  Roblox's to change, and the two choices baked in here follow from that:
//
//    * a reply that does not parse yields a message, never a silent empty list -
//      "no servers" and "I could not read the reply" have to be tellable apart;
//    * a server row whose player count could not be read is dropped rather than
//      shown as 0/0, because "HOP TO AN OPEN SERVER" picks on that number.
//
//  GET games.roblox.com/v1/games/{placeId}/servers/Public?limit=100
//      {"previousPageCursor":null,"nextPageCursor":"eyJvZmZzZXQiOjEwMH0",
//       "data":[{"id":"0f2b...-guid","maxPlayers":40,"playing":12,
//                "players":[{"id":"1","username":null,"displayName":null}],
//                "serverType":"Public","visitedPlayers":30,
//                "place":{"id":2585160604,"name":"Build a Boat..."},"linkedPlaceId":null}]}
//
//  GET apis.roblox.com/universes/v1/places/{placeId}/universe
//      {"universeId":994732206}
//  GET games.roblox.com/v1/games?universeIds={universeId}
//      {"data":[{"id":994732206,"name":"Build a Boat for Treasure","rootPlaceId":2585160604}]}
//
//  multiget-place-details is the other documented name source, but it answers
//  401 / code 9002 ("Authentication Token is missing") without a .ROBLOSECURITY
//  cookie. This project does not send cookies, so the two public endpoints above
//  are the ones we actually call. ParsePlaceName still accepts either shape.
//
//  Note which id goes into which URL: the servers endpoint is keyed by PLACE id,
//  not by universe/game id. (The experience name used to come from the datamodel's
//  instance name, which is the DataModel *kind* - "Ugc", "Game" - and never was the
//  name you see on the website.)
// -----------------------------------------------------------------------------

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace features {
namespace serversjson {

enum { kIdLen = 40 };   // a gameInstanceId is a 36-char guid, + NUL, with slack

struct ServerRow
{
    char id[kIdLen];
    int  playing;
    int  max_players;
};

// -----------------------------------------------------------------------------
//  readers
// -----------------------------------------------------------------------------
// Value of "key": "..." inside body[from,to). Escapes honoured, including \uXXXX
// with surrogate pairs - display names carry emoji, and a half-decoded name is a
// name you do not recognise as yours.
inline bool Str(const std::string& body, size_t from, size_t to,
                const char* key, std::string& out)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = body.find(pat, from);
    if (p == std::string::npos || p >= to) return false;
    p = body.find(':', p + pat.size());
    if (p == std::string::npos || p >= to) return false;
    ++p;
    while (p < to && (body[p] == ' ' || body[p] == '\t' ||
                      body[p] == '\n' || body[p] == '\r')) ++p;
    if (p >= to || body[p] != '"') return false;

    out.clear();
    ++p;
    while (p < to) {
        const char c = body[p];
        if (c == '"') return true;
        if (c == '\\' && p + 1 < to) {
            const char e = body[p + 1];
            if (e != 'u') { out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' :
                                                  e == 'r' ? '\r' : e); p += 2; continue; }
            unsigned cp = 0;
            size_t q = p + 2;
            for (int k = 0; k < 4; ++k, ++q) {
                if (q >= to || !isxdigit((unsigned char)body[q])) { cp = 0xFFFFFFFFu; break; }
                const char h = (char)tolower((unsigned char)body[q]);
                cp = cp * 16u + (unsigned char)(isdigit((unsigned char)h) ? h - '0' : h - 'a' + 10);
            }
            if (cp > 0xFFFFu) { out.push_back(e); p += 2; continue; }   // bad escape: keep it visible
            // A high surrogate only means something with its pair right behind it;
            // without the pair it is emitted as the replacement character.
            bool paired = false;
            if (cp >= 0xD800u && cp <= 0xDBFFu && body.compare(q, 2, "\\u") == 0) {
                unsigned lo = 0; size_t r = q + 2; int k = 0;
                for (; k < 4 && r < to; ++k, ++r) {
                    const char h = (char)tolower((unsigned char)body[r]);
                    if (!isxdigit((unsigned char)h)) break;
                    lo = lo * 16u + (unsigned)(isdigit((unsigned char)h) ? h - '0' : h - 'a' + 10);
                }
                if (k == 4 && lo >= 0xDC00u && lo <= 0xDFFFu) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    q = r; paired = true;
                }
            }
            if (!paired && cp >= 0xD800u && cp <= 0xDFFFu) cp = 0xFFFDu;
            if (cp < 0x80u) {
                out.push_back((char)cp);
            } else if (cp < 0x800u) {
                out.push_back((char)(0xC0u | (cp >> 6)));
                out.push_back((char)(0x80u | (cp & 0x3Fu)));
            } else if (cp < 0x10000u) {
                out.push_back((char)(0xE0u | (cp >> 12)));
                out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
                out.push_back((char)(0x80u | (cp & 0x3Fu)));
            } else {
                out.push_back((char)(0xF0u | (cp >> 18)));
                out.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
                out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
                out.push_back((char)(0x80u | (cp & 0x3Fu)));
            }
            p = q;
            continue;
        }
        out.push_back(c);
        ++p;
    }
    return false;                                   // string never closed
}

// Value of "key": 123 (also accepts a quoted number, and null -> false).
inline bool Num(const std::string& body, size_t from, size_t to,
                const char* key, long long& out)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = body.find(pat, from);
    if (p == std::string::npos || p >= to) return false;
    p = body.find(':', p + pat.size());
    if (p == std::string::npos || p >= to) return false;
    ++p;
    while (p < to && (body[p] == ' ' || body[p] == '\t')) ++p;
    bool neg = false;
    if (p < to && body[p] == '"') ++p;
    if (p < to && body[p] == '-') { neg = true; ++p; }
    if (p >= to || !isdigit((unsigned char)body[p])) return false;
    long long v = 0;
    for (; p < to && isdigit((unsigned char)body[p]); ++p) v = v * 10 + (body[p] - '0');
    out = neg ? -v : v;
    return true;
}

// The objects of one JSON array, each returned as its own balanced substring.
// Brace counting is string-aware: a server entry holds a `players` array of
// objects, so stopping at the next '}' would cut an entry in half.
// `array_key` names the member to look under; null takes the first top-level array.
inline bool Entries(const std::string& body, const char* array_key,
                    std::vector<std::string>& out, std::string& err)
{
    out.clear();
    size_t p = 0;
    if (array_key) {
        const std::string pat = std::string("\"") + array_key + "\"";
        p = body.find(pat);
        if (p == std::string::npos) { err = "the reply has no \"data\" array in it"; return false; }
    }
    p = body.find('[', p);
    if (p == std::string::npos) { err = "the reply has no array where one was expected"; return false; }
    ++p;

    int depth = 0;
    bool instr = false;
    size_t start = std::string::npos;
    for (; p < body.size(); ++p) {
        const char c = body[p];
        if (instr) {
            if (c == '\\') { ++p; continue; }
            if (c == '"') instr = false;
            continue;
        }
        if (c == '"') { instr = true; continue; }
        if (c == '{') { if (depth == 0) start = p; ++depth; continue; }
        if (c == '}') {
            if (depth > 0 && --depth == 0 && start != std::string::npos) {
                out.push_back(body.substr(start, p - start + 1));
                start = std::string::npos;
            }
            continue;
        }
        if (c == ']' && depth == 0) return true;
    }
    err = "the reply was cut off mid-server";
    return false;
}

// -----------------------------------------------------------------------------
//  the two replies
// -----------------------------------------------------------------------------
// Returns the number of rows written, 0 for "no public servers right now", or -1
// with `err` set when the body was not readable at all. `place_name` is filled
// from the first entry that carries one, which is why a hop and a name refresh are
// the same request.
inline int ParseServers(const std::string& body, ServerRow* rows, int max,
                        std::string& place_name, std::string& err)
{
    err.clear();
    std::vector<std::string> ents;
    if (!Entries(body, "data", ents, err)) return -1;

    int n = 0;
    for (size_t i = 0; i < ents.size() && n < max; ++i) {
        const std::string& e = ents[i];
        const size_t from = 0, to = e.size();

        std::string id;
        if (!Str(e, from, to, "id", id) || id.empty()) continue;
        long long playing = 0, maxp = 0;
        if (!Num(e, from, to, "playing", playing)) continue;
        if (!Num(e, from, to, "maxPlayers", maxp)) continue;
        if (id.size() + 1 > sizeof(rows[n].id)) {
            err = "a server id came back longer than a row can hold";
            return -1;
        }

        memcpy(rows[n].id, id.c_str(), id.size() + 1);
        rows[n].playing     = (int)playing;
        rows[n].max_players = (int)maxp;
        ++n;

        if (place_name.empty()) {
            const size_t pp = e.find("\"place\"");
            std::string nm;
            if (pp != std::string::npos && Str(e, pp, to, "name", nm) && !nm.empty())
                place_name = nm;
        }
    }
    return n;
}

// A gameInstanceId is 36 chars and a menu row is about 22: the head and the tail are
// what tell two servers apart, so keep both and drop the middle. Returns false when
// `out` is too small to hold the result, rather than writing a truncated guid that
// would read like a different server.
inline bool ShortGuid(const char* id, char* out, size_t n)
{
    if (!id || !out || n < 8) return false;
    const size_t len = strlen(id);
    if (len <= n - 1) { memcpy(out, id, len + 1); return true; }
    // 6 + ".." + 4 = 12 characters of identity, plus the NUL.
    if (len > 36 || n < 13) { out[0] = '\0'; return false; }
    snprintf(out, n, "%.6s..%.4s", id, id + len - 4);
    return true;
}

// First object's "name" in either a top-level array (old place-details) or
// {"data":[...]} (games?universeIds=). Same field, two envelopes.
inline bool ParsePlaceName(const std::string& body, std::string& name, std::string& err)
{
    err.clear();
    std::vector<std::string> ents;
    if (!Entries(body, nullptr, ents, err)) return false;
    for (size_t i = 0; i < ents.size(); ++i) {
        std::string nm;
        if (Str(ents[i], 0, ents[i].size(), "name", nm) && !nm.empty()) {
            name = nm;
            return true;
        }
    }
    err = "the place details reply carried no name";
    return false;
}

inline bool ParseUniverseId(const std::string& body, long long& universe_id, std::string& err)
{
    err.clear();
    long long v = 0;
    if (!Num(body, 0, body.size(), "universeId", v) || v <= 0) {
        err = "the universe reply carried no universeId";
        return false;
    }
    universe_id = v;
    return true;
}

} // namespace serversjson
} // namespace features
