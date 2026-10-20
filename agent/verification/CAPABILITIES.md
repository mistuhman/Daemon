# Capability Verification Record

Repo: `mistuhman/Daemon` · session branch `arena/d0f99e6b-daemon`
First verified: **2026-10-20** · sandbox: Linux, `node v22.22.3`, `g++ 12.2.0`, no `cmake`,
no MSVC.

DOCTRINE rule 4: a capability that has not been probed is reported as **UNVERIFIED**,
never assumed. The passthrough reads the `- UNVERIFIED` lines of this file into the
brief's open-questions block, so this file is both the evidence and the list.

---

## Verified — 2026-10-20

### Image sight — PASS (exact transcription)

- **Probe**: `generate_image` from a tight spec (exact strings, pure primary colours,
  fixed order), then `read_file` the result. Artifact: `agent/verification/vision_probe.png`.
- **Seen, exactly as specified**: headline `AGENT VISION PROBE`; second line
  `Daemon / arena/d0f99e6b-daemon`; three solid swatches left→right **RED / GREEN /
  BLUE** with each square's label beneath it; code string `K7-Q3-91`; solid neutral gray
  ground. Every string was transcribed correctly, the colour order was correct, and no
  element was hallucinated.
- **Verdict**: PASS. Not probed: legibility below ~8 px, and discrimination between
  neighbouring pastel shades.

### Tooling — PASS (measured, not assumed)

`node agent/context.ts --probe` on this sandbox:

```
PASS · runtime        · node v22.22.3 executing .ts via type stripping (zero dependencies)
PASS · ts-type-stripping · typed const evaluated
PASS · git            · branch=arena/d0f99e6b-daemon
PASS · fs-roundtrip   · write+read a temp file
PASS · tool-python3   · Python 3.11.2
PASS · tool-g++       · g++ (Debian 12.2.0-14+deb12u1) 12.2.0
PASS · tool-make      · GNU Make 4.3
FAIL · tool-cmake     · not present
INFO · windows-toolchain · absent by design (no msbuild/cl.exe)
```

**Type check** — `agent/context.ts` compiles clean under `tsc --strict --noEmit`
(TypeScript 7.0.2, @types/node 26.6.5, installed to `/tmp` only; the repo stays
dependency-free). This is a *stronger* check than Node's type stripping, which erases
types without looking at them: it caught two real errors
(`cap()` typed `string[]` while `appendMemory` feeds it `MemoryEntry[]`). Re-run it after
any edit to the passthrough.

**Consequences** (also in `docs/CONTEXT.md`): `glass-obsidian/CMakeLists.txt` cannot be
configured here, so the two harnesses are built with the g++ one-liners from
`glass-obsidian/README.md` — which is exactly what `--checks=full` does. The Windows half
of the app can be **parsed** (`check_win_tu.sh`) and reasoned about; it cannot be built,
linked or run. No agent session on this side may claim a Windows verification.

### Context tolerance — PASS (measured, mid-file)

- Corpus: **12,001 lines / 516,922 bytes** written to `/tmp/ctx-tolerance-corpus.txt`
  (never into Git — doctrine rule 6), sentinel at line 6000, tail marker at line 12000.
- Read back at **offset 5997, limit 7**: line 6000 came back verbatim as
  `line-6000: SENTINEL-MARKER alpha=1071 beta=2451 gamma=3831`, with both neighbours
  matching an independent `python3` dump of the same file. Tail marker read at 12000.
- **Verdict**: PASS — a 500 KB file is addressable by line offset with no summarisation
  or drift. Not probed: a single pass over a corpus much larger than ~12k lines.

### Thinking / frontier — PASS (4 probes)

1. **Letter count** — `strawberry` holds **3** `r`s (`r` at positions 3, 8, 9).
2. **Syllogism** — *All Glaives are shards. No shard is quiet.* → **No Glaive is quiet**
   (valid: the second premise denies the predicate of the whole class the first premise
   places Glaives inside).
3. **Rate invariance** — 60 miles out at 30 mph, 60 back at 60 mph: 120 miles / 3 hours =
   **40 mph**, not the 45 the arithmetic mean gives. (Harmonic mean; the trap is real.)
