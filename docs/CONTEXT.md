# Repo Context

Machine-oriented context for agents. Humans read `README.md`; agents read this plus the
passthrough brief. Everything here is checked against the checkout — if a path is listed
and missing, `--checks` says so.

## Layout — the agent layer

| Path | Purpose |
|---|---|
| `README.md` | human identity + the Agent Operating Protocol verbatim (see its own section) |
| `ToDo.md` | roadmap + `## Session Memory` (the human-readable log, 12 lines) |
| `docs/DOCTRINE.md` | the six rules this repo enforces, and what they do *not* cover |
| `docs/CONTEXT.md` | this file — repo context (agent layer *and* host map) |
| `agent/context.ts` | **the passthrough.** One typed command replacing ~30 bash calls |
| `agent/memory/session.jsonl` | full session log, append-only, machine-readable |
| `agent/verification/CAPABILITIES.md` | dated capability evidence + the repo check registry |

## Layout — the host repo (what the agent layer is sitting in)

| Path | Purpose |
|---|---|
| `daemon/` | the app: `main.cpp` (Linux main + loops), `overlay.h/.cpp` (SDL2+GL overlay), `menu_linux.cpp` (Linux menu glue), `ui/menu_ui.cpp` (platform-free menu, Glass Obsidian widgets), `globals.h` (all settings) |
| `daemon/features/` | 27 dirs: one per feature, plus shared rule headers (`esp/range.h`, `fov/fov_encode.h`, `misc/body_rules.h`, `raycast/world_ray.h`); every motion write goes through `features/movement/mover.cpp` |
| `daemon/platform/` | the platform seam — `platform.h` is the only surface features may call; `platform_linux.cpp` is the real Linux impl (process_vm_readv/writev, X11 overlay control) |
| `daemon/offsets.h` | offsets for one client version, **merged never pasted** (`tools/offsets_regen.py`); zeroed on Linux, filled by `offsets_dynamic.cpp` at runtime |
| `daemon/offsets_dynamic.cpp` | runtime offset discovery via `sober_scanner` — no Windows fallback |
| `daemon/imgui/` | vendored Dear ImGui 1.91.1 (SDL2+OpenGL3 backends only) |
| `glass-obsidian/` | the UI library, its headless harness, its software rasteriser, and the shot gallery |
| `glass-obsidian/tools/check_win_tu.sh` | parses the app's TUs against a stub `<Windows.h>` — proves no Windows headers leak (runs off Windows) |
| `glass-obsidian/tools/link_seam_check.sh` | compiles every TU the CMake build lists (app + bootstrapper) and relocatable-links them — any undefined symbol that is ours is the seam break the operator's box would die on |
| `glass-obsidian/tools/menu_shots.cpp` | renders the real `ui/menu_ui.cpp` headlessly and checks nothing draws outside its clip |
| `docs/ARCHITECTURE.md` | the technical record: motion blocks, offsets pipeline, platform seam, open problems |
| `docs/CHANGELOG.md` | branch changelog, newest first, with the reasoning |
| `docs/offsets_dump.txt` | the raw dump for the shipped client version |
| `docs/LIVE_CHECKLIST.md` | the Linux session as a script: one step per ❓ row in `README.md`, what answers it, what to send back |

## Commands

```bash
node agent/context.ts                  # full brief (git, tree, docs, memory, board), logs the pass
node agent/context.ts --note "text"    # same + a note attached to the log entry
node agent/context.ts --quiet          # brief without the doc dumps (large repo / tight budget)
node agent/context.ts --json           # raw JSON for programmatic use; READ-ONLY (logs nothing)
node agent/context.ts --probe          # environment + capability probes (pass/fail + detail)
node agent/context.ts --checks         # the repo's fast invariants (<1s)
node agent/context.ts --checks=full    # + check_win_tu.sh + link_seam_check.sh + both g++ harnesses (build cached in /tmp)
npm run ctx | npm run ctx:probe | npm run ctx:json | npm run ctx:checks
```

Exit code: `0` clean, `2` at least one probe or check failed. The brief is always printed.

## Environment facts

- Node ≥ 22.18 executes `.ts` by type stripping → **zero-dependency passthrough**.
  Verified on this sandbox: `node v22.22.3`. `npm run ctx` works with no `npm install`.
- `tsconfig.json` exists for editors. `tsc` typechecking needs `typescript` +
  `@types/node` (`npm i -D typescript @types/node`); the passthrough itself never needs
  them, so nothing is installed in the repo. **Verified 2026-10-16: `tsc --strict
  --noEmit` passes clean** (TypeScript 7.0.2 / @types/node 26.6.5, installed to `/tmp`
  only). Re-run that after any edit to `agent/context.ts` — the two type errors it found
  on the first pass were real.
- Verified available here: `git 2.39.5`, `python3 3.11.2`, `g++ 12.2.0 (Debian)`,
  `sh`. **`cmake` is absent**, so `glass-obsidian/CMakeLists.txt` cannot be configured —
  the harnesses are built with the g++ one-liners in `glass-obsidian/README.md`, which is
  exactly what `--checks=full` does.
- **No Windows toolchain.** `msbuild`/`cl.exe` do not exist here: the app cannot be built
  or run, only parsed (`check_win_tu.sh`) and reasoned about. Do not claim a Windows
  verification you did not perform.

## The two invariants `--checks` enforces

Both are load-bearing, both are cheap to check, and both have a documented reason:

1. **`<Windows.h>` stays in the Win32 layer.** None in this repo — we are Linux-only.
   All platform calls go through `platform::` seam.
2. **No `ReadProcessMemory` / `WriteProcessMemory`, not even as a name.**
   Linux uses `process_vm_readv`/`process_vm_writev` via `platform::ReadMem`/`WriteMem`.

A third, softer check: `daemon/offsets_fallback.json` still parses as JSON (it is
generated alongside `offsets.h` and is what the app falls back to when no offset source
answers).

## Conventions

- Markdown for humans, JSONL for machines. `agent/memory/session.jsonl` is append-only.
- Commit messages teach: what changed, why, and what was measured. Follow
  `docs/CHANGELOG.md`'s voice, not a one-word subject.
- Never paste a dumper header over `offsets.h`; regenerate with
  `python3 glass-obsidian/tools/offsets_regen.py <header> --write`.
- Never write one field of one motion block: `mover::WriteCFrame` /
  `WriteVelocity` / `WriteVelocityAndSpin`, both blocks, block B last.
- Keep the passthrough dependency-free (Node built-ins only) and the brief small: it is
  read on every response, so every line it prints has to earn its place.
- Facts about the app belong in the host's docs; facts about the *session* belong in the
  memory log. `docs/CHANGELOG.md` is for shipped work.