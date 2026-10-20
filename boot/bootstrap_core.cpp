// -----------------------------------------------------------------------------
//  boot/bootstrap_core.cpp — readiness + overlay supervision (see header)
// -----------------------------------------------------------------------------
#include "bootstrap_core.h"

#include "platform/platform.h"
#include "platform/sober_scanner.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bootcore {
namespace {

pid_t g_overlay_pid = -1;

bool FileExists(const std::string& p)
{
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

int ReadYama()
{
    std::ifstream f("/proc/sys/kernel/yama/ptrace_scope");
    if (!f) return -1;
    int v = -1;
    f >> v;
    return v;
}

// The overlay binary, beside this executable. 'external' is the build-tree
// name; 'roblox-external' is what assets/install.sh installs it as.
std::string OverlayPath()
{
    const std::string dir = platform::ExeDir();
    if (FileExists(dir + "external")) return dir + "external";
    if (FileExists(dir + "roblox-external")) return dir + "roblox-external";
    return dir + "external";
}

} // namespace

const char* ExeDir()
{
    // platform::ExeDir() returns by value — keep one static copy for callers.
    static std::string s = platform::ExeDir();
    return s.c_str();
}

Status Poll()
{
    Status s;

    s.sober_pid = sober::FindSoberPid();
    s.sober = s.sober_pid != 0;
    if (s.sober) {
        // A pid without its libroblox.so is the Flatpak-ns case: the overlay
        // cannot read memory yet and would sit at "waiting for Sober".
        s.libroblox = sober::LibRobloxBase(s.sober_pid) != 0;
    }
    s.yama = ReadYama();
    s.display = platform::HasGraphicalDisplay();
    s.overlay_file = FileExists(OverlayPath());

    if (g_overlay_pid > 0) {
        int st = 0;
        const pid_t r = waitpid(g_overlay_pid, &st, WNOHANG);
        if (r == g_overlay_pid || (r < 0 && errno == ECHILD)) g_overlay_pid = -1;
    }
    s.overlay_running = g_overlay_pid > 0;

    // Readiness, in the order the operator hits the wall:
    //  Sober -> memory access (yama) -> display -> the overlay binary.
    const bool yama_ok = (s.yama == 0 || s.yama == -1);
    s.ready = s.sober && s.libroblox && yama_ok && s.display && s.overlay_file;

    if (s.overlay_running) {
        snprintf(s.line, sizeof(s.line), "overlay running (pid %d)", (int)g_overlay_pid);
        snprintf(s.detail, sizeof(s.detail), "locked to Sober — stop it here or close Sober");
    } else if (!s.sober) {
        snprintf(s.line, sizeof(s.line), "looking for Sober...");
        snprintf(s.detail, sizeof(s.detail),
                 "launch Sober: flatpak run org.vinegarhq.Sober  (or /opt/sober/sober)");
    } else if (!s.libroblox) {
        snprintf(s.line, sizeof(s.line), "Sober found (pid %u) — not ready", s.sober_pid);
        snprintf(s.detail, sizeof(s.detail),
                 "libroblox.so is not visible in its maps (Flatpak ns?). "
                 "Try: flatpak run org.vinegarhq.Sober, or run the overlay as root once");
    } else if (s.yama > 0) {
        snprintf(s.line, sizeof(s.line), "Sober found (pid %u) — not ready", s.sober_pid);
        snprintf(s.detail, sizeof(s.detail),
                 "yama ptrace_scope=%d blocks cross-process reads — run: "
                 "sudo sysctl kernel.yama.ptrace_scope=0", s.yama);
    } else if (!s.display) {
        snprintf(s.line, sizeof(s.line), "Sober found — not ready");
        snprintf(s.detail, sizeof(s.detail), "no DISPLAY/WAYLAND_DISPLAY — the overlay needs a graphical session");
    } else if (!s.overlay_file) {
        snprintf(s.line, sizeof(s.line), "Sober found — not ready");
        snprintf(s.detail, sizeof(s.detail),
                 "no overlay binary (external / roblox-external) beside the bootstrapper — run setup.sh first");
    } else {
        snprintf(s.line, sizeof(s.line), "Sober found (pid %u) — ready", s.sober_pid);
        snprintf(s.detail, sizeof(s.detail), "libroblox base 0x%llx — click Load to launch the overlay",
                 (unsigned long long)sober::LibRobloxBase(s.sober_pid));
    }
    return s;
}

bool StartOverlay(char* err, std::size_t errlen)
{
    if (err && errlen) err[0] = '\0';
    if (g_overlay_pid > 0) {
        if (err && errlen) snprintf(err, errlen, "overlay already running (pid %d)", (int)g_overlay_pid);
        return false;
    }
    const std::string exe = OverlayPath();
    if (!FileExists(exe)) {
        if (err && errlen) snprintf(err, errlen, "no overlay binary at %s", exe.c_str());
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        if (err && errlen) snprintf(err, errlen, "fork failed (errno %d)", errno);
        return false;
    }
    if (pid == 0) {
        // Child: the overlay in locked mode — hidden unless Sober's window is
        // there, geometry following it. Its own process group so our Ctrl-C
        // does not race it; it exits on SIGTERM with its restore path.
        setsid();
        const char* argv[] = { "external", "--locked", nullptr };
        execv(exe.c_str(), (char* const*)argv);
        _exit(127);
    }
    g_overlay_pid = pid;
    return true;
}

bool StopOverlay()
{
    if (g_overlay_pid <= 0) return false;
    kill(g_overlay_pid, SIGTERM);
    for (int i = 0; i < 30; ++i) {
        int st = 0;
        if (waitpid(g_overlay_pid, &st, WNOHANG) == g_overlay_pid) {
            g_overlay_pid = -1;
            return true;
        }
        usleep(100 * 1000);
    }
    // Not exiting after 3 s: leave it, report the pid; its writes restore only
    // on a clean exit, so we do NOT SIGKILL by default.
    return true;
}

} // namespace bootcore
