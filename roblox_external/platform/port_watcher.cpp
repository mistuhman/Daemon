// ----------------------------------------------------------------------------
//  platform/port_watcher.cpp — Plug-and-play + port watching (VOID_PLUG_AND_PLAY.md §4)
// ----------------------------------------------------------------------------
//  Two jobs: (1) auto-attach daemon Tick() that polls FindSoberPid every
//  250ms and reports pid/base change, (2) net/tcp poll for game server port.
//  No ptrace, no sudo: /proc/<pid>/fd + /proc/net/tcp are world-readable for
//  same-uid. Build: g++ -fsyntax-only -I roblox_external platform/port_watcher.cpp
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
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>

namespace portwatch {
namespace {

// Parse hex without sscanf
bool ParseHexU32(const std::string& s, uint32_t& out) {
    try { size_t idx=0; out = (uint32_t)std::stoul(s,&idx,16); return idx>0; } catch(...) { return false; }
}

// Convert little-endian hex IP (from /proc/net/tcp) to dotted string.
// /proc/net/tcp stores IP as 0100007F for 127.0.0.1 (little-endian per 4-byte word)
std::string HexToIp(const std::string& hex) {
    if (hex.size() != 8) return hex;
    uint32_t v = 0;
    if (!ParseHexU32(hex, v)) return hex;
    // v is little-endian: bytes reversed
    unsigned b0 = v & 0xFF;
    unsigned b1 = (v >> 8) & 0xFF;
    unsigned b2 = (v >> 16) & 0xFF;
    unsigned b3 = (v >> 24) & 0xFF;
    char buf[32];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b0,b1,b2,b3);
    return std::string(buf);
}

// Collect socket inodes owned by pid via /proc/<pid>/fd symlink read
std::vector<std::string> SocketInodesForPid(uint32_t pid) {
    std::vector<std::string> out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/fd", pid);
    DIR* d = opendir(path);
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0]=='.') continue;
        // Worst case: "/proc/" (6) + 10-digit pid + "/fd/" (4) + NAME_MAX 255 +
        // NUL = 276. 128 tripped -Wformat-truncation on gcc 14; 320 covers it.
        char linkPath[320];
        snprintf(linkPath, sizeof(linkPath), "/proc/%u/fd/%s", pid, e->d_name);
        char target[256] = {};
        ssize_t n = readlink(linkPath, target, sizeof(target)-1);
        if (n <= 0) continue;
        std::string t(target, (size_t)n);
        // format: socket:[12345]
        if (t.rfind("socket:[",0)==0) {
            size_t a = t.find('[');
            size_t b = t.find(']', a);
            if (a!=std::string::npos && b!=std::string::npos && b>a+1) {
                out.push_back(t.substr(a+1, b-a-1));
            }
        }
    }
    closedir(d);
    return out;
}

struct NetTcpEntry {
    std::string inode;
    std::string local_ip, local_port_hex;
    std::string remote_ip, remote_port_hex;
    uint32_t local_port = 0, remote_port = 0;
    std::string remote_ip_dotted;
    int state = 0;
};

