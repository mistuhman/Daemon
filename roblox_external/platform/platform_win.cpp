// -----------------------------------------------------------------------------
//  platform/platform_win.cpp  --  the Windows implementation of platform.h
// -----------------------------------------------------------------------------
//  This is the ONE file that knows what OS the tool runs on (together with
//  the Win32 UI layer: overlay.cpp, menu.cpp, and the D3D11 backend). The NT
//  plumbing below was not written here - it moved from process.cpp (the
//  NtQuerySystemInformation enumeration and the PEB read), memory.cpp (the
//  handle open) and servers_http.cpp (curl discovery, the shell hand-off) so
//  that those files could become platform-free.
//
//  Everything that touches the game goes through the nt::* indirect-syscall
//  wrappers (see nt_syscalls.h): no WinAPI OpenProcess/ReadProcessMemory on
//  the hot paths, same as before the layer existed.
// -----------------------------------------------------------------------------
#include "platform.h"

#include <Windows.h>
#include <shellapi.h>

#include "nt_syscalls.h"

#include <new>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

namespace platform {

bool Init()
{
    return nt::Init();
}

namespace {

// ---- the game window cache ---------------------------------------------------
// FindGameWindow() used to be a per-call EnumWindows (process::GetRobloxWindow,
// once per projected ESP point). Cached per pid + invalidated when the window
// dies, the lookup now happens on attach and after a game restart instead.
struct FindData { DWORD pid; HWND result; };
DWORD g_game_pid = 0;       // what SetGamePid last said (0 = detached)
HWND  g_game_hwnd = nullptr;

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam)
{
    FindData* d = reinterpret_cast<FindData*>(lparam);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == d->pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd)) {
        d->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindGameWindow()
{
    if (g_game_pid == 0) return nullptr;
    if (g_game_hwnd && IsWindow(g_game_hwnd)) return g_game_hwnd;
    FindData d{ g_game_pid, nullptr };
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&d));
    g_game_hwnd = d.result;
    return g_game_hwnd;
}

// Thread trampoline: CreateThread wants a DWORD-returning WINAPI function,
// the platform API hands out plain void(void*). A cast would "work" on x64 by
// leaving RAX alone; this is the version that is not luck.
struct Thunk { void (*fn)(void*); void* arg; };
DWORD WINAPI ThreadTrampoline(LPVOID p)
{
    Thunk* t = static_cast<Thunk*>(p);
    t->fn(t->arg);
    delete t;
    return 0;
}

} // namespace

// ---- process + memory --------------------------------------------------------

uint32_t FindGameProcess()
{
    // ── enumerate processes via NtQuerySystemInformation ──────────────────
    // (moved verbatim from process.cpp; TlHelp32 was never used - the comment
    // there replaces CreateToolhelp32Snapshot with this.)
    ULONG bufLen = 0;
    NTSTATUS status = nt::QuerySystemInformation(
        SystemProcessInformation, nullptr, 0, &bufLen);

    // STATUS_INFO_LENGTH_MISMATCH = 0xC0000004 — expected on first call
    if (bufLen == 0) return 0;

    std::vector<BYTE> buffer(bufLen + 4096); // extra room for new processes
    status = nt::QuerySystemInformation(
        SystemProcessInformation, buffer.data(), (ULONG)buffer.size(), &bufLen);
    if (!NT_SUCCESS(status)) return 0;

    auto* current = (SYSTEM_PROCESS_INFORMATION*)buffer.data();
    while (true) {
        if (current->ImageName.Buffer && current->ImageName.Length > 0) {
            // UNICODE_STRING.Buffer is NOT guaranteed null-terminated —
            // use the Length field (in bytes, not including null).
            size_t charLen = current->ImageName.Length / sizeof(wchar_t);
            const wchar_t* name = current->ImageName.Buffer;

            if ((charLen == 20 && _wcsnicmp(name, L"RobloxPlayerBeta.exe", 20) == 0) ||
                (charLen == 10 && _wcsnicmp(name, L"Roblox.exe", 10) == 0)) {
                return (uint32_t)(uintptr_t)current->UniqueProcessId;
            }
        }
        if (current->NextEntryOffset == 0) break;
        current = (SYSTEM_PROCESS_INFORMATION*)((BYTE*)current + current->NextEntryOffset);
    }
    return 0;
}

