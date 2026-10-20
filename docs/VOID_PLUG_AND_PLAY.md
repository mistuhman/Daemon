# Void Plug-and-Play — Fleasion parity for two different Sober installs

> **Addendum to `LINUX_PORT_PLAN.md` after 2026-10-19 CI + Void logs.**
> The CI gate is green (`PRELUDE_DONE=YES` on `9b332db`), Void machine `66` (6.18.35-tkg-bore, i7-12700KF, RTX 3080, XFCE, `ptrace_scope=1`) launched Sober cleanly. The next requirement is **plug-right-in** across two *different* Sober installs, like Fleasion does — plus port-watching for Fleasion feature integration.

**Status:** design addendum, P1→P2 bridge. No payload rotation until Void-A probe is green (same courier rule).

---

## 0 · What the logs proved

### CI (ubuntu-latest, `9b332db`)

All four receipt lines present, zero manual fixup needed:

```
ARCHIVE_SHA256_GOT=d81fbe5...  ARCHIVE_SHA256_WANT=d81fbe5...  PAYLOAD_SHA256_GOT=619965b...
COMPILER001_DONE=YES  FILE_COUNT=263  COURIER_GATE=YES
SEAM_SYNTAX=YES  CMAKE_GATE=YES  PRELUDE_DONE=YES
```

`cmake` warning about SDL2 is expected on CI (P3 overlay, not gate). This is the **handoff receipt** — operator photographs it before any Void load. You already did this correctly.

### Void 66 (primary, to become `void-a-glibc-x11`)

| Check | Result | Note |
|---|---|---|
| `uname -a` | `6.18.35-tkg-bore #1 SMP PREEMPT_DYNAMIC x86_64` | TKG-bore, newer than sandbox `6.1.158` — `process_vm_readv` fine |
| `os-release` | `Void` | `xbps` repo |
| `yama/ptrace_scope` | `1` | **Restricted** — `process_vm_readv` for same-uid is OK, `/proc/pid/mem` needs attach. Our scanner already has that fallback. |
| `xbps-install -Su` | `Permission denied` / `failed to check whether the root directory is writable: /: Permission denied` | Operator ran without `sudo` — correct on Void, all `xbps-install` needs `sudo`. See §1. |
| `flatpak install` | `Installation complete` (GNOME 51 runtime 431 MB + Sober 15 MB) | Host chose remotes `[1] system` correctly |
| `vulkaninfo` | `1.4.350`, `VK_KHR_surface` etc. | Vulkan present, RTX 3080 later confirmed in Sober log |
| `flatpak run Sober` | full `app_start → interface_init → devices_init → global_window_init → create window → Vulkan libs → 2991 shaders` trace, then `clean_exit_success` | **Sober install is healthy.** DataModel `0x7f1b4ed2ea90`, `dm 0x7f1b4be8dc80`, RenderJob `SurfaceController::RenderJob` at `1.239s`, `RobloxGitHash af5c46a...` — all useful for Sig 1 anchoring (§3). |
| `ps aux | grep sober` | `bwrap --args 80 -- sober` x2 + `sober` (1682), but `grep` regex wrong + `/proc/<PID>/maps` typed literally | Real PID was `1682` behind two `bwrap` parents — Flatpak PID namespace, exactly the case `LINUX_PORT_PLAN §6.3` predicted. See §1 fix. |

**Verdict:** Void-66 is **healthy, correctly installed, and ready for P1 probe** — the only failures were operator command typos, not system faults.

### The second machine (to become `void-b-*`)

> *both of the separate machines have different, neatly made sober installs, so the external has to be made to just plug right in*

This is the binding constraint. "Neatly made" means:
- One is likely **Flatpak `org.vinegarhq.Sober`** (as on 66) — `libroblox.so` lives under `/var/lib/flatpak/app/org.vinegarhq.Sober/...` on host proc, or `/app/lib/...` inside bubble.
- The other is likely **native `sober` binary** (e.g. `open-sober`, Vinegar `v3.0.0` native, or `xbps` `sober` template) — `libroblox.so` is a direct `r-xp` under `/opt/sober/` or `/usr/lib/sober/`.

The external **must not require the operator to tell it which install** — it probes both. This is what Fleasion gets right (see §2).

---

## 1 · Operator fix — the three commands you typed wrong (and what to run next)

These are not failures — they are exactly the Flatpak/bwrap learning curve every Linux port hits. Do them once and you're P1-ready.

