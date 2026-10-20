// ----------------------------------------------------------------------------
//  platform/sober_scanner.cpp — P0+ P1 plug-and-play + P2 scanning scaffold
// ----------------------------------------------------------------------------
//  Attachment uses shared read-only ELF inspection, not comm/cgroup ranking.
//  Object layout remains unresolved until independently validated. No guessed
//  scheduler anchors, Windows offsets, nsenter or /proc/self/maps fallbacks.
// ----------------------------------------------------------------------------

#include "sober_scanner.h"

#ifdef __linux__

#include "linux_elf.h"
#include <sys/stat.h>

#include <cerrno>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <unistd.h>
#include <utility>

namespace sober {

namespace {

// --- helpers ---------------------------------------------------------------

std::string TrimLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

// Parse a single /proc/<pid>/maps line without sscanf.
// Format: "00400000-00409000 r-xp 00000000 08:01 12345 /path"
// We need start, end, perms, path (optional).
bool ParseMapsLine(const std::string& line, uintptr_t& start, uintptr_t& end, std::string& perms, std::string& path) {
    start = 0; end = 0; perms.clear(); path.clear();
    // Find '-' separating start-end
    size_t dash = line.find('-');
    if (dash == std::string::npos) return false;
    size_t sp1 = line.find(' ', dash);
    if (sp1 == std::string::npos) return false;
    std::string s_start = line.substr(0, dash);
    std::string s_end = line.substr(dash + 1, sp1 - dash - 1);
    try {
        start = (uintptr_t)std::stoull(s_start, nullptr, 16);
        end   = (uintptr_t)std::stoull(s_end, nullptr, 16);
    } catch (...) { return false; }
    // perms is next token
    size_t p1 = line.find_first_not_of(' ', sp1);
    if (p1 == std::string::npos) return false;
    size_t p2 = line.find(' ', p1);
    if (p2 == std::string::npos) return false;
    perms = line.substr(p1, p2 - p1);
    // Skip offset, dev, inode (3 tokens)
    size_t cur = p2;
    for (int i = 0; i < 3; ++i) {
        cur = line.find_first_not_of(' ', cur);
        if (cur == std::string::npos) return true; // anon line with no path
        size_t nxt = line.find(' ', cur);
        if (nxt == std::string::npos) return true;
        cur = nxt;
    }
    cur = line.find_first_not_of(' ', cur);
    if (cur != std::string::npos) {
        path = TrimLeft(line.substr(cur));
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------- MapRegions

std::vector<MapRegion> MapRegions(uint32_t pid, bool* denied_out) {
    std::vector<MapRegion> out;
    if (denied_out) *denied_out = false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/maps", pid);
    std::ifstream f(path);
    if (!f) {
        // Permission denied opening maps → yama ptrace_scope (or MAC) is
        // blocking PTRACE_MODE_READ on this pid. Callers must distinguish
        // this from "libroblox not mapped" (which would be a true flatpak-ns
        // or still-starting symptom) so the operator gets the right fix.
        if (denied_out) {
            if (errno == EACCES || errno == EPERM) {
                *denied_out = true;
            } else if (errno == ENOENT || errno == ESRCH) {
                // pid gone — not a permission error, leave false
            }
        }
        return out;
    }
    std::string line;
    while (std::getline(f, line)) {
        uintptr_t start = 0, end = 0;
        std::string perms, mpath;
        if (!ParseMapsLine(line, start, end, perms, mpath)) continue;
        MapRegion r;
        r.start = start;
        r.end = end;
        // maps offset is a separate token; preserve it for ELF header selection.
        std::istringstream tokens(line);
        std::string range, permissions, offset;
        tokens >> range >> permissions >> offset;
        try { r.offset = std::stoull(offset, nullptr, 16); } catch (...) { continue; }
        r.perms = perms;
        r.path = mpath;
        out.push_back(std::move(r));
    }
    return out;
}

// ---------------------------------------------------------------- FindSoberPid

namespace {
uintptr_t VerifiedBase(uint32_t pid, const std::vector<MapRegion>& regions) {
    uintptr_t found = 0;
    for (const auto& region : regions) {
        if (region.offset || region.perms.empty() || region.perms[0] != 'r' ||
            region.path.empty() || region.path[0] != '/') continue;
        // File/memfd labels are not identity. Read the ELF itself, including
        // unnamed/deleted mappings, using exactly the diagnostic's inspector.
        auto image = linux_elf::Inspect(pid, region.start);
        if (!image.valid || image.soname != "libroblox.so") continue;
        bool executable = false;
        for (const auto& code : image.executable_ranges)
            for (const auto& mapping : regions)
                if (mapping.perms.find('x') != std::string::npos &&
                    mapping.start < code.second && mapping.end > code.first)
                    executable = true;
        if (!executable) continue;
        if (found && found != image.bias) return 0; // Ambiguous images: fail closed.
        found = image.bias;
    }
    return found;
}
}

uint32_t FindSoberPid() {
    DIR* directory = opendir("/proc");
    if (!directory) return 0;
    uint32_t found = 0;
    while (auto* entry = readdir(directory)) {
        char* end = nullptr;
        unsigned long value = std::strtoul(entry->d_name, &end, 10);
        if (*end || !value || value > UINT32_MAX) continue;
        const auto pid = static_cast<uint32_t>(value);
        struct stat st{};
        const auto root = "/proc/" + std::to_string(pid);
        if (stat(root.c_str(), &st) || st.st_uid != getuid()) continue;
        // Start-time snapshots prevent accepting an exited/reused process.
        auto start = ProcessStartTicks(pid);
        auto regions = MapRegions(pid);
        if (!VerifiedBase(pid, regions) || start.empty() || start != ProcessStartTicks(pid)) continue;
        if (found && found != pid) { found = 0; break; }
        found = pid;
    }
    closedir(directory);
    return found;
}

uintptr_t LibRobloxBase(uint32_t pid, bool* denied_out) {
    auto start = ProcessStartTicks(pid);
    auto regions = MapRegions(pid, denied_out);
    auto base = VerifiedBase(pid, regions);
    return !start.empty() && start == ProcessStartTicks(pid) ? base : 0;
}

std::string ProcessStartTicks(uint32_t pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string line; std::getline(file, line);
    auto close = line.rfind(')');
    if (close == std::string::npos) return {};
    std::istringstream fields(line.substr(close + 1));
    std::string value;
    for (int field = 3; field <= 22; ++field) if (!(fields >> value)) return {};
    return value;
}

// ---------------------------------------------------------------- InReadableRegion

bool InReadableRegion(uintptr_t addr, const std::vector<MapRegion>& regions) {
    for (auto& r : regions) {
        if (addr >= r.start && addr < r.end) {
            return !r.perms.empty() && r.perms[0] == 'r';
        }
    }
    return false;
}

// ---------------------------------------------------------------- TryPooledString — stub for now

std::string TryPooledString(void* /*platform_handle*/, uintptr_t /*container*/) {
    // Pooled string decode needs StringPool base from Sig1 neighbourhood.
    // Until Sig1 is proven we cannot decode pool — caller falls back to std::string.
    return {};
}

// ---------------------------------------------------------------- Scan — P0+ heuristic

ScanResult Scan(void*, uint32_t pid, uintptr_t libBase) {
    ScanResult result;
    result.diag = "scan: verified ELF attachment pid=" + std::to_string(pid) +
                  "; no validated Linux object-layout resolver for this image. "
                  "Windows/static anchor fallback disabled.";
    if (!pid || !libBase) result.diag = "scan: no verified Linux attachment";
    return result;
}

} // namespace sober

#else // !__linux__

namespace sober {
uint32_t FindSoberPid() { return 0; }
uintptr_t LibRobloxBase(uint32_t, bool*) { return 0; }
std::string ProcessStartTicks(uint32_t) { return {}; }
std::vector<MapRegion> MapRegions(uint32_t, bool*) { return {}; }
ScanResult Scan(void*, uint32_t, uintptr_t) { return {}; }
std::string TryPooledString(void*, uintptr_t) { return {}; }
bool InReadableRegion(uintptr_t, const std::vector<MapRegion>&) { return false; }
}

#endif
