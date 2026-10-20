# Doctrine

This repo carries the **agent operating layer** from
[Glockerz/Repo-Template](https://github.com/Glockerz/Repo-Template): doctrine, context,
plans, memory, and one capability-verified passthrough, all *inside* the repo, so a fresh
agent session can cold-start in one command, remember what happened, and prove itself
before doing work.

It is not a copy of the template. The template is generic C++-agnostic; this repo is a
Windows C++ external that is developed mostly from **Linux sandboxes that cannot build
the Windows half**. That asymmetry is what the layer was fitted to: the passthrough
dumps the host's real map, and `--checks` runs the invariant checks that *do* work here
instead of pretending a Linux box can run `msbuild`.

## The six rules

1. **Self-describing repo.** Doctrine, context, and plans live in the repo —
   `docs/DOCTRINE.md` (this file), `docs/CONTEXT.md`, `ToDo.md`. An agent never has to
   guess a convention; it reads one. The host repo's own depth (`README.md`,
   `docs/ARCHITECTURE.md` §10, `docs/CHANGELOG.md`) is context too and the brief links it
   rather than restating it.

2. **One passthrough, not thirty commands.** The legacy workflow — `git status`,
   `git log`, `git diff`, `ls -R`, `cat` every doc, tail logs, count tokens, check the
   environment — is one typed command:

   ```bash
   node agent/context.ts          # or: npm run ctx
   ```

   It is run **before every response**. It parses full context (git, tree, docs, host
   map), reads short-term memory, and emits a problem-solving board. Zero dependencies:
   Node ≥ 22.18 runs the `.ts` by type stripping, so it works on a fresh checkout with no
   `npm install`.

3. **Session Memory logs everything.** Every pass appends a dated, machine-readable entry
   to `agent/memory/session.jsonl` (append-only, never pruned) and mirrors the 12 most
   recent entries into `ToDo.md`'s `## Session Memory` section. Nothing a session did is
   lost; the next session inherits it.

4. **Verify before trusting.** Capabilities are probed, never assumed:
   `agent/verification/CAPABILITIES.md` records dated evidence for image sight, tooling,
   thinking/frontier, and context tolerance. Anything unprobed is reported as
   **UNVERIFIED** in the brief's open-questions block, and the brief stays UNCERTAIN about
   it until a probe lands.

   This repo already had that habit before the agent layer arrived — `check_win_tu.sh`,
   `tools/mover_pace_test.cpp`, the headless harnesses — so the doctrine extends it rather
   than introducing it: **`node agent/context.ts --checks` runs the repo's own cheap
   invariants** (the Windows.h containment rule, the no-direct-WinAPI-read/write rule, the
   offsets fallback parses), and `--checks=full` adds the two g++ harnesses that prove the
   app's UI and the library headlessly.

5. **Branch discipline.** Work happens only on the session branch
   (`arena/13aa7aab-roblox-external` for this session). Never create, switch to, or push
   another branch; never push `main`. The passthrough's risk block flags a branch that is
   neither the session branch nor `main`.

6. **Artifact discipline.** Generated bloat stays out of Git: probe corpora and build
   caches go to `/tmp` (the passthrough caches harness binaries under
   `<tmp>/roblox-external-checks/`). The one committed exception is the shot gallery
   (`glass-obsidian/out/`, `glass-obsidian/preview/`) — and it is committed because the
   encoder that produces it lives in the repo too (`tools/encode_shots.py`); ARCHITECTURE
   §9 records what happened when that pipeline was a scratch-dir script nobody could
   re-run.

## What the doctrine does not do

- **It does not commit, push, merge, or open PRs by itself.** It logs and reports; the
  agent still has to say what it did.
- **It does not build the Windows half.** No `msbuild` exists in a Linux sandbox and no
  amount of TypeScript changes that. `check_win_tu.sh` *parses* 26 of the app's TUs against
  a stub `<Windows.h>`; `main.cpp`, `overlay.cpp` and `media_tracker.cpp` are checked on
  the Windows side, by hand. A green `--checks` is "nothing here broke the parseable half",
  not "the exe links".
- **It does not touch the game.** Nothing in `agent/` opens a process, reads memory, or
  makes a network request. The layer is documentation, a git/doc reader, and two g++
  invocations.
- **It does not replace the host's docs.** `docs/ARCHITECTURE.md` remains the record of
  *why* the code is shaped the way it is; this layer is the record of *what the repo is*
  and *what the last session did*.

## Session lifecycle (what every future session does)

1. `node agent/context.ts --note "<why this session started>"` — read the brief.
2. Do the requested work — on the session branch only.
3. `node agent/context.ts --checks` before ending a turn that touched code;
   `--checks=full` when the UI or the library moved.
4. `node agent/context.ts --note "<what was done>"` before ending the turn, so the next
   session starts with it.
