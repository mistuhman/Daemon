// -----------------------------------------------------------------------------
//  A minimal stand-in for <Windows.h>, used only by tools/check_win_tu.sh to run
//  `-fsyntax-only` over the host app's Win32 glue on a machine with no Windows
//  SDK. Signatures follow the real headers where it matters (const-ness of the
//  buffers, DWORD for the thread-proc return) because that is exactly what the
//  check is for; everything returns zero/null because nothing here can run.
//
//  Deliberately NOT a shim: no lib, no ABI, no includes beyond stdint. If a TU
//  needs toolhelp or D3D, it is out of scope for this file -- see the header of
//  tools/check_win_tu.sh.
// -----------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <cstddef>
typedef void* HANDLE;
typedef void* HWND;
typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef unsigned int UINT;
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef const char* LPCSTR;
typedef char* LPSTR;
typedef void* LPVOID;
typedef void* HMODULE;
typedef unsigned short WORD;
typedef int INT;
typedef unsigned long REGSAM;

typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;
#define TRUE 1
#define FALSE 0
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_ESCAPE 0x1B
#define VK_SHIFT 0x10
#define VK_SPACE 0x20
#define VK_LSHIFT 0xA0
#define VK_LCONTROL 0xA2
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define XBUTTON1 1
#define XBUTTON2 2
// Bare macros the real umbrella headers define, which eat innocent identifiers.
// winnt.h access rights (the DELETE one cost a real MSVC build - platform.h
// once declared `constexpr int DELETE = 0x2E;` and every Windows.h-first TU
// got C2059 "syntax error: 'constant'" at that line):
#define DELETE           (0x00010000L)
#define READ_CONTROL     (0x00020000L)
#define WRITE_DAC        (0x00040000L)
#define WRITE_OWNER      (0x00080000L)
#define SYNCHRONIZE      (0x00100000L)
#define MAXIMUM_ALLOWED  (0x02000000L)
#define GENERIC_ALL      (0x10000000L)
#define GENERIC_EXECUTE  (0x20000000L)
#define GENERIC_WRITE    (0x40000000L)
#define GENERIC_READ     (0x80000000L)
// wingdi.h background-mode trio (ERROR 0 is real, and famous):
#define TRANSPARENT 1
#define OPAQUE 2
#define ERROR 0
#define PROCESS_VM_READ 0x10
#define PROCESS_VM_WRITE 0x20
#define PROCESS_QUERY_INFORMATION 0x400
#define MAX_PATH 260
inline void Sleep(DWORD) {}
inline short GetAsyncKeyState(int) { return 0; }
inline UINT MapVirtualKeyA(UINT, UINT) { return 0; }
inline int GetKeyNameTextA(LONG, LPSTR, int) { return 0; }
inline HWND WindowFromPoint(POINT) { return nullptr; }
inline BOOL GetCursorPos(POINT* p) { (void)p; return TRUE; }
// client <-> screen: the point is passed through unchanged, which is all a parse needs
inline BOOL ScreenToClient(HWND, POINT* p) { (void)p; return TRUE; }
inline BOOL ClientToScreen(HWND, POINT* p) { (void)p; return TRUE; }
inline BOOL GetClientRect(HWND, RECT* r) { if (r) { r->left=0; r->top=0; r->right=1920; r->bottom=1080; } return TRUE; }
inline BOOL ReadProcessMemory(HANDLE, void*, void*, size_t, size_t*) { return 0; }
inline BOOL WriteProcessMemory(HANDLE, void*, const void*, size_t, size_t*) { return 0; }
inline BOOL CloseHandle(HANDLE) { return TRUE; }
inline HANDLE OpenProcess(DWORD, BOOL, DWORD) { return nullptr; }
// ---- extras needed by the app's glue TUs (memory / cache / config) ----------
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x0010
#define PROCESS_VM_OPERATION 0x0008
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define CREATE_NEW 1
#define SW_SHOWDEFAULT 10
#define _TRUNCATE ((size_t)-1)
#define WINAPI
#define CALLBACK
#define __stdcall
typedef struct { DWORD dwFileAttributes; DWORD nFileSizeHigh; DWORD nFileSizeLow;
                 char cFileName[MAX_PATH]; } WIN32_FIND_DATAA;
