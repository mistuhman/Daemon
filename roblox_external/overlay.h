#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  overlay.h — public interface for the transparent DX11 + ImGui overlay
//
//  The implementation lives in overlay.cpp.  Only main.cpp includes this
//  header; feature translation units must NOT pull it in (it drags d3d11.h,
//  dxgi.h and the ImGui back-ends with it).
// ─────────────────────────────────────────────────────────────────────────────

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace overlay
{
    // ── D3D state shared with the render thread ─────────────────────────────
    extern ID3D11Device*           g_device;
    extern ID3D11DeviceContext*    g_context;
    extern ID3D11RenderTargetView* g_render_target;

    struct State
    {
        HWND                    window      = nullptr;
        IDXGISwapChain*         swap_chain  = nullptr;
        ID3D11RenderTargetView* rtv         = nullptr;
        D3D_FEATURE_LEVEL       feature_level{};
        bool                    menu_open   = true;   // start with the menu visible
        HWND                    prev_foreground = nullptr;
    };

    extern State g_state;

    // ── Lifecycle ────────────────────────────────────────────────────────────
    void run();
    void shutdown();

    // Defined in main.cpp — called once per frame from the render loop.
    void render_ui();
}
