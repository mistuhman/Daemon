// ─────────────────────────────────────────────────────────────────────────────
//  media_tracker.cpp — now-playing detection + per-app volume + media controls
//
//  Detection: SMTC (Windows System Media Transport Controls) via raw COM
//             Falls back to EnumWindows + UI Automation if SMTC unavailable
// ─────────────────────────────────────────────────────────────────────────────

#include "media_tracker.h"
#include "globals.h"
#include "smtc_raw.h"
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <UIAutomation.h>
#include <cstring>
#include <mutex>
#include <string>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "runtimeobject.lib")

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

// Define the SMTC interface GUIDs
const IID IID_IGlobalSystemMediaTransportControlsSessionManagerStatics = 
    { 0x7137a65f, 0xa5a2, 0x4b6f, { 0x8c, 0x8f, 0x9d, 0x1f, 0x5f, 0x8e, 0x8e, 0x8f } };

const IID IID_IGlobalSystemMediaTransportControlsSessionManager = 
    { 0x2050c444, 0x3b2d, 0x5d5f, { 0x9b, 0x3e, 0x8f, 0x4e, 0x6e, 0x7f, 0x8d, 0x9e } };

const IID IID_IGlobalSystemMediaTransportControlsSession = 
    { 0xd1c5a3b2, 0x4e6f, 0x5a7b, { 0x8c, 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d } };

const IID IID_IGlobalSystemMediaTransportControlsSessionPlaybackInfo = 
    { 0xa1b2c3d4, 0xe5f6, 0x7890, { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90 } };

const IID IID_IGlobalSystemMediaTransportControlsSessionMediaProperties = 
    { 0xb2c3d4e5, 0xf6a7, 0x8901, { 0xbc, 0xde, 0xf1, 0x23, 0x45, 0x67, 0x89, 0x01 } };

// Forward declarations for SMTC interfaces (we'll use IInspectable and query)
namespace ABI {
    namespace Windows {
        namespace Media {
            namespace Control {
                struct IGlobalSystemMediaTransportControlsSessionManager;
                struct IGlobalSystemMediaTransportControlsSession;
                struct IGlobalSystemMediaTransportControlsSessionMediaProperties;
                struct IGlobalSystemMediaTransportControlsSessionPlaybackInfo;
            }
        }
    }
}

