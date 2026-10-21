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
#include <fcntl.h>
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

// The overlay binary, beside this executable: 'daemon' in the build tree and
// as assets/install.sh installs it (~/.local/bin/daemon next to daemon-boot).
std::string OverlayPath()
{
    return platform::ExeDir() + "daemon";
}

std::string OverlayLogPath()
{
    std::string exeDir = platform::ExeDir();
    // When running from an extracted payload's build/ (e.g. ~/daemon-010/build/),
    // the log beside the exe would be hidden inside build/. Put it at the
    // extraction root instead (~/daemon-010/overlay.log) — the file-manager
    // view the operator photographed (7 items at the root). Installed
    // ~/.local/bin/daemon-boot keeps its log beside the binary.
    const std::string suffix = "build/";
    if (exeDir.size() >= suffix.size() &&
        exeDir.compare(exeDir.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string root = exeDir.substr(0, exeDir.size() - suffix.size());
        return root + "overlay.log";
    }
    return exeDir + "overlay.log";
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
    s.libroblox = false;
    s.maps_denied = false;
    if (s.sober) {
        // Distinguish three states so the operator gets the right fix line:
        //   (a) maps readable + libroblox found  → ready to load
        //   (b) maps UNREADABLE (EACCES)         → yama ptrace_scope problem
        //   (c) maps readable, libroblox missing → true flatpak-ns / early start
        bool denied = false;
        uintptr_t base = sober::LibRobloxBase(s.sober_pid, &denied);
        s.libroblox = (base != 0);
        s.maps_denied = denied && base == 0;
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
    //  Sober -> memory access (yama — gate this BEFORE libroblox because a
    //  yama block causes libroblox to appear "missing" too) -> display -> binary.
    // Successful cross-process ELF reads are stronger evidence than a global
    // policy value (a permitted process may read even with ptrace_scope > 0).
    s.ready = s.sober && s.libroblox && s.display && s.overlay_file;

    if (s.overlay_running) {
        snprintf(s.line, sizeof(s.line), "overlay running (pid %d)", (int)g_overlay_pid);
        snprintf(s.detail, sizeof(s.detail), "locked to Sober — stop it here or close Sober");
    } else if (!s.sober) {
        snprintf(s.line, sizeof(s.line), "looking for Sober...");
        if (s.yama > 0) {
            snprintf(s.detail, sizeof(s.detail),
                     "no unique readable libroblox ELF; ptrace_scope=%d may restrict memory probing. Run the read-only diagnostic.", s.yama);
        } else {
            snprintf(s.detail, sizeof(s.detail),
                     "no unique readable libroblox ELF; open a game or inspect permissions/multiple instances with the diagnostic");
        }
    } else if (s.yama > 0 && !s.libroblox) {
        // yama blocks PTRACE_MODE_READ on /proc/<pid>/maps — this is the #1
        // reason libroblox "isn't visible", and it's the fix the operator can
        // apply without reinstalling anything. Show it BEFORE the flatpak-ns
        // guess so the "run as root" suggestion doesn't mask it.
        snprintf(s.line, sizeof(s.line), "Sober found (pid %u) — not ready", s.sober_pid);
        snprintf(s.detail, sizeof(s.detail),
                 "yama ptrace_scope=%d blocks reading Sober's memory. "
                 "Fix once per boot:  sudo sysctl kernel.yama.ptrace_scope=0", s.yama);
    } else if (s.maps_denied) {
        // yama reads as 0 but /proc/<pid>/maps still opens EACCES — usually
        // the LSM (AppArmor/SELinux/TOMOYO) or the pid is a setuid helper.
        snprintf(s.line, sizeof(s.line), "Sober found (pid %u) — not ready", s.sober_pid);
        snprintf(s.detail, sizeof(s.detail),
                 "permission denied reading /proc/%u/maps even with yama=0; "
                 "inspect target credentials and security policy.", s.sober_pid);
    } else if (!s.libroblox) {
        // Maps are readable but libroblox.so isn't there — this is the real
        // flatpak-ns / bwrap-startup case.
        snprintf(s.line, sizeof(s.line), "Sober found (pid %u) — not ready", s.sober_pid);
        snprintf(s.detail, sizeof(s.detail),
                 "no readable ELF image self-identifying as libroblox.so; "
                 "no filename or namespace cause is assumed");
    } else if (!s.display) {
        snprintf(s.line, sizeof(s.line), "Sober found — not ready");
        snprintf(s.detail, sizeof(s.detail), "no DISPLAY/WAYLAND_DISPLAY — the overlay needs a graphical session");
    } else if (!s.overlay_file) {
        snprintf(s.line, sizeof(s.line), "Sober found — not ready");
        snprintf(s.detail, sizeof(s.detail),
                 "no overlay binary (daemon) beside the bootstrapper — run setup.sh first");
    } else {
        snprintf(s.line, sizeof(s.line), "memory attached (pid %u) — shell ready", s.sober_pid);
        uintptr_t base = sober::LibRobloxBase(s.sober_pid);
        snprintf(s.detail, sizeof(s.detail), "ELF load bias 0x%llx — Load opens the shell; game features stay disabled until Linux layout validation",
                 (unsigned long long)base);
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
    // Resolve log path before fork so parent can report it even if child fails
    const std::string log = OverlayLogPath();
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
        // Capture stdout/stderr to overlay.log. At baseline-009 a crash left no
        // trace because the manager kept no pipe — the log is where the next
        // "what died" answers itself. ti-10 hid the file inside build/ (7 items
        // at the extraction root, log invisible); the path now points at the
        // extraction root when running from build/, otherwise beside the exe.
        // Fallback to /tmp if the primary location is not writable.
        std::string chosen = log;
        int fd = open(chosen.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            const char* fallback = "/tmp/daemon-overlay.log";
            fd = open(fallback, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) chosen = fallback;
        }
        if (fd >= 0) {
            dprintf(fd, "[overlay.log] Daemon %s — writing overlay stdout/stderr here (overlay pid will log next)\n",
                    chosen.c_str());
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) close(fd);
        }
        const char* argv[] = { "daemon", "--locked", nullptr };
        execv(exe.c_str(), (char* const*)argv);
        dprintf(STDERR_FILENO, "overlay exec failed for %s: errno %d\n", exe.c_str(), errno);
        _exit(127);
    }
    g_overlay_pid = pid;
    return true;
}

std::string OverlayLogPathForUi()
{
    return OverlayLogPath();
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
