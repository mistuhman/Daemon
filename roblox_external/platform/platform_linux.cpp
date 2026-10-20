// -----------------------------------------------------------------------------
//  platform/platform_linux.cpp  --  the Linux implementation of platform.h
// -----------------------------------------------------------------------------
//  NOT part of the Windows build (it is not in the vcxproj). It is the
//  starting point for a Linux port, and it COMPILES as-is with:
//
//      g++ -std=c++17 -fsyntax-only -Wall roblox_external/platform/platform_linux.cpp
//
//  What works today, and what a real port still has to decide:
//
//  * memory: process_vm_readv/process_vm_writev over the pid - the honest
//    Linux equivalent of NtRead/NtWriteVirtualMemory, and the reason the
//    platform handle is a heap-allocated pid token rather than an OS handle.
//  * process finding: /proc/*/comm scan. NOTE: the Windows client runs under
//    Wine as "RobloxPlayerBeta.exe" in comm (truncated to 15 chars by the
//    kernel), so the match is on that prefix; a native Linux client would
//    need its own name here.
//  * base address: first mapping of /proc/<pid>/maps, which is the main
//    executable's load address - same job the PEB read does on Windows.
//  * clock/threads/exe-dir/curl: clock_gettime, pthreads, /proc/self/exe -
//    complete, no decisions left.
//  * honest gaps, returning failure instead of pretending:
//      - AllocMem/FreeMem: no remote allocator over /proc. The skybox changer
//        (the only AllocMem user) would need ptrace or a different approach.
//      - SampleKey/IsKeyDown/KeyPressed, mouse output, window rects: these
//        need X11/XWayland (XQueryPointer, XTestFakeMotionEvent, window
//        lookup by pid via _NET_WM_PID). The right shape on Linux is probably
//        the event stream the overlay already receives (like input_mouse.h's
//        WM_INPUT accumulator), not polling - but that is the port's call.
// -----------------------------------------------------------------------------
#include "platform.h"

#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <new>
#include <cstdlib>
#include <cstring>
#include <string>

namespace platform {
namespace {

// The platform "handle": the Windows API hands out an OS HANDLE; Linux memory
// syscalls take the pid every call, so the token just carries it.
struct GameHandle { uint32_t pid; };

std::string ReadFirstLine(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return std::string();
    char buf[512] = {};
    const char* got = fgets(buf, sizeof(buf), f);
    fclose(f);
    return got ? std::string(buf) : std::string();
}

struct Thunk { void (*fn)(void*); void* arg; };

void* PthreadTrampoline(void* p)
{
    Thunk* t = static_cast<Thunk*>(p);
    t->fn(t->arg);
    delete t;
    return nullptr;
}

} // namespace

bool Init()
{
    // No syscall layer to set up on Linux - the memory calls are plain
    // process_vm_readv/writev.
    return true;
}

// ---- process + memory --------------------------------------------------------

uint32_t FindGameProcess()
{
    // comm holds the executable name, truncated to 15 chars by the kernel.
    // Wine runs the client as "RobloxPlayerBeta." (15) - match the prefix.
    DIR* d = opendir("/proc");
    if (!d) return 0;
    struct dirent* e = nullptr;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        const std::string comm = ReadFirstLine((std::string("/proc/") + e->d_name + "/comm").c_str());
        if (comm.compare(0, 15, "RobloxPlayerBeta") == 0 ||
            comm.compare(0, 6,  "Roblox") == 0) {
            const uint32_t pid = (uint32_t)strtoul(e->d_name, nullptr, 10);
            closedir(d);
            return pid;
        }
    }
    closedir(d);
    return 0;
}

uintptr_t GameBaseAddress(uint32_t pid)
{
    // First line of maps = the main executable's mapping ("base-address ...").
    // Same job the PEB+0x10 read does on Windows.
    const std::string line = ReadFirstLine(("/proc/" + std::to_string(pid) + "/maps").c_str());
    if (line.empty()) return 0;
    return (uintptr_t)strtoull(line.c_str(), nullptr, 16);
}

bool OpenGameHandle(uint32_t pid, void** out)
{
    // process_vm_readv/writev need no handle - the "handle" carries the pid.
    // It does need ptrace permission (same story as /proc/pid/mem): same-user
    // + no yama restriction, or the port runs privileged.
    *out = new (std::nothrow) GameHandle{ pid };
    return *out != nullptr;
}

void CloseHandle(void* h)
{
    delete static_cast<GameHandle*>(h);
}

bool ProcessAlive(void* process_h)
{
    const GameHandle* g = static_cast<GameHandle*>(process_h);
    if (!g) return false;
    struct stat st{};
    const std::string path = "/proc/" + std::to_string(g->pid);
    return stat(path.c_str(), &st) == 0;
}

bool ReadMem(void* h, uintptr_t addr, void* buf, size_t size)
{
    const GameHandle* g = static_cast<GameHandle*>(h);
    if (!g) return false;
    const iovec local{ buf, size };
    const iovec remote{ (void*)addr, size };
    return process_vm_readv((pid_t)g->pid, &local, 1, &remote, 1, 0) == (ssize_t)size;
}

bool WriteMem(void* h, uintptr_t addr, const void* buf, size_t size)
{
    const GameHandle* g = static_cast<GameHandle*>(h);
    if (!g) return false;
    const iovec local{ const_cast<void*>(buf), size };
    const iovec remote{ (void*)addr, size };
    return process_vm_writev((pid_t)g->pid, &local, 1, &remote, 1, 0) == (ssize_t)size;
}

