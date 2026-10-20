// Stub <shellapi.h> for tools/check_win_tu.sh: only what features/servers asks for.
// HINSTANCE-returning, in range-or-error semantics, exactly like the real one - so
// `if (code > 32) return true;` in the app is checked against something honest.
#pragma once
#include <Windows.h>
inline HINSTANCE ShellExecuteW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT) { return nullptr; }
// (SE_ERR_* used by platform_win.cpp's OpenUrl, which moved the shell
// hand-off out of servers_http.cpp)
#define SE_ERR_FNF      2
#define SE_ERR_NOASSOC  31
#define SE_ERR_DDEFAIL  29
#define SE_ERR_DDEBUSY  30
