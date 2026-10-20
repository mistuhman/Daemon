# Linux read-only attachment receipt

Run from an extracted Linux payload (no installation or root required):

```sh
sh tools/sober-attach
sh tools/sober-attach --pick           # X11 window picker, optional
sh tools/sober-attach --pid 12345      # replace 12345 with the current host PID
sh tools/sober-attach --window-pid 3   # example: inspect an advertised X11 PID
```

Uses the existing g++ dependency and libc/libdl. No Python runtime, Xmu, CE
server, kernel module, network service, new package, or remote writes required.
The optional picker dynamically loads the existing X11 library when X11 headers
were available at compile time. Automatic and explicit-PID modes work without X11.
Native Wayland window picking is not implemented. Never run a desktop overlay as
root to work around an unmeasured error.

Automatic mode enumerates same-UID host processes and checks module mappings,
not process-name scores, command arguments, machine paths or cgroups. It reports
all recognized-module candidates rather than silently choosing between them.
Recognized module basenames are currently libroblox.so and libbadcpu.so, including
deleted mappings. No recognized module is not proof that Sober is absent; explicit
mode includes the selected target even without recognized modules. All receipts
include a compact per-path backing inventory and probe other readable offset-zero
file/memfd mappings for ELF headers. An unnamed/deleted backing is not identified
as Roblox merely because it is an ELF file.
Maps-denied counts are reported separately. Other-UID processes are intentionally
not scanned automatically; explicit mode can report their kernel errors.

Receipt includes host PID, comm, exe, NSpid when exposed, UID, PID namespace,
mappings with file offsets, actual process_vm_readv byte counts/errors, ELF
class/endian/machine, PT_LOAD-derived load bias, GNU build ID when present, and
process start time checked before/after inspection. No command-line arguments
or memory dumps are printed. Paths and executable names may contain private
information: review before sharing. The start-time comparison detects a changed
process during inspection; it does not pin the process after the receipt ends.

PASS means a recognized module's ELF header/program headers were read from a
stable target and its load bias derived. It does not mean game objects, offsets,
features, or write permission have been validated. Multiple targets each receive
a separate receipt. Exit codes: 0 at least one read-only PASS, 1 no PASS, 2 usage
or selection failure. A missing build ID is reported by absence, not fabricated.
ELF64 little-endian is currently parsed; other ABIs are identified but not passed.
The module's offset-zero readable mapping must contain its ELF/program headers.

The X11 window property is an advertised PID, not a host PID. V2 enumerates
same-user processes and matches it to the innermost NSpid, then requires recognized
module evidence. If multiple namespaces contain matching candidates, it reports
ambiguity and refuses to select one. This is evidence-constrained matching, not
a trusted association to the X server's specific namespace. Clients advertising
an outer-namespace PID, missing NSpid, or unknown modules need explicit --pid.
The --window-pid option exercises the same resolver without requiring a display.
It never blindly inspects host PID 3 just because a window advertised PID 3.

## Source provenance

Inspected upstream source, not inferred behavior:

* Cheat Engine ceserver, commit ec45d5f47f92a239ba0bf51ec5d04a7509c3fd37,
  `Cheat Engine/ceserver/api.c`, OpenProcess and memory read implementation:
  https://github.com/cheat-engine/cheat-engine/blob/ec45d5f47f92a239ba0bf51ec5d04a7509c3fd37/Cheat%20Engine/ceserver/api.c
  Excerpts: `sprintf(processpath,"/proc/%d/maps", pid);` and
  `bread=readv(p->pid,&local,1,&remote,1,0);`.
* XFCE Task Manager, commit 3a21eccf2689c3b8ed3c4ec51e2c0dea418278d3,
  `src/process-window.c`, xwininfo_clicked_cb:
  https://github.com/xfce-mirror/xfce4-taskmanager/blob/3a21eccf2689c3b8ed3c4ec51e2c0dea418278d3/src/process-window.c#L128-L175
  Excerpts: `XInternAtom (dpy, "_NET_WM_PID", False)` and
  `memcpy (&pid, prop, sizeof (pid));`.

