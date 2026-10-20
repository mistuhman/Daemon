// ----------------------------------------------------------------------------
//  platform/sober_scanner.cpp — P0+ P1 plug-and-play + P2 scanning scaffold
// ----------------------------------------------------------------------------
//  This file compiles on Linux without Sober running. Scan() starts as a
//  heuristic chain walk using known UC offsets relative to libBase, then
//  falls back to pattern scan when those move. No signature bytes are
//  vendored as hard constants until void-a proves one (LINUX_PORT_PLAN §6.2).
//
//  Build check (Debian 12 ~= ubuntu-latest):
//    g++ -std=c++17 -fsyntax-only -I roblox_external roblox_external/platform/sober_scanner.cpp
//    g++ -std=c++17 -fsyntax-only -I roblox_external roblox_external/platform/platform_linux.cpp
// ----------------------------------------------------------------------------

#include "sober_scanner.h"

#ifdef __linux__

#include "platform.h"

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

namespace sober {

namespace {

// --- helpers ---------------------------------------------------------------

bool EndsWith(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string TrimLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

std::string ReadComm(uint32_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    std::ifstream f(path);
    if (!f) return {};
    std::string c;
    std::getline(f, c);
    while (!c.empty() && (c.back() == '\n' || c.back() == '\r')) c.pop_back();
    return c;
}

bool MapsHasLibRoblox(uint32_t pid) {
    auto regs = MapRegions(pid);
    for (auto& r : regs) if (EndsWith(r.path, "libroblox.so")) return true;
    return false;
}

bool MapsHasLibBadCpu(uint32_t pid) {
    auto regs = MapRegions(pid);
    for (auto& r : regs) if (EndsWith(r.path, "libbadcpu.so")) return true;
    return false;
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

// Read an absolute hex value from string without sscanf
bool ParseHex(const std::string& s, uintptr_t& out) {
    try {
        size_t idx = 0;
        out = (uintptr_t)std::stoull(s, &idx, 16);
        return idx > 0;
    } catch (...) { return false; }
}

} // namespace

// ---------------------------------------------------------------- MapRegions

std::vector<MapRegion> MapRegions(uint32_t pid) {
    std::vector<MapRegion> out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/maps", pid);
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        uintptr_t start = 0, end = 0;
        std::string perms, mpath;
        if (!ParseMapsLine(line, start, end, perms, mpath)) continue;
        MapRegion r;
        r.start = start;
        r.end = end;
        r.perms = perms;
        r.path = mpath;
        out.push_back(std::move(r));
    }
    return out;
}

// ---------------------------------------------------------------- FindSoberPid

uint32_t FindSoberPid() {
    DIR* d = opendir("/proc");
    if (!d) return 0;

    uint32_t best = 0;
    size_t bestDepth = 0;

    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        uint32_t pid = static_cast<uint32_t>(std::strtoul(e->d_name, nullptr, 10));
        if (pid == 0) continue;

        std::string comm = ReadComm(pid);
        // Void Flatpak Sober proof: outer supervisor is often \"bwrap\" with
        // cgroup org.vinegarhq.Sober; inner may be sober/app_process/Roblox.
        // Also check cmdline/exe for sober marker.
        std::string cmdline;
        {
            char cpath[64];
            snprintf(cpath, sizeof(cpath), "/proc/%u/cmdline", pid);
            std::ifstream cf(cpath);
            if (cf) {
                std::string raw((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
                for (char& ch : raw) if (ch == '\0') ch = ' ';
                cmdline = raw;
            }
        }
        std::string exeLink;
        {
            char epath[64];
            snprintf(epath, sizeof(epath), "/proc/%u/exe", pid);
            char buf[1024] = {};
            ssize_t n = readlink(epath, buf, sizeof(buf)-1);
            if (n > 0) exeLink = std::string(buf, (size_t)n);
        }

        bool commMatch = (comm == "sober" || comm == "Sober" || comm == "SOBER" ||
                          comm == "app_process" ||
                          comm == "RobloxPlayerBeta" || comm.rfind("RobloxPlayerBeta", 0) == 0 ||
                          comm == "Roblox" || comm == "bwrap" || comm == "flatpak-bwrap");
        // Also match via cmdline/exe: contains sober/libroblox/Roblox (covers bwrap + Wine)
        bool extraMatch = false;
        if (!commMatch) {
            if (cmdline.find("sober") != std::string::npos ||
                cmdline.find("Sober") != std::string::npos ||
                cmdline.find("org.vinegarhq.Sober") != std::string::npos ||
                exeLink.find("sober") != std::string::npos ||
                exeLink.find("Sober") != std::string::npos) {
                extraMatch = true;
            }
        }

        // Score every bwrap/Roblox-like candidate via cgroup + libroblox visibility
        bool isCandidate = commMatch || extraMatch;
        // Even if comm doesn't match, allow cgroup-based score (bwrap hiding)
        size_t depth = 0;
        bool cgroupSober = false;
        {
            char cpath[64];
            snprintf(cpath, sizeof(cpath), "/proc/%u/cgroup", pid);
            std::ifstream cf(cpath);
            if (cf) {
                std::string cg((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
                if (cg.find("org.vinegarhq.Sober") != std::string::npos) { depth += 100; cgroupSober = true; }
                if (cg.find("flatpak") != std::string::npos) depth += 10;
                if (cg.find("sober") != std::string::npos) depth += 20;
            }
        }
        if (cgroupSober) isCandidate = true;
        if (!isCandidate) continue;

        if (MapsHasLibRoblox(pid)) depth += 50;
        if (MapsHasLibBadCpu(pid)) depth += 50;
        // Bonus for exe/cmdline sober marker
        if (extraMatch) depth += 30;
        if (commMatch) depth += 5;
        // Ensure at least 1 so cgroup-only picks are valid
        if (depth == 0) depth = 1;
        if (depth > bestDepth || best == 0) {
            best = pid;
            bestDepth = depth;
        }
    }
    closedir(d);
    return best;
}

// ---------------------------------------------------------------- LibRobloxBase

uintptr_t LibRobloxBase(uint32_t pid) {
    auto regs = MapRegions(pid);
    for (auto& r : regs) {
        if (r.perms == "r-xp" && EndsWith(r.path, "libroblox.so")) {
            return r.start;
        }
    }
    // Flatpak bwrap hides libroblox.so from host maps — try mount-ns via /proc/pid/root
    {
        char ns_path[128];
        snprintf(ns_path, sizeof(ns_path), "/proc/%u/root/proc/%u/maps", pid, pid);
        std::ifstream nf(ns_path);
        if (nf) {
            std::string line;
            while (std::getline(nf, line)) {
                uintptr_t start = 0, end = 0;
                std::string perms, mpath;
                if (!ParseMapsLine(line, start, end, perms, mpath)) continue;
                if (perms == "r-xp" && EndsWith(mpath, "libroblox.so")) return start;
            }
        }
    }
    // Last resort: nsenter (may need util-linux, no sudo — may fail silently)
    {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "nsenter -m -t %u cat /proc/self/maps 2>/dev/null | grep -m1 'r-xp.*libroblox.so' | cut -d- -f1", pid);
        FILE* f = popen(cmd, "r");
        if (f) {
            char buf[32] = {};
            if (fgets(buf, sizeof(buf), f)) {
                // Trim
                std::string s(buf);
                while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
                while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
                uintptr_t v = 0;
                if (ParseHex(s, v) && v != 0) { pclose(f); return v; }
            }
            pclose(f);
        }
    }
    return 0;
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

ScanResult Scan(void* platform_handle, uint32_t pid, uintptr_t libBase) {
    ScanResult r;
    std::ostringstream diag;

    if (pid == 0 || libBase == 0) {
        diag << "scan: no pid or no libBase (pid=" << pid << " libBase=0x" << std::hex << libBase << std::dec << ")";
        r.diag = diag.str();
        return r;
    }

    // P1 heuristic: try known UC offsets relative to libBase as TaskScheduler anchor.
    // From UC 771298 (idom) and LINUX_PORT_PLAN §6.2: TaskScheduler @ 0x7a0b720 on that build.
    // Sober builds move this ~tens of KB per bump; if validation fails we fall back to
    // pattern scan (future) and report diag. The chain walk itself is the value.
    constexpr uintptr_t kKnownTaskSchedulerOffsets[] = {
        0x7a0b720, 0x7b00000, 0x7a00000, 0x8bdd8e8 // last is Windows fallback for Wine testing
    };

    // Offsets for Sober job table (UC, stable across bumps per thread)
    constexpr uintptr_t kJobStartOff = 0xc8;
    constexpr uintptr_t kJobEndOff   = 0xd0;
    constexpr uintptr_t kJobNameOff  = 0x18;
    // RenderJob fields (UC Sober)
    constexpr uintptr_t kRenderJobRealDataModelOff = 0x1f0;
    constexpr uintptr_t kRenderJobFakeDataModelOff = 0x38;

    diag << "scan: pid=" << pid << " libBase=0x" << std::hex << libBase << std::dec << " | ";

    uintptr_t taskScheduler = 0;
    uintptr_t taskSchedulerPtr = 0;
    bool foundTask = false;

    for (uintptr_t off : kKnownTaskSchedulerOffsets) {
        uintptr_t ptrAddr = libBase + off;
        uintptr_t candidate = 0;
        if (platform_handle && platform::ReadMem(platform_handle, ptrAddr, &candidate, sizeof(candidate)) && candidate != 0) {
            // Validate: candidate should be heap (rw-p) and have sane job vector
            auto regs = MapRegions(pid);
            if (!InReadableRegion(candidate, regs)) {
                diag << "task@0x" << std::hex << ptrAddr << std::dec << "=0x" << std::hex << candidate << std::dec << " not readable; ";
                continue;
            }
            // Try to read JobStart/JobEnd
            uintptr_t jobStart = 0, jobEnd = 0;
            if (platform::ReadMem(platform_handle, candidate + kJobStartOff, &jobStart, sizeof(jobStart)) &&
                platform::ReadMem(platform_handle, candidate + kJobEndOff, &jobEnd, sizeof(jobEnd)) &&
                jobStart != 0 && jobEnd != 0 && jobStart < jobEnd && (jobEnd - jobStart) < 0x100000) {
                taskScheduler = candidate;
                taskSchedulerPtr = ptrAddr;
                foundTask = true;
                diag << "taskScheduler 0x" << std::hex << taskScheduler << std::dec << " via ptr@0x" << std::hex << ptrAddr << std::dec << " (off 0x" << std::hex << off << std::dec << ") | ";
                break;
            } else {
                diag << "task candidate 0x" << std::hex << candidate << std::dec << " job vector bad; ";
            }
        } else {
            diag << "ptr@0x" << std::hex << ptrAddr << std::dec << " unreadable; ";
        }
    }

    if (!foundTask) {
        diag << "no TaskScheduler anchor — need Sig1 pattern scan (run sober_sig_refresh.py) | hint: check process_vm_readv perms (sudo or echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope) and verify libBase is libroblox.so r-xp (not exe ELF first mapping 0x457F)";
        r.diag = diag.str();
        return r;
    }

    r.taskScheduler = taskScheduler;

    // Walk job table for RenderJob
    uintptr_t jobStart = 0, jobEnd = 0;
    if (!platform::ReadMem(platform_handle, taskScheduler + kJobStartOff, &jobStart, sizeof(jobStart)) ||
        !platform::ReadMem(platform_handle, taskScheduler + kJobEndOff, &jobEnd, sizeof(jobEnd))) {
        diag << "job table unreadable";
        r.diag = diag.str();
        return r;
    }

    uintptr_t renderJob = 0;
    for (uintptr_t cur = jobStart; cur < jobEnd; cur += 8) {
        uintptr_t job = 0;
        if (!platform::ReadMem(platform_handle, cur, &job, sizeof(job))) break;
        if (job == 0) continue;
        uintptr_t namePtr = 0;
        if (!platform::ReadMem(platform_handle, job + kJobNameOff, &namePtr, sizeof(namePtr))) continue;
        if (namePtr == 0) continue;
        char nameBuf[32] = {};
        if (!platform::ReadMem(platform_handle, namePtr, nameBuf, sizeof(nameBuf)-1)) continue;
        // Compare name
        if (std::string(nameBuf) == "RenderJob" || std::string(nameBuf).rfind("Render", 0) == 0) {
            renderJob = job;
            break;
        }
    }

    if (!renderJob) {
        diag << "RenderJob not in table (scanned " << (jobEnd - jobStart)/8 << " jobs)";
        r.diag = diag.str();
        return r;
    }
    r.renderJob = renderJob;
    diag << "renderJob 0x" << std::hex << renderJob << std::dec << " | ";

    // RenderJob -> DataModel
    uintptr_t dataModel = 0;
    if (platform::ReadMem(platform_handle, renderJob + kRenderJobRealDataModelOff, &dataModel, sizeof(dataModel)) && dataModel != 0) {
        r.dataModel = dataModel;
        diag << "dataModel 0x" << std::hex << dataModel << std::dec << " | ";
    } else {
        // Try fake path
        uintptr_t fake = 0;
        if (platform::ReadMem(platform_handle, renderJob + kRenderJobFakeDataModelOff, &fake, sizeof(fake)) && fake != 0) {
            diag << "realDataModel miss, fake 0x" << std::hex << fake << std::dec << " ";
            // Fake -> Real via 0x1f8 on some builds
            uintptr_t real = 0;
            if (platform::ReadMem(platform_handle, fake + 0x1f8, &real, sizeof(real)) && real != 0) {
                r.dataModel = real;
                diag << "real 0x" << std::hex << real << std::dec << " | ";
            }
        }
    }

    if (!r.dataModel) {
        diag << "dataModel not found";
        r.diag = diag.str();
        return r;
    }

    // DataModel -> Workspace (offset varies per build: try known ones)
    constexpr uintptr_t kWorkspaceOffsets[] = { 0x160, 0x1a8, 0x180 };
    for (uintptr_t off : kWorkspaceOffsets) {
        uintptr_t ws = 0;
        if (platform::ReadMem(platform_handle, r.dataModel + off, &ws, sizeof(ws)) && ws != 0) {
            // Validate by reading class name pointer? Simple: non-zero and readable
            auto regs = MapRegions(pid);
            if (InReadableRegion(ws, regs)) {
                r.workspace = ws;
                diag << "workspace 0x" << std::hex << ws << std::dec << " via off 0x" << std::hex << off << std::dec << " | ";
                break;
            }
        }
    }

    if (!r.workspace) {
        diag << "workspace not found (tried 0x160/0x1a8/0x180)";
        r.diag = diag.str();
        return r;
    }

    // Workspace -> World
    constexpr uintptr_t kWorldOffsets[] = { 0x410, 0x4d0, 0x4b8 };
    for (uintptr_t off : kWorldOffsets) {
        uintptr_t w = 0;
        if (platform::ReadMem(platform_handle, r.workspace + off, &w, sizeof(w)) && w != 0) {
            auto regs = MapRegions(pid);
            if (InReadableRegion(w, regs)) {
                r.world = w;
                diag << "world 0x" << std::hex << w << std::dec << " via off 0x" << std::hex << off << std::dec << " | ";
                break;
            }
        }
    }

    if (!r.world) {
        diag << "world not found";
        r.diag = diag.str();
        return r;
    }

    // World -> Primitives (optional for initial bring-up)
    constexpr uintptr_t kPrimOffsets[] = { 0x2b0, 0x2f0, 0x2c0 };
    for (uintptr_t off : kPrimOffsets) {
        uintptr_t p = 0;
        if (platform::ReadMem(platform_handle, r.world + off, &p, sizeof(p)) && p != 0) {
            r.primitives = p;
            diag << "primitives container 0x" << std::hex << p << std::dec << " off 0x" << std::hex << off << std::dec;
            break;
        }
    }

    // Primitive motion fields are stable per repo (stride 0x48)
    r.primitiveRotation = 0xb0;
    r.primitivePosition = 0xd4;
    r.primitiveFlags = 0x1be;

    r.diag = diag.str();
    return r;
}

} // namespace sober

#else // !__linux__

namespace sober {
uint32_t FindSoberPid() { return 0; }
uintptr_t LibRobloxBase(uint32_t) { return 0; }
std::vector<MapRegion> MapRegions(uint32_t) { return {}; }
ScanResult Scan(void*, uint32_t, uintptr_t) { return {}; }
std::string TryPooledString(void*, uintptr_t) { return {}; }
bool InReadableRegion(uintptr_t, const std::vector<MapRegion>&) { return false; }
}

#endif
