#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  smtc_raw.h — Raw COM interfaces for Windows.Media.Control (SMTC)
//
//  Manually defined GUIDs and interface structures to avoid C++/WinRT dependency.
//  These match the WinRT runtime interfaces for media transport controls.
// ─────────────────────────────────────────────────────────────────────────────

#include <windows.h>
#include <inspectable.h>
#include <asyncinfo.h>

// Interface GUIDs for Windows.Media.Control
// Defined in media_tracker.cpp to avoid multiple definition errors
extern const IID IID_IGlobalSystemMediaTransportControlsSessionManagerStatics;
extern const IID IID_IGlobalSystemMediaTransportControlsSessionManager;
extern const IID IID_IGlobalSystemMediaTransportControlsSession;
extern const IID IID_IGlobalSystemMediaTransportControlsSessionPlaybackInfo;
extern const IID IID_IGlobalSystemMediaTransportControlsSessionMediaProperties;

// Runtime class name
#define SMTC_MANAGER_CLASS L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager"

// Playback status enum (matches Windows.Media.SystemMediaTransportControlsPlaybackStatus)
enum PlaybackStatus {
    PlaybackStatus_Closed = 0,
    PlaybackStatus_Opened = 1,
    PlaybackStatus_Changing = 2,
    PlaybackStatus_Stopped = 3,
    PlaybackStatus_Playing = 4,
    PlaybackStatus_Paused = 5
};

// Forward declarations
struct IGlobalSystemMediaTransportControlsSessionManagerStatics;
struct IGlobalSystemMediaTransportControlsSessionManager;
struct IGlobalSystemMediaTransportControlsSession;
struct IGlobalSystemMediaTransportControlsSessionPlaybackInfo;
struct IGlobalSystemMediaTransportControlsSessionMediaProperties;

// IGlobalSystemMediaTransportControlsSessionManagerStatics
// Factory interface to get the session manager
struct IGlobalSystemMediaTransportControlsSessionManagerStatics : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(
        /* out */ void** asyncOp) = 0;
};

// IGlobalSystemMediaTransportControlsSessionManager
// Main manager interface
struct IGlobalSystemMediaTransportControlsSessionManager : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(
        /* out */ IGlobalSystemMediaTransportControlsSession** session) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetSessions(
        /* out */ void** sessions) = 0;  // Returns IVectorView<Session>
};

// IGlobalSystemMediaTransportControlsSession
// Represents a single media session
struct IGlobalSystemMediaTransportControlsSession : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(
        /* out */ void** asyncOp) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(
        /* out */ IGlobalSystemMediaTransportControlsSessionPlaybackInfo** info) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(
        /* out */ HSTRING* appId) = 0;
    
    // ... other methods omitted for brevity
};

// IGlobalSystemMediaTransportControlsSessionPlaybackInfo
// Playback state information
struct IGlobalSystemMediaTransportControlsSessionPlaybackInfo : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(
        /* out */ PlaybackStatus* status) = 0;
    
    // ... other methods omitted
};

// IGlobalSystemMediaTransportControlsSessionMediaProperties
// Media metadata (title, artist, etc.)
struct IGlobalSystemMediaTransportControlsSessionMediaProperties : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(
        /* out */ HSTRING* title) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE get_Artist(
        /* out */ HSTRING* artist) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(
        /* out */ HSTRING* album) = 0;
    
    // ... other properties omitted
};

// IAsyncOperation<TResult> template for async operations
template<typename TResult>
struct IAsyncOperation : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(/* out */ TResult* results) = 0;
};

// Specialization for IGlobalSystemMediaTransportControlsSessionManager
typedef IAsyncOperation<IGlobalSystemMediaTransportControlsSessionManager*> 
    IAsyncOperation_SessionManager;

// Specialization for IGlobalSystemMediaTransportControlsSessionMediaProperties
typedef IAsyncOperation<IGlobalSystemMediaTransportControlsSessionMediaProperties*> 
    IAsyncOperation_MediaProperties;

// IVectorView<T> for collections
template<typename T>
struct IVectorView : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(
        /* in */ unsigned index,
        /* out */ T* item) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE get_Size(
        /* out */ unsigned* size) = 0;
    
    // ... other methods omitted
};

typedef IVectorView<IGlobalSystemMediaTransportControlsSession*> 
    IVectorView_Session;

// Helper to wait for async operation with timeout
template<typename TResult>
HRESULT WaitForAsync(IAsyncOperation<TResult>* asyncOp, TResult* result, DWORD timeoutMs = 5000) {
    if (!asyncOp) return E_INVALIDARG;
    
    // Create event for completion
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hEvent) return E_FAIL;
    
    // Set completion handler (simplified - just signal event)
    struct Handler : public IInspectable {
        HANDLE event;
        ULONG refCount;
        
        Handler(HANDLE e) : event(e), refCount(1) {}
        
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
            if (riid == IID_IUnknown || riid == IID_IInspectable) {
                *ppv = this;
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override {
            if (--refCount == 0) { delete this; return 0; }
            return refCount;
        }
        
        HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override {
            *count = 0; *iids = nullptr; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
            *name = nullptr; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
            *level = BaseTrust; return S_OK;
        }
        
        // AsyncActionCompletedHandler invoke
        HRESULT STDMETHODCALLTYPE Invoke(IAsyncOperation<TResult>*, AsyncStatus) {
            SetEvent(event);
            return S_OK;
        }
    };
    
    Handler* handler = new Handler(hEvent);
    HRESULT hr = asyncOp->put_Completed(handler);
    handler->Release();
    
    if (SUCCEEDED(hr)) {
        DWORD waitResult = WaitForSingleObject(hEvent, timeoutMs);
        if (waitResult == WAIT_OBJECT_0) {
            hr = asyncOp->GetResults(result);
        } else {
            hr = E_FAIL;  // Timeout or error
        }
    }
    
    CloseHandle(hEvent);
    return hr;
}
