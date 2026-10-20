// -----------------------------------------------------------------------------
//  platform/streamproof_watcher.cpp — /proc capturer scan (see header)
// -----------------------------------------------------------------------------
#include "streamproof_watcher.h"

#ifdef __linux__

#include <dirent.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <time.h>
#include <vector>

namespace streamproof {
namespace {

// Built-in capturer names. Whole-comm match OR a hyphen/dot/underscore token
// match (so "wf-recorder" matches its comm exactly and "com.obsproject.Studio"
// matches on "obsproject"). Operators extend via globals::streamproof_apps.
const char* const kDefaultApps[] = {
    "obs", "obsproject", "vlc", "mpv", "ffmpeg",
    "simplescreenrecorder", "wf-recorder", "gpu-screen-recorder",
    "kazam", "peek", "vokoscreen", "kdenlive", "screenstudio",
};

uint64_t g_last_scan_ms = 0;
bool g_last_result = false;
char g_last_name[64] = "";

bool EqNoCase(const char* a, size_t alen, const char* b)
{
    size_t blen = std::strlen(b);
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Any '-'/'_'/'.'/'/'/' ' token of haystack equals name, or the whole haystack
// equals name (covers exact comm matches like "wf-recorder").
bool NameIn(const std::string& haystack, const char* name)
{
    if (EqNoCase(haystack.data(), haystack.size(), name)) return true;
    size_t i = 0;
    while (i < haystack.size()) {
        while (i < haystack.size() &&
               (haystack[i] == '-' || haystack[i] == '_' || haystack[i] == '.' ||
                haystack[i] == '/' || haystack[i] == ':' || haystack[i] == ' ' ||
                haystack[i] == '\0' || haystack[i] == '\t'))
            ++i;
        size_t start = i;
        while (i < haystack.size() &&
               !(haystack[i] == '-' || haystack[i] == '_' || haystack[i] == '.' ||
                 haystack[i] == '/' || haystack[i] == ':' || haystack[i] == ' ' ||
                 haystack[i] == '\0' || haystack[i] == '\t'))
            ++i;
        if (i > start && EqNoCase(haystack.data() + start, i - start, name))
            return true;
    }
    return false;
}

bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

uint64_t NowMonoMs()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

} // namespace

bool CapturerRunning(const char* extra_apps)
{
    const uint64_t now = NowMonoMs();
    if (g_last_scan_ms != 0 && now - g_last_scan_ms < 2000)
        return g_last_result;
    g_last_scan_ms = now;
    g_last_result = false;
    g_last_name[0] = '\0';

    std::vector<std::string> names;
    for (const char* n : kDefaultApps) names.push_back(n);
    if (extra_apps && extra_apps[0]) {
        std::string list(extra_apps), cur;
        for (size_t i = 0; i <= list.size(); ++i) {
            if (i == list.size() || list[i] == ';') {
                if (!cur.empty()) names.push_back(cur);
                cur.clear();
            } else if (list[i] != ' ') {
                cur.push_back(list[i]);
            }
        }
    }

    DIR* d = opendir("/proc");
    if (!d) return false;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        const std::string base = std::string("/proc/") + e->d_name;
        std::string comm, cmdline;
        if (!ReadFile(base + "/comm", comm)) continue;
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r'))
            comm.pop_back();
        ReadFile(base + "/cmdline", cmdline);
        for (char& c : cmdline) if (c == '\0') c = ' ';
        const std::string haystack = comm + " " + cmdline;
        for (const std::string& n : names) {
            if (NameIn(haystack, n.c_str())) {
                g_last_result = true;
                snprintf(g_last_name, sizeof(g_last_name), "%s", n.c_str());
                closedir(d);
                return true;
            }
        }
    }
    closedir(d);
    return false;
}

const char* CapturerName() { return g_last_name; }

const char* DefaultApps()
{
    static std::string s;
    if (s.empty()) {
        for (const char* n : kDefaultApps) {
            if (!s.empty()) s += ";";
            s += n;
        }
    }
    return s.c_str();
}

} // namespace streamproof

#else // !__linux__

namespace streamproof {
bool CapturerRunning(const char*) { return false; }
const char* CapturerName() { return ""; }
const char* DefaultApps() { return ""; }
}

#endif
