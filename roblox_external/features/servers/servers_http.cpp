// -----------------------------------------------------------------------------
//  features/servers/servers_http.cpp  --  WinHTTP + the shell
// -----------------------------------------------------------------------------
//  Not in tools/check_win_tu.sh's default list: it needs <winhttp.h>, which the
//  stub folder does not carry, and it is 90% handle plumbing. What can go wrong in
//  it is either reported through `err` or visible as an empty list that says why.
// -----------------------------------------------------------------------------
#include "servers_http.h"

// Included bare, like every other TU here. (An earlier version of this file defined
// WIN32_LEAN_AND_MEAN and blamed it for the C3861 _beginthreadex failure in <thread>;
// that diagnosis was wrong - the cause is this project's own process.h shadowing the
// CRT header, see servers.cpp and HANDOFF section 9 - and the define is out for
// consistency, not because it was the bug.)
#include <Windows.h>
#include <winhttp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "memory.h"      // mem::roblox_h, for "has the client actually gone"
#include "game.h"        // game::ReadDatamodel
#include "offsets.h"     // Offsets::DataModel::*
#include "globals.h"     // g_base_address

#pragma comment(lib, "winhttp.lib")

namespace features {
namespace servershttp {
namespace {

constexpr size_t kMaxBody = 4u << 20;   // 4 MB: a server list is ~40 KB, so this is
                                        // a runaway-response guard, not a limit
struct Handle
{
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET in) : h(in) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring Wide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string LastError(const char* what)
{
    return std::string(what) + " failed (win32 error " + std::to_string(GetLastError()) + ")";
}

// First line of `s`, cut short: the point is to show which complaint came back, not
// to dump a page of HTML into a status line.
std::string FirstLine(const std::string& s, size_t cap = 160)
{
    size_t n = s.find_first_of("\r\n");
    std::string out = (n == std::string::npos) ? s : s.substr(0, n);
    const size_t first = out.find_first_not_of(" \t");
    if (first != std::string::npos) out = out.substr(first);
    if (out.size() > cap) out.resize(cap);
    return out;
}

// The UA every request presents unless the caller says otherwise. A browser
// string, because the hosts we do not control (Cloudflare fronts, reader
// proxies) score a plain "libwww" style client as a bot on sight.
constexpr const char* kBrowserUa =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

// Decodes WINHTTP_OPTION_SECURITY_FLAGS off a failed request, so a 12175 stops
// being a number and starts being a direction: certificate problems are ours
// (intercepting AV, corporate proxy, missing root), a cut channel is the
// server's (bot filter, IP block, or no TLS version in common). The flag
// values are the ones winhttp.h has carried since forever; spelled out here
// because the diagnostic only exists to be read.
std::string SecureChannelDetail(HINTERNET req)
{
#ifndef WINHTTP_OPTION_SECURITY_FLAGS
#define WINHTTP_OPTION_SECURITY_FLAGS 31
#endif
    DWORD flags = 0, size = sizeof(flags);
    if (!WinHttpQueryOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, &size) || !flags)
        return " (no secure-channel detail reported)";
    struct Bit { DWORD bit; const char* what; };
    static const Bit bits[] = {
        { 0x00000001, "revocation check unavailable" },
        { 0x00000002, "invalid certificate" },
        { 0x00000004, "certificate revoked" },
        { 0x00000008, "untrusted certificate chain - something on THIS machine "
                       "re-signed the TLS (AV / corporate proxy) or the root store "
                       "is broken" },
        { 0x00000010, "certificate name mismatch" },
        { 0x00000020, "certificate expired" },
        { 0x00000040, "certificate wrong usage" },
        { 0x00000080, "the channel was cut during the handshake - the REMOTE side "
                       "dropped the hello (bot filtering / IP block), or no TLS "
                       "version in common" },
    };
    std::string out = " (secure-channel:";
    bool first = true;
    for (const Bit& b : bits) {
        if (!(flags & b.bit)) continue;
        out += first ? " " : " | ";
        out += b.what;
        first = false;
    }
    if (first) {
        char hex[16];
        snprintf(hex, sizeof(hex), "0x%lx", (unsigned long)flags);
        out += std::string(" ") + hex;
    }
    out += ")";
    return out;
}

} // namespace

bool Get(const char* url_s, std::string& body, std::string& err, const char* ua)
{
    body.clear();
    err.clear();

    const std::string url = url_s ? url_s : "";
    // Only ever an API call over TLS. Not paranoia about the URL we built ourselves:
    // it is what keeps a future caller from sending a place id into the clear.
    if (url.compare(0, 8, "https://") != 0) { err = "refusing a non-https URL"; return false; }

    const std::wstring wurl = Wide(url);

    Handle session(WinHttpOpen(Wide(ua && ua[0] ? ua : kBrowserUa).c_str(),
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) { err = LastError("WinHttpOpen"); return false; }

    // Force TLS 1.2 + 1.3.  Without this, WinHTTP on some Windows builds
    // defaults to TLS 1.0 only, which modern servers reject (error 12175).
    // A build that does not know the 1.3 flag rejects the WHOLE call, which
    // would silently leave the session on defaults - so retry with 1.2 alone.
    {
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                              &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                             &protocols, sizeof(protocols));
        }
    }

