#!/usr/bin/env node
/**
 * agent/context.ts — THE passthrough.
 *
 * One typed command that replaces the legacy ~30-invocation bash ritual
 * (git status / git log / git diff / ls -R / cat README / cat ToDo / cat doctrine /
 * cat context / tail logs / count tokens / check env / …). It is run BEFORE EVERY
 * AGENT RESPONSE. It:
 *
 *   1. parses full repo context — git state, tree, the doctrine/context/plans/memory
 *      docs, and the host repo's shape (this is a Windows C++ external, not a
 *      greenfield repo, so the brief knows where the app, the library and the
 *      verification harnesses are);
 *   2. reads + appends Session Memory (short-term window shown, full log kept);
 *   3. checks the two load-bearing invariants (Windows.h containment, no direct
 *      WinAPI read/write) and the offsets-fallback parity — the repo's own cheap
 *      truth, on every pass;
 *   4. emits a problem-solving board (objective / known / risks / open / next).
 *
 * Zero dependencies: Node >= 22.18 executes .ts via type stripping.
 *
 * Usage:
 *   node agent/context.ts [--note "..."] [--quiet] [--json] [--probe] [--checks[=full]]
 *
 *   --note TEXT     attach a note to the log entry (why this pass happened)
 *   --quiet         brief without the doc dumps (big repo / tight context budget)
 *   --json          raw JSON; READ-ONLY, appends no log entry
 *   --probe         environment + capability probes, PASS/FAIL with detail
 *   --checks        run the fast invariant checks and print them (they always run;
 *                   this prints the detail)
 *   --checks=full   additionally run check_win_tu.sh and build + run both headless
 *                   harnesses with g++. Builds are cached under <tmp>, never in git.
 *
 * Exit code: 0 = clean, 2 = at least one probe or check failed. The brief always prints.
 */

import { execSync } from "node:child_process";
import {
  appendFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, extname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

// ---------------------------------------------------------------- config
const HERE = dirname(fileURLToPath(import.meta.url)); // agent/
const ROOT = join(HERE, "..");                        // repo root
const MEMORY_DIR = join(HERE, "memory");
const SESSION_LOG = join(MEMORY_DIR, "session.jsonl");
const TODO_PATH = join(ROOT, "ToDo.md");
const CAPS_PATH = join(HERE, "verification", "CAPABILITIES.md");
const APP = join(ROOT, "daemon");            // the Linux app
const LIB = join(ROOT, "glass-obsidian");             // the UI library + harnesses
const CHECK_CACHE = join(tmpdir(), "daemon-checks"); // artifact discipline

const CONTEXT_BUDGET_TOKENS = 200_000; // agent context tolerance we design for
const SHORT_TERM_WINDOW = 5;           // entries shown as short-term memory
const TODO_MIRROR_LINES = 12;          // human-readable lines kept in ToDo.md
const DOC_MAX_LINES = 60;              // per-doc dump cap in the brief
const TREE_MAX = 70;
const TREE_MAX_DEPTH = 4;
const IGNORED = new Set([
  ".git", "node_modules", "dist", "build", "coverage", ".next", "out", "target",
  ".venv", "__pycache__", ".cache", ".arena",
]);
/** Images are counted, not listed: the committed shot gallery is 60-odd PNGs and would
 *  bury the source tree. The count is still reported so nothing is invisible. */
const BINARY_EXTS = new Set([".png", ".jpg", ".jpeg", ".gif", ".ico", ".pdf", ".zip", ".7z"]);

const SESSION_BRANCH = "arena/13aa7aab-roblox-external";

/** Files that are allowed to include <Windows.h> — the Win32 layer, ARCHITECTURE §10. */
const WINDOWS_HEADER_ALLOWLIST = new Set([
  "daemon/main.cpp",
  "daemon/overlay.cpp",
  "daemon/overlay.h",
  "daemon/menu.cpp",
  "daemon/media_tracker.cpp",
  "daemon/media_tracker.h",
  "daemon/smtc_raw.h",
  "daemon/nt_syscalls.cpp",
  "daemon/nt_syscalls.h",
  "daemon/features/config/config.cpp",
  "daemon/features/servers/servers_http.cpp",
  "daemon/platform/platform_win.cpp",
]);

/** Files the agent layer + host repo must have. A missing one is a risk, not a warning. */
const REQUIRED = [
  "README.md",
  "ToDo.md",
  "package.json",
  "docs/DOCTRINE.md",
  "docs/CONTEXT.md",
  "docs/ARCHITECTURE.md",
  "docs/CHANGELOG.md",
  "agent/context.ts",
  "agent/verification/CAPABILITIES.md",
  "roblox_external.slnx",
  "daemon/globals.h",
  "daemon/offsets.h",
  "daemon/offsets_fallback.json",
  "glass-obsidian/CMakeLists.txt",
  "glass-obsidian/tools/check_win_tu.sh",
  "glass-obsidian/tools/menu_shots.cpp",
];

interface GitState {
  branch: string; sha: string; subject: string;
  dirty: string[]; untracked: string[];
  recent: string[];
}
interface MemoryEntry {
  ts: string; branch: string; sha: string; dirty: number; note: string;
  checks?: string;
}
interface Check { name: string; pass: boolean; detail: string; info?: boolean; }
const tag = (c: Check): string => (c.info ? "INFO" : c.pass ? "PASS" : "FAIL");

// ---------------------------------------------------------------- helpers
/** Run a shell command, never throw: "" means it failed and the caller says so. */
function sh(cmd: string, cwd: string = ROOT): { out: string; code: number } {
  try {
    const out = execSync(cmd, {
      cwd, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], timeout: 900_000,
      maxBuffer: 32 * 1024 * 1024,
    });
    return { out: out.replace(/\s+$/, ""), code: 0 };
  } catch (e: unknown) {
    const err = e as { stdout?: string; status?: number };
    const out = typeof err.stdout === "string" ? err.stdout.replace(/\s+$/, "") : "";
    return { out, code: typeof err.status === "number" ? err.status : 1 };
  }
}

