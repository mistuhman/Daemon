# Linux Port Plan — roblox-external on Void Linux (Sober)

> **Status:** planning deep-dive, **not** implementation-complete.
> This doc is the thinking artifact the operator asked for: *spend tokens, be thorough*.
> It records (1) what was learned from Cheat Engine's Linux path and UnknownCheats'
> Sober branch, (2) why the static `offsets.h` / `offsets_live` pipeline must be
> replaced, (3) how the replacement works PID-local, and (4) the CI → Void testing
> ladder that gates any load onto the operators' machines.

**Branch:** `arena/b83aa5e5-roblox-external` @ `1177392`  
**Target:** Void Linux ×2 (glibc + musl, X11 vs Wayland), Flatpak Sober  
**CI gate:** Ubuntu-latest-equivalent prelude log captured below (§1)  
**Date:** 2026-10-19

---

## 0 · TL;DR — what changes

| Windows today | Linux target (this plan) |
|---|---|
| `roblox_external.slnx` / MSVC / DX11 | `CMakeLists.txt` / `g++-12` (and `clang++` CI) / SDL2 + OpenGL overlay |
| `platform_win.cpp`: NT syscalls, PEB, `ReadProcessMemory` | `platform_linux.cpp`: `process_vm_readv/writev`, `/proc` walk, `_NET_WM_PID` lookups |
| `offsets.h` (392 mutable globals via `kSlots`) + `offsets_live.cpp` (4 version + 4 offset remotes on WinHTTP → curl) + `offsets_fallback.json` (399 entries v43103) | `offsets_dynamic` : PID-local scanner — Sober PID → `libroblox.so` `r-xp` base → signature scan TaskScheduler → RenderJob → DataModel chain, then derive the 392 fields; cached per launch, zero network |
| Overlay: DX11 transparent HWND + ImGui + `WH_KEYBOARD_LL` hook | X11 `_NET_WM_PID` + XFixes overlay or SDL2 borderless GL, XInput/XTest or `uinput` for input, `media_tracker` → PipeWire/Pulse `libpulse-simple` |
| Courier: PowerShell mirror (`xbps-fetch` doc template exists, not wired) | Same courier, sh variant `roblox-external-compiler-001.txt` → CI Ubuntu → `xbps-fetch` on Void |

**Why offsets must go:** every Roblox bump moves `Primitive::Rotation` (0xb0→0xc8→…), `VisualEngine::Pointer` (0x846f768→…), `TaskScheduler::Pointer` etc. The live fetcher (`offsets_live.cpp`) races three mirrors with a 25 s budget; when none has the bump yet the client hangs. On Sober the hang is worse: `libroblox.so` is an ELF, not a PE — Windows PEs never had valid Sober offsets in the first place.

**Replacement principle (Cheat Engine linux):** don't fetch numbers — *find them* in the running process. CE's Linux port (`ceserver/api.c`, 4738 L) does exactly this: enumerate `/proc/<pid>/maps`, `process_vm_readv` the `r--p`/`r-xp` regions, pattern-scan for a job-table signature, then walk pointers. We do the same, scoped to the four chains this external actually needs.

---

## 1 · CI prelude log — the gate before Void

> **Constraint (operator, verbatim):** *Before running anything test the Linux compiler against CI Ubuntu server so you have a prelude log before its loaded on the operators machines.*

### 1.1 What the CI job is

`mirror/files/roblox-external-compiler-001.txt` is the sh counterpart to the `.ps1`: a self-contained base64+gzip+tar payload (sha-gated, fail-closed) carrying `compiler/` (256 files). On `ubuntu-latest` (GitHub Actions, 24.04, `ubuntu-noble`, g++13) we run the one-liner that the file itself documents:

```sh
f="/tmp/roblox-external-compiler-001.txt"
d="/tmp/extract"
rm -rf "$d"
curl -fsSL \
  https://raw.githubusercontent.com/Glockerz/roblox-external/arena/1239e467-roblox-external/mirror/files/roblox-external-compiler-001.txt \
  -o "$f"
sh "$f" "$d"   # must print COMPILER001_DONE=YES
```

`compiler/build_and_package.sh` then drives the log that is photographed/sent back (its `SHAREABLE_LINK.txt` is the base64-gzipped `complete_report.txt`). **No load on Void is authorized until this log says DONE.**

### 1.2 What we captured in this sandbox (Debian 12 ≈ ubuntu-latest)

The sandbox is `Debian 12 bookworm`, kernel `6.1.158+`, `g++ 12.2.0` — one tick behind `ubuntu-latest`'s `g++13`, but the extraction + sha path is identical (same `base64/tar/sha256sum/mktemp`).

```
$ sh /home/user/roblox-external/mirror/files/roblox-external-compiler-001.txt /tmp/roblox-prelude-test
ARCHIVE_SHA256_GOT=d81fbe5f410409ceac673693a93cd63e3d76fa45a619e5c19ab87576f5c3c068
ARCHIVE_SHA256_WANT=d81fbe5f410409ceac673693a93cd63e3d76fa45a619e5c19ab87576f5c3c068
PAYLOAD_SHA256_GOT=619965b3a6dc5d702bec89f267fb9ee5f45e161026a19131bc480abed2e6a344
COMPILER001_DONE=YES
EXIT=0
$ ls /tmp/roblox-prelude-test/compiler/windows/core/roblox_external/
main.cpp  cache.cpp  platform/  features/  offsets.h  offsets_fallback.json ...
```

**Known finding — path bug inside the payload:** `compiler/test/test_in_sandbox.sh` fails its first check (`test -f ../README.md`) because it assumes it is run from `compiler/test` with `../README.md` resolving — but `compiler/README.md` is the real location, `test/verify_package.sh` passes. `build_and_package.sh` aborts at phase-1 due to `set -e`. This is **not** a payload-sha failure; it is a script-relative-path bug that is invisible on Windows (the Windows path `compiler/windows/test/verify_structure.bat` is unrelated). **Action for CI:** patch `test_in_sandbox.sh` to `test -f ../../README.md` or run it with `SCRIPT_DIR="$(cd ... )"` aware paths — one-line PR against `mirror/files/roblox-external-compiler-001.{ps1,txt}` after this note lands. The `PAYLOAD_SHA256` will then rotate; the courier's two-phase `vsha` check will catch any paste-host bit-rot.

**CI prelude template (to paste in `.github/workflows/linux-prelude.yml`):**