inline HANDLE CreateThread(void*, size_t, DWORD (*)(void*), void*, DWORD, DWORD*) { return nullptr; }
inline DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
inline BOOL CloseHandle_(HANDLE) { return TRUE; }
inline HANDLE FindFirstFileA(LPCSTR, WIN32_FIND_DATAA*) { return INVALID_HANDLE_VALUE; }
inline BOOL FindNextFileA(HANDLE, WIN32_FIND_DATAA*) { return FALSE; }
inline BOOL FindClose(HANDLE) { return TRUE; }
inline BOOL CreateDirectoryA(LPCSTR, void*) { return FALSE; }
inline BOOL DeleteFileA(LPCSTR) { return TRUE; }
inline BOOL MoveFileA(LPCSTR, LPCSTR) { return TRUE; }
inline HANDLE ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT) { return nullptr; }
// ---- registry + tick count (the rail card shows the OS; the camera cache ages
// itself by tick) ----
inline DWORD GetTickCount() { return 0; }
// ---- the high-resolution clock the movement features pace themselves with ----
// (features/movement/mover.cpp: GetTickCount's ~15ms granularity is coarser than
// the 16ms step it schedules, so the real thing has to be QPC.)
typedef union { struct { DWORD LowPart; LONG HighPart; }; long long QuadPart; } LARGE_INTEGER;
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* p) { if (p) p->QuadPart = 10000000; return TRUE; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* p) { if (p) p->QuadPart = 0; return TRUE; }
typedef DWORD* LPDWORD;
typedef void* HKEY;
#define HKEY_LOCAL_MACHINE ((HKEY)(long long)0x80000002)
#define KEY_READ 0x20019
#define ERROR_SUCCESS 0L
typedef unsigned char BYTE, *LPBYTE;
typedef const unsigned char* LPCBYTE;
inline LONG RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, HKEY*) { return 0; }
inline LONG RegQueryValueExA(HKEY, LPCSTR, LPDWORD, DWORD*, LPBYTE, LPDWORD) { return 0; }
inline LONG RegCloseKey(HKEY) { return 0; }
// MSVC's bounds-checked CRT, enough for a syntax pass
template <size_t N> inline int strcpy_s(char (&d)[N], const char* s) { (void)d; (void)s; return 0; }
inline int strncpy_s(char* d, size_t cap, const char* s, size_t n) { (void)d; (void)cap; (void)s; (void)n; return 0; }
template <size_t N> inline int strncpy_s(char (&d)[N], size_t cap, const char* s, size_t n) { (void)d; (void)cap; (void)s; (void)n; return 0; }
template <size_t N> inline int sprintf_s(char (&d)[N], const char* f, ...) { (void)d; (void)f; return 0; }
template <size_t N> inline int snprintf_s(char (&d)[N], size_t, const char* f, ...) { (void)d; (void)f; return 0; }


// ---- wide strings, the process handle's lifetime, and the shell hand-off ------
// (added for features/servers/servers_http.cpp: the type names below are the real
// ones -- URL_COMPONENTSW, SE_ERR_FNF, GetTickCount64 -- because a stub that invented
// a comfortable alias would let a wrong name in the app pass. This is the whole point
// of the file: MSVC-only mistakes, caught off Windows.)
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef const char* LPCCH;
typedef void* HINSTANCE;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;              // mover's hold-thread pacing uses it
typedef short SHORT;                     // mover's shared space poll returns it
typedef unsigned long long ULONG_PTR;
typedef long long LONG_PTR;
typedef unsigned long long DWORD_PTR;
#define CP_UTF8 65001
#define SW_SHOWNORMAL 1
#define WM_CLOSE 0x0010
#define STILL_ACTIVE 259
#define SE_ERR_FNF 2
#define SE_ERR_DDEFAIL 29
#define SE_ERR_DDEBUSY 30
#define SE_ERR_NOASSOC 31
inline int MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int) { return 0; }
inline int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, BOOL*) { return 0; }
inline DWORD GetLastError() { return 0; }
inline void SetLastError(DWORD) {}
inline ULONGLONG GetTickCount64() { return 0; }
inline BOOL GetExitCodeProcess(HANDLE, LPDWORD) { return TRUE; }
inline BOOL IsWindow(HWND) { return FALSE; }
inline BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
inline DWORD GetModuleFileNameA(HMODULE, LPSTR, DWORD) { return 0; }

// --- critical sections / thread priority (used by mover's hold thread) ---
typedef struct _RTL_CRITICAL_SECTION { void* opaque[8]; } CRITICAL_SECTION, *LPCRITICAL_SECTION;
inline void InitializeCriticalSection(LPCRITICAL_SECTION) {}
inline void DeleteCriticalSection(LPCRITICAL_SECTION) {}
inline void EnterCriticalSection(LPCRITICAL_SECTION) {}
inline void LeaveCriticalSection(LPCRITICAL_SECTION) {}
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
inline int SetThreadPriority(HANDLE, int) { return 1; }
inline HANDLE GetCurrentThread() { return (HANDLE)0; }
inline int SwitchToThread() { return 0; }

// --- interlocked (used by mover's velocity-burst write counter). Real
// signatures: they take LONG volatile* and return the new value, and MSVC's
// winnt.h declares exactly these - a stub that took a plain LONG* would pass
// here and fail on the machine that matters. ---
inline LONG InterlockedIncrement(LONG volatile* dst) { return ++*dst; }
inline LONG InterlockedExchange(LONG volatile* dst, LONG val) { LONG o = *dst; *dst = val; return o; }

// --- the raw-NT type names memory.h / nt_syscalls.h spell --- 
// (12 TUs failed the parse on exactly these four; they are plain aliases of
// ones above, but the app says ULONG, so the stub has to too.)
typedef unsigned long ULONG, *PULONG;
typedef unsigned short USHORT, *PUSHORT;
typedef void* PVOID;
typedef wchar_t* PWSTR;
typedef size_t SIZE_T, *PSIZE_T;