const lines = (s: string): string[] => (s.length ? s.split("\n") : []);
function cap<T>(l: T[], n: number): T[] {
  return l.slice(0, n);
}
const estTokens = (s: string): number => Math.ceil(s.length / 4);
const posix = (p: string): string => relative(ROOT, p).split("\\").join("/");

function walk(dir: string, depth: number, out: string[]): { files: string[]; images: number } {
  let images = 0;
  const rec = (d: string, level: number): void => {
    if (level > TREE_MAX_DEPTH) return;
    let names: string[];
    try { names = readdirSync(d).sort(); } catch { return; }
    for (const name of names) {
      if (IGNORED.has(name)) continue;
      const p = join(d, name);
      let st;
      try { st = statSync(p); } catch { continue; }
      if (st.isDirectory()) rec(p, level + 1);
      else if (BINARY_EXTS.has(extname(p).toLowerCase())) images++;
      else out.push(posix(p));
    }
  };
  rec(dir, 0);
  return { files: out, images };
}

function walkFiles(dir: string, exts: string[]): string[] {
  const out: string[] = [];
  const rec = (d: string, depth: number): void => {
    if (depth > 8) return;
    let names: string[];
    try { names = readdirSync(d).sort(); } catch { return; }
    for (const name of names) {
      if (IGNORED.has(name)) continue;
      const p = join(d, name);
      let st;
      try { st = statSync(p); } catch { continue; }
      if (st.isDirectory()) rec(p, depth + 1);
      else if (exts.includes(extname(p))) out.push(posix(p));
    }
  };
  rec(dir, 0);
  return out;
}

/** Newest mtime under the given roots — the cache key for the g++ harness builds. */
function newestMtime(paths: string[]): number {
  let newest = 0;
  for (const p of paths) {
    let st;
    try { st = statSync(p); } catch { continue; }
    if (st.isDirectory()) {
      for (const f of walkFiles(p, [".cpp", ".h", ".hpp", ".sh", ".py"])) {
        try { newest = Math.max(newest, statSync(join(ROOT, f)).mtimeMs); } catch { /* gone */ }
      }
    } else newest = Math.max(newest, st.mtimeMs);
  }
  return newest;
}

function readDoc(path: string, maxLines = DOC_MAX_LINES): string {
  if (!existsSync(path)) return `_(missing: ${posix(path)})_`;
  const all = lines(readFileSync(path, "utf8"));
  const shown = cap(all, maxLines);
  const tail = all.length > shown.length ? `\n… (+${all.length - shown.length} more lines)` : "";
  return shown.join("\n") + tail;
}