namespace media {

static std::mutex s_mutex;
static NowPlaying s_current{};
static DWORD s_last_tick = 0;
static HWND s_media_hwnd = nullptr;
static DWORD s_media_pid = 0;

// ── Known Monochrome domains ────────────────────────────────────────────────
static const char* kMonochromeDomains[] = {
    "monochrome.tf",
    "monochrome.samidy.com",
    "lossless.wtf",
    "if-it-runs-ship-it.lol",
    nullptr
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static void SendKeyPress(WORD vk) {
    keybd_event((BYTE)vk, 0, 0, 0);
    keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
}

static bool IsMonochromeDomain(const char* url) {
    for (int i = 0; kMonochromeDomains[i]; i++) {
        if (strstr(url, kMonochromeDomains[i]))
            return true;
    }
    return false;
}

// Wide string to UTF-8
static std::string WtoU8(const wchar_t* w) {
    if (!w || !*w) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// Strip characters outside basic Latin (ASCII + Latin-1) that the default font can't render
static void SanitizeForDisplay(char* str, size_t max_len) {
    if (!str || !*str) return;
    
    std::string result;
    result.reserve(max_len);
    
    const char* p = str;
    while (*p && result.size() < max_len - 1) {
        unsigned char c = (unsigned char)*p;
        
        if (c >= 0x20 && c <= 0x7E) {
            result += (char)c;
            p++;
        }
        else if ((c & 0xE0) == 0xC0 && p[1]) {
            unsigned char c2 = (unsigned char)p[1];
            if ((c2 & 0xC0) == 0x80) {
                uint32_t codepoint = ((c & 0x1F) << 6) | (c2 & 0x3F);
                if (codepoint >= 0x00A0 && codepoint <= 0x00FF) {
                    result += *p;
                    result += p[1];
                } else {
                    result += '?';
                }
                p += 2;
            } else {
                result += '?';
                p++;
            }
        }
        else if ((c & 0xF0) == 0xE0 && p[1] && p[2]) {
            result += '?';
            p += 3;
        }
        else if ((c & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            result += '?';
            p += 4;
        }
        else {
            result += '?';
            p++;
        }
    }
    
    strncpy_s(str, max_len, result.c_str(), _TRUNCATE);
}

// ── SMTC detection via raw COM ───────────────────────────────────────────────

static bool ReadMediaSMTC(NowPlaying& out) {
    IGlobalSystemMediaTransportControlsSessionManagerStatics* factory = nullptr;
    IAsyncOperation_SessionManager* asyncOp = nullptr;
    IGlobalSystemMediaTransportControlsSessionManager* manager = nullptr;
    IGlobalSystemMediaTransportControlsSession* session = nullptr;
    IGlobalSystemMediaTransportControlsSessionPlaybackInfo* playbackInfo = nullptr;
    IAsyncOperation_MediaProperties* propsAsync = nullptr;
    IGlobalSystemMediaTransportControlsSessionMediaProperties* props = nullptr;
    HSTRING title = nullptr;
    HSTRING artist = nullptr;
    HSTRING appId = nullptr;
    
    bool success = false;
    
    // Initialize COM for this thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;
    
    // Get the factory for SMTC session manager
    HSTRING className;
    HSTRING_HEADER header;
    hr = WindowsCreateStringReference(SMTC_MANAGER_CLASS, 
                                      (UINT32)wcslen(SMTC_MANAGER_CLASS), 
                                      &header, &className);
    if (FAILED(hr)) goto cleanup;
    
    hr = RoGetActivationFactory(className,
                                IID_IGlobalSystemMediaTransportControlsSessionManagerStatics,
                                (void**)&factory);
    WindowsDeleteString(className);
    if (FAILED(hr)) goto cleanup;
    
    // Request the session manager (async operation)
    hr = factory->RequestAsync((void**)&asyncOp);
    if (FAILED(hr)) goto cleanup;
    
    // Wait for async operation to complete
    hr = WaitForAsync(asyncOp, &manager, 3000);
    if (FAILED(hr) || !manager) goto cleanup;
    
    // Get current session
    hr = manager->GetCurrentSession(&session);
    if (FAILED(hr) || !session) goto cleanup;
    
    // Get playback info
    hr = session->GetPlaybackInfo(&playbackInfo);
    if (FAILED(hr) || !playbackInfo) goto cleanup;
    
    // Check playback status
    PlaybackStatus status;
    hr = playbackInfo->get_PlaybackStatus(&status);
    if (FAILED(hr)) goto cleanup;
    
    if (status != PlaybackStatus_Playing && status != PlaybackStatus_Paused) {
        goto cleanup;  // Not playing or paused
    }
    
    // Get media properties (async)
    hr = session->TryGetMediaPropertiesAsync((void**)&propsAsync);
    if (FAILED(hr)) goto cleanup;
    
    hr = WaitForAsync(propsAsync, &props, 3000);
    if (FAILED(hr) || !props) goto cleanup;
    
    // Extract title
    hr = props->get_Title(&title);
    if (SUCCEEDED(hr) && title) {
        const wchar_t* titleStr = WindowsGetStringRawBuffer(title, nullptr);
        if (titleStr) {
            std::string titleUtf8 = WtoU8(titleStr);
            strncpy_s(out.track, titleUtf8.c_str(), _TRUNCATE);
        }
    }
    
    // Extract artist
    hr = props->get_Artist(&artist);
    if (SUCCEEDED(hr) && artist) {
        const wchar_t* artistStr = WindowsGetStringRawBuffer(artist, nullptr);
        if (artistStr) {
            std::string artistUtf8 = WtoU8(artistStr);
            strncpy_s(out.artist, artistUtf8.c_str(), _TRUNCATE);
        }
    }
    
    // Extract source from app user model ID
    hr = session->get_SourceAppUserModelId(&appId);
    if (SUCCEEDED(hr) && appId) {
        const wchar_t* appIdStr = WindowsGetStringRawBuffer(appId, nullptr);
        if (appIdStr) {
            std::string appIdUtf8 = WtoU8(appIdStr);
            
            // Identify source from app ID
            if (appIdUtf8.find("Spotify") != std::string::npos) {
                strcpy_s(out.source, "spotify");
            } else if (appIdUtf8.find("YouTube") != std::string::npos ||
                       appIdUtf8.find("Chrome") != std::string::npos ||
                       appIdUtf8.find("Firefox") != std::string::npos ||
                       appIdUtf8.find("Edge") != std::string::npos ||
                       appIdUtf8.find("Monochrome") != std::string::npos) {
                // Browser - try to identify specific service from title
                if (strstr(out.track, "YouTube") || strstr(out.artist, "YouTube")) {
                    strcpy_s(out.source, "youtube");
                } else if (strstr(out.track, "Monochrome") || strstr(out.artist, "Monochrome")) {
                    strcpy_s(out.source, "monochrome");
                } else {
                    strcpy_s(out.source, "browser");
                }
            } else {
                strcpy_s(out.source, "unknown");
            }
        }
    }
    
    out.playing = (status == PlaybackStatus_Playing);
    success = true;
    
cleanup:
    if (appId) WindowsDeleteString(appId);
    if (artist) WindowsDeleteString(artist);
    if (title) WindowsDeleteString(title);
    if (props) props->Release();
    if (propsAsync) propsAsync->Release();
    if (playbackInfo) playbackInfo->Release();
    if (session) session->Release();
    if (manager) manager->Release();
    if (asyncOp) asyncOp->Release();
    if (factory) factory->Release();
    
    return success;
}

// ── Read browser address bar via UI Automation ──────────────────────────────

static std::string GetBrowserUrl(HWND hwnd) {
    std::string url;

    IUIAutomation* pAutomation = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                 __uuidof(IUIAutomation), (void**)&pAutomation)))
        return url;

    IUIAutomationElement* pRoot = nullptr;
    if (FAILED(pAutomation->ElementFromHandle(hwnd, &pRoot))) {
        pAutomation->Release();
        return url;
    }

    IUIAutomationCondition* editCondition = nullptr;
    VARIANT varEdit;
    VariantInit(&varEdit);
    varEdit.vt = VT_I4;
    varEdit.lVal = UIA_EditControlTypeId;
    pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, varEdit, &editCondition);
    VariantClear(&varEdit);

