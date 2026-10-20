// -----------------------------------------------------------------------------
//  platform/platform_linux.cpp  --  the Linux implementation of platform.h
// -----------------------------------------------------------------------------
//  NOT part of the Windows build (it is not in the vcxproj). It is the
//  starting point for a Linux port, and it COMPILES as-is with:
//
//      g++ -std=c++17 -fsyntax-only -Wall daemon/platform/platform_linux.cpp
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
//  * window rects + cursor + overlay window control (P3): the x11overlay
//    layer in this file, compiled when CMake finds X11 and defines HAS_X11
//    (HAS_XFIXES additionally enables the XFixes click-through input shape).
//    GameClientRect is an EWMH lookup: _NET_WM_PID match on the pid the
//    attach path stored via SetGamePid, title match fallback. The overlay
//    uses the same layer for dock hints, click-through and the root-grabbed
//    HOME/F8 hotkey (overlay_x11.h).
//  * honest gaps, returning failure instead of pretending:
//      - AllocMem/FreeMem: no remote allocator over /proc. The skybox changer
//        (the only AllocMem user) would need ptrace or a different approach.
//      - SampleKey/IsKeyDown/KeyPressed, mouse output (aimbot's MoveMouseRelative
//        and ClickMouseLeft): these need XTest/libXtst or uinput - still stubs.
//      - Wayland-native (no XWayland): none of the x11overlay layer works; the
//        windowed menu keeps working, click-through does not. Sober runs under
//        XWayland, so this is the corner, not the target.
// -----------------------------------------------------------------------------
#include "platform.h"
#ifdef HAS_SDL_OVERLAY
#include <SDL.h>
#endif
#include "sober_scanner.h"
#include "overlay_x11.h"

#if defined(HAS_X11)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#if defined(HAS_XFIXES)
#include <X11/extensions/Xfixes.h>
#endif
#endif

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
#include <mutex>
#include <string>
#include <vector>