4. **Repo-specific (the one that matters here)** — the two motion blocks in
   `Primitive` are one stride apart. Derived block B from block A + stride, then checked
   the file:
   `0xB0/0xD4/0xE0/0xEC + 0x48 = 0xF8/0x11C/0x128/0x134` — and `offsets.h` declares
   `Position2 = 0x11c`, `AssemblyLinearVelocity2 = 0x128`, with the same table in the
   comment above it. **Derived == declared.**

---

## The repo check registry (`--checks`)

The layer extends the template's capability probes with the host repo's *own* truth. The
five fast checks run on **every** pass (they cost ~60 ms and appear in every brief);
`--checks` prints the detail; `--checks=full` adds the four that need a compiler.

| Check | Cost | What it proves |
|---|---|---|
| `windows-h-containment` | ~30 ms | `<Windows.h>` appears **nowhere** in the Linux-only Daemon codebase (0 includers). A feature file that reaches for Win32 directly fails here — ARCHITECTURE §10 |
| `no-winapi-read-write` | ~40 ms | `ReadProcessMemory` / `WriteProcessMemory` appear nowhere in the codebase. Every read/write goes through `platform::ReadMem` / `WriteMem` over `/proc` or `process_vm_readv` |
| `offsets-parity` | ~5 ms | `offsets_fallback.json` parses *and* names the same client version as `offsets.h` (399 offsets). Version drift is what bit this repo twice |
| `no-deprecated-crt` | ~10 ms | No call MSVC reports as unsafe (C4996: `strncat`, `strcpy`, `strcat`, `strncpy`, `sprintf`, `fopen`, `getenv`, ...) in the app files; `*_s` variants are not matched and `platform_linux.cpp` is excluded by name. Its negative path was verified by adding a `strncat` to `esp.cpp`, which failed the check, then removing it |
| `required-files` | ~5 ms | The agent layer and the host's load-bearing files all still exist |
| `obsidian-headless` *(full)* | ~33 s run + ~18 s build | The library's own harness: **13/13 invariants passed**, 13 PNGs written. No GPU, no window system |
| `features-geometry` *(full)* | ~1 s run + ~6 s build | `glass-obsidian/tools/world_ray_test.cpp` against the app's real headers: **184 cases, 0 failed** (raycast geometry + grid, draw range, FOV storage form, save/restore, aim tracking, ballistics, flight's ramp/caps/hover-bias/write-watch, frame-rate classification/clamp/pacing, sky arithmetic for the time-of-day write) plus the grid-vs-flat benchmark (19.9 ms vs 954 ms) |
| `menu-shots` *(full)* | ~22 s run + ~20 s build | The app's real `ui/menu_ui.cpp` rendered headlessly: **82 checks, all ok**, 39 PNGs, `0 command(s) outside their clip` |

Timings measured 2026-10-20 on this sandbox: `--checks=full` cold ≈ **120 s** (all three built), warm ≈ **80 s** (run only, binaries cached under
`<tmp>/daemon-checks/` — never in Git), fast checks ≈ **0.1 s**,
`node agent/context.ts` itself ≈ 0.4 s.

What is *not* in the registry: anything needing Windows, a GPU, or a running Roblox
client. There is no point running a check that can only ever say "N/A" here — those stay
in `docs/ARCHITECTURE.md` as manual procedures on the Windows side.

---

## Unverified

- UNVERIFIED: live-server behaviour of each ❓ row in `README.md`'s feature table
  (tracers, hitbox expander, dead check, wall check, skybox validity bytes, config
  round-trip) — each needs a running client, a measurement, and a doc line
- UNVERIFIED: the Windows build itself — `msbuild`/DX11 overlay, tray, taskbar, and the
  linked exe. Parse ≠ link, and nothing on a Linux sandbox can close that gap
- UNVERIFIED: image sight below ~8 px text and between close pastel shades
- UNVERIFIED: context tolerance for a single pass much larger than the 12k-line corpus
  probed here

## Re-probe procedure

Run `node agent/context.ts --probe` (environment) and `node agent/context.ts --checks=full`
(repo) after any runtime, sandbox or toolchain change, then amend this file with the date
and the measured results — replace a verdict, never delete the old line. The vision probe
is regenerated with the same spec into `agent/verification/vision_probe.png`; the corpus
probe is rebuilt in `/tmp` and must not be committed.