    if (editCondition) {
        IUIAutomationElementArray* elements = nullptr;
        pRoot->FindAll(TreeScope_Descendants, editCondition, &elements);

        if (elements) {
            int count = 0;
            elements->get_Length(&count);

            for (int i = 0; i < count && url.empty(); i++) {
                IUIAutomationElement* elem = nullptr;
                elements->GetElement(i, &elem);
                if (!elem) continue;

                VARIANT val;
                VariantInit(&val);
                if (SUCCEEDED(elem->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &val))) {
                    if (val.vt == VT_BSTR && val.bstrVal) {
                        std::string buf = WtoU8(val.bstrVal);
                        if (buf.find("http://") != std::string::npos || 
                            buf.find("https://") != std::string::npos ||
                            IsMonochromeDomain(buf.c_str())) {
                            url = buf;
                        }
                    }
                    VariantClear(&val);
                }

                if (url.empty()) {
                    BSTR name = nullptr;
                    if (SUCCEEDED(elem->get_CurrentName(&name)) && name) {
                        std::string buf = WtoU8(name);
                        if (IsMonochromeDomain(buf.c_str())) {
                            url = buf;
                        }
                        SysFreeString(name);
                    }
                }

                elem->Release();
            }
            elements->Release();
        }
        editCondition->Release();
    }

    pRoot->Release();
    pAutomation->Release();
    return url;
}

// ── Per-app volume control via WASAPI ────────────────────────────────────────

static bool AdjustAppVolume(DWORD pid, float delta) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioSessionManager2* session_mgr = nullptr;
    IAudioSessionEnumerator* session_enum = nullptr;
    bool success = false;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                 __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
        return false;

    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        enumerator->Release();
        return false;
    }

    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                 (void**)&session_mgr))) {
        device->Release();
        enumerator->Release();
        return false;
    }

    if (FAILED(session_mgr->GetSessionEnumerator(&session_enum))) {
        session_mgr->Release();
        device->Release();
        enumerator->Release();
        return false;
    }

    int count = 0;
    session_enum->GetCount(&count);

    for (int i = 0; i < count; i++) {
        IAudioSessionControl* session_ctrl = nullptr;
        IAudioSessionControl2* session2 = nullptr;
        ISimpleAudioVolume* volume = nullptr;

        if (FAILED(session_enum->GetSession(i, &session_ctrl))) continue;
        if (FAILED(session_ctrl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&session2))) {
            session_ctrl->Release();
            continue;
        }

        DWORD session_pid = 0;
        if (SUCCEEDED(session2->GetProcessId(&session_pid)) && session_pid == pid) {
            if (SUCCEEDED(session2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&volume))) {
                float current = 0.0f;
                volume->GetMasterVolume(&current);
                float new_vol = current + delta;
                if (new_vol < 0.0f) new_vol = 0.0f;
                if (new_vol > 1.0f) new_vol = 1.0f;
                volume->SetMasterVolume(new_vol, nullptr);
                volume->Release();
                success = true;
            }
        }

        session2->Release();
        session_ctrl->Release();
        if (success) break;
    }

    session_enum->Release();
    session_mgr->Release();
    device->Release();
    enumerator->Release();
    return success;
}

