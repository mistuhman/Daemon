#pragma once
// -----------------------------------------------------------------------------
//  features/servers/servers_http.h  --  the one place server hopping touches
//  the network, so the rest of the module stays testable and stub-parseable
// -----------------------------------------------------------------------------
//  Both endpoints are public and unauthenticated (games.roblox.com's place details
//  and server list), so this sends a GET with no cookie, no token and nothing that
//  identifies the account.
// -----------------------------------------------------------------------------
#include <string>

namespace features {
namespace servershttp {

// GETs an https URL, `body` receives the reply. True on 2xx only. `err` carries the
// status code and the first line of the error body when there was one, because
// "403" and "the TLS handshake failed" and "that place id does not exist" need
// different fixes and all three look like an empty list if you do not say them.
// On a secure-channel failure (win32 12175) the message also carries the decoded
// WINHTTP_OPTION_SECURITY_FLAGS: "untrusted certificate chain" points at a local
// intercept (AV / corporate proxy / root store), "the channel was cut" points at
// the server dropping the hello (bot filtering, IP block).
// `ua` overrides the browser User-Agent WinHTTP presents; the offset service's
// own docs (offsets.imtheo.lol/docs/cpp-auto-update) call it with
// "OffsetsClient/1.0", so that is what the imtheo requests send.
bool Get(const char* url, std::string& body, std::string& err, const char* ua = nullptr);

// Same contract as Get, but through %SystemRoot%\System32\curl.exe (ships with
// Windows 10 1803+) with the reply read from a pipe. Exists because offsets.imtheo.lol
// is behind Cloudflare bot protection that drops WinHTTP's TLS ClientHello
// (win32 error 12175 on WinHttpSendRequest) while curl's different hello passes.
// `ua` overrides curl's browser User-Agent the same way.
bool GetViaCurl(const char* url, std::string& body, std::string& err, const char* ua = nullptr);

// Get() first, GetViaCurl() if that failed; `err` ends up carrying both
// complaints. For callers that talk to hosts we do not control (the offset
// sources) - the Roblox API endpoints stay on plain Get().
bool GetAny(const char* url, std::string& body, std::string& err, const char* ua = nullptr);

// Hands a URL to the shell - roblox:// goes to the installed client, not a
// browser. False plus a reason when nothing is registered for that protocol.
bool Open(const char* url, std::string& err);

// There deliberately is no "open the link and watch memory for the JobId to change"
// helper here any more (it was TryInInstanceHop). The wait belonged to the feature loop,
// which reads the same chain once a second anyway: as a helper it put a second thread on
// the datamodel, and it held servers::g_busy - and so the card's FETCHING state and every
// other request - open for its whole timeout. See RunLaunch / HopWatch in servers.cpp.

} // namespace servershttp
} // namespace features