namespace platform {
namespace {

// The platform "handle": the Windows API hands out an OS HANDLE; Linux memory
// syscalls take the pid every call, so the token just carries it.
struct GameHandle { uint32_t pid; std::string start; };

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
    // process_vm_readv/writev. XInitThreads must be the first Xlib call of
    // the process when several threads share Xlib (the x11overlay control
    // connection is used from the overlay thread and from feature threads
    // through GameClientRect); SDL's own connection comes later.
#if defined(HAS_X11)
    XInitThreads();
#endif
    return true;
}

// ---- process + memory --------------------------------------------------------

uint32_t FindGameProcess()
{
    return sober::FindSoberPid();
}

uintptr_t GameBaseAddress(uint32_t pid)
{
    return sober::LibRobloxBase(pid);
}

bool OpenGameHandle(uint32_t pid, void** out)
{
    if (!out) return false;
    *out = nullptr;
    const auto start = sober::ProcessStartTicks(pid);
    // Unlike a PID token allocation, verified ELF reads prove read permission.
    if (start.empty() || !sober::LibRobloxBase(pid) || start != sober::ProcessStartTicks(pid)) return false;
    *out = new (std::nothrow) GameHandle{pid, start};
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
    return !g->start.empty() && sober::ProcessStartTicks(g->pid) == g->start;
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
bool GameWindowForeground()            { return x11overlay::ActiveWindowKind() == 'g'; }
bool ForegroundIsGameOrUs()
{
    const char k = x11overlay::ActiveWindowKind();
    return k == 'g' || k == 'u';   // unknown ('o'/0) keeps anti-AFK quiet
}
void SetMouseScreen(int x, int y)      { (void)x; (void)y; }
int ScreenDpi()
{
    const int dpi = x11overlay::ScreenDpi();
    return dpi > 0 ? dpi : 96;
}

// ---- screen / game window (EWMH via x11overlay) -----------------------------

bool HasGraphicalDisplay()
{
    // SDL's video backends honour these same two variables; reading them here
    // keeps getenv() in the one TU the repo's no-deprecated-crt check excludes.
    const char* x11 = getenv("DISPLAY");
    const char* wayland = getenv("WAYLAND_DISPLAY");
    return (x11 && *x11) || (wayland && *wayland);
}

void SetGamePid(uint32_t pid) { x11overlay::SetGamePid(pid); }
bool GameClientRect(int& x, int& y, int& w, int& h)
{
    return x11overlay::GameWindowRect(x, y, w, h);
}
bool CursorScreen(int& x, int& y) { return x11overlay::CursorScreen(x, y); }
bool CursorClient(int& x, int& y)
{
    int gx = 0, gy = 0, gw = 0, gh = 0;
    if (!x11overlay::GameWindowRect(gx, gy, gw, gh)) return false;
    if (!x11overlay::CursorScreen(x, y)) return false;
    x -= gx;
    y -= gy;
    return true;
}

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

// =============================================================================
//  x11overlay — EWMH game rect, XFixes click-through, root-grabbed hotkeys
//  (overlay_x11.h). Compiled only with HAS_X11; stubs otherwise so every
//  headless binary (sober_probe, CI parses) links unchanged.
// =============================================================================

#if defined(HAS_X11)

namespace x11overlay {
namespace {

std::mutex      g_ctl_mutex;
Display*        g_ctl = nullptr;      // control connection (overlay + feature threads)
Window          g_self = 0;           // our overlay window
int             g_depth = 0;
uint32_t        g_game_pid = 0;
bool            g_click_through = false;
bool            g_hotkeys = false;
bool            g_have_xfixes = false;
void          (*g_toggle_cb)() = nullptr;
char            g_status[256] = "x11: not initialised";

// Xlib's default error handler prints and EXITS the process on BadWindow -
// fatal when a window dies between our query and the read (game closing,
// overlay resize). Every call in this layer is best-effort.
int IgnoreXError(Display*, XErrorEvent*) { return 0; }

uint64_t NowMonoMs()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

// Caller holds g_ctl_mutex.
Display* Ctl()
{
    if (!g_ctl) {
        g_ctl = XOpenDisplay(nullptr);
        if (g_ctl) XSetErrorHandler(IgnoreXError);
    }
    return g_ctl;
}

// Format-32 properties come back as arrays of long (32 bits per slot, even on
// LP64) - _NET_CLIENT_LIST, _NET_WM_PID, _NET_ACTIVE_WINDOW all use this.
bool ReadProp32(Display* d, Window w, Atom atom, std::vector<unsigned long>& out)
{
    out.clear();
    if (atom == None) return false;
    Atom type = 0;
    int fmt = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char* prop = nullptr;
    if (XGetWindowProperty(d, w, atom, 0, 1024, False, AnyPropertyType,
                           &type, &fmt, &nitems, &bytes_after, &prop) != Success || !prop)
        return false;
    const bool ok = (fmt == 32);
    if (ok) {
        const unsigned long* vals = reinterpret_cast<const unsigned long*>(prop);
        out.assign(vals, vals + nitems);
    }
    XFree(prop);
    return ok;
}

std::string ReadPropString(Display* d, Window w, Atom atom)
{
    if (atom == None) return {};
    Atom type = 0;
    int fmt = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char* prop = nullptr;
    if (XGetWindowProperty(d, w, atom, 0, 1024, False, AnyPropertyType,
                           &type, &fmt, &nitems, &bytes_after, &prop) != Success || !prop)
        return {};
    std::string s(reinterpret_cast<const char*>(prop), nitems);
    XFree(prop);
    return s;
}

std::vector<Window> TopLevelWindows(Display* d, Window root)
{
    std::vector<Window> out;
    Atom list = XInternAtom(d, "_NET_CLIENT_LIST", True);
    std::vector<unsigned long> v;
    if (ReadProp32(d, root, list, v) && !v.empty()) {
        for (unsigned long x : v) out.push_back((Window)x);
        return out;
    }
    Window r = 0, parent = 0, *children = nullptr;
    unsigned n = 0;
    if (XQueryTree(d, root, &r, &parent, &children, &n)) {
        for (unsigned i = 0; i < n; ++i) out.push_back(children[i]);
        if (children) XFree(children);
    }
    return out;
}

bool WindowUsable(Display* d, Window w)
{
    XWindowAttributes wa{};
    return XGetWindowAttributes(d, w, &wa) && wa.map_state == IsViewable &&
           wa.width >= 2 && wa.height >= 2;
}

std::string WindowTitle(Display* d, Window w)
{
    Atom net_name = XInternAtom(d, "_NET_WM_NAME", True);
    std::string t = ReadPropString(d, w, net_name);
    if (t.empty()) t = ReadPropString(d, w, XA_WM_NAME);
    return t;
}

bool TitleIsGame(const std::string& t)
{
    std::string l;
    l.reserve(t.size());
    for (char c : t) l.push_back((char)tolower((unsigned char)c));
    return l.find("roblox") != std::string::npos ||
           l.find("sober") != std::string::npos;
}

// Caller holds g_ctl_mutex.
Window FindGameWindow(Display* d, Window root, uint32_t pid)
{
    const std::vector<Window> wins = TopLevelWindows(d, root);
    if (pid) {
        Atom wm_pid = XInternAtom(d, "_NET_WM_PID", True);
        for (Window w : wins) {
            std::vector<unsigned long> v;
            if (ReadProp32(d, w, wm_pid, v) && !v.empty() && (uint32_t)v[0] == pid)
                return w;
        }
    }
    for (Window w : wins) {
        if (!WindowUsable(d, w)) continue;
        if (TitleIsGame(WindowTitle(d, w))) return w;
    }
    return None;
}

void* HotkeyThread(void*)
{
    // Own Display connection: SDL owns its own, and a passive grab delivers to
    // the grabbing connection. Root is an ancestor of every focus window, so
    // the grab fires no matter who has focus - including our menu, and
    // including the game when the overlay is click-through.
    Display* d = XOpenDisplay(nullptr);
    if (!d) return nullptr;
    XSetErrorHandler(IgnoreXError);
    const Window root = DefaultRootWindow(d);
    const KeyCode k_home = XKeysymToKeycode(d, XK_Home);
    const KeyCode k_f8 = XKeysymToKeycode(d, XK_F8);
    XGrabKey(d, k_home, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(d, k_f8, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
    XSync(d, False);
    uint64_t last = 0;
    for (;;) {
        XEvent ev;
        XNextEvent(d, &ev);
        if (ev.type != KeyPress || !g_toggle_cb) continue;
        const uint64_t now = NowMonoMs();
        if (now - last < 250) continue;   // auto-repeat + dual delivery debounce
        last = now;
        g_toggle_cb();
    }
    return nullptr;
}

} // namespace

bool Available()
{
    const char* disp = getenv("DISPLAY");
    return disp && *disp;
}

void SetGamePid(uint32_t pid)
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    g_game_pid = pid;
}

bool AttachSelf(const char* wm_class)
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d || !wm_class || !wm_class[0]) return false;
    const Window root = DefaultRootWindow(d);
    Atom wclass = XInternAtom(d, "WM_CLASS", True);
    for (Window w : TopLevelWindows(d, root)) {
        std::string cls = ReadPropString(d, w, wclass);
        if (cls.empty()) continue;
        const size_t nul = cls.find('\0');
        const std::string first = cls.substr(0, nul == std::string::npos ? cls.size() : nul);
        bool match = first.size() == strlen(wm_class);
        if (match)
            for (size_t i = 0; i < first.size(); ++i)
                if (tolower((unsigned char)first[i]) != tolower((unsigned char)wm_class[i])) {
                    match = false;
                    break;
                }
        if (!match) continue;
        g_self = w;
        XWindowAttributes wa{};
        if (XGetWindowAttributes(d, g_self, &wa)) g_depth = wa.depth;
        return true;
    }
    return false;
}

bool SetDockHints()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d || !g_self) return false;
    const Window root = DefaultRootWindow(d);
    const Atom state = XInternAtom(d, "_NET_WM_STATE", False);
    const Atom above = XInternAtom(d, "_NET_WM_STATE_ABOVE", False);
    const Atom skip_task = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
    const Atom skip_pager = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", False);

    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = g_self;
    ev.xclient.message_type = state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;                       // _NET_WM_STATE_ADD
    ev.xclient.data.l[1] = (long)above;
    ev.xclient.data.l[2] = (long)skip_task;
    ev.xclient.data.l[3] = 1;                       // source: application
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    ev.xclient.data.l[1] = (long)skip_pager;
    ev.xclient.data.l[2] = 0;
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    const Atom sticky = XInternAtom(d, "_NET_WM_STATE_STICKY", False);
    ev.xclient.data.l[1] = (long)sticky;
    ev.xclient.data.l[2] = 0;
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);

    const Atom wtype = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    const Atom utility = XInternAtom(d, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(d, g_self, wtype, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&utility), 1);
    XFlush(d);
    return true;
}