// ── Window enumeration ──────────────────────────────────────────────────────

struct EnumData {
    char title[512];
    char source[32];
    char url[512];
    HWND hwnd;
    DWORD pid;
    bool found;
};

static bool IsBrowserClass(const char* cls) {
    return strcmp(cls, "Chrome_WidgetWin_1") == 0 ||
           strcmp(cls, "Chrome_WidgetWin_0") == 0 ||
           strcmp(cls, "MozillaWindowClass") == 0 ||
           strcmp(cls, "Windows.UI.Core.CoreWindow") == 0;
}

static BOOL CALLBACK EnumMediaWnd(HWND hwnd, LPARAM lParam) {
    EnumData* d = (EnumData*)lParam;
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t wtitle[512] = {};
    int len = GetWindowTextW(hwnd, wtitle, 511);
    if (len <= 0) return TRUE;
    
    std::string title = WtoU8(wtitle);
    if (title.empty()) return TRUE;

    char cls[128] = {};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);

    // Spotify
    if ((strcmp(cls, "Chrome_WidgetWin_0") == 0 ||
         strcmp(cls, "SpotifyMainWindow") == 0 ||
         strcmp(cls, "SDL_app") == 0) &&
        strstr(title.c_str(), " - ") &&
        !strstr(title.c_str(), "Spotify Free") &&
        !strstr(title.c_str(), "Spotify Premium") &&
        !strstr(title.c_str(), "Advertisement")) {
        strncpy_s(d->title, title.c_str(), _TRUNCATE);
        strncpy_s(d->source, "spotify", _TRUNCATE);
        d->hwnd = hwnd;
        GetWindowThreadProcessId(hwnd, &d->pid);
        d->found = true;
        return FALSE;
    }

    // Browser windows
    if (IsBrowserClass(cls)) {
        // YouTube Music
        if (strstr(title.c_str(), "YouTube Music")) {
            strncpy_s(d->title, title.c_str(), _TRUNCATE);
            strncpy_s(d->source, "youtube", _TRUNCATE);
            d->hwnd = hwnd;
            GetWindowThreadProcessId(hwnd, &d->pid);
            d->found = true;
            return FALSE;
        }

        // Regular YouTube
        if (strstr(title.c_str(), " - YouTube") && !strstr(title.c_str(), "YouTube Music")) {
            strncpy_s(d->title, title.c_str(), _TRUNCATE);
            strncpy_s(d->source, "youtube", _TRUNCATE);
            d->hwnd = hwnd;
            GetWindowThreadProcessId(hwnd, &d->pid);
            d->found = true;
            return FALSE;
        }

        // Monochrome by title (fallback)
        if (strstr(title.c_str(), "Monochrome") || strstr(title.c_str(), "monochrome")) {
            strncpy_s(d->title, title.c_str(), _TRUNCATE);
            strncpy_s(d->source, "monochrome", _TRUNCATE);
            d->hwnd = hwnd;
            GetWindowThreadProcessId(hwnd, &d->pid);
            d->found = true;
            return FALSE;
        }

        // Monochrome by URL
        std::string url = GetBrowserUrl(hwnd);
        if (!url.empty() && IsMonochromeDomain(url.c_str())) {
            strncpy_s(d->title, title.c_str(), _TRUNCATE);
            strncpy_s(d->url, url.c_str(), _TRUNCATE);
            strncpy_s(d->source, "monochrome", _TRUNCATE);
            d->hwnd = hwnd;
            GetWindowThreadProcessId(hwnd, &d->pid);
            d->found = true;
            return FALSE;
        }
    }

    return TRUE;
}

