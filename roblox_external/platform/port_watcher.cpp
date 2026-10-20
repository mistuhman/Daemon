// ----------------------------------------------------------------------------
//  platform/port_watcher.cpp — P1.5 scaffold (VOID_PLUG_AND_PLAY.md §4)
// ----------------------------------------------------------------------------
//  Scaffold only: Poll() and Tick() return "no sober" / false until the
//  operator's Void maps dump confirms /proc/net/tcp visibility on Void's
//  6.18.35-tkg-bore kernel. The real /proc/net/tcp parser is one function
//  (ParseNetTcp) — intentionally left as commented stub so review can see
//  the Yama-1 safety claim before wire-up.
//
//  Build check: g++ -fsyntax-only -I roblox_external platform/port_watcher.cpp
// ----------------------------------------------------------------------------

#ifdef __linux__

#include "port_watcher.h"
#include "sober_scanner.h"
#include "platform.h"

#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <string>

namespace portwatch {

// ---------------------------------------------------------------- Snapshot

Snapshot Poll(uint32_t sober_pid) {
    Snapshot s;
    s.pid = sober_pid;
    if (sober_pid == 0) {
        s.diag = "portwatch: no sober pid (waiting for attach)";
        return s;
    }
    // P1.5 scaffold: real poll needs /proc/<pid>/fd → socket:[ino] → /proc/net/tcp
    // parse. Stub proves the contract compiles and debug page renders without
    // needing a live Sober. Next commit fills this with:
    //
    //   auto inodes = SocketInodesForPid(sober_pid); // readdir /proc/pid/fd
    //   auto nets   = ParseNetTcp("/proc/net/tcp");   // hex ip:port → dotted, state
    //   for (n: nets) if (inodes.contains(n.inode) && n.state==0x01) s.conns.push_back(n);
    //   pick game_port = first conn with remote_ip not in 1e100 CDN + port != 80/443
    //
    // Until then, diag explains why portwatch is pending.
    s.diag = "portwatch: pid " + std::to_string(sober_pid) + " — net/tcp poll pending (P1.5 scaffold, next commit wires /proc/net/tcp)";
    return s;
}

Snapshot PollAny() {
    uint32_t pid = sober::FindSoberPid();
    return Poll(pid);
}

// ---------------------------------------------------------------- ProcessWatcher

bool ProcessWatcher::Tick(uint32_t* out_pid, uintptr_t* out_base) {
    uint32_t pid = sober::FindSoberPid();
    uintptr_t base = (pid ? sober::LibRobloxBase(pid) : 0);
    if (pid != current_pid_ || base != current_base_) {
        current_pid_ = pid;
        current_base_ = base;
        last_snap_ = Poll(pid);
        if (out_pid)  *out_pid  = pid;
        if (out_base) *out_base = base;
        return true;
    }
    // Even without pid change, refresh game_port view (teleport detection)
    if (pid) {
        auto cur = Poll(pid);
        last_snap_ = cur;
    }
    return false;
}

bool ProcessWatcher::GamePortChanged(uint32_t* old_port, uint32_t* new_port) {
    if (!last_snap_.pid) return false;
    uint32_t cur = last_snap_.game_port;
    if (cur != last_game_port_) {
        if (old_port) *old_port = last_game_port_;
        if (new_port) *new_port = cur;
        last_game_port_ = cur;
        return true;
    }
    return false;
}

std::string ProcessWatcher::DebugLine() const {
    std::ostringstream o;
    if (!current_pid_) {
        o << "attach waiting (no sober) — start Sober via flatpak run org.vinegarhq.Sober";
    } else {
        o << "attach pid=" << current_pid_
          << " base=0x" << std::hex << current_base_ << std::dec;
        // isFlatpak hint: base maps path contains /var/lib/flatpak or /app/
        // (cheap: last_snap_.diag already carries it once wired)
        if (!last_snap_.diag.empty())
            o << "  " << last_snap_.diag;
    }
    o << "  pulse=" << (portwatch::Poll(current_pid_).diag.size() ? "pending" : "ok");
    return o.str();
}

} // namespace portwatch

#else // !__linux__

namespace portwatch {
Snapshot Poll(uint32_t pid) { Snapshot s; s.pid=pid; s.diag="portwatch stub (non-linux)"; return s; }
Snapshot PollAny() { return Poll(0); }
bool ProcessWatcher::Tick(uint32_t* o, uintptr_t* b) { if(o) *o=0; if(b) *b=0; return false; }
bool ProcessWatcher::GamePortChanged(uint32_t* a,uint32_t* b){ if(a)*a=0; if(b)*b=0; return false; }
std::string ProcessWatcher::DebugLine() const { return "portwatch stub (non-linux)"; }
}

#endif