uintptr_t GameBaseAddress(uint32_t pid)
{
    // ── open the process and read PEB → ImageBaseAddress ──────────────────
    HANDLE hProcess = nullptr;
    if (!nt::OpenProcess(pid,
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, &hProcess)) {
        return 0;
    }

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG retLen = 0;
    NTSTATUS qstatus = IndirectSyscall6(
        nt::sc_NtQueryInformationProcess.syscall_addr,
        nt::sc_NtQueryInformationProcess.ssn,
        hProcess,
        ProcessBasicInformation,
        &pbi,
        sizeof(pbi),
        &retLen,
        0);

    if (!NT_SUCCESS(qstatus) || !pbi.PebBaseAddress) {
        nt::CloseHandle(hProcess);
        return 0;
    }

    // PEB+0x10 on x64 = ImageBaseAddress (pointer to the loaded PE base)
    // This is a well-known offset that doesn't change across Windows versions.
    uintptr_t pebBase = (uintptr_t)pbi.PebBaseAddress;
    uintptr_t imageBase = 0;

    if (!nt::ReadVirtualMemory(hProcess, pebBase + 0x10,
            &imageBase, sizeof(imageBase), nullptr)) {
        nt::CloseHandle(hProcess);
        return 0;
    }

    nt::CloseHandle(hProcess);
    return imageBase;
}

bool OpenGameHandle(uint32_t pid, void** out)
{
    // The exact access the tool's features need, no more: reads (ESP, cache),
    // writes (movement, fov, skybox strings), VM_OPERATION for the remote
    // allocation, QUERY for the alive-check.
    const DWORD access = PROCESS_VM_READ | PROCESS_VM_WRITE |
                         PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION;
    HANDLE h = nullptr;
    if (!nt::OpenProcess(pid, access, &h)) return false;
    *out = h;
    return true;
}

void CloseHandle(void* h)
{
    if (h) nt::CloseHandle(h);
}

bool ProcessAlive(void* process_h)
{
    if (!process_h) return false;
    DWORD code = 0;
    if (!nt::GetProcessExitStatus(process_h, &code)) return false;
    return code == STILL_ACTIVE;
}

bool ReadMem(void* h, uintptr_t addr, void* buf, size_t size)
{
    return nt::ReadVirtualMemory(h, addr, buf, size, nullptr);
}

bool WriteMem(void* h, uintptr_t addr, const void* buf, size_t size)
{
    return nt::WriteVirtualMemory(h, addr, buf, size, nullptr);
}

bool AllocMem(void* h, size_t size, uintptr_t* out)
{
    void* base = nullptr;
    if (!nt::AllocateVirtualMemory(h, &base, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
        return false;
    *out = (uintptr_t)base;
    return true;
}

void FreeMem(void* h, uintptr_t base)
{
    void* b = (void*)base;
    nt::FreeVirtualMemory(h, &b, 0, MEM_RELEASE);
}

// ---- clock -------------------------------------------------------------------

uint64_t NowMs()
{
    return GetTickCount64();
}

uint64_t NowNs()
{
    // QPC into nanoseconds. The multiply goes through double on purpose:
    // MSVC x64 has no __int128, and a tick counter that fills double's 53-bit
    // mantissa at 10 MHz takes ~28 years of uptime. Deltas stay exact enough
    // for dt integration; nobody here cares about a nanosecond of drift.
    static double s_ns_per_tick = 0.0;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (s_ns_per_tick == 0.0) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        s_ns_per_tick = freq.QuadPart > 0 ? 1e9 / (double)freq.QuadPart : 1.0;
    }
    return (uint64_t)((double)now.QuadPart * s_ns_per_tick);
}

void SleepMs(uint32_t ms)
{
    ::Sleep(ms);
}

// ---- input state -------------------------------------------------------------

KeyState SampleKey(int key)
{
    const SHORT s = GetAsyncKeyState(key);
    return KeyState{ (s & 0x8000) != 0, (s & 0x0001) != 0 };
}

bool IsKeyDown(int key)  { return SampleKey(key).down; }
bool KeyPressed(int key) { return SampleKey(key).pressed_since_last; }

// ---- mouse output ------------------------------------------------------------

void MoveMouseRelative(int dx, int dy)
{
    // Relative MOUSEEVENTF_MOVE: not touched by the game's cursor re-centering
    // the way an absolute SetCursorPos is.
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = (LONG)dx;
    input.mi.dy = (LONG)dy;
    SendInput(1, &input, sizeof(INPUT));
}

void SetMouseScreen(int x, int y)
{
    SetCursorPos(x, y);
}

int ScreenDpi()
{
    // The aimbot used to cache this itself; the value cannot change while the
    // process runs without a WM_DPICHANGED nobody here listens for.
    static int s_dpi = 0;
    if (s_dpi == 0) {
        HDC hdc = GetDC(nullptr);
        s_dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc) ReleaseDC(nullptr, hdc);
        if (s_dpi <= 0) s_dpi = 96;
    }
    return s_dpi;
}

