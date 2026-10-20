// ----------------------------------------------------------------------------
//  platform/sober_scanner.cpp — scaffold, P0 (LINUX_PORT_PLAN §6/§11)
// ----------------------------------------------------------------------------
//  This file compiles on Linux without Sober running. Scan() is intentionally
//  a stub that returns diag="scanner not yet proven on void-a" until a
//  signature (Sig 1, §6.2) is captured from a real libroblox.so image on
//  void-a and pasted here. Shipping an unproven byte pattern would be worse
//  than shipping no pattern — it would silently mis-identify memory.
//
//  Next step (P1→P2): operator on void-a runs tools/sober_sig_refresh.py
//  against the Sober pid, pastes the TaskScheduler neighbourhood bytes,
//  and this file's SIG_TASKSCHEDULER_LEA is set.
//
//  Build check (this sandbox, Debian 12 ~= ubuntu-latest):
//    g++ -std=c++17 -fsyntax-only -I roblox_external roblox_external/platform/sober_scanner.cpp
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

namespace sober {

// ---------------------------------------------------------------- Sig bytes
// Sig 1 — TaskScheduler Lea (see LINUX_PORT_PLAN §6.2). NOT YET PROVEN.
// When void-a operator dumps the neighbourhood around UC's 0x7a0b720 with
// sober_sig_refresh.py, the bytes go here as "48 8D 0D ? ? ? ? ..." plus
// the lea length (7) and disp offset (3). Until then Scan() refuses.
//
// Naming: SIG_ prefix so grep -r SIG_ finds all vendored patterns in one
// place — the agent's cheap invariant for "do we have any wild vendor bytes".

// static const char SIG_TASKSCHEDULER_LEA[] = "48 8D 0D ? ? ? ? 48 8B D7 E8 ? ? ? ?";
// static constexpr int SIG_TASKSCHEDULER_LEA_LEN = 7;
// static constexpr int SIG_TASKSCHEDULER_DISP_OFF = 3;

// ---------------------------------------------------------------- helpers

std::vector<MapRegion> MapRegions(uint32_t pid) {
    std::vector<MapRegion> out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/maps", pid);
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        // format: 00400000-00409000 r-xp 00000000 08:01 12345 /path/libroblox.so
        uintptr_t start = 0, end = 0;
        char perms[8] = {};
        char mpath[512] = {};
        // sscanf is forgiving on anon lines (no path)
        int got = sscanf(line.c_str(), "%lx-%lx %7s %*x %*x:%*x %*u %511[^\n]",
                         &start, &end, perms, mpath);
        if (got < 3) continue;
        MapRegion r;
        r.start = start;
        r.end = end;
        r.perms = perms;
        if (got >= 4) r.path = mpath;
        // trim leading space from path (maps has a double space before anon)
        while (!r.path.empty() && r.path[0] == ' ') r.path.erase(0, 1);
        out.push_back(std::move(r));
    }
    return out;
}