bool SetClickThrough(bool enable)
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
#if defined(HAS_XFIXES)
    Display* d = Ctl();
    if (!d || !g_self) return false;
    int evb = 0, errb = 0;
    if (!XFixesQueryExtension(d, &evb, &errb)) {
        g_click_through = false;
        return false;
    }
    // XFixesSetWindowShapeRegion with an empty region makes the input shape
    // empty (clicks fall through); a full-window region restores normal input.
    // Shape kind 2 == ShapeInput from <X11/extensions/shape.h> (libXext); the
    // numeric value is protocol-stable and avoids a header dependency on Xext
    // for one constant.
    const int kShapeInput = 2;
    if (enable) {
        XserverRegion empty = XFixesCreateRegion(d, nullptr, 0);
        XFixesSetWindowShapeRegion(d, g_self, kShapeInput, 0, 0, empty);
        XFixesDestroyRegion(d, empty);
    } else {
        XWindowAttributes wa{};
        if (!XGetWindowAttributes(d, g_self, &wa)) return false;
        XRectangle r{};
        r.width = (unsigned short)wa.width;
        r.height = (unsigned short)wa.height;
        XserverRegion full = XFixesCreateRegion(d, &r, 1);
        XFixesSetWindowShapeRegion(d, g_self, kShapeInput, 0, 0, full);
        XFixesDestroyRegion(d, full);
    }
    XFlush(d);
    g_click_through = enable;
    return true;