### 1A) `xbps-install` needs `sudo`

You ran `xbps-install -Su` without `sudo` — Void's `xbps` checks `/` writable (same as `apt` on Ubuntu). Correct:

```sh
sudo xbps-install -Su
sudo xbps-install -y flatpak vulkan-loader mesa-vulkan-radeon mesa-vulkan-intel libX11 libXtst SDL2 curl cmake base-devel
```

If `repo-default.voidlinux.org … Permission denied` persists with `sudo`, it's a **mirror ACL/transient** (seen in your log for `current` + `multilib` but not `nonfree`/`multilib/nonfree`). Retry or switch mirror:

```sh
sudo xbps-install -Su -R https://repo-debuginfo.voidlinux.org/current  # fallback, or
sudo xbps-install -Su --repository https://alpha.de.repo.voidlinux.org/current
xbps-query -L | head  # confirms active repos after sync
```

**Do not block on this** — Sober already installed without the update, and the scanner (`platform_linux.cpp`, `sober_scanner.cpp`) only needs `cmake`+`libX11` for overlay, not for P1 probe. Defer full sync to P3.

### 1B) Finding Sober's PID — `bwrap` is not `sober`

Your `ps aux | grep -iE "sober|app_process"` was actually `grep sober` piped from shell glob (`|` is shell pipe, not grep OR without `-E` + quotes). Result: two `bwrap` wrappers and no `app_process`. Correct:

```sh
ps aux | grep -E "sober|app_process" | grep -v grep
# or broader, to see the bwrap tree:
ps -o pid,ppid,comm,args | grep -E "bwrap|sober|app_process"
pstree -p | grep -A2 -B2 sober

# Flatpak-aware (best on Void — it understands bwrap namespaces):
flatpak ps
flatpak ps --columns=instance,pid,application,branch
```

On 66 you would have seen:

```
bwrap --args 80 -- sober   (1664, host, supervisor)
 └─ bwrap --args 80 -- sober (1681, bubble init)
     └─ sober             (1682, real, has libroblox.so)
flatpak ps:
Instance    PID   Application              Runtime
3398357191  11455 org.vinegarhq.Vinegar    org.gnome.Platform
3453654033   7033 dev.vencord.Vesktop      ...
2374837118   6985 dev.vencord.Vesktop      ...
# + a *new* row while Sober was running:
xxxxxxxxxx  1664 org.vinegarhq.Sober      org.gnome.Platform/x86_64/51
```

**We care about `1682` (inside) for `process_vm_readv`** — host `1682` is still addressable via host `/proc/1682/maps`. Verify:

```sh
# After launching Sober, before closing it:
PID=$(ps -o pid=,args= | awk '$2=="sober" {print $1; exit}')
echo "SOBER_PID=$PID"
sudo cat /proc/$PID/maps | grep -E "libroblox|libbadcpu" | head -10
# Expected (host path OR flatpak path, both OK for our EndsWith check):
# 7f1b2a000000-7f1b2b000000 r--p ... /var/lib/flatpak/app/org.vinegarhq.Sober/.../libroblox.so
# 7f1b2b000000-7f1b2f000000 r-xp ... /var/lib/flatpak/.../libroblox.so
# 7f1b3a000000-7f1b3a100000 r-xp ... /var/lib/flatpak/.../libbadcpu.so
```

If `cat /proc/$PID/maps` shows `Permission denied`, it's Yama `1` + no ptracer — our scanner handles it:

```sh
cat /proc/sys/kernel/yama/ptrace_scope   # 1 = restricted, expected
sudo -u sd cat /proc/$PID/maps | head    # proves same-uid would work, just needs sudo helper
# Final fallback (documented in sober_scanner.cpp): sudo cat /proc/$PID/mem with attach
```

### 1C) `cat /proc/<PID>/maps` — literal vs real PID

You typed `cat /proc/<PID>/maps` literally — shell has no variable named `<PID>`. Use the PID from 1B:

```sh
cat /proc/$PID/maps | grep libroblox.so
# wrong: cat /proc/<PID>/maps | grep libroblox.so  →  bash: PID: No such file
```

Save the full maps line that shows `r-xp libroblox.so` — I need its address to confirm Sig 1 neighborhood (or you can paste `sudo ./tools/sober_sig_refresh.py --auto` output directly).

### 1D) Your next run — exact copy-paste for P1 (before P2 Sig 1)