static bool EndsWith(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string ReadComm(uint32_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    std::ifstream f(path);
    if (!f) return {};
    std::string c;
    std::getline(f, c);
    while (!c.empty() && (c.back() == '\n' || c.back() == '\r')) c.pop_back();
    return c;
}

static bool MapsHasLibRoblox(uint32_t pid) {
    auto regs = MapRegions(pid);
    for (auto& r : regs) if (EndsWith(r.path, "libroblox.so")) return true;
    return false;
}

static bool MapsHasLibBadCpu(uint32_t pid) {
    auto regs = MapRegions(pid);
    for (auto& r : regs) if (EndsWith(r.path, "libbadcpu.so")) return true;
    return false;
}

// ---------------------------------------------------------------- FindSoberPid

uint32_t FindSoberPid() {
    DIR* d = opendir("/proc");
    if (!d) return 0;

    uint32_t best = 0;
    size_t bestDepth = 0; // prefer deeper PPid chain → Flatpak inner

    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        uint32_t pid = static_cast<uint32_t>(atoi(e->d_name));
        if (pid == 0) continue;

        std::string comm = ReadComm(pid);
        // Sober-native comms (case-insensitive) and Wine fallback
        bool commMatch = (comm == "sober" || comm == "Sober" || comm == "SOBER" ||
                          comm == "app_process" || // Bionic wrapper
                          comm == "RobloxPlayerBeta" || comm.rfind("RobloxPlayerBeta", 0) == 0);
        if (!commMatch) continue;

        // Do NOT gate on MapsHasLibRoblox — Flatpak bwrap hides libroblox.so
        // from host /proc/pid/maps (your Void 24670 proved it: host maps
        // had no libroblox.so, only inside mount ns). Score it, don't
        // require. Task manager's click-to-find is just _NET_WM_PID, same
        // cheap comm check — easy as you said.
        size_t depth = 1; // any comm match counts
        if (MapsHasLibRoblox(pid)) depth += 50;
        if (MapsHasLibBadCpu(pid)) depth += 50;
        {
            char cpath[64];
            snprintf(cpath, sizeof(cpath), "/proc/%u/cgroup", pid);
            std::ifstream cf(cpath);
            std::string cg((std::istreambuf_iterator<char>(cf)),
                            std::istreambuf_iterator<char>());
            if (cg.find("org.vinegarhq.Sober") != std::string::npos) depth += 100;
            if (cg.find("flatpak") != std::string::npos) depth += 10;
        }
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
            return r.start; // first r-xp of libroblox.so = PIE load base (native)
        }
    }
    // Flatpak bwrap hides libroblox.so from host maps — try mount-ns via nsenter
    // (task manager's click uses _NET_WM_PID, same host pid, but maps live in
    // child's mount ns). Try: nsenter -m -t <pid> cat /proc/self/maps analogue
    // via reading /proc/<pid>/root/proc/<pid>/maps if available, then direct.
    // Fallback: try nsenter binary if present.
    {
        char ns_path[128];
        snprintf(ns_path, sizeof(ns_path), "/proc/%u/root/proc/%u/maps", pid, pid);
        std::ifstream nf(ns_path);
        if (nf) {
            std::string line;
            while (std::getline(nf, line)) {
                uintptr_t start=0,end=0; char perms[8]={}; char mpath[512]={};
                int got = sscanf(line.c_str(), "%lx-%lx %7s %*x %*x:%*x %*u %511[^\n]", &start,&end,perms,mpath);
                if (got < 3) continue;
                std::string mp = (got>=4)? mpath: "";
                while (!mp.empty() && mp[0]==' ') mp.erase(0,1);
                if (std::string(perms)=="r-xp" && EndsWith(mp, "libroblox.so")) return start;
            }
        }
    }
    // Last resort: try nsenter -m -t pid (needs util-linux, no sudo on Void — may fail)
    {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "nsenter -m -t %u cat /proc/self/maps 2>/dev/null | grep -m1 'r-xp.*libroblox.so' | cut -d- -f1", pid);
        FILE* f = popen(cmd, "r");
        if (f) {
            char buf[32]={};
            if (fgets(buf, sizeof(buf), f)) {
                uintptr_t v=0; sscanf(buf, "%lx", &v);
                pclose(f);
                if (v) return v;
            } else pclose(f);
        }
    }
    return 0;
}

// ---------------------------------------------------------------- InReadableRegion

bool InReadableRegion(uintptr_t addr, const std::vector<MapRegion>& regions) {
    for (auto& r : regions) {
        if (addr >= r.start && addr < r.end) {
            // readable: r--p, rw-p, r-xp all have 'r' in perms[0]
            return !r.perms.empty() && r.perms[0] == 'r';
        }
    }
    return false;
}

// ---------------------------------------------------------------- TryPooledString — stub

std::string TryPooledString(void* /*platform_handle*/, uintptr_t /*container*/) {
    // Pooled string decode needs the StringPool base from Sig 1
    // neighbourhood (same lea family as TaskScheduler). Until Sig 1 is
    // proven we cannot decode the pool — caller falls back to std::string.
    return {};
}

// ---------------------------------------------------------------- Scan — P0 stub

ScanResult Scan(void* /*platform_handle*/, uint32_t pid, uintptr_t libBase) {
    ScanResult r;
    std::ostringstream diag;
    diag << "sober_scanner P0 scaffold — no signature vendored yet (see LINUX_PORT_PLAN §6.2 P2). "
         << "pid=" << pid << " libBase=0x" << std::hex << libBase << std::dec
         << " — run tools/sober_sig_refresh.py on void-a to capture Sig 1.";
    r.diag = diag.str();
    // Intentionally ok()=false until void-a operator pastes a proven pattern.
    // Callers (game.cpp via offsets_dynamic) treat this as "keep using
    // offsets.h fallback" — same behaviour as before, no hang.
    return r;
}

} // namespace sober

#else // !__linux__

// Non-Linux: provide stubs so IDEs on Windows still parse the TU.
namespace sober {
uint32_t FindSoberPid() { return 0; }
uintptr_t LibRobloxBase(uint32_t) { return 0; }
std::vector<MapRegion> MapRegions(uint32_t) { return {}; }
ScanResult Scan(void*, uint32_t, uintptr_t) { return {}; }
std::string TryPooledString(void*, uintptr_t) { return {}; }
bool InReadableRegion(uintptr_t, const std::vector<MapRegion>&) { return false; }
}

#endif