Our implementation is independent: direct Linux syscalls and dynamically loaded
Xlib; no upstream code was copied into the diagnostic. PT_LOAD/load-bias parsing,
process-start validation and module discovery policy are our implementation,
not attributed to XFCE. This executable intentionally does not import offsets.h,
offsets_live, the existing scanner, or any Windows offset fallback.

## Reproducible tests

```sh
g++ -std=c++17 -Wall -Wextra -O2 tools/sober_attach.cpp -ldl -o /tmp/sober-attach
python3 boot/test/attach_diagnostic.py /tmp/sober-attach
```

Python is test-only. Tests map a genuine ELF file under the recognized basename
in a live fixture process and verify explicit and automatic discovery, actual
cross-process reads, build ID, load bias, process identity, vanished-PID failure,
and invalid-ELF rejection. They do not establish live Sober compatibility or
exercise the X11 picker without an X server.

## V2: operator evidence and additional sources

The 018 receipt established readable libbadcpu ELF headers in three Sober
processes, with no reported libroblox mapping. One process had comm Main, and
NSpid mapped its host PID to inner PID 3. The window picker returned 3 but inspected
a different host kernel process. V2 corrects this concrete namespace mismatch.
Readable libbadcpu is not proof of a discovered Roblox module or object layout.

Linux proc_pid_status documentation:
https://man7.org/linux/man-pages/man5/proc_pid_status.5.html
Excerpt: "followed by the value in successively nested inner namespaces";
NSpid fields are ordered as for NStgid. This supports interpreting the last
field as the innermost PID, not treating it as the host number.

Sober issue #1813 is useful corroborating evidence, not proof of this user's layout:
https://github.com/vinegarhq/sober/issues/1813
Crash excerpt: `Module /memfd: (deleted) without build-id.` and
`#6 ... n/a (/memfd: (deleted) + 0x258ab79)`.
This motivates inspecting file/memfd-backed ELF headers without relying on a
libroblox filename. It does not identify every memfd mapping as Roblox, establish
a usable offset, or justify reusing that crash's numeric offsets.

Additional test: compile and run boot/test/namespace_pid.cpp with -ldl. It checks
host-only, nested, missing and empty NSpid parser cases. Integration tests also
exercise --window-pid, unnamed memfd inventory and helpful rejection of literal
HOST_PID. X11 interactions and live namespace translation still require the
operator receipt; parser fixtures are not represented as live sandbox tests.

## V3: ELF self-identification and compact standalone receipt

The subsequent raw operator maps show an unnamed memfd with executable offset-zero,
read-only and writable regions only in the Main candidate. That is a candidate
image, not yet identified as libroblox. V3 reads PT_DYNAMIC, DT_STRTAB, DT_STRSZ,
and optional DT_SONAME directly from the selected process. It considers absolute
and ELF-relative string-table pointers only within this ELF's PT_LOAD ranges,
refusing ambiguous representations. Missing/stripped/inaccessible SONAME stays
UNAVAILABLE. No offsets are inferred from its absence. GNU notes are now read
using their virtual addresses rather than treating file offsets as runtime VAs.

ELF gABI dynamic section specification:
https://gabi.xinuos.com/elf/08-dynamic.html
DT_SONAME excerpt: "giving the name of the shared object"; the offset is an
index into the table recorded in DT_STRTAB. SONAME is optional and is an ELF's
self-identification, not a cryptographic identity or validated object layout.

`--images` is automatic discovery with compact output: known module evidence
and memfd header probes only, no giant inventory of fonts/drivers/assets. The
small standalone `sober-elf-021.txt` payload embeds the same source and builds in
a private temporary directory, defaulting to --images. It is shipped by the
existing frozen courier, not a new delivery service. It does not install or
launch the overlay. Source hash is verified before compilation. Tests include
native-loader relocated DT_STRTAB and a synthetic memfd ELF retaining relative
DT_STRTAB, with distinct fixture SONAMEs. No actual Sober SONAME has yet been read.