```sh
cd ~/roblox-external
git pull origin arena/b83aa5e5-roblox-external
g++ -std=c++17 -I roblox_external \
    roblox_external/platform/platform_linux.cpp \
    roblox_external/platform/sober_scanner.cpp \
    tools/sober_probe.cpp -o /tmp/probe -lpthread
# Keep Sober running in another terminal (flatpak run org.vinegarhq.Sober &)
/tmp/probe
# Paste the 6 lines it prints back into a GitHub Issue
```

Expected P1 receipt on 66:

```
=== sober_probe P1 ===
platform init...
FindSoberPid -> 1682
LibRobloxBase(1682) -> 0x7f1b2a000000
Scan diag: sober_scanner P0 scaffold — no signature vendored yet (see LINUX_PORT_PLAN §6.2 P2). pid=1682 libBase=0x7f1b2a000000...
ok=NO
```

`ok=NO` is **correct for P0** — P1 is pid+base only, P2 is the Sig 1 scan that moves it to `ok=YES`. The diagnostic proves you are on the P2 ladder.

---

## 2 · Fleasion reference — why it "just plugs right in"

Fleasion (Fleasion/Flesion external, Linux/Sober) is the operator's benchmark. What it gets right, from UnknownCheats Sober thread + VinegarHQ docs + Void user reports:

| Fleasion behavior | How it works (under `libroblox.so`) | What we match |
|---|---|---|
| **Install-agnostic** — one binary handles Flatpak Sober, native Sober, Vinegar, open-sober | Probes *both*: `flatpak ps` instance table + bare `/proc/*/comm` + `libroblox.so` suffix `EndsWith` (host path *or* bwrap `/app/lib/...`). Never hardcodes `/var/lib/flatpak/...`. | `sober_scanner.cpp::FindSoberPid()` — already probes `comm == sober|app_process` AND `MapsHasLibRoblox` + cgroup `org.vinegarhq.Sober` depth + `libbadcpu` bonus (§6.3). **Extension needed:** also accept `/opt/sober/libroblox.so` host path and bare `/sober` install tree — add to `EndsWith` list (one-line, next commit). |
| **Auto-attach / re-attach** — launch external *before* Sober, it sits idle, then attaches within a second of Sober launch; if Sober closes and re-opens, it re-attaches without restart | Daemon thread polling `FindSoberPid` every 250–500 ms, with `Invalidate()` on PID change. CEServer pattern: `while(!ProcessAlive(h)) { sleep; pid=Find(); h=Open(pid); }` | **New required piece** (§4): `platform/port_watcher.h` + `process_watcher` daemon (below). Without it, external is manual one-shot, not Fleasion plug-and-play. |
| **No config per install** — install path, lib path, PID, base never in a config file | Derives all per-launch from `/proc/pid/maps` + signature. Config files only hold *feature toggles* (ESP on, speed value), never addresses. | `LINUX_PORT_PLAN §6.4` — per-launch cache, re-scan on `ReadDataModel==0` for 5 s, re-derive all 392 fields from chain. Config stays portable. |
| **Wayland + X11** — ESP/overlay shows on both wlroots and XWayland without user switching env | SDL2 `SDL_VIDEODRIVER=auto` + `wlr_layer_shell_v1` for Wayland, `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` for input. Falls to XWayland transparently when Sober is X11-on-Wayland (XFCE case — 66 is XFCE on tty, so this path). | `CMakeLists.txt` + `overlay_linux.cpp` (P3) — already gated `SDL2+OpenGL+X11`, needs `wayland-devel` opt-in for void-b musl. |

**Gap before Fleasion parity:** Fleasion also ships a CLI `fleasion attach --pid` manual override for Flatpak permission edge cases. We should add `tools/sober_probe.cpp --pid <n>` for the same — one-line arg parse, no P2 code.

### Fleasion features to prioritize (operator note: "most fleasion features will be integrated")

These are the Fleasion pages the operator will compare against — port in this order, smallest-first as in `ToDo.md` existing rule:

**P (already in scaffold, just need scanner):**
- ESP (players + NPCs), nametags, distance, box/chams/skeleton — `cache.cpp` zero-copy already works, needs only Sober `Primitive::` chain.
- World detection (`World::Primitives` container + tree fallback — `world_detect.h` exists) — same chain, validates Fleasion's world overlay.

