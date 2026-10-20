#pragma once
// ----------------------------------------------------------------------------
//  platform/port_watcher.h — Fleasion-style plug-and-play (VOID_PLUG_AND_PLAY.md §4)
// ----------------------------------------------------------------------------
//  Two jobs, one TU family:
//
//  1) Port watching: /proc/net/tcp poll for the Sober PID → game server
//     POP IP:port (RbxTransport ESTABLISHED sockets). No ptrace, no sudo,
//     world-readable for same-uid. Used for:
//       - teleport gating (game_port change → cache::InvalidateWorld + mover release)
//       - health watch (Disconnected - stop() called in sober log → in_game false)
//       - debug page "game 128.116.x.x:PORT in-game"
//
//  2) Process watching: daemon that Tick()s FindSoberPid() every 250-500 ms,
//     reports PID+base change. Enables "launch external before Sober, it
//     auto-attaches" (Fleasion plug-right-in).
//
//  Builds on sober_scanner.h's FindSoberPid/MapRegions, not the other way
//  around — so it compiles without scanner signatures (P0/P1).
// ----------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace portwatch {

struct SocketInfo {
    uint32_t local_port  = 0;
    uint32_t remote_port = 0;
    std::string local_ip;
    std::string remote_ip; // e.g. "128.116.115.3"
    int state = 0;         // TCP states: 0x01 ESTABLISHED, others ignored
};

struct Snapshot {
    uint32_t pid = 0;                   // sober pid at poll time (0 = no sober)
    std::vector<SocketInfo> conns;      // all ESTABLISHED TCP for that pid
    uint32_t game_port = 0;             // heuristic best guess, 0 = not in-game (lobby/menu)
    std::string game_ip;                // remote ip of game_port
    std::string diag;                   // human log line (debug page)
    bool in_game() const { return game_port != 0; }
};

// Poll-safe: never throws, no ptrace, reads only /proc/<pid>/fd + /proc/net/tcp.
// Returns diag="no sober" when pid==0 or pid vanished.
Snapshot Poll(uint32_t sober_pid);

// Convenience overload: finds PID itself.
Snapshot PollAny();

// Process watcher — Fleasion auto-attach. One instance per external process,
// Tick() from a dedicated thread or from main FeatureLoop (250 ms period).
class ProcessWatcher {
public:
    // Returns true when PID or base changed (caller should Invalidate()+Scan()).
    // out_pid and out_base are set to new values on change, undefined otherwise.
    bool Tick(uint32_t* out_pid, uintptr_t* out_base);

    // Returns true when game_port changed (place teleport / server hop).
    // Only meaningful after Tick has returned a live pid+base at least once.
    bool GamePortChanged(uint32_t* old_port, uint32_t* new_port);

    // One-liner for debug page (globals.h overlay):
    // "attach pid=1682 base=0x7f... [flatpak]  game 128.116.x:PORT in-game  pulse 12ms"
    std::string DebugLine() const;

private:
    uint32_t current_pid_ = 0;
    uintptr_t current_base_ = 0;
    uint32_t last_game_port_ = 0;
    Snapshot last_snap_{};
    uint64_t last_poll_ms_ = 0;
    std::string last_debug_;
};

} // namespace portwatch
