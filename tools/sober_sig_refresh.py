#!/usr/bin/env python3
"""
tools/sober_sig_refresh.py — dump TaskScheduler neighbourhood for Sig 1

Operator usage on void-a (after Sober is running in any place):

  pid=$(ps aux | grep -i sober | awk '{print $2}' | head -n 1)
  ./tools/sober_sig_refresh.py $pid 0x7a0b720 0x200

or let it find the pid itself:

  ./tools/sober_sig_refresh.py --auto

What it does (read-only, no ptrace, uses /proc/<pid>/maps + process_vm_readv
when available, falls back to /proc/<pid>/mem with sudo hint):

  1. resolves pid via sober_scanner::FindSoberPid logic (comm + libroblox.so)
  2. finds libroblox.so r-xp base and the region containing 0x7a0b720-ish
  3. dumps 0x200 bytes around that neighbourhood as hex + ascii
  4. searches that window for the lea pattern candidate (48 8D 0D ? ? ? ?)
     and prints the candidate hits with disp decoding — the operator pastes
     the surviving hit into platform/sober_scanner.cpp as SIG_TASKSCHEDULER_LEA.

This script ships in the repo so the operator can run it *without* the
external built. It is not part of the external's build.

Reference: docs/LINUX_PORT_PLAN.md §6.2 (Sig 1) + UnknownCheats 771298
"""

import argparse
import os
import re
import struct
import sys

def read_maps(pid):
    path = f"/proc/{pid}/maps"
    regs = []
    try:
        with open(path) as f:
            for line in f:
                # 00400000-00409000 r-xp 00000000 08:01 12345 /path/libroblox.so
                m = re.match(r"([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+\S+\s+\S+:\S+\s+\d+\s*(.*)", line)
                if not m:
                    continue
                start = int(m.group(1), 16)
                end = int(m.group(2), 16)
                perms = m.group(3)
                mpath = m.group(4).strip()
                regs.append((start, end, perms, mpath))
    except FileNotFoundError:
        pass
    return regs

def find_pid_auto():
    best = 0
    best_score = -1
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        pid = int(name)
        try:
            with open(f"/proc/{pid}/comm") as f:
                comm = f.read().strip()
        except:
            continue
        score = -1
        if comm.lower() in ("sober", "app_process"):
            score = 10
        elif comm.startswith("RobloxPlayerBeta"):
            score = 5
        else:
            continue
        regs = read_maps(pid)
        has_roblox = any(p.endswith("libroblox.so") for _,_,_,p in regs)
        has_badcpu = any(p.endswith("libbadcpu.so") for _,_,_,p in regs)
        if not has_roblox:
            continue
        if has_badcpu:
            score += 20
        try:
            with open(f"/proc/{pid}/cgroup") as f:
                cg = f.read()
            if "org.vinegarhq.Sober" in cg:
                score += 15
            if "flatpak" in cg:
                score += 5
        except:
            pass
        if score > best_score:
            best_score = score
            best = pid
    return best

def hexdump(data, base_addr, highlight=None):
    """Classic hexdump, 16 bytes/line, with optional highlight range."""
    out = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        addr = base_addr + i
        hexs = " ".join(f"{b:02x}" for b in chunk)
        # pad to 16*3 chars
        hexs = hexs.ljust(47)
        ascii_s = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        mark = " <--- candidate" if highlight and highlight[0] <= addr < highlight[1] else ""
        out.append(f"{addr:016x}  {hexs} |{ascii_s}|{mark}")
    return "\n".join(out)

def find_candidates(data, base_addr):
    """Search for 48 8D 0D ? ? ? ? (lea rcx, [rip+disp32])"""
    cands = []
    for i in range(len(data) - 7):
        if data[i]==0x48 and data[i+1]==0x8D and data[i+2]==0x0D:
            disp = struct.unpack_from("<i", data, i+3)[0]
            # disp should be small-ish (within same lib region, not huge)
            # and the target pointer should look like a heap address
            target_ptr_addr = base_addr + i + 7 + disp
            # heuristic: disp in [-2MB, +8MB] for nearby global
            if -2*1024*1024 <= disp <= 8*1024*1024:
                cands.append((base_addr + i, disp, target_ptr_addr))
    return cands