#else
    (void)enable;
    return false;
#endif
}

bool StartToggleHotkeys(void (*on_toggle)())
{
    {
        std::lock_guard<std::mutex> lk(g_ctl_mutex);
        if (g_hotkeys) return true;
        g_toggle_cb = on_toggle;
    }
    pthread_t t{};
    pthread_attr_t attr{};
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    const bool ok = pthread_create(&t, &attr, HotkeyThread, nullptr) == 0;
    pthread_attr_destroy(&attr);
    if (ok) {
        std::lock_guard<std::mutex> lk(g_ctl_mutex);
        g_hotkeys = true;
    }
    return ok;
}

int WindowDepth()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    if (!g_self) return 0;
    return g_depth;
}

bool ArgbVisualAvailable()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d) return false;
    XVisualInfo vi{};
    return XMatchVisualInfo(d, DefaultScreen(d), 32, TrueColor, &vi) != 0;
}

uint64_t ArgbVisualId()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d) return 0;
    XVisualInfo vi{};
    if (!XMatchVisualInfo(d, DefaultScreen(d), 32, TrueColor, &vi)) return 0;
    return (uint64_t)vi.visualid;
}

// Follow-tick stacking guard: trust the EWMH stacking list first, and only pay
// for a client message + raise when it says we are UNDER the game.
bool EnsureAboveGame()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d || !g_self) return false;
    const Window root = DefaultRootWindow(d);
    const Window gw = FindGameWindow(d, root, g_game_pid);
    if (gw == None) return true;               // no game window yet: nothing to beat
    Atom stacking = XInternAtom(d, "_NET_CLIENT_LIST_STACKING", True);
    std::vector<unsigned long> order;
    if (stacking && ReadProp32(d, root, stacking, order)) {
        long our_i = -1, game_i = -1;
        for (size_t i = 0; i < order.size(); ++i) {
            if ((Window)order[i] == g_self)  our_i = (long)i;
            else if ((Window)order[i] == gw) game_i = (long)i;
        }
        // bottom-to-top list: a higher index is already on top.
        if (our_i >= 0 && game_i >= 0 && our_i > game_i) return true;
        if (our_i < 0 || game_i < 0)
            return true;   // we cannot see both: dock hints already asked for ABOVE
    }
    const Atom state  = XInternAtom(d, "_NET_WM_STATE", False);
    const Atom above  = XInternAtom(d, "_NET_WM_STATE_ABOVE", False);
    XEvent ev{};
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = g_self;
    ev.xclient.message_type = state;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = 1;               // _NET_WM_STATE_ADD
    ev.xclient.data.l[1]    = (long)above;
    ev.xclient.data.l[2]    = 0;
    ev.xclient.data.l[3]    = 1;               // source: application
    XSendEvent(d, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XRaiseWindow(d, g_self);
    XFlush(d);
    return true;
}

bool GameWindowRect(int& x, int& y, int& w, int& h)
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d) return false;

    static Window cached = None;
    static uint64_t cached_at = 0;
    static int rx = 0, ry = 0, rw = 0, rh = 0;
    const uint64_t now = NowMonoMs();
    if (cached != None && now - cached_at < 100) {
        x = rx; y = ry; w = rw; h = rh;
        return true;
    }

    const Window root = DefaultRootWindow(d);
    const Window gw = FindGameWindow(d, root, g_game_pid);
    if (gw == None) { cached = None; cached_at = now; return false; }

    XWindowAttributes wa{};
    if (!XGetWindowAttributes(d, gw, &wa) || wa.map_state != IsViewable ||
        wa.width < 2 || wa.height < 2) {
        cached = None;
        cached_at = now;
        return false;
    }
    Window child = 0;
    int ax = 0, ay = 0;
    if (!XTranslateCoordinates(d, gw, root, 0, 0, &ax, &ay, &child)) {
        cached = None;
        cached_at = now;
        return false;
    }
    rx = ax; ry = ay; rw = wa.width; rh = wa.height;
    cached = gw;
    cached_at = now;
    x = rx; y = ry; w = rw; h = rh;
    return true;
}