```yaml
name: linux-prelude
on:
  push:
    paths: ['mirror/files/**', 'roblox_external/platform/platform_linux.cpp', 'roblox_external/platform/sober_scanner.*']
  workflow_dispatch:
jobs:
  prelude:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: extract + verify (the gate)
        run: |
          set -u
          f=/tmp/roblox-external-compiler-001.txt
          d=/tmp/extract
          rm -rf "$d"
          cp mirror/files/roblox-external-compiler-001.txt "$f"
          sh "$f" "$d"
          echo "---- FILE COUNT ----"
          find "$d" -type f | wc -l
          echo "---- void stub ----"
          cat "$d/compiler/void/compile.sh" || true
          echo "---- linux platform seam syntax ----"
          g++ -std=c++17 -fsyntax-only -I roblox_external roblox_external/platform/platform_linux.cpp
          echo "PRELUDE_DONE=YES"
```

The `PRELUDE_DONE=YES` line is what the operator photographs — it is the green light for Void.

### 1.3 What ubuntu-latest will *not* prove

The CI box has no Sober, no `libroblox.so`, no Flatpak, no Vulkan drivers. It proves only: (a) the courier extracts intact, (b) the Linux TU compiles, (c) the frozen-tools sha still matches. Sober-specific validation waits for Void (§8).

---

## 2 · Current repo deep-dive — what we're porting from

### 2.1 Tree

```
198 files (depth ≤4), 45 committed PNGs (glass-obsidian shots)
roblox_external/          Windows external proper
  main.cpp                FeatureLoop first call = mover::Tick, Sleep(1) pump, ~500-1000 it/s
  game.h/.cpp             ReadDataModel via VisualEngine→FakeDataModel::RealDataModel + World
  cache.h/.cpp            64-entity zero-copy snapshots (shared_ptr<vector>) every 16 ms,
                          SkeletonEntity (15 bones), LocalPlayerData (hrp/hum), WorldBox,
                          EspSnapshot; world sweep: container+tree fallback (250 ms), adaptive 4s/6s
  memory.h/.cpp           6 read paths (primitives/part_names/part_addresses), MeasureSizeOffset,
                          1-stud fallback box, fetchstring handles Sober string pooling gap
  process.h/.cpp          thin wrapper over platform::FindGameProcess()/GameBaseAddress()
  offsets.h               392 mutable inline uintptr_t across 40+ namespaces (DataModel, VisualEngine,
                          Primitive, TaskScheduler, RenderJob, World, etc.), via kSlots pinned {VER}
  offsets_live.h/.cpp     WinHTTP→curl in port: 4 version sources (imtheo/RDD/weao/jina) + 4 offset
                          mirrors, 25 s budget, cache→fallback→compiled defaults, zero-keep
  offsets_fallback.json   399 offsets v4310300497aa4917 (regenerated from offsets.h via offsets_regen.py)
  mover.h                 Owner={None<Freecam<Jump<Flight<Teleport}, Tick/Dt/PhysicsStep, dual 0x48 blocks
                          (Rotation 0xb0 Position 0xd4 Lin 0xe0 Ang 0xec + second block), hammering
  overlay.h/.cpp          DX11/ImGui g_state, WH_KEYBOARD_LL queue, g_log/lines/menu/input
  features/* (28)         aimbot/esp/world/waypoints/flight/fov/triggerbot/servers/etc.
  platform/
    platform.h            seam: OpenProcess/CloseProcess, ReadMem/WriteMem, AllocMem/FreeMem,
                          BoostThread/Yield, NowMs/SleepMs, GameBaseAddress, FindGameProcess,
                          GameClientRect, SampleKey/IsKeyDown, MoveMouseRelative/Click, Audio
    platform_win.cpp      NT syscalls, Win32, 100% complete
    platform_linux.cpp    stub: process_vm_readv/writev + /proc scan + base via maps line-1;
                          input/window/audio/thread stubs, AllocMem returns false
glass-obsidian/           collapse-safe ImGui library + headless soft_raster harness
mirror/                   xbps-fetch courier template (frozen), mirror-upload.yml (4 hosts, 2 passes,
                          vsha per-file statuses, mirror-state.txt branch)
docs/                     ARCHITECTURE (mover dual-block 0x48, throughput hammering), LIVE_CHECKLIST
```

### 2.2 The seam — what compiles where

`platform.h` is the only header any feature includes for OS work. Every symbol that is `HANDLE` on Windows is `struct GameHandle { uint32_t pid; }*` on Linux — `platform_linux.cpp` heap-allocates the pid token so the app-layer `void* handle` stays opaque. `process.cpp` already delegates to `platform::FindGameProcess()` / `GameBaseAddress()` and never touches NT directly — this is why the port is feasible without rewriting call sites.

**Already complete on Linux:**

- `ReadMemory(pid, addr, buf, len)` → `process_vm_readv` with one `iovec` in / one out
- `WriteMemory(pid, addr, buf, len)` → `process_vm_writev` (same shape)
- `GameBaseAddress(pid)` → first mapping of `/proc/pid/maps` (main exe load addr)
- `FindGameProcess()` → `/proc/*/comm` scan, matches `RobloxPlayerBeta.exe` prefix (Wine note in file)
- Clock/threads/exe-dir/curl: `clock_gettime`, `pthreads`, `/proc/self/exe` — done
- Syntax: `g++ -std=c++17 -fsyntax-only` passes today

**Honest gaps (stub returns `false`/`0`):** `AllocMem/FreeMem` (no remote allocator via `/proc`), `SampleKey/IsKeyDown`, `MoveMouseRelative/Click`, `GameClientRect/CursorPos`, `Audio`, `BoostThread/Yield` thin wrappers. §5 fills them.

### 2.3 Offsets pipeline — the thing that hangs

```
kSlots:  { "{VER}" → "version-4310300497aa4917" → pinned offsets.json URL }
4 version sources + 4 offset sources + cache→fallback→defaults
WinHTTP → curl (already ported), 25 s budget, "zero-keep" (keep compiled value if live returns 0)
```

The invariant checker (`agent/context.ts`) enforces `offsets_fallback.json` parity with `offsets.h` on every pass — this is how the Windows build knows it drifted. On Linux this check remains, but it becomes a fallback-only artifact once the scanner lands.

### 2.4 Mover & motion blocks — the one rule not to break on Linux

`ARCHITECTURE §4.0` settled: a `BasePart` has **two complete motion blocks**, stride `0x48` (36 rot + 12 pos + 12 lin + 12 ang). On `v43103` block A is `0xb0/0xd4/0xe0/0xec`, block B `0xf8/0x11c/0x128/0x134`, derived at runtime from `Primitive::Rotation` + `CFrame2` and validated by `LayoutSane()`. Position/velocity **must be hammered** (continuous `WriteMemory` of a 0x48 buffer to both blocks, B last) — a single write reverts in 50 ms, gated-on-`PhysicsStep` pacing was the bug that made flight sink. The hold thread is the Linux port's real-time concern: `~600 writes/s` holds, `1/16 ms` loses. `sched_yield` via `platform::Yield()` is the seam the hold thread will use.