// --- child process + pipes (added for servers_http.cpp's curl.exe transport;
// offsets.imtheo.lol sits behind Cloudflare and drops WinHTTP's TLS hello, so
// the fallback fetch runs System32\curl.exe and reads its stdout) ---
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; }
        SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED { DWORD Internal, InternalHigh; } OVERLAPPED, *LPOVERLAPPED;
typedef struct { DWORD cb; LPVOID lpReserved, lpDesktop, lpTitle; DWORD dwX, dwY, dwXSize, dwYSize,
                 dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; WORD wShowWindow, cbReserved2;
                 LPBYTE lpReserved2; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOW, *LPSTARTUPINFOW;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;
#define GENERIC_READ  0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ   0x00000001
#define FILE_SHARE_WRITE  0x00000002
#define OPEN_EXISTING 3
#define CREATE_ALWAYS  2
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define HANDLE_FLAG_INHERIT 0x00000001
#define STARTF_USESTDHANDLES 0x00000100
#define CREATE_NO_WINDOW 0x08000000
typedef HANDLE* PHANDLE;
inline BOOL CreatePipe(PHANDLE, PHANDLE, LPSECURITY_ATTRIBUTES, DWORD) { return FALSE; }
inline BOOL SetHandleInformation(HANDLE, DWORD, DWORD) { return FALSE; }
inline HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return (HANDLE)0; }
inline BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) { return FALSE; }
inline BOOL PeekNamedPipe(HANDLE, LPVOID, DWORD, LPDWORD, LPDWORD, LPDWORD) { return FALSE; }
inline BOOL CreateProcessW(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
                           LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) { return FALSE; }
inline BOOL TerminateProcess(HANDLE, UINT) { return FALSE; }
inline UINT GetSystemDirectoryW(LPWSTR, UINT) { return 0; }
inline DWORD GetFileAttributesW(LPCWSTR) { return INVALID_FILE_ATTRIBUTES; }
inline HANDLE GetStdHandle(DWORD) { return (HANDLE)0; }
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

// --- window enumeration + input/output (added for platform_win.cpp: the
// game-window cache, cursor/client transforms and mouse output moved there
// when the platform seam landed; the app-side features call platform::*) ---
#define GW_OWNER 0
#define WM_APP 0x8000
typedef struct { DWORD ck; HWND hwnd; UINT msg; WPARAM wParam; LPARAM lParam; } MSG, *LPMSG;
typedef int (CALLBACK *WNDENUMPROC)(HWND, LPARAM);
// CRT wide-compare (MSVC's <wchar.h> spells it _wcsnicmp; g++ does not)
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const wchar_t ca = a[i], cb = b[i];
        if (ca != cb) {
            const int da = (ca >= L'a' && ca <= L'z') ? ca - 32 : ca;
            const int db = (cb >= L'a' && cb <= L'z') ? cb - 32 : cb;
            if (da != db) return da < db ? -1 : 1;
        }
        if (!a[i]) return 0;
    }
    return 0;
}
inline BOOL EnumWindows(WNDENUMPROC, LPARAM) { return FALSE; }
inline HWND GetWindow(HWND, UINT) { return nullptr; }
inline BOOL IsWindowVisible(HWND) { return FALSE; }
inline DWORD GetWindowThreadProcessId(HWND, LPDWORD) { return 0; }
inline DWORD GetCurrentProcessId() { return 0; }
// mouse output (aimbot) + DPI
inline BOOL SetCursorPos(int, int) { return FALSE; }
typedef struct { DWORD type; DWORD dwFlags; LONG dx, dy; DWORD mouseData, time; ULONG_PTR dwExtraInfo; } MOUSEINPUT, *LPMOUSEINPUT;
typedef struct { WORD wVk; WORD wScan; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; } KEYBDINPUT, *LPKEYBDINPUT;
// An anonymous union, exactly like the real thing: MOUSEINPUT and KEYBDINPUT
// overlap in the real struct, and platform_win.cpp writes both (the aimbot
// moves the mouse, anti-AFK taps a key). Without the union the keyboard member
// does not exist here and the TU stops parsing - which is the stub's one job.
typedef struct tagINPUT { DWORD type; union { MOUSEINPUT mi; KEYBDINPUT ki; }; } INPUT, *LPINPUT;
#define INPUT_MOUSE 0
#define INPUT_KEYBOARD 1
#define KEYEVENTF_KEYUP 0x0002
inline HWND GetForegroundWindow() { return nullptr; }
#define MOUSEEVENTF_MOVE 0x0001
#define MOUSEEVENTF_LEFTDOWN 0x0002
#define MOUSEEVENTF_LEFTUP 0x0004
#define MOUSEEVENTF_RIGHTDOWN 0x0008
#define MOUSEEVENTF_RIGHTUP 0x0010
inline UINT SendInput(UINT, LPINPUT, int) { return 0; }
typedef void* HDC;
#define LOGPIXELSX 88
#define LOGPIXELSY 90
inline HDC GetDC(HWND) { return nullptr; }
inline int ReleaseDC(HWND, HDC) { return 1; }
inline int GetDeviceCaps(HDC, int) { return 96; }