static bool ReadMediaWindow(NowPlaying& out) {
    EnumData d{};
    d.found = false;
    d.hwnd = nullptr;
    d.pid = 0;
    d.url[0] = '\0';
    EnumWindows(EnumMediaWnd, (LPARAM)&d);

    if (!d.found) return false;

    s_media_hwnd = d.hwnd;
    s_media_pid = d.pid;

    const char* title = d.title;
    const char* source = d.source;

    // Strip browser suffix
    std::string clean(title);
    
    // Normalize em-dash and en-dash
    {
        std::string em = "\xe2\x80\x94";
        std::string en = "\xe2\x80\x93";
        std::string rep = " - ";
        size_t pos;
        while ((pos = clean.find(em)) != std::string::npos) clean.replace(pos, em.size(), rep);
        while ((pos = clean.find(en)) != std::string::npos) clean.replace(pos, en.size(), rep);
    }
    
    const char* suffixes[] = {
        " - Google Chrome", " - Microsoft Edge",
        " - Mozilla Firefox", " - Firefox",
        " - LibreWolf", " - Brave", " - Opera", " - Vivaldi", " - Arc",
        " - Safari", " - Chromium", " - Waterfox", " - Floorp",
        " - Zen Browser", " - Zen", " - Mullvad Browser",
        " - Tor Browser", " - Pale Moon", " - Firefox Developer Edition",
        " - Firefox Nightly", " - Chrome", " - Edge",
        nullptr
    };

    bool stripped = false;
    for (int i = 0; suffixes[i]; i++) {
        size_t pos = clean.rfind(suffixes[i]);
        if (pos != std::string::npos && pos + strlen(suffixes[i]) == clean.size()) {
            clean = clean.substr(0, pos);
            stripped = true;
            break;
        }
    }

    if (!stripped) {
        size_t last_dash = clean.rfind(" - ");
        if (last_dash != std::string::npos && last_dash + 3 < clean.size()) {
            std::string after = clean.substr(last_dash + 3);
            if (after.length() < 40) {
                bool has_digit = false;
                int spaces = 0;
                for (char c : after) {
                    if (c >= '0' && c <= '9') has_digit = true;
                    if (c == ' ') spaces++;
                }
                if (!has_digit && spaces <= 2) {
                    clean = clean.substr(0, last_dash);
                    stripped = true;
                }
            }
        }
    }

    // YouTube Music
    if (strcmp(source, "youtube") == 0 && strstr(title, "YouTube Music")) {
        size_t ytm = clean.rfind(" - YouTube Music");
        if (ytm != std::string::npos) clean = clean.substr(0, ytm);
        while (clean.size() > 2 && (unsigned char)clean[0] >= 0xE0) clean = clean.substr(3);
        while (!clean.empty() && clean[0] == ' ') clean = clean.substr(1);

        const char* dash = strstr(clean.c_str(), " - ");
        if (dash) {
            strncpy_s(out.track, std::string(clean.c_str(), dash - clean.c_str()).c_str(), _TRUNCATE);
            strncpy_s(out.artist, (dash + 3), _TRUNCATE);
        } else {
            strncpy_s(out.track, clean.c_str(), _TRUNCATE);
        }
        strncpy_s(out.source, "youtube", _TRUNCATE);
        out.playing = true;
        return true;
    }

    // Regular YouTube
    if (strcmp(source, "youtube") == 0) {
        size_t yt = clean.rfind(" - YouTube");
        if (yt != std::string::npos) clean = clean.substr(0, yt);

        const char* dash = strstr(clean.c_str(), " - ");
        if (dash) {
            strncpy_s(out.track, std::string(clean.c_str(), dash - clean.c_str()).c_str(), _TRUNCATE);
            strncpy_s(out.artist, (dash + 3), _TRUNCATE);
        } else {
            strncpy_s(out.track, clean.c_str(), _TRUNCATE);
            strncpy_s(out.artist, "YouTube", _TRUNCATE);
        }
        strncpy_s(out.source, "youtube", _TRUNCATE);
        out.playing = true;
        return true;
    }

    // Monochrome
    if (strcmp(source, "monochrome") == 0) {
        size_t mono = clean.find("Monochrome");
        if (mono == std::string::npos) mono = clean.find("monochrome");
        if (mono != std::string::npos) {
            if (mono > 3 && clean[mono-1] == ' ' && clean[mono-2] == '-' && clean[mono-3] == ' ')
                clean = clean.substr(0, mono - 3);
            else if (mono + 10 < clean.size() && clean[mono+10] == ' ' && clean[mono+11] == '-' && clean[mono+12] == ' ')
                clean = clean.substr(mono + 13);
            else if (mono + 10 < clean.size() && clean[mono+10] == '-' && clean[mono+11] == ' ')
                clean = clean.substr(mono + 12);
            else
                clean = clean.substr(0, mono);
        }

        while (!clean.empty() && (clean.back() == ' ' || clean.back() == '-')) clean.pop_back();
        size_t start = clean.find_first_not_of(" -");
        if (start != std::string::npos) clean = clean.substr(start);
        while (clean.size() > 2 && (unsigned char)clean[0] >= 0xE0) clean = clean.substr(3);
        while (!clean.empty() && clean[0] == ' ') clean = clean.substr(1);

        if (!clean.empty()) {
            const char* dash = strstr(clean.c_str(), " - ");
            if (dash) {
                strncpy_s(out.artist, std::string(clean.c_str(), dash - clean.c_str()).c_str(), _TRUNCATE);
                strncpy_s(out.track, (dash + 3), _TRUNCATE);
            } else {
                strncpy_s(out.track, clean.c_str(), _TRUNCATE);
            }
            strncpy_s(out.source, "monochrome", _TRUNCATE);
            out.playing = true;
            return true;
        }

        strncpy_s(out.source, "monochrome", _TRUNCATE);
        strncpy_s(out.track, "monochrome", _TRUNCATE);
        out.playing = false;
        return true;
    }

    // Spotify
    if (strcmp(source, "spotify") == 0) {
        const char* dash = strstr(title, " - ");
        if (dash) {
            strncpy_s(out.artist, std::string(title, dash - title).c_str(), _TRUNCATE);
            strncpy_s(out.track, (dash + 3), _TRUNCATE);
            strncpy_s(out.source, "spotify", _TRUNCATE);
            out.playing = true;
            return true;
        }
    }

    return false;
}