std::vector<NetTcpEntry> ParseNetTcp(const char* path) {
    std::vector<NetTcpEntry> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    std::getline(f, line); // skip header
    while (std::getline(f, line)) {
        // Collapse whitespace into tokens
        std::istringstream iss(line);
        std::vector<std::string> tok;
        std::string w;
        while (iss >> w) tok.push_back(w);
        // tok[0]=sl, tok[1]=local_address, tok[2]=rem_address, tok[3]=st, tok[9]=inode (varies)
        if (tok.size() < 10) continue;
        std::string local = tok[1];
        std::string remote = tok[2];
        std::string st = tok[3];
        std::string inode = tok[9];
        // local/remote format: 0100007F:0035 (ip:port hex)
        size_t colon1 = local.find(':');
        size_t colon2 = remote.find(':');
        if (colon1==std::string::npos || colon2==std::string::npos) continue;
        std::string lip = local.substr(0, colon1);
        std::string lportHex = local.substr(colon1+1);
        std::string rip = remote.substr(0, colon2);
        std::string rportHex = remote.substr(colon2+1);
        NetTcpEntry e;
        e.inode = inode;
        e.local_ip = lip;
        e.local_port_hex = lportHex;
        e.remote_ip = rip;
        e.remote_port_hex = rportHex;
        e.remote_ip_dotted = HexToIp(rip);
        // state hex
        try { e.state = std::stoi(st, nullptr, 16); } catch(...) { e.state = 0; }
        uint32_t lp=0, rp=0;
        ParseHexU32(lportHex, lp);
        ParseHexU32(rportHex, rp);
        e.local_port = lp;
        e.remote_port = rp;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------- Snapshot

Snapshot Poll(uint32_t sober_pid) {
    Snapshot s;
    s.pid = sober_pid;
    if (sober_pid == 0) {
        s.diag = "portwatch: no sober pid (waiting for attach)";
        return s;
    }

    auto inodes = SocketInodesForPid(sober_pid);
    if (inodes.empty()) {
        // No sockets yet (lobby) or permission? Check if pid alive
        char alivePath[64];
        snprintf(alivePath, sizeof(alivePath), "/proc/%u/comm", sober_pid);
        std::ifstream af(alivePath);
        if (!af) {
            s.diag = "portwatch: pid vanished";
            return s;
        }
        s.diag = "portwatch: pid " + std::to_string(sober_pid) + " — no sockets (lobby/menu)";
        return s;
    }

    auto tcp4 = ParseNetTcp("/proc/net/tcp");
    auto tcp6 = ParseNetTcp("/proc/net/tcp6");

    // Filter to entries whose inode is owned by pid and state ESTABLISHED (0x01)
    std::vector<NetTcpEntry> owned;
    for (auto& e : tcp4) {
        if (e.state != 0x01) continue;
        if (std::find(inodes.begin(), inodes.end(), e.inode) != inodes.end()) {
            owned.push_back(e);
        }
    }
    for (auto& e : tcp6) {
        if (e.state != 0x01) continue;
        if (std::find(inodes.begin(), inodes.end(), e.inode) != inodes.end()) {
            owned.push_back(e);
        }
    }

    for (auto& e : owned) {
        SocketInfo si;
        si.local_port = e.local_port;
        si.remote_port = e.remote_port;
        si.local_ip = HexToIp(e.local_ip);
        si.remote_ip = e.remote_ip_dotted;
        si.state = e.state;
        s.conns.push_back(std::move(si));
    }

    // Heuristic for game port: ESTABLISHED, remote ip not 1e100/CDN, port != 80/443
    // Prefer Roblox POP range (128.116.* or 104.199.* or 8.42.*) else highest remote port
    uint32_t best = 0;
    std::string bestIp;
    for (auto& c : s.conns) {
        if (c.remote_port == 80 || c.remote_port == 443) continue;
        // Skip obvious CDN/google
        if (c.remote_ip.rfind("1e100",0) != std::string::npos) continue;
        if (best == 0 || c.remote_port > best) {
            best = c.remote_port;
            bestIp = c.remote_ip;
        }
        // POP heuristic: if matches known POP, take it immediately
        if (c.remote_ip.rfind("128.116.",0)==0 || c.remote_ip.rfind("104.199.",0)==0) {
            best = c.remote_port;
            bestIp = c.remote_ip;
            break;
        }
    }
    s.game_port = best;
    s.game_ip = bestIp;

    if (s.conns.empty()) {
        s.diag = "portwatch: pid " + std::to_string(sober_pid) + " — " + std::to_string(inodes.size()) + " sockets, no ESTABLISHED";
    } else if (best) {
        s.diag = "portwatch: pid " + std::to_string(sober_pid) + " game " + bestIp + ":" + std::to_string(best) + " (" + std::to_string(s.conns.size()) + " ESTABLISHED)";
    } else {
        s.diag = "portwatch: pid " + std::to_string(sober_pid) + " " + std::to_string(s.conns.size()) + " conns, lobby/CDN only";
    }
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
    bool changed = (pid != current_pid_ || base != current_base_);
    if (changed) {
        current_pid_ = pid;
        current_base_ = base;
        last_snap_ = Poll(pid);
        // Reset game port tracking on pid change
        if (pid != 0) last_game_port_ = 0;
        if (out_pid)  *out_pid  = pid;
        if (out_base) *out_base = base;
        return true;
    }
    // Even without pid change, refresh game_port view for teleport detection
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
        if (!last_snap_.diag.empty())
            o << "  " << last_snap_.diag;
        if (last_snap_.game_port)
            o << "  in-game " << last_snap_.game_ip << ":" << last_snap_.game_port;
        else
            o << "  lobby";
    }
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