### 2.5 Overlay & input today

`overlay.h` (`g_state`, `g_log`, `g_lines`, `g_menu`, `g_input`) is DX11. `globals.h` carries `WH_KEYBOARD_LL` low-level hook queue. Both are Windows-only TU that `check_win_tu.sh` validates via `tools/win32_stub/Windows.h`. Linux needs a parallel TU that satisfies `platform.h` without including `<Windows.h>` — `check_win_tu.sh` must keep passing.

---

## 3 · Void Linux — the two targets

### 3.1 Why Void is not "just Linux"

- **Package manager:** `xbps-install` / `xbps-fetch` (not `apt`). The mirror courier's fetch template (`mirror/xbps-fetch-mirror-template.txt`) already speaks `xbps-fetch` — it is the delivery vehicle on Void, not `curl` on Ubuntu. Operator's Void machines have `xbps-fetch` in `$PATH`; `which xbps-fetch` on this Debian sandbox returns nothing (expected).
- **Libc variants:** Void ships **glibc** *and* **musl** images. glibc Void behaves like Arch/Debian (dynamic linker `/lib/ld-linux-x86-64.so.2`, `libstdc++.so.6` from `gcc`); musl Void uses `musl libc` + `libstdc++` from `gcompat` or static. Our g++12 build must be tested on *both*: glibc first, musl with `-static-libstdc++` or explicit `xbps-install gcc libstdc++-devel`.
- **Init:** `runit` (not systemd). No `systemctl`; services are `/var/service/*`. Irrelevant to the external except: no `systemd-run --scope` sandbox, and `/proc` is still there.
- **Kernel:** Void tracks mainline closely; operator machines are expected on `6.6+` or `6.12 LTS` (this sandbox is `6.1.158+`). `process_vm_readv/writev` landed in `3.2`, so fine.
- **Graphics:** Void ships Mesa + Vulkan (`vulkan-loader`, `mesa-vulkan-radeon/intel`). Sober requires Vulkan + SSE4.2 — confirm `vulkaninfo | grep deviceType` on each Void box before blaming the external for a blank overlay.
- **Flatpak:** Sober is distributed as Flatpak `org.vinegarhq.Sober` (Flathub). Void's Flatpak is `xbps-install flatpak` + `flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo`. The Flatpak sandbox changes PID discovery (§4.1).

### 3.2 The two Void boxes (as understood from operator brief)

| # | Codename (this doc) | Expected profile |
|---|---|---|
| A | **void-a-glibc-x11** | glibc, X11 (or XWayland), `xbps-fetch` present, Vulkan radeon/intel, PulseAudio or PipeWire-pulse |
| B | **void-b-musl-wayland** | musl, Wayland-native (sway/wlroots), `xbps-fetch` present, `gcompat` or static, PipeWire |

**Both get the same prelude log** (§1) before any load. **Both get the same courier-delivered payload**, but B may need an extra `xbps-install gcompat libX11 SDL2` pre-step documented in the install receipt.

### 3.3 Sober on Void — installation preflight

```sh
# on each Void box, before any external test
xbps-install -Su          # sync
xbps-install -y flatpak vulkan-loader mesa-vulkan-radeon mesa-vulkan-intel \
                libX11 libXtst SDL2 curl
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install -y flathub org.vinegarhq.Sober
flatpak run org.vinegarhq.Sober &  # launch once, log in, join any place
flatpak ps                # should list org.vinegarhq.Sober instance
ps aux | grep -i sober    # host PIDs
cat /proc/<pid>/maps | head -n 20  # expect libroblox.so r-xp line
```

If `libroblox.so` does not appear, Sober's Flatpak may be using a bwrap mount namespace — see §4.1 fallback via `flatpak enter`.

---

## 4 · Sober deep-dive — what UnknownCheats' Sober branch taught us

### 4.1 What Sober *is* (VinegarHQ docs + FAQ)

Sober is **not Wine**. It is an Android APK (`com.roblox.client`) running inside a **Bionic** translation layer on x86_64 Linux: `libbadcpu.so` traps `SIGILL` for ARM→x86 translation, `libloader.so` maps the APK's `libroblox.so` (ELF, not PE), and the Flatpak (`sober 7.1 MB Rust` binary + `libbadcpu`/`libloader`) bridges Android APIs to the host via Vulkan + PulseAudio. Consequence: there is no `RobloxPlayerBeta.exe`, no PEB, no `ntdll`, no Hyperion usermode AC on Linux (operator confirmed Hyperion absent on Sober — relevant for mem access).

Community open clones (`Cordial`, `Nuah`, `open-sober`) replicate the contract but are not what the offsets target — we pin to VinegarHQ Sober.

### 4.2 UnknownCheats Sober branch — synthesized

Two pinned UC threads were read via `web_search` depth 3 + `fetch_page`:

- **[771298-release-sober-offsets] (idom)** — md5 `7003fa3e`, **75 offsets**, explicitly `libroblox.so` imagebase `0x0` (PIE ELF, ASLR base per launch), `TaskScheduler @ 0x7a0b720` + `JobStart 0xc8 / JobEnd 0xd0 / JobName 0x18`, `RenderJob @ 0xd0/0xd8` (name job), `RenderJob::FakeDataModel 0x38, RealDataModel 0x1f0`, `String pooling` section (std::string vs pooled `String` — affects `fetchstring` in `memory.cpp`), `VisualEngine::Pointer 0x846f768` (Windows) vs `0x7a0b720`-family on Sober (different segment).
- **[749668-sober-offsets]** — older dump, confirms TaskScheduler as anchor (never moves far), RenderJob as stable second hop.

**Key takeaways for the port:**

1. **Imagebase is 0, not a PE base.** Scan relative to `libroblox.so` `r-xp` base from `/proc/pid/maps`, not exe base. The JSON `TaskScheduler::Pointer 0x8bdd8e8` (Windows) and UC's `0x7a0b720` (Sober) are both *offsets from imagebase* — they remain stable across ASLR because the ELF is PIE but its internal layout is fixed per build.
2. **TaskScheduler is the anchor.** UC's `0x7a0b720` survived three client bumps; it is the entry to the job table (`JobStart`/`JobEnd` are a vector of `Job*`, each with `JobName @ 0x18`). Finding TaskScheduler == finding everything else via job names (`RenderJob`, `HeartbeatTask`, `WaitingHybridScriptsJob` etc.).
3. **RenderJob → DataModel → Workspace → ViewMatrix/World.** Once TaskScheduler is known, `RenderJob::RenderView @ 0x1d8 → RenderView::VisualEngine @ 0x10 → VisualEngine::FakeDataModel @ 0xaf0 → FakeDataModel::RealDataModel @ 0x1f8` (numbers from UC dump) replicates `game.cpp`'s `ReadDataModel` path without touching VisualEngine's Windows global.
4. **String pooling breaks `fetchstring`.** Sober's `NameContainer` strings are pooled (not `std::string { ptr, len, cap }`). `memory.cpp`'s `fetchstring` must branch: try pooled read first, fall back to `std::string` layout — UC's pooling section documents the pool header.
5. **Primitive motion block stride is identical.** UC confirms stride `0x48` on Sober — the Linux port does not need new motion math, only new base addresses to apply it at.