## Live ELF identity established; production integration (022)

Operator V3 receipt directly read `ELF_SONAME=libroblox.so` from the unnamed
memfd in Main, GNU build ID `913ea9839b6084470a25e4be8feddc88214a26a3`.
Companions contained the runtime and libbadcpu, but did not report this image.
Neither PID, base address nor that build ID is hardcoded into discovery.

`platform/linux_elf.h` now provides the same bounded inspector to diagnostics
and production attachment. FindSoberPid enumerates same-user processes, accepts
only an ELF SONAME match with an actually executable mapping overlapping a
PT_LOAD executable range, checks process start times, and refuses multiple
matching processes or images. LibRobloxBase returns the ELF load bias, not a
segment start. The false `/proc/self/maps` nsenter fallback and name/cgroup
scores are removed. OpenGameHandle verifies ELF reads rather than just allocating
a PID token, and ProcessAlive checks the start time to detect PID reuse.
This detects ordinary exit/reuse; it is not a promise of atomic remote reads or
a pinned process across exec. No Windows/Wine process or base fallback remains.

The unvalidated scheduler/field guesses are removed from Scan rather than
renamed dynamic. It currently returns an explicit unsupported-layout diagnostic.
ReadDatamodel cannot fall through into Windows fields. Offset-resolution cache
entries are keyed to process/start/base and failures don't latch forever before
startup. The Linux feature loop stays off unless ScanResult is valid; the shell
reports memory attachment separately from layout readiness. Load may open this
safe shell, but is not a claim that game features now work. Actual successful
ELF reads, not a blanket ptrace_scope value, determine read readiness.

022 is a small source upgrade for the unchanged source preimages carried by
018/019/020, at a user-specified extracted payload path. All source hashes are
checked before changing any file. Unknown/local modifications fail closed;
reapplying identical upgraded sources is accepted. The new header must either
be absent or match exactly. It uses the existing setup.sh afterward, not a new
installer or transport. Original Windows source behavior is retained behind its
preprocessor branch. Full SDL/GL build remains unverified in this sandbox;
bootstrapper smoke and changed Linux TU syntax checks pass.

Production scanner integration test: compile attachment_selection.cpp together
with sober_scanner.cpp and run verified_selection.py against it. It uses actual
cross-process reads and an executable unnamed memfd with ELF/SONAME fixture data,
rejects comm-only/data-only identity, checks exact bias, rejects a second matching
process, rediscovers after that process exits, and rejects an unloaded image.
Synthetic fixture data is not represented as a Roblox object-layout test.


## Operator verdict after 022 (next-session handoff)

The operator successfully rebuilt the full SDL/GL application on Void and launched
Load from the installed bootstrapper. Production log confirms a newly discovered
host PID and ELF base through port_watcher, a verified handle, and the explicit
unsupported Linux layout diagnostic with features disabled. Thus production
attachment is now live-confirmed, not merely a synthetic-test result. The earlier
setup-time no-target scan does not override the later successful attachment.

The operator rejects the current visual result: it is an ordinary decorated
window displaying the shell, not a useful game overlay. The runtime reported
`window visual is not 32-bit ARGB (compositor alpha impossible)` and created a
windowed SDL shell. GTK CSS color parsing warnings also remain. None of these
messages establishes a game-object layout or functioning feature.

User explicitly requested PR creation/merge now, then next chat:
1. Implement and validate Linux-native dynamic object/field resolution and
   progressively enable working features; never reinstate Windows/guessed values.
2. Make the overlay actually overlay Roblox with appropriate transparency,
   stacking, geometry, click-through and minimal window chrome.
3. Investigate Dotline, the crosshair app, as the user's reference for priority
   across common DEs (GNOME/KDE/etc). Its repository/implementation has NOT been
   inspected in this session; identify it and cite actual source excerpts before
   claiming an approach. Assess X11 vs native Wayland separately, including any
   compositor-specific limitations and real dependency requirements.

No further installer, overlay redesign or feature implementation was requested
for this closing turn. Do not describe this merge as a completed working external.