// ── Public API ───────────────────────────────────────────────────────────────

void Tick() {
    DWORD now = GetTickCount();
    if (now - s_last_tick < 2000) return;
    s_last_tick = now;

    // Initialize COM for UI Automation
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    NowPlaying np{};
    
    // Try SMTC first (placeholder - not fully implemented)
    if (!ReadMediaSMTC(np)) {
        // Fall back to window enumeration
        ReadMediaWindow(np);
    }
    
    // Sanitize track/artist to remove characters the font can't render
    SanitizeForDisplay(np.track, sizeof(np.track));
    SanitizeForDisplay(np.artist, sizeof(np.artist));

    std::lock_guard<std::mutex> lock(s_mutex);
    s_current = np;
}

NowPlaying Get() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_current;
}

// ── Playback controls ────────────────────────────────────────────────────────

void PlayPause()  { SendKeyPress(VK_MEDIA_PLAY_PAUSE); }
void NextTrack()  { SendKeyPress(VK_MEDIA_NEXT_TRACK); }
void PrevTrack()  { SendKeyPress(VK_MEDIA_PREV_TRACK); }

void VolumeUp() {
    // Try per-app volume for all sources
    if (s_media_pid && AdjustAppVolume(s_media_pid, 0.05f)) return;
    
    // For browser sources, don't fall back to system volume (too confusing)
    NowPlaying np = Get();
    if (strcmp(np.source, "youtube") == 0 || strcmp(np.source, "monochrome") == 0) {
        return;  // Browser volume control failed, do nothing
    }
    
    // For native apps like Spotify, fall back to system volume
    SendKeyPress(VK_VOLUME_UP);
}

void VolumeDown() {
    // Try per-app volume for all sources
    if (s_media_pid && AdjustAppVolume(s_media_pid, -0.05f)) return;
    
    // For browser sources, don't fall back to system volume (too confusing)
    NowPlaying np = Get();
    if (strcmp(np.source, "youtube") == 0 || strcmp(np.source, "monochrome") == 0) {
        return;  // Browser volume control failed, do nothing
    }
    
    // For native apps like Spotify, fall back to system volume
    SendKeyPress(VK_VOLUME_DOWN);
}

void SeekForward() {
    NowPlaying np = Get();
    if (strcmp(np.source, "youtube") == 0) SendKeyPress('L');
    else SendKeyPress(VK_RIGHT);
}

void SeekBackward() {
    NowPlaying np = Get();
    if (strcmp(np.source, "youtube") == 0) SendKeyPress('J');
    else SendKeyPress(VK_LEFT);
}

} // namespace media