    // No cookie jar (there is nothing to send and nothing to keep) and no following
    // of redirects, so a 302 shows up as a 302 instead of a body from somewhere else.
    // Timeouts because this runs on a worker thread that the menu waits on indirectly:
    // a hung request must end, not age.
    DWORD feature = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(session.h, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
    // By value into a local: WinHttpSetOption takes LPVOID, so &const is not it.
    auto set_ms = [&session](DWORD opt, DWORD ms) {
        WinHttpSetOption(session.h, opt, &ms, sizeof(ms));
    };
    set_ms(WINHTTP_OPTION_RESOLVE_TIMEOUT, 8000);
    set_ms(WINHTTP_OPTION_CONNECT_TIMEOUT, 8000);
    set_ms(WINHTTP_OPTION_SEND_TIMEOUT, 15000);
    set_ms(WINHTTP_OPTION_RECEIVE_TIMEOUT, 15000);

    // dwXxxLength = (DWORD)-1 asks CrackUrl to *point into* `wurl` rather than
    // copy. Leaving those at 0 (the zero-init) is the other mode: it only fills
    // the lengths and leaves the pointers null, which is exactly the "no host
    // or no path" failure this used to report for every well-formed URL.
    URL_COMPONENTSW comp{};
    comp.dwStructSize      = sizeof(comp);
    comp.dwSchemeLength    = (DWORD)-1;
    comp.dwHostNameLength  = (DWORD)-1;
    comp.dwUrlPathLength   = (DWORD)-1;
    comp.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comp)) {
        err = "could not split the URL (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (!comp.lpszHostName || comp.dwHostNameLength == 0 ||
        !comp.lpszUrlPath  || comp.dwUrlPathLength  == 0) {
        err = "the URL had no host or no path";
        return false;
    }
    // Copies, not views: the pointers land inside `wurl` and are not
    // null-terminated at the component edge. OpenRequest also wants the query
    // string (`?placeIds=` / `?limit=100`), which CrackUrl splits off as extra.
    const std::wstring host(comp.lpszHostName, comp.dwHostNameLength);
    std::wstring path(comp.lpszUrlPath, comp.dwUrlPathLength);
    if (comp.lpszExtraInfo && comp.dwExtraInfoLength)
        path.append(comp.lpszExtraInfo, comp.dwExtraInfoLength);

    Handle conn(WinHttpConnect(session.h, host.c_str(), comp.nPort, 0));
    if (!conn.h) { err = LastError("WinHttpConnect"); return false; }

    // No version argument in this prototype: (connect, verb, object, referer,
    // context, cert-usage, security flags). Getting that off by one hands a NULL
    // macro to a DWORD, which is a compile error rather than a mystery - the stub in
    // glass-obsidian/tools/win32_stub/winhttp.h is what caught it here.
    Handle req(WinHttpOpenRequest(conn.h, L"GET", path.c_str(), WINHTTP_NO_REFERER, 0,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req.h) { err = LastError("WinHttpOpenRequest"); return false; }

    const wchar_t* headers = L"Accept: application/json, text/plain, */*\r\n"
                             L"Accept-Language: en-US,en;q=0.9\r\n"
                             L"Accept-Encoding: identity\r\n";
    if (!WinHttpSendRequest(req.h, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        // 12175 is where Cloudflare's TLS-level blocking lands. The code is
        // captured BEFORE the string is built - GetLastError does not survive
        // the formatting calls, and the detail below is the whole point.
        const DWORD e = GetLastError();
        err = "WinHttpSendRequest failed (win32 error " + std::to_string(e) + ")";
        if (e == 12175) err += SecureChannelDetail(req.h);
        return false;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) {
        const DWORD e = GetLastError();
        err = "WinHttpReceiveResponse failed (win32 error " + std::to_string(e) + ")";
        if (e == 12175) err += SecureChannelDetail(req.h);
        return false;
    }

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(req.h,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        err = LastError("WinHttpQueryHeaders");
        return false;
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) break;      // treat as EOF
        if (avail == 0) break;
        if (body.size() + avail > kMaxBody) { err = "the reply was bigger than 4 MB"; return false; }
        std::string chunk(avail, '\0');
        DWORD got = 0;
        if (!WinHttpReadData(req.h, &chunk[0], avail, &got)) { err = LastError("WinHttpReadData"); return false; }
        chunk.resize(got);
        body += chunk;
        if (got == 0) break;
    }

    if (status < 200 || status > 299) {
        // host is wide (it is what the API wanted); the message is narrow, so take the
        // authority out of the URL we already have rather than mixing the two.
        std::string who = url.substr(8);
        const size_t slash = who.find('/');
        if (slash != std::string::npos) who = who.substr(0, slash);
        err = who + " answered " + std::to_string(status);
        const std::string why = FirstLine(body);
        if (!why.empty()) err += " - " + why;
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
//  The curl.exe transport - why it exists:
//  offsets.imtheo.lol sits behind Cloudflare bot protection that drops
//  WinHTTP's TLS ClientHello outright (WinHttpSendRequest fails with win32
//  12175 before any HTTP happens). Windows ships curl.exe in System32 since
//  10 1803; it also uses Schannel but assembles a different ClientHello, and
//  the same URL that WinHTTP cannot handshake comes back fine through it.
// -----------------------------------------------------------------------------
bool GetViaCurl(const char* url_s, std::string& body, std::string& err, const char* ua)
{
    body.clear(); err.clear();
    const std::string url = url_s ? url_s : "";
    if (url.compare(0, 8, "https://") != 0) { err = "refusing a non-https URL"; return false; }

    // The curl binary is the platform layer's to find (System32 on Windows,
    // PATH on a posix port). This transport itself - CreateProcessW + pipes -
    // is the one piece of servers_http a Linux port rewrites (fork/exec), and
    // it is isolated in this one function on purpose.
    const std::string curl = platform::CurlPath();
    if (curl.empty()) {
        err = "no curl binary for the fallback transport";
        return false;
    }
    const std::wstring exe = Wide(curl);

    // -s no progress meter, -S but print errors, -f HTTP >= 400 is a failure
    // and not a "body", -L follow the reader proxies' redirects, --compressed
    // send Accept-Encoding and decode, --max-time hard stop. A quote inside a
    // UA would break out of the -A "..." argument, so anything that odd falls
    // back to the browser UA.
    const char* curl_ua = (ua && ua[0] && !strchr(ua, '"')) ? ua : kBrowserUa;
    std::wstring cmd = L"\"" + exe + L"\" -sS -f -L --compressed --max-time 20"
        L" -A \"" + Wide(curl_ua) + L"\""
        L" \"" + Wide(url) + L"\"";

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
        err = LastError("CreatePipe");
        if (out_r) CloseHandle(out_r);  if (out_w) CloseHandle(out_w);
        if (err_r) CloseHandle(err_r);  if (err_w) CloseHandle(err_w);
        return false;
    }
    // The READ ends must not be inherited: the child would hold them open and
    // the pipes would never look closed from this side.
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    // curl reads nothing for our argument set, but a NULL stdin under
    // STARTF_USESTDHANDLES is the one shape some CRT startups choke on.
    si.hStdInput  = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    si.hStdOutput = out_w;
    si.hStdError  = err_w;

    PROCESS_INFORMATION pi = {};
    // &cmd[0], not cmd.c_str(): CreateProcess wants a WRITABLE command line.
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        err = LastError("CreateProcessW (curl.exe)");
        CloseHandle(out_r); CloseHandle(err_r);
        if (si.hStdInput && si.hStdInput != INVALID_HANDLE_VALUE) CloseHandle(si.hStdInput);
        return false;
    }
    CloseHandle(out_w); CloseHandle(err_w);      // the child holds its own copies now

    // Drain stdout until the pipe is empty AND curl is gone. --max-time bounds
    // the child; the 30s deadline is the belt to that braces (a wedged curl
    // gets killed, not waited on). Buffered bytes can outlive the writer, so
    // the order is peek -> read -> check-exit, never exit-first.
    const uint64_t deadline = platform::NowMs() + 30000;
    bool overflow = false;
    char buf[8192];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(out_r, nullptr, 0, nullptr, &avail, nullptr)) break;   // pipe torn down
        if (avail > 0) {
            const DWORD chunk = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
            DWORD got = 0;
            if (!ReadFile(out_r, buf, chunk, &got, nullptr) || got == 0) break;
            if (body.size() + got > kMaxBody) { overflow = true; break; }
            body.append(buf, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;          // exited, drained
        if (platform::NowMs() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            err = "curl.exe did not finish in 30s (killed)";
            break;
        }
        platform::SleepMs(15);
    }

    // It has had every chance to exit; ask for the code, then drain stderr -
    // after exit that can no longer block. curl error lines are small.
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 0;
    const bool have_code = GetExitCodeProcess(pi.hProcess, &code) != 0;

    std::string why;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(err_r, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
        const DWORD chunk = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
        DWORD got = 0;
        if (!ReadFile(err_r, buf, chunk, &got, nullptr) || got == 0) break;
        why.append(buf, got);
        if (why.size() > 1024) break;
    }

    CloseHandle(out_r); CloseHandle(err_r);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (si.hStdInput && si.hStdInput != INVALID_HANDLE_VALUE) CloseHandle(si.hStdInput);

    if (overflow) { err = "the reply was bigger than 4 MB"; return false; }
    if (!err.empty()) return false;               // the timeout-kill path said why
    if (have_code && code != 0) {
        err = "curl.exe exited with code " + std::to_string(code);
        const std::string line = FirstLine(why);
        if (!line.empty()) err += " - " + line;
        return false;
    }
    if (body.empty()) { err = "curl.exe returned nothing"; return false; }
    return true;
}