char ActiveWindowKind()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d) return 0;
    const Window root = DefaultRootWindow(d);
    Atom active = XInternAtom(d, "_NET_ACTIVE_WINDOW", True);
    std::vector<unsigned long> v;
    if (!ReadProp32(d, root, active, v) || v.empty()) return 0;
    const Window aw = (Window)v[0];
    if (!aw) return 0;
    if (g_self && aw == g_self) return 'u';
    if (g_game_pid) {
        Atom wm_pid = XInternAtom(d, "_NET_WM_PID", True);
        std::vector<unsigned long> pv;
        if (ReadProp32(d, aw, wm_pid, pv) && !pv.empty() && (uint32_t)pv[0] == g_game_pid)
            return 'g';
    }
    return TitleIsGame(WindowTitle(d, aw)) ? 'g' : 'o';
}

bool CursorScreen(int& x, int& y)
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d) return false;
    const Window root = DefaultRootWindow(d);
    Window r = 0, c = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    if (!XQueryPointer(d, root, &r, &c, &rx, &ry, &wx, &wy, &mask)) return false;
    x = rx;
    y = ry;
    return true;
}

int ScreenDpi()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    Display* d = Ctl();
    if (!d) return 0;
    const int scr = DefaultScreen(d);
    const int wmm = DisplayWidthMM(d, scr);
    if (wmm <= 0) return 0;
    return (int)((double)DisplayWidth(d, scr) * 25.4 / (double)wmm + 0.5);
}

const char* StatusLine()
{
    std::lock_guard<std::mutex> lk(g_ctl_mutex);
    if (!Ctl()) {
        snprintf(g_status, sizeof(g_status), "x11: no DISPLAY (headless or wayland-only)");
        return g_status;
    }
    snprintf(g_status, sizeof(g_status),
             "x11: window=%s depth=%d xfixes=%d click-through=%s hotkeys=%s pid=%u",
             g_self ? "found" : "not found", g_depth,
#if defined(HAS_XFIXES)
             1,
#else
             0,
#endif
             g_click_through ? "on" : "off",
             g_hotkeys ? "on" : "off",
             g_game_pid);
    return g_status;
}

} // namespace x11overlay

#else // !HAS_X11

namespace x11overlay {
bool Available() { return false; }
void SetGamePid(uint32_t) {}
bool AttachSelf(const char*) { return false; }
bool SetDockHints() { return false; }
bool SetClickThrough(bool) { return false; }
bool StartToggleHotkeys(void (*)()) { return false; }
int WindowDepth() { return 0; }
bool ArgbVisualAvailable() { return false; }
uint64_t ArgbVisualId() { return 0; }
bool EnsureAboveGame() { return false; }
bool GameWindowRect(int&, int&, int&, int&) { return false; }
char ActiveWindowKind() { return 0; }
bool CursorScreen(int&, int&) { return false; }
int ScreenDpi() { return 0; }
const char* StatusLine() { return "x11: not in build (cmake found no X11)"; }
} // namespace x11overlay

#endif // HAS_X11

// ---- raw mouse input ----------------------------------------------------------
// Uses SDL relative mouse mode. Requires an SDL window/context to be active.
namespace {
    bool s_raw_mouse_active = false;
}

bool platform::TakeRawMouse(int& dx, int& dy) {
#ifdef HAS_SDL_OVERLAY
    dx = 0; dy = 0;
    if (!s_raw_mouse_active) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        s_raw_mouse_active = true;
        // First call after enabling returns 0,0 as the cursor is recentered
        return false;
    }
    // Get relative mouse motion from SDL event queue
    // We'll read from SDL_GetRelativeMouseState or events
    // For now, use SDL_GetRelativeMouseState
    dx = 0; dy = 0;
    Uint32 buttons = SDL_GetRelativeMouseState(&dx, &dy);
    return (dx != 0 || dy != 0);
#else
    dx = 0; dy = 0;
    return false;
#endif
}

void platform::DropRawMouse() {
#ifdef HAS_SDL_OVERLAY
    if (s_raw_mouse_active) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        s_raw_mouse_active = false;
    }
#endif
}

bool platform::IsRawMouseActive() {
    return s_raw_mouse_active;
}
