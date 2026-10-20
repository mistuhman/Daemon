// tools/sober_probe.cpp — P1 bring-up probe (LINUX_PORT_PLAN §8.3 step 2)
// Usage (one line each; no trailing backslashes so the comments stay separate):
//   g++ -std=c++17 -I daemon daemon/platform/platform_linux.cpp daemon/platform/sober_scanner.cpp tools/sober_probe.cpp -o /tmp/probe && /tmp/probe
//   or via cmake: cmake --build build-void --target sober_probe && ./build-void/sober_probe
//
// Prints sober pid, libroblox.so base, and ScanResult::diag — the P1 receipt
// the operator photographs. No overlay, no features, no window — just the
// scanner contract, so it can run on a headless CI with a fake pid (shows
// "no sober") and on Void with a real Sober.

#include <cstdio>
#include "platform/sober_scanner.h"
#include "platform/platform.h"

int main() {
    printf("=== sober_probe P1 ===\n");
    printf("platform init...\n");
    if (!platform::Init()) {
        printf("[!] platform::Init failed\n");
        return 2;
    }

    // verbose candidate dump (CE PID explorer analogue: list every comm==sober)
    {
        FILE* pf = popen("ps -o pid,comm,args -e 2>/dev/null | grep -i sober", "r");
        if (pf) { char buf[1024]; bool any=false; while (fgets(buf,sizeof(buf),pf)) { if (!any) printf("  ps candidates:\\n"); any=true; printf("    %s", buf);} if(!any) printf("  ps candidates: (none)\n"); pclose(pf); }
        FILE* ff = popen("flatpak ps 2>&1", "r");
        if (ff) { char buf[2048]; bool any=false; while (fgets(buf,sizeof(buf),ff)) { if (!any) printf("  flatpak ps:\\n"); any=true; printf("    %s", buf);} if(!any) printf("  flatpak ps: (none)\\n"); pclose(ff); }
    }
    uint32_t pid = sober::FindSoberPid();
    printf("FindSoberPid -> %u\n", pid);
    if (!pid) {
        printf("[FAIL] no sober pid — Sober not running, or comm mismatch\n");
        printf("  hint: cat /proc/sys/kernel/yama/ptrace_scope; ls /proc/*/comm | xargs grep -l '^sober$' 2>/dev/null\n");
        printf("diag: sober not found — CI OK if Sober not running\n");
        return 0;
    } else {
        printf("[OK] sober pid %u\n", pid);
    }

    uintptr_t base = sober::LibRobloxBase(pid);
    printf("LibRobloxBase(%u) -> 0x%lx\n", pid, (unsigned long)base);
    if (base) printf("PROBE_OK pid=%u base=0x%lx\n", pid, (unsigned long)base);
    else printf("PROBE_FAIL pid=%u base=0x0 (Flatpak mount-ns fallback missed)\n", pid);
    if (!base) {
        printf("[!] pid %u has no libroblox.so r-xp (not Sober, or Flatpak mount-ns)\n", pid);
        auto regs = sober::MapRegions(pid);
        printf("  maps (%zu regions):\n", regs.size());
        for (auto& r : regs) {
            if (!r.path.empty()) printf("    %016lx-%016lx %s %s\n", (unsigned long)r.start, (unsigned long)r.end, r.perms.c_str(), r.path.c_str());
        }
        return 1;
    }

    // Open pid via platform seam
    void* h = nullptr;
    if (!platform::OpenGameHandle(pid, &h)) {
        printf("[!] OpenGameHandle(%u) failed (Yama ptrace_scope? try sudo)\n", pid);
        printf("  check: cat /proc/sys/kernel/yama/ptrace_scope\n");
        return 1;
    }

    sober::ScanResult res = sober::Scan(h, pid, base);
    platform::CloseHandle(h);

    printf("Scan diag: %s\n", res.diag.c_str());
    printf("Scan ok=%s task=0x%lx render=0x%lx data=0x%lx ws=0x%lx world=0x%lx prims=0x%lx\n",
           res.ok() ? "YES" : "NO",
           (unsigned long)res.taskScheduler, (unsigned long)res.renderJob,
           (unsigned long)res.dataModel, (unsigned long)res.workspace,
           (unsigned long)res.world, (unsigned long)res.primitives);

    if (!res.ok()) {
        printf("\nP1 DONE (pid/base proven), P2 pending (Sig 1 not yet vendored — see LINUX_PORT_PLAN §6.2)\n");
        printf("  next: ./tools/sober_sig_refresh.py %u  (on void-a) and paste Sig 1\n", pid);
    } else {
        printf("\nP2 DONE — scanner ok, offsets patchable\n");
    }
    return 0;
}
