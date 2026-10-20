// -----------------------------------------------------------------------------
//  Stub <winhttp.h> for tools/check_win_tu.sh.
// -----------------------------------------------------------------------------
//  Not a shim: nothing here can run, and the *names and const-ness* are copied from
//  the real header on purpose, including the ones that are easy to get wrong --
//  URL_COMPONENTSW (there is no WINHTTP_URL_COMPONENTS), WinHttpSetOption taking
//  LPVOID (so &const is an error), WinHttpConnect taking four arguments. The struct
//  below carries only the members the app reads, which is what turns a typo in a
//  member name into a compile failure off Windows instead of a build failure on it.
// -----------------------------------------------------------------------------
#pragma once
#include <Windows.h>

typedef void* HINTERNET;
typedef WORD INTERNET_PORT;

#define WINHTTP_FLAG_SECURE               0x00800000
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0
#define WINHTTP_OPTION_DISABLE_FEATURE    63
#define WINHTTP_OPTION_RESOLVE_TIMEOUT    3
#define WINHTTP_OPTION_CONNECT_TIMEOUT    2
#define WINHTTP_OPTION_SEND_TIMEOUT       5
#define WINHTTP_OPTION_RECEIVE_TIMEOUT    6
#define WINHTTP_OPTION_SECURE_PROTOCOLS  84
#define WINHTTP_DISABLE_COOKIES           0x00000004
#define WINHTTP_DISABLE_REDIRECTS         0x00000080
#define WINHTTP_QUERY_STATUS_CODE         19
#define WINHTTP_QUERY_FLAG_NUMBER         0x20000000
#define WINHTTP_HEADER_NAME_BY_INDEX      nullptr
#define WINHTTP_NO_HEADER_INDEX           nullptr
#define WINHTTP_NO_PROXY_NAME             nullptr
#define WINHTTP_NO_PROXY_BYPASS           nullptr
#define WINHTTP_NO_REFERER                nullptr
#define WINHTTP_NO_REQUEST_DATA            nullptr
#define WINHTTP_DEFAULT_ACCEPT_TYPES      nullptr

typedef struct {
  DWORD        dwStructSize;
  LPWSTR       lpszScheme;     DWORD dwSchemeLength;
  INTERNET_PORT nPort;
  LPWSTR       lpszHostName;   DWORD dwHostNameLength;
  LPWSTR       lpszUserName;   DWORD dwUserNameLength;
  LPWSTR       lpszPassword;   DWORD dwPasswordLength;
  LPWSTR       lpszUrlPath;    DWORD dwUrlPathLength;
  LPWSTR       lpszExtraInfo;  DWORD dwExtraInfoLength;
} URL_COMPONENTSW, *LPURL_COMPONENTSW;

inline HINTERNET WinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) { return nullptr; }
inline HINTERNET WinHttpConnect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) { return nullptr; }
// (connect, verb, object, referer, context, certUsageTypes, securityFlags): the real
// prototype has no version slot, and the difference is a DWORD where a NULL macro
// would otherwise be waved through.
inline HINTERNET WinHttpOpenRequest(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, DWORD,
                                    const void* const*, DWORD) { return nullptr; }
inline BOOL WinHttpSendRequest(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, ULONG_PTR) { return FALSE; }
inline BOOL WinHttpReceiveResponse(HINTERNET, LPVOID) { return FALSE; }
inline BOOL WinHttpQueryHeaders(HINTERNET, DWORD, LPCWSTR, LPVOID, DWORD*, DWORD*) { return FALSE; }
inline BOOL WinHttpQueryDataAvailable(HINTERNET, DWORD*) { return FALSE; }
inline BOOL WinHttpReadData(HINTERNET, LPVOID, DWORD, DWORD*) { return FALSE; }
inline BOOL WinHttpSetOption(HINTERNET, DWORD, LPVOID, DWORD) { return FALSE; }
inline BOOL WinHttpQueryOption(HINTERNET, DWORD, LPVOID, DWORD*) { return FALSE; }
inline BOOL WinHttpCloseHandle(HINTERNET) { return FALSE; }
inline BOOL WinHttpCrackUrl(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTSW) { return FALSE; }
