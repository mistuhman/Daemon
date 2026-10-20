// ----------------------------------------------------------------------------
//  platform/servers_http_linux.cpp — Linux HTTP (curl via platform::CurlPath)
// ----------------------------------------------------------------------------
//  Windows version uses WinHTTP + System32\curl.exe fallback. Linux uses
//  platform::CurlPath() (which is "curl" on PATH) via popen. Same contract:
//  Get() returns true on 2xx, body holds reply, err holds status/first line.
// ----------------------------------------------------------------------------
#ifdef __linux__

#include "../features/servers/servers_http.h"
#include "../platform/platform.h"

#include <cstdio>
#include <array>
#include <string>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace features {
namespace servershttp {

static bool CurlGet(const char* url, std::string& body, std::string& err, const char* ua) {
    body.clear(); err.clear();
    if (!url || !url[0]) { err = "empty url"; return false; }
    std::string curl = platform::CurlPath();
    if (curl.empty()) curl = "curl";
    // Build command: curl -sL -w "\n%{http_code}" <url> [-A ua]
    std::string cmd = curl + " -sL -w \"\\n%{http_code}\" ";
    if (ua && ua[0]) {
        cmd += "-A '";
        cmd += ua;
        cmd += "' ";
    }
    cmd += "'";
    cmd += url;
    cmd += "' 2>&1";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) { err = "popen curl failed"; return false; }
    std::string out;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), (int)buf.size(), f)) out += buf.data();
    int rc = pclose(f);
    (void)rc;
    if (out.empty()) { err = "empty reply from curl"; return false; }
    // Last line is http code
    size_t nl = out.find_last_of('\n');
    if (nl == std::string::npos) { err = "no http code in reply"; body = out; return false; }
    std::string codeStr = out.substr(nl+1);
    // Trim
    while (!codeStr.empty() && (codeStr.back()=='\n' || codeStr.back()=='\r' || codeStr.back()==' ')) codeStr.pop_back();
    while (!codeStr.empty() && (codeStr.front()==' ')) codeStr.erase(codeStr.begin());
    int code = 0;
    try { code = std::stoi(codeStr); } catch(...) { code = 0; }
    body = out.substr(0, nl);
    if (code >= 200 && code < 300) return true;
    err = "http " + codeStr;
    if (!body.empty()) {
        size_t eol = body.find('\n');
        std::string first = (eol==std::string::npos) ? body : body.substr(0, eol);
        if (first.size() > 200) first = first.substr(0,200);
        err += " " + first;
    }
    return false;
}

bool Get(const char* url, std::string& body, std::string& err, const char* ua) {
    return CurlGet(url, body, err, ua);
}

bool GetViaCurl(const char* url, std::string& body, std::string& err, const char* ua) {
    return CurlGet(url, body, err, ua);
}

bool GetAny(const char* url, std::string& body, std::string& err, const char* ua) {
    // On Linux Get and GetViaCurl are same transport; try once.
    return CurlGet(url, body, err, ua);
}

bool Open(const char* url, std::string& err) {
    return platform::OpenUrl(url, err);
}

} // namespace servershttp
} // namespace features

#endif