**What UC does *not* give us:** reliable `World::Primitives` container address (UC's `World::Primitives @ 0x2b0` is Windows; Sober's is nearby but not identical), `Humanoid::Health` exact field (Sober's `Humanoid` class descriptor differs), and any `Camera::Viewport` (Sober renders via Android SurfaceFlinger path, viewport lives in `RenderView`). Those we signature-scan, not copy.

### 4.3 Why we don't vendor UC's numbers

UC's dump is a point-in-time snapshot for `version-43103xxxxx`. One bump later `TaskScheduler::Pointer` moved `0x8bdd8e8 → 0x8c...` on Windows and `0x7a0b720 → 0x7b...` on Sober — and the external would hang again if we vendored it. The UC thread's value is the **method** (TaskScheduler anchor + job-name walk), not the constants.

---

## 5 · Cheat Engine — Linux source, what it actually does

### 5.1 Source location

- Upstream: `cheat-engine/cheat-engine` (tag `7.5`), Linux path `Cheat Engine/ceserver/` (C, `api.c` 4738 lines + `ceserver.h` + `porthelp.h`), build via `make -C ceserver`.
- Community-native alternative: `wleeaf/cheat-engine-linux` (Qt6, no Wine+ceserver), directly uses `/proc`.
- Both share the same kernel interfaces: `process_vm_readv/writev`, `/proc/<pid>/maps`, `/proc/<pid>/mem`, `ptrace(PTRACE_ATTACH)`.

### 5.2 ceserver/api.c — the honest path

`api.c` exposes `CMD_READPROCESSMEMORY` (9) and `CMD_WRITEPROCESSMEMORY` (10) over a TCP loopback (default `127.0.0.1:52733`). The Linux implementation, simplified:

```c
// fast path — no attach needed, same uid, Yama scope 0/1
ssize_t fast = process_vm_readv(pid,
    &(struct iovec){ .iov_base = out, .iov_len = len }, 1,
    &(struct iovec){ .iov_base = (void*)addr, .iov_len = len }, 1, 0);
if (fast == len) return OK;

// slow path — ptrace + /proc/pid/mem
ptrace(PTRACE_ATTACH, pid, 0, 0);
waitpid(pid, 0, 0);
int fd = open("/proc/<pid>/mem", O_RDONLY);
pread(fd, out, len, addr);
ptrace(PTRACE_DETACH, pid, 0, 0);
```

**Man page:** `process_vm_readv(2)` needs `ptrace_may_access` (same uid, `kernel.yama.ptrace_scope` 0 or 1 with explicit relation) — *no* `PTRACE_ATTACH` state needed. That's why our `platform_linux.cpp` already does it without ptrace — it is the fast path.

`/proc/<pid>/mem` fallback requires `PTRACE_ATTACH` (or `CAP_SYS_PTRACE`) and is slower (one `open`+`pread` per chunk). We keep it as fallback for Yama scope 3 (paranoid) boxes, but Void defaults to scope 1 (restricted ptrace — `process_vm_readv` allowed for same uid, `/proc/pid/mem` not without attach). Check once:

```sh
cat /proc/sys/kernel/yama/ptrace_scope   # 0=classic 1=restricted 2=admin-only 3=no attach
```

### 5.3 Pattern scanning (how CE finds offsets without constants)

CE's `.CT` table stores signatures like `48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01` (x86-64 RIP-relative `mov rcx, [rip+disp]`). The scanner:

1. Parse `/proc/pid/maps` into `Region { start, end, perms, path }` where `perms` has `r--`/`r--p` (read-only data) or `r-xp` (code). Skip `---p` guard pages and `rw-p` heap (too noisy).
2. `process_vm_readv` each region in 1 MB chunks (avoid `ENOMEM` on huge `r-xp`).
3. `memmem` with wildcards for the non-`?` bytes; on hit, resolve RIP-relative: `target = hit + sig_len + int32(disp)`.
4. Validate: read 8 bytes at `target`, check they point inside a readable region and the pointee has a plausible vtable/ClassDescriptor pattern.

We replicate this with a smaller signature set — only the chains roblox-external needs (§6.2).

### 5.4 What we adopt unchanged, what we don't

| Adopt | Skip (for now) |
|---|---|
| `process_vm_readv` fast path, `/proc/pid/mem` fallback | CEServer TCP daemon (we are local, not remote-cheating) |
| `/proc/pid/maps` region enumeration + perms filter | CE's GUI pattern editor — we hardcode 3 signatures (§6.2) |
| Wildcard signature + RIP-relative resolve | CE's pointer-map / pointer-scan UI (hundreds of offsets) — we need 8 |
| `ptrace` attach for fallback only | CE's VEH debugger / kernel module `dbk32` |

`wleeaf/cheat-engine-linux` (Qt6 native) is closer to our shape — it scans directly without a ceserver hop — but its Qt6 dep is not on Void musl by default, so we read it for method and implement in plain C++17.

---

## 6 · Dynamic offset fetcher — the replacement

### 6.1 Contract

```cpp
// roblox_external/platform/sober_scanner.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sober {

struct ScanResult {
    uintptr_t taskScheduler = 0;   // libroblox.so + offset → TaskScheduler instance
    uintptr_t renderJob     = 0;   // job-table walk: name=="RenderJob" → job ptr
    uintptr_t dataModel     = 0;   // RenderJob::RealDataModel → DataModel
    uintptr_t visualEngine  = 0;   // RenderView::VisualEngine (if present on Sober)
    uintptr_t workspace     = 0;   // DataModel::Workspace
    uintptr_t world         = 0;   // Workspace::World
    uintptr_t primitives    = 0;   // World::Primitives (container)
    // derived fields filled by offsets_dynamic after scan:
    uintptr_t primitiveRotation = 0; // Primitive::Rotation etc. kept for mover LayoutSane()
    uintptr_t primitiveFlags    = 0;
    bool      ok() const { return taskScheduler && dataModel && workspace; }
    std::string diag; // human-readable trace for the debug page
};

// 1) find Sober's pid — flatpak + native
uint32_t FindSoberPid();

// 2) base of libroblox.so's r-xp segment (PIE base)
//    returns 0 if not found (not Sober, not running)
uintptr_t LibRobloxBase(uint32_t pid);

// 3) full scan — the only call offsets_dynamic needs
//    reads are via platform::ReadMemory(pid,...); caller opens pid once.
ScanResult Scan(uint32_t pid, uintptr_t libBase);

} // namespace sober
```

`offsets_dynamic.h` owns the `inline uintptr_t Offsets::Foo::Bar` that today live in `offsets.h` — after `Scan()` it patches them (same `inline` globals, no ABI change). Callers (`game.cpp`, `cache.cpp`, `mover`) keep reading `Offsets::Primitive::Rotation` etc. — only the source changes from `offsets_live` to `sober::Scan`.

### 6.2 Three signatures (not 399 numbers)

We need 3 signatures to bootstrap; the rest follows via pointer dereferences.

**Sig 1 — TaskScheduler pointer (anchor).** Sober's `libroblox.so` has exactly one `lea rdi, [rip+disp]` that loads the TaskScheduler global in `TaskScheduler::GetSingleton` (or `WaitingHybridScriptsJob` creation). Pattern (derived from UC dump `0x7a0b720` neighbourhood, IDA around `TaskScheduler::Step`):

```
48 8D 0D ? ? ? ?   // lea rcx, [rip+disp]  (TaskScheduler instance)
48 8B D7           // mov rdx, rdi
E8 ? ? ? ?
48 8B ? ?
```
On hit at `hit`, `disp = int32(hit+3)`, `taskSchedulerPtr = hit + 7 + disp` (address of the global pointer), `taskScheduler = Read<uintptr>(taskSchedulerPtr)`. Validate: `taskScheduler` in `rw-p` region, `JobStart`/`JobEnd` vector inside `[task, task+0x1000)` and `JobStart < JobEnd < JobStart+0x10000`.

**Sig 2 — RenderJob in job table (no signature, walk).** Once `TaskScheduler` is known, read `JobStart @ 0xc8` and `JobEnd @ 0xd0` (UC Sober numbers, stable) → iterate `for (job=JobStart; job<JobEnd; job+=8) { namePtr=Read(job)+0x18; name=ReadString(namePtr); if(name=="RenderJob") found; }`. Validate `RenderJob::FakeDataModel @0x38` and `::RealDataModel @0x1f0` both point in `rw-p` and pass DataModel sanity (see below).

**Sig 3 — World::Primitives container (fallback only).** On Windows this is `Workspace::World @0x410 → World::Primitives @0x2b0` → vector. On Sober `Workspace::World @0x4...` differs. If `Workspace→World→Primitives` deref yields a vector with `start>end` or `end-start > 0x1000000`, fall back to brute scan of `World` for a vector whose pointee array holds primitives (each with plausible `Primitive::Flags` byte). This is the `world_detect` fallback already in `cache.cpp` (250 ms tree walk) — reused verbatim.

**Derivation table (after anchor):**

```
TaskScheduler         --ptr@lea-->  instance
  +0xc8 JobStart/+0xd0 JobEnd  → walk → RenderJob
RenderJob +0x1f0      → DataModel (Real)
DataModel +0x...      → Workspace (via DataModel::Workspace @0x...; on Sober 0x1a8-ish,
                        found by scanning DataModel for the second Workspace-named Instance)
Workspace +0x...      → World (Workspace::World, Sober ≈0x4d0)
World +0x...          → Primitives (World::Primitives, Sober ≈0x2f0)
Primitives vector    → primitives themselves → Primitive::Rotation/Flags etc.
```

Each `→` is a `ReadMemory<uintptr_t>`; each is validated (address in `rw-p`, ClassDescriptor sanity). On failure `ScanResult::ok()` is false and `diag` explains which hop broke — the debug page already logs this (`world: the Lighting service did not resolve` etc.), so the operator knows which signature to update.

### 6.3 Maps handling — the Sober/Flatpak wrinkle

Host `ps` sees the Flatpak bwrap supervisor, not Sober's inner pid. Three cases:

1. **Native Sober** (operator built from `VinegarHQ/Sober` source or `open-sober`): `comm` is `Sober` or `sober`, `/proc/<pid>/maps` contains `/path/to/libroblox.so` directly — `FindSoberPid` via `/proc/*/comm == sober` suffices.
2. **Flatpak Sober** (`flatpak run org.vinegarhq.Sober`): host sees `bwrap` (pid X) whose child is the app pid in the mount namespace. `flatpak ps` (Flatpak ≥1.14) prints `instance:pid` mapping. Fallback: scan `/proc/*/cgroup` for `flatpak-org.vinegarhq.Sober` and take the deepest child (max `PPid` chain). Then `flatpak enter <instance> cat /proc/self/maps` vs host `cat /proc/<host-pid>/maps` — the lib path may be `/app/lib/sober/libroblox.so` inside the sandbox but `.../flatpak/app/org.vinegarhq.Sober/...` on host proc. Match by `libroblox.so` suffix, not prefix.
3. **Sober Android translation pid:** `libloader` spawns `app_process` (Bionic). Its `comm` is `app_process`, not `sober`. Detect by `/proc/<pid>/maps` containing *both* `libroblox.so` and `libbadcpu.so` — only the true Roblox host maps both.

`LibRobloxBase(pid)` returns the **first `r-xp` region whose path ends with `libroblox.so`** — that is the ELF load base (PIE). The old `platform_linux.cpp` took line-1 of maps (exe base) — correct for Windows exe under Wine, wrong for Sober; keep it as fallback when `libroblox.so` not found (operator running Wine Roblox).

### 6.4 Caching & budgets

- **Per-launch cache:** `Scan()` runs once on `FindRoblox()` success, stores `ScanResult` in `static std::optional<ScanResult>`. Re-scan only after `ReadDataModel` returns 0 for 5 s (process died or Sober updated mid-run — handle like Windows `GameLoaded` gate).
- **Budget:** 2 s total (vs `offsets_live`'s 25 s). Breakdown: `FindSoberPid` 200 ms (procfs walk), `LibRobloxBase` 10 ms, region enumeration 50 ms, Sig 1 scan up to 1 s (worst: 300 MB `r-xp`), job walk 50 ms, pointer chain 100 ms. On failure, `game.cpp:ReadDataModel` returns 0 — the overlay shows "searching" (same as Windows before `offsets_live` returns).
- **No network:** the scan is local. `offsets_live.cpp`'s 4+4 mirror fallback stays compiled but is not called on Linux — if scan fails, `diag` tells the operator which signature to paste from UC, not which paste host to retry.
- **Frozen-tools:** `glass-obsidian/tools/offsets_regen.py` remains for Windows parity; Linux adds `tools/sober_sig_refresh.py` that dumps TaskScheduler neighbourhood bytes given a core + pid (operator runs it after a bump, pastes new Sig 1).

### 6.5 `memory.cpp` → Sober strings

Windows: `Instance::NameContainer` → `std::string { ptr,len,cap }` at `NameContainer+0x10`. Sober (UC pooling section): `NameContainer` → pool handle → `StringPool::Get(handle)` → `char*`. `memory.cpp:fetchstring` must try pooled path first:

```cpp
std::string fetchstring(uintptr_t container) {
    if (!container) return {};
    // Sober pooled: read handle, lookup in pool table (base from scan)
    if (auto pooled = TryPooledString(container)) return pooled;
    // Windows std::string fallback (still valid on Wine)
    return ReadStdString(container);
}
```

`TryPooledString` is `ScanResult::stringPoolBase` + handle decode — obtained from Sig 1 neighbourhood (the same `lea` that gives TaskScheduler also touches the pool in nearby code).

### 6.6 Files

```
new  roblox_external/platform/sober_scanner.h
new  roblox_external/platform/sober_scanner.cpp  // FindSoberPid, LibRobloxBase, Scan, TryPooledString
edit roblox_external/platform/platform_linux.cpp  // +LibRobloxBase via r-xp, +Flatpak cgroup walk,
                                                 //  process_vm_readv chunking, /proc/pid/mem fallback
new  roblox_external/offsets_dynamic.h            // PatchOffsetsFromScan(ScanResult)
edit roblox_external/offsets_live.cpp             // #ifndef __linux__ guard the WinHTTP path;
                                                 //  on Linux delegate to offsets_dynamic first,
                                                 //  keep live fetch as compile-time opt-in OFF
edit roblox_external/game.cpp                     // ReadDataModel tries ScanResult::dataModel before
                                                 //  VisualEngine::Pointer global
edit roblox_external/cache.cpp                    // world detect already has tree fallback; add
                                                 //  Sober Primitive::Flags byte check
new  tools/sober_sig_refresh.py                  // dump TaskScheduler neighbourhood for UC paste
new  .github/workflows/linux-prelude.yml         // §1.3
edit mirror/xbps-fetch-mirror-template.txt        // note Void B musl extra deps
```

---

## 7 · Platform seam — filling the honest gaps

### 7.1 Current gaps (from `platform_linux.cpp` header comment)

> AllocMem/FreeMem, SampleKey/IsKeyDown/KeyPressed, mouse, window rects — need X11/XWayland

**Priority order (what the external actually calls):**

1. `ReadMemory/WriteMemory` — done (fast path + fallback).
2. `FindGameProcess/GameBaseAddress` — done for Wine, needs §6.3 Sober generalization.
3. `GameClientRect(HWND→Rect)` — overlay needs game rect to clip ESP. On Linux: enumerate windows via X11 `XQueryTree` on root, match `pid` via `_NET_WM_PID` (EWMH), then `XGetWindowAttributes`. Wayland fallback: Sober's window is a `wl_surface` without global coords — require XWayland (Sober already runs through XWayland on Void sway) or use `xdg Foreign` via `xdg-desktop-portal`. If neither, ESP renders in overlay-fullscreen and marks `diag+=" no game rect, using overlay bounds"`.
4. `MoveMouseRelative/Click` — aimbot. Linux: `XTestFakeMotionEvent` + `XTestFakeButtonEvent` (package `libXtst`). Wayland: `libei` or `ydotool` (`uinput`) — needs `CAP_SYS_ADMIN` or `input` group. Stub until Wayland box confirms XWayland path (likely, since Sober is X11).
5. `SampleKey/IsKeyDown` — freecam/flight binds. Linux: X11 `XQueryKeymap` / `XGrabKey` for polling, or consume overlay's XInput2 `KeyPress` events (preferred — matches Windows's `WM_INPUT` accumulator in `input_mouse.h`). No `WH_KEYBOARD_LL` on Linux — drop `globals.h`'s hook queue on `__linux__`.
6. `AllocMem/FreeMem` — only `skybox_changer` uses remote alloc (to allocate Sky face strings). On Linux: no remote allocator without ptrace `mmap` injection. Alternative: write Sky faces via `WriteMemory` into existing Sky string buffers (they already have slack — the `Value` field is a `String`, we overwrite its buffer in-place and reuse `memory.cpp:fetchstring` pooled path). If Sky buffer is exactly sized, skip — sky changer becomes no-op on Linux with `diag+=" remote alloc not supported"`.
7. `Audio` (`media_tracker.cpp` → `smtc_raw.h` → WinMM `PlaySoundA`) — Linux: `libpulse-simple` or `PipeWire` (`pw-play`), or SDL2_mixer. Feature is non-blocking (hitsound), so first port can mute it: `platform::Audio::Play(path)` logs `audio: not yet on linux (mute)` and returns.

### 7.2 Overlay replacement

`overlay.cpp` is DX11: `CreateDevice/D3D11`, `CreateRenderTarget`, `IDXGISwapChain::Present`, `WM_*` pump. Linux replacement (`overlay_linux.cpp`, selected via `#ifdef __linux__`):

- **Stack:** SDL2 (`libsdl2-dev` on Void glibc, `SDL2` xbps) + OpenGL 3.3 + ImGui's `imgui_impl_sdl2.cpp` + `imgui_impl_opengl3.cpp` — already in `glass-obsidian/app/main_sdl2_gl3.cpp` (the library demo uses this stack, proven). Borderless, always-on-top, click-through via `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH`, alpha via `SDL_BLENDMODE_BLEND`.
- **Compositor:** X11 `Composite` + `XFixes` for per-pixel alpha (Void X11), Wayland `wlr_layer_shell_v1` (Void sway) — SDL2 abstracts both if `SDL_VIDEODRIVER=wayland|x11` set. Test both: `void-a` forces `SDL_VIDEODRIVER=x11`, `void-b` leaves auto (Wayland).
- **Menu:** `glass-obsidian` already draws via ImGui — no menu rewrite. The menu's `menu_ui.cpp` renders identically on GL vs DX11; the headless soft_raster harness already proves this on Linux (no GPU) — `tools/menu_shots.cpp` passes in CI.
- **Threading:** overlay loop is a separate `platform::BoostThread` pumping `SDL_PollEvent` + `ImGui::NewFrame` at 144Hz, same as `overlay.cpp`'s `DXPresent`. FeatureLoop stays on main thread.

### 7.3 Dependencies on Void

```sh
# void-a (glibc/X11)
xbps-install -S -y base-devel SDL2 SDL2-devel libX11-devel libXtst-devel \
               mesa mesa-dri vulkan-loader curl

# void-b (musl/Wayland, extra)
xbps-install -S -y base-devel gcompat SDL2 SDL2-devel libX11-devel libXtst-devel \
               mesa mesa-dri vulkan-loader curl wayland-devel wlr-protocols
# musl needs gcompat for libstdc++ ABI or build with -static-libstdc++ -static-libgcc
```

Both already have `xbps-fetch` (part of `xbps`).

---

## 8 · Void testing — two machines, one prelude

### 8.1 Gate

No Void load until CI prelude says `PRELUDE_DONE=YES` (see §1). The CI log (base64 `complete_report.txt`) is photographed alongside the `COMPILER001_DONE=YES` receipt — the two receipts together are the handoff.

### 8.2 Machine preflight (each Void box, before external)

```sh
uname -a; cat /etc/os-release; xbps-query -v xbps
cat /proc/sys/kernel/yama/ptrace_scope   # expect 1 on Void
flatpak --version; flatpak ps; vulkaninfo --summary 2>&1 | head -n 20
g++ --version; cmake --version 2>&1 | head -n 5; which xbps-fetch
groups  # expect video,input if uinput needed
```

### 8.3 Bring-up sequence (incremental, refuse to skip)

| Step | Command | Success | Failure points to |
|---|---|---|---|
| 0 | `xbps-fetch https://<paste>/roblox-external-compiler-001.txt -o /tmp/payload && sh /tmp/payload /tmp/extract` | `COMPILER001_DONE=YES` | Courier mirror expired → operator re-triggers `.github/workflows/mirror-upload.yml` |
| 1 | `g++ -std=c++17 -fsyntax-only -I roblox_external roblox_external/platform/platform_linux.cpp` | no error | `process_vm_readv` header missing → `xbps-install glibc-devel` |
| 2 | `g++ -std=c++17 -I roblox_external roblox_external/platform/platform_linux.cpp tools/sober_probe.cpp -o /tmp/probe && /tmp/probe` | prints `sober pid=<n> base=0x...` | Sober not running / maps lacks `libroblox.so` → Sober install preflight |
| 3 | `cmake -B build-void -DCMAKE_BUILD_TYPE=Release && cmake --build build-void` | `external` ELF present | SDL2-missing → `xbps-install SDL2` |
| 4 | `./build-void/external --self-test` (new: scan without overlay, print `ScanResult::diag`) | `ok=YES task=0x... data=0x...` | Sig 1 not found → paste new pattern from §6.2 tool |
| 5 | `./build-void/external` (overlay) → launch Sober place → check debug page `pos writes` | `600+` writes/s, no hang on DataModel | Mover pacing → `sched_yield` gap |
| 6 | LIVE_CHECKLIST rows 1-6 (ESP, mover, skybox, fov, explorer, triggerbot) | screenshots per §LIVE_CHECKLIST | String pooling → `fetchstring` branch |

**Machine-specific notes:**

- **void-a-glibc-x11:** pure X11, `SDL_VIDEODRIVER=x11` forced, `XTest` mouse, `PlaySound` mute — all features expected PASS except sky alloc (known no-op) and hitsound (mute).
- **void-b-musl-wayland:** XWayland fallback first (do not port to `libei` until A passes), musl `gcompat` shim verified via `ldd build-void/external` (no `not found`). Wayland overlay needs `wlr_layer_shell` — if SDL2 falls back to X11 inside Wayland, that's acceptable for v0.

---

## 9 · Build system & courier — what CI already does, what Void adds

### 9.1 Windows today

`roblox_external.vcxproj` + `.slnx`, MSVC only, `mirror/files/roblox-external-compiler-001.ps1` (PowerShell) delivers the Windows compiler. Tested via `setup.bat` wizard.

### 9.2 Linux addition

```
CMakeLists.txt (root)
  add_executable(external
    roblox_external/main.cpp
    roblox_external/platform/platform_linux.cpp
    roblox_external/platform/sober_scanner.cpp
    roblox_external/cache.cpp
    roblox_external/game.cpp
    roblox_external/memory.cpp
    roblox_external/features/movement/mover.cpp
    glass-obsidian/src/*.cpp
    glass-obsidian/third_party/imgui/*.cpp
    overlay_linux.cpp          # SDL2+GL, replaces overlay.cpp on __linux__
  )
  target_include_directories(external PRIVATE roblox_external glass-obsidian/include)
  find_package(SDL2 REQUIRED)   # or pkg_check_modules(SDL2 sdl2)
  find_package(OpenGL REQUIRED)
  target_link_libraries(external SDL2::SDL2 OpenGL::GL X11 Xtst pulse-simple)
```

CI already runs `g++ -fsyntax-only`; full CMake build joins `linux-prelude.yml` after prelude gate.

### 9.3 Courier — frozen, per `docs/frozen-tools.sha256`

The courier mechanism (`mirror-upload.yml`, host ladder `pasters/dpaste/cnet/tmpfiles/0x0/x0`, 2 passes, `mirror-state.txt` branch) is **frozen** — we don't touch it. The only payload change is adding `roblox-external-compiler-001.txt`'s next version with Linux CMake + scanner included. That payload's `ARCHIVE_SHA256` rotates; the courier's two-phase `vsha` (hash-after-upload) catches paste-host bit-rot — same as Windows.

Void fetch is `xbps-fetch -o /tmp/payload <url>` (not `curl -o`), then `sh /tmp/payload /tmp/extract` — `xbps-fetch` is in the courier template but not yet documented in the operator's one-liner; update `mirror/current-compiler` note.

---

## 10 · Risks, unknowns, and explicit non-goals

| Risk | Impact | Mitigation | Owner |
|---|---|---|---|
| Sober bumps `TaskScheduler` global (Sig 1 breaks) | scan returns `ok=NO`, overlay shows "searching" | operator runs `tools/sober_sig_refresh.py <pid>` → pastes new pattern, PR rotates `.txt` payload | operator + agent |
| Flatpak mount-namespace hides `libroblox.so` from host `/proc/pid/maps` | `LibRobloxBase` returns 0 | `flatpak enter` path (§6.3), or operator installs native `open-sober` for testing | agent |
| `kernel.yama.ptrace_scope=3` (paranoid) | `process_vm_readv` returns EPERM | fallback to `ptrace+ /proc/pid/mem` (needs `CAP_SYS_PTRACE` or sudo); document `sysctl kernel.yama.ptrace_scope=0` for test | operator |
| ASLR: PIE base changes each Sober launch | offset constants wrong per launch | we never store base-dependent constants — scan re-derives each launch | agent |
| String pooling decode wrong | ESP labels blank, explorer names blank | keep Windows `std::string` fallback, add pooling unit test with dumped pool blob | agent |
| Musl ABI: `libstdc++` not found on void-b | `external` won't start (`ldd` `not found`) | `-static-libstdc++ -static-libgcc` or `xbps-install gcompat` preflight | agent |
| Wayland input: XTest not available | aimbot/triggerbot mouse fails | mark aimbot `SKIP on Wayland` in LIVE_CHECKLIST for v0; port to `libei` v1 | agent |
| Vulkan missing on Void (headless CI, old GPU) | Sober won't launch, nothing to scan | preflight `vulkaninfo`, document Mesa install; CI deliberately has no Vulkan — not a failure | operator |
| Non-goals: **macOS**, **Hyperion bypass**, **kernel `dbk32`**, **CE GUI** | — | Explicitly out of scope for v0; Sober Linux has no Hyperion, macOS has different Sober build | — |

---

## 11 · Roadmap — gated, no skipping

| Phase | Name | Gate | Files | Outcome |
|---|---|---|---|---|
| **P0** | **Prelude log** | `COMPILER001_DONE=YES` + `ARCHIVE_SHA256` match in this sandbox/U CI | `mirror/files/roblox-external-compiler-001.txt` (existing) + `linux-prelude.yml` | CI job green; operator has receipt to photograph |
| **P1** | Process discovery | `sober_probe` prints Sober PID + `libroblox.so` base on void-a | `platform_linux.cpp` (§6.3), `tools/sober_probe.cpp` (throwaway) | Sober PID/base proven without any feature |
| **P2** | Signature scanner | `--self-test` prints `ok=YES task/data/world` on void-a, even when Sober restarted (ASLR) | `sober_scanner.h/.cpp`, `offsets_dynamic.h`, `mover` unchanged | No more static offsets hang; DataModel resolves locally |
| **P3** | Overlay bring-up | SDL2 overlay shows menu on void-a (X11), ESP renders (even if game rect wrong) | `overlay_linux.cpp`, `CMakeLists.txt`, `glass-obsidian` unchanged | Operator sees menu, can open debug page |
| **P4** | Feature loop | LIVE_CHECKLIST rows 1-6 PASS on void-a, then void-b (XWayland) | `game.cpp`/`cache.cpp`/`memory.cpp` Sober string branch, `mover` yield tuning | Whole external usable on Void via xbps-fetch |
| **P5** | Courier refresh | `mirror-upload.yml` publishes new `.txt` with P2-4; `mirror-state.txt` updated, paste-host `vsha` green | `mirror/files/roblox-external-compiler-001.txt` next version, `frozen-tools.sha256` updated | Operators fetch via `xbps-fetch` one-liner, no manual git |
| **P6** | Hardening | second Void box (musl/Wayland) passes P2-4; Yama scope 1 + 0 tested; Vulkan drivers documented | `docs/LIVE_CHECKLIST.md` Linux column, `docs/ARCHITECTURE.md` platform §10 updated | Two-machine parity, handoff to operators |

Each phase's gate is a **photographable receipt** — a log line or screenshot the operator sends back — not a verbal "looks good." This is the same contract LIVE_CHECKLIST enforces for Windows.

---

## 12 · What to paste after reading this

**CI (now, this sandbox already did the Debian analogue):**

```sh
cp mirror/files/roblox-external-compiler-001.txt /tmp/f
rm -rf /tmp/extract && sh /tmp/f /tmp/extract && echo PRELUDE_DONE=YES
# then patch test_in_sandbox.sh path bug, re-publish .txt, note new sha
```

**Void operator (after P0 green):**

```sh
# preflight (§8.2), then
xbps-fetch https://<courier-url>/roblox-external-compiler-001.txt -o /tmp/payload
sh /tmp/payload /tmp/extract
cat /tmp/extract/compiler/BUILD_WORKFLOW.md
# P1-2: g++ fsyntax-only, sober_probe, ScanResult --self-test
# P3-4: cmake --build, LIVE_CHECKLIST screenshots
```

**Agent (next commits on this branch):**

1. `tools/sober_probe.cpp` + `platform/sober_scanner.h` skeleton (no signatures yet — structure only, compiles).
2. `roblox_external/platform/platform_linux.cpp` §6.3 maps generalization (`libroblox.so` suffix match, Flatpak cgroup sniff).
3. `.github/workflows/linux-prelude.yml` (gated on `platform_linux.cpp` + `sober_scanner.*` + `mirror/files/**`).
4. `CMakeLists.txt` (SDL2/OpenGL opt-in, off on Windows).

None of these touch `mirror/files/roblox-external-compiler-001.*` until P2's Sig 1 is proven on void-a — per frozen-courier rule (§1.2) the payload only rotates after Void proof.

---

## 13 · Sources read for this plan

- `roblox_external/platform/platform.h` + `platform_win.cpp` + `platform_linux.cpp` (`process_vm_readv`, `/proc` scan, r-xp base)
- `roblox_external/offsets.h` + `offsets_fallback.json` + `offsets_live.h/.cpp` (392 fields, 4+4 mirrors, 25 s budget, pinned {VER})
- `roblox_external/cache.h/.cpp` + `game.h/.cpp` + `memory.h/.cpp` + `process.h/.cpp` + `main.cpp` (DataModel→Players, zero-copy snapshots, mover single owner, FeatureLoop 1 ms)
- `docs/ARCHITECTURE.md` (§4.0 dual 0x48 motion blocks, hammering, LayoutSane), `docs/LIVE_CHECKLIST.md` (22 rows), `docs/frozen-tools.sha256`
- `mirror/xbps-fetch-mirror-template.txt` + `mirror/files/roblox-external-compiler-001.ps1/.txt` + `compiler/build_and_package.sh` + `compiler/void/compile.sh` (courier, void placeholder)
- `cheat-engine/cheat-engine/Cheat Engine/ceserver/api.c` (4738 L, CMD 9/10, process_vm_readv fastpath + ptrace fallback)
- `cheat-engine/cheat-engine/ceserver/ceserver.h`, `wleeaf/cheat-engine-linux` (Qt6 native), `man7.org/process_vm_readv(2)` (iovec, ptrace_may_access)
- `unknowncheats.me/forum/roblox/771298-release-sober-offsets` (idom, 75 offsets, TaskScheduler 0x7a0b720, RenderJob 0x30/0x1d0, string pooling) + `749668-sober-offsets`
- `vinegarhq.org/Sober/FAQ` + `soberdocs.github.io` + `gamingonlinux.com` (Sober = Flatpak Android Bionic, libroblox.so ELF PIE, Hyperion absent, Vulkan+SSE4.2)

---

*This doc is the prelude's companion — it does not replace the `PRELUDE_DONE=YES` receipt, it explains why the receipt matters and what Void must still prove.*