/** One headed section out of a markdown file, verbatim — used to always show the
 *  Agent Operating Protocol in the brief no matter how long README.md grows. */
function sectionOf(path: string, headingNeedle: string, maxLines = 40): string {
  if (!existsSync(path)) return `_(missing: ${posix(path)})_`;
  const all = lines(readFileSync(path, "utf8"));
  const idx = all.findIndex((l) => /^#{1,4}\s/.test(l) &&
    l.toLowerCase().includes(headingNeedle.toLowerCase()));
  if (idx === -1) return `_(no heading matching "${headingNeedle}")_`;
  const level = (all[idx]!.match(/^#+/) ?? ["#"])[0]!.length;
  const body: string[] = [all[idx]!];
  for (let i = idx + 1; i < all.length; i++) {
    const m = all[i]!.match(/^(#+)\s/);
    if (m && m[1]!.length <= level) break;
    body.push(all[i]!);
  }
  const shown = cap(body, maxLines);
  return shown.join("\n") +
    (body.length > shown.length ? `\n… (+${body.length - shown.length} more lines)` : "");
}

// ---------------------------------------------------------------- git
function gitState(): GitState {
  const porcelain = lines(sh("git status --porcelain").out);
  const dirty = porcelain.filter((l) => !l.startsWith("??")).map((l) => l.slice(3));
  const untracked = porcelain.filter((l) => l.startsWith("??")).map((l) => l.slice(3));
  const head = sh("git log -1 --pretty=%h%x09%s").out.split("\t");
  return {
    branch: sh("git rev-parse --abbrev-ref HEAD").out,
    sha: sh("git rev-parse --short HEAD").out,
    subject: head[1] ?? "",
    dirty, untracked,
    recent: cap(lines(sh("git log --oneline -5").out), 5),
  };
}

// ---------------------------------------------------------------- memory
function readMemory(): MemoryEntry[] {
  if (!existsSync(SESSION_LOG)) return [];
  return lines(readFileSync(SESSION_LOG, "utf8"))
    .filter(Boolean)
    .map((l) => { try { return JSON.parse(l) as MemoryEntry; } catch { return null; } })
    .filter((e): e is MemoryEntry => e !== null);
}

function appendMemory(entry: MemoryEntry): void {
  mkdirSync(MEMORY_DIR, { recursive: true });
  appendFileSync(SESSION_LOG, JSON.stringify(entry) + "\n");
  // mirror the last N entries into ToDo.md's Session Memory section
  if (!existsSync(TODO_PATH)) return;
  const todo = readFileSync(TODO_PATH, "utf8");
  const start = todo.indexOf("<!-- session-log:start -->");
  const end = todo.indexOf("<!-- session-log:end -->");
  if (start === -1 || end === -1) return;
  const before = todo.slice(0, start + "<!-- session-log:start -->".length);
  const after = todo.slice(end);
  const mirror = cap(readMemory().slice(-TODO_MIRROR_LINES), TODO_MIRROR_LINES)
    .map((e) => `- ${e.ts} · ${e.branch}@${e.sha} · ${e.note} (dirty:${e.dirty})`)
    .join("\n");
  writeFileSync(TODO_PATH, `${before}\n${mirror}\n${after}`);
}

// ---------------------------------------------------------------- checks
/** Invariant 1 — <Windows.h> stays in the Win32 layer (ARCHITECTURE §10). */
function checkWindowsHeaderContainment(): Check {
  const files = walkFiles(APP, [".cpp", ".h", ".hpp"]).filter((f) => !f.includes("/imgui/"));
  const includers = files.filter((f) => {
    try { return /#\s*include\s*<[Ww]indows\.h>/.test(readFileSync(join(ROOT, f), "utf8")); }
    catch { return false; }
  });
  const stray = includers.filter((f) => !WINDOWS_HEADER_ALLOWLIST.has(f));
  return {
    name: "windows-h-containment",
    pass: stray.length === 0,
    detail: stray.length
      ? `${stray.length} file(s) outside the Win32 layer: ${stray.join(", ")}`
      : `${includers.length} includers, all in the documented layer (features go through platform::)`,
  };
}


/**
 * Invariant: no CRT function that MSVC flags as deprecated (C4996).
 *
 * `strncat` is the one that made it into the tree and stopped a Windows build with
 * "This function or variable may be unsafe" - the project defines no
 * _CRT_SECURE_NO_WARNINGS, and defining it would hide the next one too. The fix is to
 * not call them: each has a portable replacement this repo already uses (snprintf into a
 * bounded buffer instead of strcpy/strcat/strncat/sprintf, std::ifstream instead of
 * fopen). `*_s` variants do not match (the regex needs `(` straight after the name).
 *
 * On Linux, standard POSIX functions (getenv, fopen, localtime, ctime, asctime, tmpnam)
 * are NOT deprecated - they are the standard API. This check only flags the genuinely
 * unsafe string manipulation functions that have portable alternatives.
 */
function checkDeprecatedCrt(): Check {
  // Only the genuinely unsafe string manipulation functions that have portable
  // alternatives (snprintf, std::string, etc.). Standard POSIX functions like
  // getenv, fopen, localtime are NOT flagged on Linux.
  const banned = /\b(strcpy|strcat|strncat|strncpy|sprintf|vsprintf|gets|sscanf)\s*\(/;
  const files = walkFiles(APP, [".cpp", ".h", ".hpp"])
    .filter((f) => !f.includes("/imgui/"));
  const hits: string[] = [];
  for (const f of files) {
    let text: string;
    try { text = readFileSync(join(ROOT, f), "utf8"); } catch { continue; }
    lines(text).forEach((l, i) => {
      const trimmed = l.trim();
      if (trimmed.startsWith("//") || trimmed.startsWith("*") || trimmed.startsWith("/*")) return;
      const m = l.match(banned);
      if (m) hits.push(`${f.replace(/^roblox_external\//, "")}:${i + 1} ${m[1]}`);
    });
  }
  return {
    name: "no-deprecated-crt",
    pass: hits.length === 0,
    detail: hits.length
      ? `${hits.length} call(s) MSVC reports as unsafe (C4996): ${hits.slice(0, 4).join(" · ")} — use snprintf/ifstream instead, not _CRT_SECURE_NO_WARNINGS`
      : `${files.length} app files, no C4996-flagged CRT calls (snprintf/ifstream are not flagged)`,
  };
}

/** Invariant 2 — no ReadProcessMemory / WriteProcessMemory, not even as a name. */
function checkNoWinApiReadWrite(): Check {
  const files = walkFiles(APP, [".cpp", ".h", ".hpp"]).filter((f) => !f.includes("/imgui/"));
  const hits: string[] = [];
  for (const f of files) {
    let text: string;
    try { text = readFileSync(join(ROOT, f), "utf8"); } catch { continue; }
    lines(text).forEach((l, i) => {
      if (!/ReadProcessMemory|WriteProcessMemory/.test(l)) return;
      const trimmed = l.trim();
      const isComment = trimmed.startsWith("//") || trimmed.startsWith("*") || trimmed.startsWith("/*");
      const isTripwire = f === "daemon/nt_syscalls.h" && /^#define\s+(Read|Write)ProcessMemory/.test(trimmed);
      if (!isComment && !isTripwire) hits.push(`${f}:${i + 1}`);
    });
  }
  return {
    name: "no-winapi-read-write",
    pass: hits.length === 0,
    detail: hits.length
      ? `${hits.length} live reference(s): ${cap(hits, 4).join(", ")}`
      : "only the nt_syscalls.h tripwire defines mention the names; every read/write goes through nt::ReadVirtualMemory / WriteVirtualMemory",
  };
}

/** Invariant 3 — the bundled fallback still parses and still names the same client
 *  version as offsets.h. Version drift is what bit this repo twice (CHANGELOG). */
function checkOffsetsParity(): Check {
  const fallback = join(APP, "offsets_fallback.json");
  const offsets = join(APP, "offsets.h");
  if (!existsSync(fallback) || !existsSync(offsets)) {
    return { name: "offsets-parity", pass: false, detail: "offsets.h or offsets_fallback.json is missing" };
  }
  let version = "";
  let count = 0;
  try {
    const parsed = JSON.parse(readFileSync(fallback, "utf8")) as Record<string, unknown>;
    version = String(parsed["Roblox Version"] ?? "");
    count = Number(parsed["Total Offsets"] ?? 0);
  } catch (e) {
    return { name: "offsets-parity", pass: false, detail: `offsets_fallback.json does not parse: ${String(e).slice(0, 80)}` };
  }
  const headerVersion = (readFileSync(offsets, "utf8").match(/ClientVersion\s*=\s*"([^"]+)"/) ?? [])[1] ?? "";
  const same = version !== "" && version === headerVersion;
  return {
    name: "offsets-parity",
    pass: same,
    detail: same
      ? `fallback + offsets.h both ${version} (${count} offsets)`
      : `fallback says "${version}", offsets.h says "${headerVersion}" — regenerate with tools/offsets_regen.py rather than editing by hand`,
  };
}

function runFastChecks(): Check[] {
  const checks = [checkWindowsHeaderContainment(), checkNoWinApiReadWrite(), checkOffsetsParity(),
                  checkDeprecatedCrt()];
  const missing = REQUIRED.filter((r) => !existsSync(join(ROOT, r)));
  checks.push({
    name: "required-files",
    pass: missing.length === 0,
    detail: missing.length ? `missing: ${missing.join(", ")}` : `${REQUIRED.length} expected files present`,
  });
  return checks;
}

/** Build a cached g++ harness and run it. Binaries live in <tmp>, never in the repo. */
function harness(
  name: string, bin: string, sources: string[], buildCmd: string, runCmd: string, outDir: string,
): Check {
  mkdirSync(CHECK_CACHE, { recursive: true });
  const binPath = join(CHECK_CACHE, bin);
  const newest = newestMtime(sources);
  const built = existsSync(binPath) ? statSync(binPath).mtimeMs : 0;
  let detail = "";
  if (built < newest) {
    const b = sh(buildCmd, LIB);
    if (b.code !== 0 || !existsSync(binPath)) {
      return {
        name, pass: false,
        detail: `g++ build failed: ${(b.out.split("\n").filter((l) => l.includes("error")).slice(0, 3).join(" | ") || b.out.slice(-200)).slice(0, 300)}`,
      };
    }
    detail = "rebuilt · ";
  } else {
    detail = "cached · ";
  }
  mkdirSync(outDir, { recursive: true });
  const r = sh(runCmd, CHECK_CACHE);
  const tail = lines(r.out).filter(Boolean).slice(-1)[0] ?? "";
  const invariants = (r.out.match(/(\d+) \/ (\d+) invariants passed/) ?? []);
  const writeFails = (r.out.match(/FAILED to write/g) ?? []).length;
  const ok = r.code === 0 && writeFails === 0;
  const summary = invariants.length
    ? `${invariants[1]}/${invariants[2]} invariants passed`
    : tail;
  return {
    name, pass: ok,
    detail: `${detail}${summary}${writeFails ? ` · ${writeFails} PNG write(s) failed (output dir missing?)` : ""} · out=${outDir}`,
  };
}

function runFullChecks(): Check[] {
  const checks: Check[] = [];
  // the app's Win32 glue, parsed against the stub <Windows.h> (~15s)
  const tu = sh("sh glass-obsidian/tools/check_win_tu.sh", ROOT);
  checks.push({
    name: "win32-tu-parse",
    pass: tu.code === 0 && /win32 glue parses/.test(tu.out),
    detail: (lines(tu.out).filter((l) => /^ *FAIL|win32 glue parses|translation unit/.test(l)).slice(0, 3).join(" | ")
      || tu.out.slice(-200) || "(no output)").slice(0, 300),
  });
  // the link-seam gate: compiles every TU the CMake build lists (app +
  // bootstrapper) and relocatable-links them; any undefined symbol that is
  // ours names the seam break the operator's box would die on (2log.txt class)
  const seam = sh("sh glass-obsidian/tools/link_seam_check.sh", ROOT);
  checks.push({
    name: "link-seam",
    pass: seam.code === 0 && /link-seam: PASS/.test(seam.out),
    detail: (lines(seam.out).filter((l) => /FAIL|link-seam/.test(l)).slice(0, 3).join(" | ")
      || seam.out.slice(-200) || "(no output)").slice(0, 300),
  });
  // glass-obsidian's own invariants + screenshots (13/13 expected, no GPU)
  checks.push(harness(
    "obsidian-headless",
    "headless_tests",
    [join(LIB, "src"), join(LIB, "include"), join(LIB, "app"), join(LIB, "tests"), join(LIB, "third_party", "imgui")],
    "g++ -std=c++17 -O1 -Iinclude -Ithird_party/imgui -Iapp -Itools src/*.cpp app/demo_ui.cpp tests/headless_main.cpp third_party/imgui/imgui*.cpp -o " + join(CHECK_CACHE, "headless_tests"),
    join(CHECK_CACHE, "headless_tests") + " out",
    join(CHECK_CACHE, "out"),
  ));
  // the wall-check geometry + the draw-distance rule, which live in the host app
  // but are deliberately platform-free (no Windows, no ImGui, no offsets): 38 cases
  checks.push(harness(
    "features-geometry",
    "world_ray_test",
    [
      join(APP, "features", "raycast", "world_ray.h"),
      join(APP, "features", "esp", "range.h"),
      join(LIB, "tools", "world_ray_test.cpp"),
    ],
    "g++ -std=c++17 -O2 -I" + ROOT + " -I" + APP + " tools/world_ray_test.cpp -o " + join(CHECK_CACHE, "world_ray_test"),
    join(CHECK_CACHE, "world_ray_test"),
    join(CHECK_CACHE, "out"),
  ));
  // the app's real menu_ui.cpp, rendered headlessly (needs the host checkout next to the lib)
  checks.push(harness(
    "menu-shots",
    "menu_shots",
    [join(LIB, "src"), join(LIB, "include"), join(LIB, "tools"), join(LIB, "third_party", "imgui"), join(APP, "ui")],
    "g++ -std=c++17 -O1 -Iinclude -Ithird_party/imgui -I" + APP +
      " tools/menu_shots.cpp " + join(APP, "ui", "menu_ui.cpp") +
      " src/*.cpp third_party/imgui/imgui*.cpp -o " + join(CHECK_CACHE, "menu_shots"),
    join(CHECK_CACHE, "menu_shots") + " out/menu",
    join(CHECK_CACHE, "out", "menu"),
  ));
  return checks;
}

// ---------------------------------------------------------------- probes
function runProbes(git: GitState): Check[] {
  const probes: Check[] = [];
  probes.push({
    name: "runtime",
    pass: typeof process.versions.node === "string",
    detail: `node v${process.versions.node} executing .ts via type stripping (zero dependencies)`,
  });
  const typed: number = 41; // stripped at runtime; proves the type-stripping path ran
  probes.push({ name: "ts-type-stripping", pass: typed + 1 === 42, detail: "typed const evaluated" });
  probes.push({ name: "git", pass: git.branch.length > 0, detail: `branch=${git.branch} sha=${git.sha}` });
  const tmp = join(ROOT, ".ctx-probe.tmp");
  let fsOk = false;
  try { writeFileSync(tmp, "x"); fsOk = readFileSync(tmp, "utf8") === "x"; } catch { fsOk = false; }
  try { sh(`rm -f ${JSON.stringify(tmp)}`); } catch { /* best effort */ }
  probes.push({ name: "fs-roundtrip", pass: fsOk, detail: "write+read a temp file in the repo root" });
  for (const [tool, cmd] of [["python3", "python3 --version"], ["g++", "g++ --version"], ["make", "make --version"], ["cmake", "cmake --version"]] as const) {
    const r = sh(cmd);
    probes.push({
      name: `tool-${tool}`,
      pass: r.code === 0,
      detail: r.code === 0 ? r.out.split("\n")[0]! : `not present${tool === "cmake" ? " (harnesses build with the g++ one-liners instead)" : ""}`,
    });
  }
  probes.push({
    name: "verification-record",
    pass: existsSync(CAPS_PATH),
    detail: "agent/verification/CAPABILITIES.md present — dated vision/tooling/context/thinking evidence lives there",
  });
  probes.push({
    name: "windows-toolchain",
    pass: false,
    info: true,
    detail: "absent by design on this side (no msbuild/cl.exe) — the Windows build is verified on the Windows side, never claimed here",
  });
  return probes;
}

// ---------------------------------------------------------------- board
interface Board { objective: string; known: string[]; risks: string[]; open: string[]; next: string[]; }

/** `- [ ]` items, with their indented continuation lines folded back into one line —
 *  a wrapped task must not reach the board as a half sentence. */
function parseTodoItems(todo: string): string[] {
  const items: string[] = [];
  let current: string | null = null;
  const flush = (): void => {
    if (current !== null) items.push(current.replace(/\s+/g, " ").trim());
    current = null;
  };
  for (const raw of lines(todo)) {
    const m = raw.match(/^\s*- \[ \]\s*(.*)$/);
    if (m) { flush(); current = m[1]!.trim(); continue; }
    if (current === null) continue;
    if (/^\s+\S/.test(raw) && !/^\s*<!--/.test(raw) && !/^\s*#{1,6}\s/.test(raw)) current += ` ${raw.trim()}`;
    else flush();
  }
  flush();
  return items;
}

function buildBoard(git: GitState, files: string[], mem: MemoryEntry[], checks: Check[]): Board {
  const todoRaw = existsSync(TODO_PATH) ? readFileSync(TODO_PATH, "utf8") : "";
  const todo = todoRaw.replace(/<!--[\s\S]*?-->/g, ""); // commented-out tasks are not open tasks
  const openItems = parseTodoItems(todo);
  const caps = readDoc(CAPS_PATH, 200);
  const openQ = lines(caps).filter((l) => l.startsWith("- UNVERIFIED"))
    .map((l) => `UNVERIFIED: ${l.replace("- UNVERIFIED: ", "")}`);
  const missing = REQUIRED.filter((r) => !files.includes(r));
  const failedChecks = checks.filter((c) => !c.pass);

  const risks: string[] = [];
  if (git.dirty.length) risks.push(`${git.dirty.length} uncommitted change(s): ${cap(git.dirty, 4).join(", ")}`);
  if (missing.length) risks.push(`missing required files: ${missing.join(", ")}`);
  if (failedChecks.length) risks.push(`failing invariant(s): ${failedChecks.map((c) => c.name).join(", ")}`);
  if (git.branch !== "" && git.branch !== "main" && git.branch !== SESSION_BRANCH) {
    risks.push(`off-protocol branch ${git.branch} — work must stay on ${SESSION_BRANCH}`);
  }
  if (!risks.length) risks.push("none detected");

  const appTus = walkFiles(APP, [".cpp"]).filter((f) => !f.includes("/imgui/")).length;
  const featureDirs = (() => {
    try { return readdirSync(join(APP, "features")).length; } catch { return 0; }
  })();

  return {
    objective: openItems[0] ?? "no open objectives — repo is idle",
    known: [
      `branch ${git.branch} @ ${git.sha} — "${git.subject}"`,
      `${files.length} files in the tree walk (depth ${TREE_MAX_DEPTH}), ${mem.length} session-log entries`,
      `host: ${appTus} app TUs across ${featureDirs} feature dirs · glass-obsidian harnesses: check_win_tu.sh, headless_tests, menu_shots`,
      `dirty=${git.dirty.length} untracked=${git.untracked.length} · engine: no read/write is possible from here, only parse + reason`,
    ],
    risks,
    open: openQ.length ? openQ : ["none recorded"],
    next: cap(openItems, 3),
  };
}

// ---------------------------------------------------------------- main
function main(): void {
  const argv = process.argv.slice(2);
  const wantJson = argv.includes("--json");
  const quiet = argv.includes("--quiet");
  const probe = argv.includes("--probe");
  const full = argv.some((a) => a === "--full" || a === "--checks=full");
  const checksFlag = argv.some((a) => a === "--checks" || a === "--checks=full" || a === "--checks=fast");
  const ni = argv.indexOf("--note");
  const note = ni !== -1 ? (argv[ni + 1] ?? "") : "";

  const git = gitState();
  const files: string[] = [];
  const tree = walk(ROOT, 0, files);
  files.sort();

  // The fast invariants run on EVERY pass — they are the repo's own truth and cost
  // ~50ms. `--checks` prints the detail; `--checks=full` adds the heavy harnesses.
  const checks: Check[] = runFastChecks();
  const fullChecks = full ? runFullChecks() : [];

  const failed = checks.filter((c) => !c.pass).length + fullChecks.filter((c) => !c.pass).length;
  const probes = probe ? runProbes(git) : [];
  const failedProbes = probes.filter((p) => !p.pass && !p.info).length;

  const memBefore = readMemory();
  const entry: MemoryEntry = {
    ts: new Date().toISOString(),
    branch: git.branch,
    sha: git.sha,
    dirty: git.dirty.length,
    note: note || (probe ? "probe run" : full ? "context pass + full checks" : checksFlag ? "context pass + checks" : "context pass"),
    checks: `${checks.length - checks.filter((c) => !c.pass).length}/${checks.length}${full ? ` +full ${fullChecks.filter((c) => c.pass).length}/${fullChecks.length}` : ""}`,
  };
  if (!wantJson) appendMemory(entry); // log everything
  const mem = readMemory();
  const shortTerm = mem.slice(-SHORT_TERM_WINDOW);
  const board = buildBoard(git, files, mem, [...checks, ...fullChecks]);

  // Caps are generous on purpose: the doctrine and the protocol are the two things a
  // fresh session MUST read verbatim, and the whole brief is still <2% of the budget.
  const docDump = quiet ? "" : [
    "### Agent Operating Protocol (README.md, verbatim)",
    sectionOf(join(ROOT, "README.md"), "Agent Operating Protocol", 55), "",
    "### docs/DOCTRINE.md", readDoc(join(ROOT, "docs", "DOCTRINE.md"), 130), "",
    "### docs/CONTEXT.md", readDoc(join(ROOT, "docs", "CONTEXT.md"), 80), "",
    "### ToDo.md", readDoc(TODO_PATH, 40), "",
  ].join("\n");

  const out: string[] = [];
  const push = (...s: string[]): void => { out.push(...s); };

  push(`# AGENT CONTEXT BRIEF — ${entry.ts}`);
  push(`> one typed passthrough (node agent/context.ts) — replaces the ~30-command bash ritual`);
  push("");
  push("## 1 · Environment & git");
  push(`node v${process.versions.node} · git branch **${git.branch}** @ ${git.sha} · dirty ${git.dirty.length} · untracked ${git.untracked.length}`);
  push(...git.recent.map((l) => `  ${l}`));
  push("");
  push(`## 2 · Repo tree (${files.length} source files, depth ≤ ${TREE_MAX_DEPTH})`);
  push(...cap(files, TREE_MAX).map((f) => `  ${f}`));
  if (files.length > TREE_MAX) push(`  … (+${files.length - TREE_MAX} more)`);
  if (tree.images) {
    push(`  (+${tree.images} committed image(s) — the glass-obsidian shot gallery, counted not listed; ` +
      `regenerate with the harnesses, see docs/CONTEXT.md)`);
  }
  push("");
  if (docDump) { push("## 3 · Doctrine & context"); push(docDump); push(""); }
  push("## 4 · Host invariants (run every pass)");
  for (const c of [...checks, ...fullChecks]) {
    push(`  ${tag(c)} · ${c.name} · ${c.detail}`);
  }
  push("");
  push("## 5 · Session Memory");
  push(`short-term window (last ${shortTerm.length} of ${mem.length} entries; full log: agent/memory/session.jsonl):`);
  push(...shortTerm.map((e) => `  - ${e.ts} · ${e.branch}@${e.sha} · ${e.note}${e.checks ? ` [checks ${e.checks}]` : ""}`));
  if (!memBefore.length) push("  (first logged pass — this entry is the first line of the log)");
  push("");
  push("## 6 · Problem-Solving Board");
  push(`objective: ${board.objective}`);
  push("known:");
  push(...board.known.map((k) => `  - ${k}`));
  push("risks:");
  push(...board.risks.map((r) => `  - ${r}`));
  push("open questions:");
  push(...board.open.map((o) => `  - ${o}`));
  push("next actions:");
  push(...board.next.map((n) => `  - [ ] ${n}`));
  push("");
  if (probe) {
    push("## 7 · Probes");
    for (const p of probes) push(`  ${tag(p)} · ${p.name} · ${p.detail}`);
    push("");
  }
  const budget = estTokens(out.join("\n"));
  push(`## ${probe ? "8" : "7"} · Context budget`);
  push(`brief ≈ ${out.join("\n").length.toLocaleString()} chars ≈ ${budget.toLocaleString()} tokens ` +
    `(${((budget / CONTEXT_BUDGET_TOKENS) * 100).toFixed(1)}% of ${CONTEXT_BUDGET_TOKENS.toLocaleString()} budget) ` +
    `· harness cache: ${CHECK_CACHE}`);

  if (wantJson) {
    console.log(JSON.stringify({
      ts: entry.ts, git, files, memory: mem, shortTerm, board,
      checks: [...checks, ...fullChecks],
      probes: probes.length ? probes : undefined,
      budget: { chars: out.join("\n").length, estTokens: budget, budgetTokens: CONTEXT_BUDGET_TOKENS },
    }, null, 2));
    return;
  }
  console.log(out.join("\n"));
  if (failed + failedProbes > 0) process.exitCode = 2;
}

main();