bool AllocMem(void* h, size_t size, uintptr_t* out)
{
    // No remote allocator over /proc - see the header note. The skybox
    // changer (the only caller) reports the failure and skips.
    (void)h; (void)size; (void)out;
    return false;
}

void FreeMem(void* h, uintptr_t base)
{
    (void)h; (void)base;    // paired with the AllocMem that never succeeds
}

// ---- clock -------------------------------------------------------------------

uint64_t NowMs()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

uint64_t NowNs()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void SleepMs(uint32_t ms)
{
    struct timespec ts{};
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000l;
    nanosleep(&ts, nullptr);
}

// ---- input state (honest gap - see header) -----------------------------------

KeyState SampleKey(int key) { (void)key; return KeyState{ false, false }; }
bool IsKeyDown(int key)     { (void)key; return false; }
bool KeyPressed(int key)    { (void)key; return false; }

// ---- mouse output (honest gap - see header) ----------------------------------

void MoveMouseRelative(int dx, int dy) { (void)dx; (void)dy; }
void ClickMouseLeft()                  { }   // needs uinput/XTest - the port's job, not a guess
void TapKey(int key)                   { (void)key; }
bool GameWindowForeground()            { return false; }
bool ForegroundIsGameOrUs()            { return false; }   // no window: anti-AFK stays quiet
void SetMouseScreen(int x, int y)      { (void)x; (void)y; }
int ScreenDpi()                        { return 96; }

// ---- screen / game window (honest gap - see header) --------------------------

void SetGamePid(uint32_t pid) { (void)pid; }
bool GameClientRect(int& x, int& y, int& w, int& h)
{
    (void)x; (void)y; (void)w; (void)h; return false;
}
bool CursorScreen(int& x, int& y) { (void)x; (void)y; return false; }
bool CursorClient(int& x, int& y) { (void)x; (void)y; return false; }

// ---- threads -----------------------------------------------------------------

bool StartThread(void (*fn)(void*), void* arg)
{
    pthread_t t{};
    pthread_attr_t attr{};
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    Thunk* thunk = new (std::nothrow) Thunk{ fn, arg };
    if (!thunk) { pthread_attr_destroy(&attr); return false; }
    const bool ok = pthread_create(&t, &attr, PthreadTrampoline, thunk) == 0;
    pthread_attr_destroy(&attr);
    if (!ok) delete thunk;
    return ok;
}

void* StartJoinableThread(void (*fn)(void*), void* arg)
{
    pthread_t* t = new (std::nothrow) pthread_t{};
    Thunk* thunk = new (std::nothrow) Thunk{ fn, arg };
    if (!t || !thunk) { delete t; delete thunk; return nullptr; }
    if (pthread_create(t, nullptr, PthreadTrampoline, thunk) != 0) {
        delete t; delete thunk;
        return nullptr;
    }
    return t;
}

bool JoinThread(void* thread_h, uint32_t timeout_ms)
{
    pthread_t* t = static_cast<pthread_t*>(thread_h);
    if (!t) return false;
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000l;
    if (ts.tv_nsec >= 1000000000l) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000l; }
    const bool exited = pthread_timedjoin_np(*t, nullptr, &ts) == 0;
    delete t;
    return exited;
}

void BoostThread()
{
    // Best effort: nicer than Windows' nothing, quieter than SCHED_FIFO.
    sched_yield();
}

void YieldThread()
{
    sched_yield();
}

// ---- files / tools -----------------------------------------------------------

std::string ExeDir()
{
    char buf[4096] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "./";
    const std::string s(buf, (size_t)n);
    const size_t slash = s.find_last_of('/');
    if (slash == std::string::npos) return "./";
    return s.substr(0, slash + 1);
}

std::string CurlPath()
{
    // The transport builds an argument list, so a PATH-resolvable name is
    // what a posix fork/exec version of GetViaCurl wants.
    return "curl";
}

namespace {

bool HasExt(const char* name, const char* ext)
{
    const char* dot = nullptr;
    for (const char* p = name; *p; ++p)
        if (*p == '.') dot = p;
    if (!dot) return false;
    for (const char* a = dot + 1, *b = ext; ; ++a, ++b) {
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (!ca || !cb) return ca == cb;
        if (ca != cb) return false;
    }
}

} // namespace

bool ListFilesWithExt(const char* dir, const char* ext, std::vector<std::string>& out)
{
    out.clear();
    if (!dir || !dir[0] || !ext || !ext[0]) return false;
    mkdir(dir, 0755);   // EEXIST is fine; a bad path fails below

    DIR* d = opendir(dir);
    if (!d) return false;
    const std::string prefix = std::string(dir) + "/";
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!HasExt(e->d_name, ext)) continue;
        struct stat st {};
        if (stat((prefix + e->d_name).c_str(), &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        out.push_back(e->d_name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return true;
}

bool PlayWav(const char* path, float volume)
{
    (void)path;
    (void)volume;
    return false;   // no audio layer in the Linux starting point
}

bool OpenUrl(const char* url, std::string& err)
{
    err.clear();
    if (!url || !url[0]) { err = "nothing to open"; return false; }
    // xdg-open returns 0 for "handed off" - unlike ShellExecute there is no
    // "nothing registered" distinction, just the exit code.
    const std::string cmd = std::string("xdg-open '") + url + "' >/dev/null 2>&1";
    if (system(cmd.c_str()) == 0) return true;
    err = "xdg-open could not hand off the link - the link is:\n" + std::string(url);
    return false;
}

} // namespace platform