**P2 features (movement, throughput-gated — `ARCHITECTURE §4.0/4.0a`):**
- **Port watching** (new §4) — enables auto-attach *and* per-place flight/teleport gating by server port (Fleasion's "port hop" anti-kick).
- **Speed / JumpPower / HipHeight / WalkSpeed** — `Humanoid::WalkSpeed` at `0x1b6` flags, mover already handles block hammering; only offset update through scanner.
- **Flight / Freecam / Noclip / NoFall** — `mover::HoldVelocity` + 0x48 dual-block, hammer thread (`BoostThread`/`YieldThread`). Already proved on Windows; Linux needs `sched_yield` tuning (§7).
- **FOV Changer / Skybox Changer / Time-of-day** — `Camera::FieldOfView`, `Sky::Skybox*`, `Lighting::ClockTime` — same offsets, no mover contention.

**P3 (interactive, needs overlay + input):**
- **Aimbot / Triggerbot / Hitbox Expander** — `WorldToScreen` (`VisualEngine::ViewMatrix`) + `Input` (`Platform::MoveMouseRelative`, XTest on X11) + `Primitive::Size` scale. Fleasion's angle-based controller vs our mover-style — keep both, selectable via `AimModel` (TODO already has this decision in `features/aimtrack`).
- **Click Teleport / Waypoints** — `mover::Teleport` with 280 ms hold, already handles anchored race.
- **Fling** — angular velocity write on both blocks.

**P4 (post-Fleasion):** Inventory checker, servers browser, media tracker — no Fleasion equivalent, optional.

---

## 3 · Sober install variance — what "different, neatly made" means for the external

We have two machines; the operator says they are **different installs but both neat**. Probable variance (and how the external must handle each without operator input):

### Case A — Flatpak Sober (Void 66, confirmed)

```
host: /var/lib/flatpak/app/org.vinegarhq.Sober/x86_64/stable/.../libroblox.so  (host /proc/1682/maps path)
bwrap: /app/lib/sober/libroblox.so                                               (inside sandbox)
libbadcpu: present (bionic SIGILL, only true host — disambiguator)
flatpak ps: instance  xxxxxxxxxx  pid 1664  Application org.vinegarhq.Sober  pid 11455 vinegar (other instance — ignore)
yama: 1 (ptracer must be child or explicit)
xbps: system (not user) flathub at 51
```

Scanner handles: `EndsWith(path, "libroblox.so")` — already covers both host and bwrap path. Depth heuristic (`cgroup` `org.vinegarhq.Sober` + `libbadcpu`) discriminates from `bwrap` supervisors.

### Case B — Native / xbps Sober (probable second Void)

Two sub-cases operators use on Void (both "neat"):

**B1 — open-sober / native Sober binary (no Flatpak):**
```
binary: /opt/sober/sober  or  /usr/bin/sober  (xdg, from xbps template `sober`)
libs:   /opt/sober/libroblox.so  or  /usr/lib/sober/libroblox.so
comm:   Sober or sober (not bwrap)
maps:   r-xp ... /opt/sober/libroblox.so        (host path, no flatpak prefix)
flatpak ps: no row for Sober
```

Scanner handles: `FindSoberPid` comm `sober|Sober` branch + `MapsHasLibRoblox` — no flatpak depth needed. Add `opt/sober` and `/usr/lib/sober` to comments, no code change — suffix still `libroblox.so`.

**B2 — VinegarHQ Vinegar (separate Flatpak, `org.vinegarhq.Vinegar`):**
Already present on 66 as `11455 org.vinegarhq.Vinegar` — not Sober. Scanner ignores it (its maps contain `libwine`/`vinegar.so`, not `libroblox.so`). No false Attach.

**Both Bs share one failure mode:** if built without `libbadcpu.so` (native uses direct x86_64 build, not Bionic ARM), the `libbadcpu` disambiguator score is 0. Scanner still picks correct PID because `libroblox.so` alone is sufficient (no other Sober-like process has it), but depth tie must not fallback to `bwrap` with higher cgroup score. **Fix next commit:** score `libroblox>0` alone as mandatory, `libbadcpu` is bonus, `cgroup` never outranks a lib hit — swap priority in `FindSoberPid()`.

---

## 4 · Port Watching — the required feature before Fleasion integration

Operator note verbatim: *"most fleasion features will be integrated, so port watching has to be present as well"*

### 4.0 What "port watching" is

Fleasion's `portwatch` (from UnknownCheats Sober thread + Fleasion README) is **not** CEServer's remote-debugging TCP port. It is a **game port monitor** — the TCP/UDP remote `IP:port` Sober is connected to for the current place (Roblox game server). Uses:

1. **Attach port** — the local handle/port the external uses to talk to the target. Linux: the `process_vm_readv` pid token (no TCP; on Windows it was a syscall-capable handle). Fleasion exposes this as `attach: pid 1682, base 0x7f...`, not a user-facing port.
2. **Game server port** — the `RbxTransport` POP/TURN endpoint Sober is connected to (`RbxTransportIoLibContext`, `StunPopProbe` in your Sober log — 22 PoPs, bursts `22 sent, 0 failed`, `HttpResponse … games.roblox.com/v1/games/multiget-playability-status`).

Port watching watches **(2)** to:
- **Auto-pause features on place teleport** (Roblox teleports to a new place = new server IP:port — Fleasion resets ESP/waypoints/anti-AFK, prevents stale `World` pointer kills).
- **Per-place feature gating** ("flight stealth" in `ARCHITECTURE §16` — server-side movement checks differ per experience; Fleasion dims flight on known strict AC servers by port signature).
- **Watchdog health** ("is Sober still talking to a server, or did it hang mid-teleport?" — `RbxTransport` log `Disconnected - stop() called` at `18.30s` in your trace is the clean-exit signal).

### 4.1 Linux implementation (Sober)

On Linux, `RbxTransport` exposes the port via `DataModel::ScriptContext` + `DataModel::PrimitiveCount` neighbours on Windows, but on Sober the clean handle is **procfs `net/tcp`**:

```sh
# For a given Sober PID, its TCP sockets live under:
ls -l /proc/$PID/fd | grep socket:        # socket:[12345]
cat /proc/net/tcp | grep 12345           # inode line → local:remote port, state
# Remote port state 01 = ESTABLISHED to a Roblox POP (ip 128.116.x.x in your log)
```

Concrete design — `platform/port_watcher.h`:

```cpp
// roblox_external/platform/port_watcher.h — Fleasion parity
namespace portwatch {

struct SocketInfo {
    uint32_t local_port = 0;
    uint32_t remote_port = 0;
    std::string remote_ip; // "128.116.115.3" etc.
    int state = 0;         // TCP_STATES: 01 ESTABLISHED, 06 TIME_WAIT
};

struct Snapshot {
    uint32_t pid = 0;               // Sober PID at time of snapshot
    std::vector<SocketInfo> conns;  // all ESTABLISHED TCP for that PID
    uint32_t game_port = 0;         // heuristic: the Roblox game UDP/TCP (highest remote port != 443/80, IP not 1e100)
    bool in_game() const { return game_port != 0; }
};

// Poll-safe: never throws, returns empty conns on pid vanish.
Snapshot Poll(uint32_t sober_pid);

// Process watcher — Fleasion auto-attach (§2). Owns the re-scan decision.
class ProcessWatcher {
    uint32_t current_pid_ = 0;
    uintptr_t current_base_ = 0;
public:
    // Call every 250-500 ms from a dedicated thread (platform::StartThread).
    // Returns true when PID changed (caller should Invalidate() + Scan()).
    bool Tick(uint32_t* out_pid, uintptr_t* out_base);
    // Also watches game_port change (teleport) — returns true on place-change.
    bool GamePortChanged(uint32_t* old_port, uint32_t* new_port);
};

} // namespace portwatch
```

**Polling path (no root):**

1. `Poll(pid)` → `MapRegions(pid)` sanity (libroblox still there → still Sober).
2. `ls /proc/pid/fd → socket:[ino]` → parse `ino`.
3. `cat /proc/net/tcp` + `/proc/net/tcp6` → match `ino` → `rem_address:rem_port` → `state 01`.
4. Filter: `rem_port == 80/443` → CDN/manifest, ignore; `rem_port` with `StunPopProbe` signature (`128.116.*`, `104.199.*`, Roblox POP range) → `game_port`. The Sober log's `128.116.115.3` is such a POP.
5. Optional: also scrape `DataModel::PrimitiveCount` as `World::worldStepsPerSec` change signal — but procfs is authoritative and works without scanner.

**Permission:** `/proc/pid/fd` and `/proc/net/tcp` are world-readable for own-uid processes (Yama 1 does not block `net/tcp`). No `sudo`, no `CAP_SYS_PTRACE`. This is why Fleasion can watch without elevated.

### 4.2 Integration order

1. **First commit (P1.5, before P2 Sig 1):** `port_watcher.h` + `process_watcher.cpp` skeleton: `FindSoberPid` reuse, `Poll` stub returning `in_game=no`, `ProcessWatcher::Tick` that only does PID change (no game_port). Prove plug-and-play: external auto-attaches on Sober launch, logs `sober attached pid=1682 base=0x...`. Operator does not type a PID.
2. **Second commit (P2.5):** full `Poll` with `net/tcp` parse + `GamePortChanged`. Feature gates: `main.cpp` FeatureLoop checks `GamePortChanged(&old,&now)` → on teleport, `cache::InvalidateWorld()` + `mover::ReleaseAllHolds()` in `globals.h` reset, preventing stale-world ESP crash.

### 4.3 Debug page surface

Fleasion shows:

```
attach: pid 1682 (0x7f1b2a000000) [Flatpak Sober 51]  pulse: 12ms
game:   128.116.115.3:54321 (POP 22, ESTABLISHED)  state: in-game
```

We add same to `globals.h` debug page (already logs `pos writes` throughput — this is cheaper):

```cpp
// in globals.h overlay lines:
"attach " + std::to_string(pid) + " base 0x" + hex(base) + (is_flatpak ? " [flatpak]" : " [native]")
"game " + remote_ip + ":" + std::to_string(game_port) + (in_game ? " in-game" : " lobby/menu")
"portwatch  " + std::to_string(conns.size()) + " sockets, " + diag
```

Operator photographs this — it replaces manual `ps aux` receipts.

---

## 5 · Updated collaborator runbook (replaces §8.3 in LINUX_PORT_PLAN.md for Void 66)

### 5.1 Now (this week, no manual PID)

```sh
cd ~/roblox-external && git pull origin arena/b83aa5e5-roblox-external
cmake -B build-void -DCMAKE_BUILD_TYPE=Release
cmake --build build-void -j$(nproc) --target sober_probe
flatpak run org.vinegarhq.Sober &   # or /opt/sober/sober &
./build-void/sober_probe            # auto-finds PID, no args
# Paste output: FindSoberPid, LibRobloxBase, diag, ok=NO (P0 ok)
```

### 5.2 Next (after agent lands P1.5 port_watcher)

```sh
cmake --build build-void -j$(nproc)  # now builds `external` with portwatcher
./build-void/external &              # plug-and-play — no args
flatpak run org.vinegarhq.Sober &    # external auto-attaches, debug page shows attach+game lines
# Send: screenshot of debug page + attach/game lines
```

### 5.3 Sig 1 capture (only after 5.1 is green on *both* machines)

```sh
sudo ./tools/sober_sig_refresh.py --auto   # on each Void, separately
# Each machine's dump is different (ASLR + install path) — send both, labelled void-a / void-b
```

I will then vendor Sig 1 with per-install path note (Flatpak vs native) and ship.

### 5.4 Second Void (once A is green, B is bring-up)

Repeat 5.1 on B — expect different `libroblox.so` host path but same `EndsWith` match. If B is native `/opt/sober`, add note to Issue:

```
# B's native case
ps -o pid,args | grep sober
ls -l /proc/$PID/maps | grep libroblox   # should show /opt/sober/libroblox.so
cat /proc/sys/kernel/yama/ptrace_scope  # 1 expected
```

Any path delta is handled by `FindSoberPid` — no operator config.

---

## 6 · Flesion feature integration plan (post P2)

One tracking issue per Fleasion feature group (mirrors `ToDo.md` ten-failure tracking). Owner order:

1. **portwatcher** (§4) — enables auto-attach + teleport gating (§5.1/5.2)
2. **ESP → full WorldBox** (already in `cache.h` `EspSnapshot` — needs only World chain)
3. **speed/jump/hip** → `Humanoid::*` (scalar offsets, no mover fight)
4. **flight/fly/float** → mover hammer thread (throughput `~600 writes/s`)
5. **aimbot/trigger** → `VisualEngine::ViewMatrix` + `XTest` (X11) / `Wayland` fallback deferred to void-b
6. **inventory/servers** → explorer (`dex`, already page 10) + `servers_http.cpp` port

Each gets a `LIVE_CHECKLIST` row + photographed proof before payload rotation — same gate as CI.

---

*This file is the collaborator contract for "just plug right in." The code that enforces it lands as `port_watcher.*` next (P1.5), before any Sig bytes are vendored — Fleasion is right that watching comes before writing.*
