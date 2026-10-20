#pragma once
// -----------------------------------------------------------------------------
//  platform/platform.h  --  the whole OS surface of the tool, in one header
// -----------------------------------------------------------------------------
//  Everything above this layer (features, game/cache logic, offsets, the menu
//  core) is written against THESE calls only. The Windows implementation lives
//  in platform_win.cpp (NT syscalls, Win32 UI glue); a Linux port implements
//  this same header - platform_linux.cpp exists as a compiling starting point
//  (process_vm_readv, /proc, pthreads) for exactly that day.
//
//  Why each group is shaped the way it is:
//
//  * Handles are void*. "HANDLE" is a Windows word; the Linux memory API takes
//    a pid, so a port's "handle" is its own token. Nothing above this layer
//    may care.
//
//  * SampleKey returns BOTH the level and the edge in one struct. On Windows
//    the edge bit ("pressed since the previous GetAsyncKeyState call for that
//    key") is CONSUMED by each call, so sampling level and edge with two
//    separate calls would silently eat the edge. The single-sample shape is
//    not a style choice; it is the only correct one.
//
//  * GameClientRect/Cursor* hide "find the game's window and convert between
//    coordinate spaces". Windows needs EnumWindows + GetClientRect +
//    ClientToScreen for that; the Windows implementation also caches the
//    window handle, which the old per-call EnumWindows lookup did not.
//
//  * Threads: StartThread is fire-and-forget (the Windows implementation
//    closes its own handle; a pthread implementation detaches). JoinableThread
//    + JoinThread are for the one thread the app waits on (the cache).
//
//  * OpenGameHandle requests the access the tool needs - the mask is an
//    implementation detail, so a port does not inherit Windows access-right
//    vocabulary.
// -----------------------------------------------------------------------------
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace platform {

// One-time setup for everything else in this header. Call before any other
// platform call (on Windows this unhooks ntdll and resolves the syscall
// numbers the memory functions ride on).
bool Init();

// ---- process + memory --------------------------------------------------------
// Returns the game's pid, or 0 when it is not running.
uint32_t FindGameProcess();
// Base address of the game's main module in that process, 0 on failure.
uintptr_t GameBaseAddress(uint32_t pid);
// Opens the game with the access the tool needs. *out must be passed to
// CloseHandle when done.
bool OpenGameHandle(uint32_t pid, void** out);
// Closes a handle from OpenGameHandle / StartJoinableThread.
void CloseHandle(void* h);
// True while the process a handle was opened for is still running.
bool ProcessAlive(void* process_h);

bool ReadMem(void* h, uintptr_t addr, void* buf, size_t size);
bool WriteMem(void* h, uintptr_t addr, const void* buf, size_t size);

// Allocates readable/writable memory INSIDE the game process (the skybox
// changer writes asset strings there). Returns false where the platform has
// no way to do that from outside (Linux: no remote allocator over /proc).
bool AllocMem(void* h, size_t size, uintptr_t* out);
void FreeMem(void* h, uintptr_t base);

// ---- clock -------------------------------------------------------------------
// Monotonic milliseconds - step cadence, timeouts, age-of-cache math.
uint64_t NowMs();
// Monotonic nanoseconds, high resolution - dt integration (the mover).
uint64_t NowNs();
void SleepMs(uint32_t ms);

// ---- input state -------------------------------------------------------------
// Key ids are the Windows VK values - they are what the keybind settings and
// the config file have always stored, so a port translates these at its input
// layer instead of renumbering everyone's saved keybinds. Only the codes the
// feature layer polls are spelled out here; the Win32 menu layer uses the
// full set straight from <Windows.h>.
namespace key {
    constexpr int LBUTTON  = 0x01;
    constexpr int RBUTTON  = 0x02;
    constexpr int MBUTTON  = 0x04;
    constexpr int MOUSE_X1 = 0x05;
    constexpr int MOUSE_X2 = 0x06;
    constexpr int BACK     = 0x08;   // backspace
    constexpr int TAB      = 0x09;
    constexpr int RETURN   = 0x0D;
    constexpr int SHIFT    = 0x10;
    constexpr int CONTROL  = 0x11;
    constexpr int ESCAPE   = 0x1B;
    constexpr int SPACE    = 0x20;
    constexpr int LEFT     = 0x25;
    constexpr int UP       = 0x26;
    constexpr int RIGHT    = 0x27;
    constexpr int DOWN     = 0x28;
    // DEL, not DELETE: winnt.h `#define DELETE (0x00010000L)` (the access
    // right) eats the identifier in every TU that includes <Windows.h> first
    // - MSVC C2059 "syntax error: 'constant'". Same trap as XBUTTON1/2.
    constexpr int DEL      = 0x2E;
    constexpr int LSHIFT   = 0xA0;
    constexpr int RSHIFT   = 0xA1;
    constexpr int LCONTROL = 0xA2;
    constexpr int RCONTROL = 0xA3;
}

struct KeyState {
    bool down;                // held right now
    bool pressed_since_last;  // edge: went down since the previous sample of this key
};
// The ONE-call form. Two separate queries lose the edge - see the header note.
KeyState SampleKey(int key);
bool IsKeyDown(int key);      // convenience: SampleKey(key).down
bool KeyPressed(int key);     // convenience: SampleKey(key).pressed_since_last

// ---- mouse output ------------------------------------------------------------
// Relative move, not clipped by the game's cursor re-centering (aimbot).
void MoveMouseRelative(int dx, int dy);

// One complete left click (down, then up) on the game window. The triggerbot
// needs a press AND a release at the same instant, and doing it in one place
// keeps the whole "how does this tool produce input" story in the Win32 layer.
void ClickMouseLeft();

// One key press (down, then up) in a single batch, aimed at whatever window has
// focus. Used by anti-AFK, which is the only caller: everything else in this tool
// writes memory rather than synthesising input.
void TapKey(int key);

// Is the GAME's window the foreground window right now?
//
// Synthesised input goes to whatever has focus, so every caller of the two
// functions above that must not type into a browser has to ask this immediately
// before the call - not once at enable time, which is a different question.
bool GameWindowForeground();
// True when the foreground window belongs to the game OR to this tool. The
// overlay is a window of its own, so a strict game-only check would stop the
// aimbot the moment the menu is focused - which reads as "the aimbot randomly
// stops working". Input is only ever sent while one of the two is in front.
bool ForegroundIsGameOrUs();
// Absolute cursor position in screen coordinates.
void SetMouseScreen(int x, int y);
// Pixels per inch of the primary screen (96 = no scaling); for DPI-corrected
// humanized mouse motion.
int ScreenDpi();

// ---- screen / game window ----------------------------------------------------
// True when a graphical session is reachable: an X11 DISPLAY or a Wayland
// socket on Linux, always true on Windows. Lets the overlay decide headless
// vs windowed before it touches any OS-specific API (the env read itself
// stays in the platform implementations, which the C4996 invariant excludes).
bool HasGraphicalDisplay();
// Tells the platform layer which pid the window functions are for. Called on
// attach and detach; the implementation uses it to find (and cache) the game
// window, and forgets it when passed 0.
void SetGamePid(uint32_t pid);
bool GameClientRect(int& x, int& y, int& w, int& h);
bool CursorScreen(int& x, int& y);   // cursor in screen coordinates
bool CursorClient(int& x, int& y);   // cursor in game CLIENT coordinates

// ---- threads -----------------------------------------------------------------
bool StartThread(void (*fn)(void*), void* arg);
void* StartJoinableThread(void (*fn)(void*), void* arg);   // nullptr on failure
// Waits up to timeout_ms; true when the thread exited. Always releases the
// thread handle.
bool JoinThread(void* thread_h, uint32_t timeout_ms);
// Raises the calling thread's scheduling priority (the mover's hold thread).
void BoostThread();
// Yields the CPU for one timeslice when busy-waiting.
void YieldThread();

// ---- files / tools -----------------------------------------------------------
// Directory of the running executable, WITH trailing separator. (Cache files
// and the bundled offsets fallback live there.)
std::string ExeDir();
// Path to a usable curl binary for the HTTP fallback transport, "" if none.
std::string CurlPath();
// Hands a URL to the OS ("open in the app that owns roblox:// links").
bool OpenUrl(const char* url, std::string& err);
// Names (no directory prefix) of regular files in `dir` whose extension is
// `ext` (no dot, ASCII case-insensitive), sorted. Creates `dir` when missing.
// A relative `dir` is relative to the CWD, like the config folder. Out is
// cleared first; false means the folder could not be created/opened.
bool ListFilesWithExt(const char* dir, const char* ext, std::vector<std::string>& out);
// Plays a .wav asynchronously (never blocks, never fails loudly) at `volume`
// 0..100. Returns false when the OS refused the file.
bool PlayWav(const char* path, float volume);

} // namespace platform

// ---- raw mouse input ----------------------------------------------------------
// Linux: uses SDL relative mouse mode (captures cursor, reports raw deltas).
// Returns true if deltas were consumed (raw mouse is active).
bool platform::TakeRawMouse(int& dx, int& dy);
// Resets/disables raw mouse capture so cursor is free again.
void platform::DropRawMouse();
// True while raw mouse mode is active.
bool platform::IsRawMouseActive();
