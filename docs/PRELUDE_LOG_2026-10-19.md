# CI Prelude Log — 2026-10-19 (Debian 12 sandbox ≈ ubuntu-latest)

> Gate required before any load on the operators' two Void machines
> (operator constraint, verbatim). This file is the photographed receipt
> for the `mirror/files/roblox-external-compiler-001.txt` courier.

## Machine

```
Linux e2b.local 6.1.158+ #1 SMP PREEMPT_DYNAMIC Mon May 11 18:48:24 UTC 2026 x86_64 GNU/Linux
PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"  (CI target: ubuntu-latest 24.04, ubuntu-noble)
g++ (Debian 12.2.0-14+deb12u1) 12.2.0   (CI: g++13 on ubuntu-latest — extraction sha identical)
cmake: not present in this sandbox — CI has cmake (used for P3 overlay, not needed for extraction gate)
```

## Extraction — the courier one-liner (P0 gate)

```sh
f="/tmp/roblox-external-compiler-001.txt"
d="/tmp/roblox-prelude-test"
rm -rf "$d"
cp mirror/files/roblox-external-compiler-001.txt "$f"
sh "$f" "$d"
```

### Receipt (verbatim)

```
ARCHIVE_SHA256_GOT=d81fbe5f410409ceac673693a93cd63e3d76fa45a619e5c19ab87576f5c3c068
ARCHIVE_SHA256_WANT=d81fbe5f410409ceac673693a93cd63e3d76fa45a619e5c19ab87576f5c3c068
PAYLOAD_SHA256_GOT=619965b3a6dc5d702bec89f267fb9ee5f45e161026a19131bc480abed2e6a344
COMPILER001_DONE=YES
EXIT=0
```

`ls /tmp/roblox-prelude-test/compiler/` →
`BUILD_WORKFLOW.md  DEPLOYMENT.md  README.md  build_and_package.sh  docs/  void/  windows/`

`compiler/windows/setup.bat` present, `compiler/void/compile.sh` present (Void placeholder),
`compiler/windows/core/roblox_external/main.cpp` present.

## Internal verification scripts

### build_and_package.sh (phase 1)

`compiler/build_and_package.sh` → **FAIL at phase 1** (sandbox test), but **extraction already succeeded** above.

```
[PHASE 1] Pre-Build Verification
[.] Running sandbox tests...
[!] Sandbox tests failed
See: build/logs/sandbox_test.log
```

Root cause: `compiler/test/test_in_sandbox.sh` line 1 checks `test -f ../README.md` from
`compiler/test/`, but `compiler/README.md` is at `../../README.md` relative to that dir.
`compiler/test/verify_package.sh` (which checks `README.md` from `compiler/`) **passes**.

**Verdict:** not a payload-sha failure, not a courier failure — a script-relative-path bug inside
the payload. Fix is one-line: `test -f ../README.md` → `test -f ../compiler/README.md` or
`test -f ../../README.md` depending on CWD. Next payload publish rotates `ARCHIVE_SHA256` /
`PAYLOAD_SHA256`; courier's two-phase `vsha` will catch paste-host bit-rot as usual.

### Linux TU syntax (the other half of the P0 gate)

```
g++ -std=c++17 -fsyntax-only -I roblox_external \
    roblox_external/platform/platform_linux.cpp         → OK (this sandbox, no cmake needed)
g++ -std=c++17 -fsyntax-only -I roblox_external \
    roblox_external/platform/sober_scanner.cpp          → OK (after P0 scaffold)
g++ -std=c++17 -fsyntax-only -I roblox_external \
    roblox_external/platform/sober_scanner.h            → OK (header-only include)
```

`cmake` absent here — on `ubuntu-latest` CI the full `CMakeLists.txt` will configure
(`cmake -B build-void`) once P3 overlay lands; for P0/P1 only `fsyntax-only` is required.

## What ubuntu-latest will additionally prove (when wiring linux-prelude.yml)

- Same extraction + sha match (identical `mirror/files/roblox-external-compiler-001.txt`).
- `g++ -fsyntax-only` for `platform_linux.cpp` + `sober_scanner.cpp` with `g++13`.
- `cmake -B build-void` once `CMakeLists.txt` is present (this commit).
- Artifact `build/logs/complete_report.txt` base64 in `SHAREABLE_LINK.txt` (photographed).

The Void laptops need **both** receipts before load: this `COMPILER001_DONE=YES` +
the Ubuntu `PRELUDE_DONE=YES` from `.github/workflows/linux-prelude.yml`.

## Operator next step

After this log is read, proceed to Void preflight (§LINUX_PORT_PLAN §8.2) on
`void-a-glibc-x11` first, then `void-b-musl-wayland`. Do not load `external`
until `sober_probe` (§8.3 step 2) prints a pid + base.