// ---- screen / game window ----------------------------------------------------

void SetGamePid(uint32_t pid)
{
    g_game_pid = pid;
    g_game_hwnd = nullptr;      // the old window belongs to the old process
}

bool GameClientRect(int& x, int& y, int& w, int& h)
{
    const HWND rbx = FindGameWindow();
    if (!rbx) return false;
    RECT client{};
    POINT origin{ 0, 0 };
    if (!GetClientRect(rbx, &client) || !ClientToScreen(rbx, &origin)) return false;
    x = origin.x;
    y = origin.y;
    w = client.right - client.left;
    h = client.bottom - client.top;
    return w > 0 && h > 0;
}

bool CursorScreen(int& x, int& y)
{
    POINT p{};
    if (!GetCursorPos(&p)) return false;
    x = p.x;
    y = p.y;
    return true;
}

bool CursorClient(int& x, int& y)
{
    const HWND rbx = FindGameWindow();
    if (!rbx) return false;
    POINT p{};
    if (!GetCursorPos(&p)) return false;
    if (!ScreenToClient(rbx, &p)) return false;
    x = p.x;
    y = p.y;
    return true;
}

// ---- threads -----------------------------------------------------------------

bool StartThread(void (*fn)(void*), void* arg)
{
    Thunk* t = new (std::nothrow) Thunk{ fn, arg };
    if (!t) return false;
    HANDLE h = CreateThread(nullptr, 0, ThreadTrampoline, t, 0, nullptr);
    if (!h) { delete t; return false; }
    ::CloseHandle(h);   // fire and forget: nothing waits on it
    return true;
}

void* StartJoinableThread(void (*fn)(void*), void* arg)
{
    Thunk* t = new (std::nothrow) Thunk{ fn, arg };
    if (!t) return nullptr;
    HANDLE h = CreateThread(nullptr, 0, ThreadTrampoline, t, 0, nullptr);
    if (!h) { delete t; return nullptr; }
    return h;
}

bool JoinThread(void* thread_h, uint32_t timeout_ms)
{
    if (!thread_h) return false;
    const bool exited = WaitForSingleObject(thread_h, timeout_ms) == WAIT_OBJECT_0;
    ::CloseHandle(thread_h);
    return exited;
}

void BoostThread()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
}

void YieldThread()
{
    SwitchToThread();
}

// ---- files / tools -----------------------------------------------------------

std::string ExeDir()
{
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!n) return ".\\";
    std::string s(path, n);
    const size_t slash = s.find_last_of("\\/");
    if (slash == std::string::npos) return ".\\";
    return s.substr(0, slash + 1);
}

std::string CurlPath()
{
    wchar_t sysdir[MAX_PATH] = {};
    if (!GetSystemDirectoryW(sysdir, MAX_PATH) || !sysdir[0]) return std::string();
    const std::wstring exe = std::wstring(sysdir) + L"\\curl.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return std::string();
    std::string out;
    out.resize(WideCharToMultiByte(CP_UTF8, 0, exe.c_str(), (int)exe.size(), nullptr, 0, nullptr, nullptr));
    if (!out.empty())
        WideCharToMultiByte(CP_UTF8, 0, exe.c_str(), (int)exe.size(), &out[0], (int)out.size(), nullptr, nullptr);
    return out;
}

bool OpenUrl(const char* url, std::string& err)
{
    err.clear();
    if (!url || !url[0]) { err = "nothing to open"; return false; }

    const int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
    std::wstring wurl((size_t)(n > 0 ? n : 1), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, url, -1, &wurl[0], n);

    const HINSTANCE r = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    const intptr_t code = (intptr_t)r;
    if (code > 32) return true;

    // ShellExecute's error codes are in the same range as its "success" handles, and
    // 31 (no association) is the one that actually happens when Roblox is not
    // installed (or its protocol handler was unregistered).
    const char* what = "the shell refused the link";
    if (code == (intptr_t)SE_ERR_NOASSOC)   what = "nothing is registered for roblox:// links";
    else if (code == (intptr_t)SE_ERR_DDEFAIL || code == (intptr_t)SE_ERR_DDEBUSY)
                                           what = "the shell was busy, try again";
    else if (code == (intptr_t)SE_ERR_FNF) what = "the shell could not find its target";
    err = std::string(what) + " (code " + std::to_string((int)code) + ") - the link is:\n" + url;
    return false;
}

} // namespace platform