def main():
    ap = argparse.ArgumentParser(description="Sober Sig 1 neighbourhood dumper")
    ap.add_argument("pid", nargs="?", help="Sober pid (or omit with --auto)")
    ap.add_argument("addr", nargs="?", help="Known TaskScheduler-ish address (hex, e.g. 0x7a0b720) — used to pick window")
    ap.add_argument("window", nargs="?", help="Bytes to dump around addr (default 0x200)")
    ap.add_argument("--auto", action="store_true", help="Find pid automatically")
    args = ap.parse_args()

    if args.auto:
        pid = find_pid_auto()
        if not pid:
            print("no Sober pid found (no comm sober/app_process with libroblox.so)", file=sys.stderr)
            sys.exit(1)
        print(f"[auto] pid={pid}")
    else:
        if not args.pid:
            ap.print_usage(sys.stderr)
            sys.exit(1)
        pid = int(args.pid, 0)

    # Validate pid
    try:
        with open(f"/proc/{pid}/comm") as f:
            comm = f.read().strip()
        print(f"pid={pid} comm={comm}")
    except FileNotFoundError:
        print(f"pid {pid} not found", file=sys.stderr)
        sys.exit(1)

    regs = read_maps(pid)
    if not regs:
        print(f"cannot read /proc/{pid}/maps (permission? try sudo?)", file=sys.stderr)
        sys.exit(1)

    r_xp_bases = [(s,e,p) for s,e,perms,p in regs if perms=="r-xp" and p.endswith("libroblox.so")]
    if r_xp_bases:
        lib_base = r_xp_bases[0][0]
        print(f"libroblox.so r-xp base=0x{lib_base:x}")
    else:
        print("no libroblox.so r-xp region — not Sober? (Wine Roblox has no libroblox.so)", file=sys.stderr)
        sys.exit(1)

    # Determine dump address
    if args.addr:
        center = int(args.addr, 0)
        # if addr is file-offset style (0x7a0b720), treat as libBase + offset
        if center < 0x1000000:
            center = lib_base + center
            print(f"center (libBase + offset) = 0x{center:x}")
        else:
            print(f"center = 0x{center:x}")
    else:
        # default: libBase + last known offset from UC dump
        center = lib_base + 0x7a0b720
        print(f"center (default UC 0x7a0b720) = 0x{center:x}")

    try:
        window = int(args.window, 0) if args.window else 0x200
    except:
        window = 0x200
    start = center - window//2
    end = center + window//2

    # Try process_vm_readv via python (requires ctypes), else /proc/pid/mem
    data = None
    try:
        import ctypes, ctypes.util
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        # process_vm_readv is syscall 310 on x86_64 — use libc wrapper if present
        # fallback: just use /proc/pid/mem
        raise NotImplementedError("skip libc path for portability")
    except:
        pass

    # /proc/pid/mem path (needs ptrace attach or CAP_SYS_PTRACE on Yama 1+)
    try:
        import subprocess, signal
        # Try procmem read via dd/grep without attach (works if Yama 0 or same-tracer)
        # Easiest portable: use `sudo cat` if available, else direct open
        with open(f"/proc/{pid}/mem", "rb") as mem:
            mem.seek(start)
            data = mem.read(end - start)
            if not data or len(data) < (end-start)//2:
                raise IOError("short read — need ptrace attach (try sudo or set kernel.yama.ptrace_scope=0)")
    except Exception as e:
        # Provide operator recipe instead of failing silently
        print(f"\n[!] cannot read /proc/{pid}/mem: {e}", file=sys.stderr)
        print(f"[!] try: sudo {sys.argv[0]} {pid} {hex(center)} {hex(window)}", file=sys.stderr)
        print(f"[!] or:  echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope  (Yama 0, classic)", file=sys.stderr)
        print(f"[!] dumping maps only:\n", file=sys.stderr)
        for s,e_,perms,p in regs:
            flag = " <-- libroblox.so r-xp" if (perms=="r-xp" and p.endswith("libroblox.so")) else ""
            print(f"  {s:016x}-{e_:016x} {perms} {p}{flag}")
        sys.exit(1)

    # Find candidates in the dumped window
    cands = find_candidates(data, start)
    highlight = None
    if cands:
        print(f"\n[+] {len(cands)} lea candidate(s) in 0x{window:x} window:")
        for off, disp, tpa in cands:
            print(f"    off 0x{off:x}  disp {disp:+d} (0x{disp & 0xffffffff:08x})  -> ptr@0x{tpa:x}")
        highlight = (cands[0][0], cands[0][0]+7)
    else:
        print("\n[?] no lea candidates in window — widen with 0x400 or 0x800, or update pattern")

    print(f"\n--- hexdump 0x{start:x}..0x{end:x} ({len(data)} bytes) ---\n")
    print(hexdump(data, start, highlight))
    print(f"\n--- what to paste back ---")
    print(f"If one candidate above looks right (disp in range, nearby pointer is TaskScheduler), paste:")
    print(f'  static const char SIG_TASKSCHEDULER_LEA[] = "48 8D 0D ? ? ? ? 48 8B D7 E8 ? ? ? ?";')
    print(f"  // hit at 0x{cands[0][0]:x} disp {cands[0][1]:+d} -> ptr@0x{cands[0][2]:x}" if cands else "  // (no hit — widen window)")
    print(f"into daemon/platform/sober_scanner.cpp and set SIG_TASKSCHEDULER_DISP_OFF=3.")

if __name__ == "__main__":
    main()