bool GetAny(const char* url, std::string& body, std::string& err, const char* ua)
{
    if (Get(url, body, err, ua)) return true;
    std::string curl_err;
    if (GetViaCurl(url, body, curl_err, ua)) return true;
    err += "; curl: " + curl_err;
    return false;
}

bool Open(const char* url_s, std::string& err)
{
    // The shell hand-off (ShellExecuteW and its error-code zoo) moved into the
    // platform layer; same messages, one place, portable signature.
    return platform::OpenUrl(url_s, err);
}


// Attempts an in-instance hop first: sends the deeplink to the running client
// and watches memory for a JobId change. If the client handles it (newer Roblox
// versions do), the hop happens without closing anything. Falls back to the
// close-and-reopen flow after `fallback_ms` if nothing changes.
bool TryInInstanceHop(const char* url, unsigned fallback_ms, std::string& err)
{
    err.clear();

    // Snapshot the current JobId so we can detect a change
    uint64_t old_place = 0;
    std::string old_job;
    if (g_base_address != 0) {
        const instance dm = game::ReadDatamodel(g_base_address);
        if (dm.is_valid()) {
            old_place = read<uint64_t>(dm.address + Offsets::DataModel::PlaceId);
            old_job = fetchstring(dm.address + Offsets::DataModel::JobId);
        }
    }

    // Send the deeplink without closing the client
    std::string open_err;
    if (!Open(url, open_err)) {
        err = open_err;
        return false;
    }

    // Watch for the JobId to change (means the client handled it in-instance)
    const uint64_t until = platform::NowMs() + (fallback_ms ? fallback_ms : 5000u);
    while (platform::NowMs() < until) {
        platform::SleepMs(100);
        if (g_base_address == 0) continue;
        
        const instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) continue;
        
        const std::string new_job = fetchstring(dm.address + Offsets::DataModel::JobId);
        if (!new_job.empty() && new_job != old_job) {
            // JobId changed — in-instance hop succeeded!
            return true;
        }
    }

    // In-instance hop didn't work — fall back to close-and-reopen
    return false;
}

} // namespace servershttp
} // namespace features
