// ─────────────────────────────────────────────────────────────────────────────
//  overlay.cpp — transparent DX11 + ImGui overlay window (implementation)
// ─────────────────────────────────────────────────────────────────────────────

#include "overlay.h"

#include <Windows.h>
#include <process.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <chrono>
#include <cstdio>

#include "memory.h"
#include "globals.h"
#include "input_mouse.h"
#include "platform/platform.h"   // JoinThread (this TU used to reach nt:: transitively via memory.h)
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "obsidian/obsidian.h"
#include "ui/menu_ui.h"      // SyncTheme(): the font half of the theme, between frames

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace overlay
{
    // ── D3D state ────────────────────────────────────────────────────────────
    ID3D11Device*           g_device        = nullptr;
    ID3D11DeviceContext*    g_context       = nullptr;
    ID3D11RenderTargetView* g_render_target = nullptr;
    State g_state;

    // ── Window bookkeeping ───────────────────────────────────────────────────
    // Window NAMES are detection surface: EnumWindows from any process (the
    // game, a helper, a driver-assisted scanner) sees every class name and
    // title on the desktop, and "RBXOverlayWnd" / a window titled "roblox
    // external" identified this tool to a two-line scanner. The names below
    // say what the window is (a widget shell built on the glass-obsidian UI)
    // and nothing about what it is pointed AT. They are all here, in one
    // place, because a name that lives in five files gets "fixed" in four.
    static HINSTANCE   g_hinstance   = nullptr;
    static ATOM        g_class_atom  = 0;
    static const char* g_class_name  = "GlassWidgetWnd";
    static const char* g_window_name = "GlassWidget";

    // ── Tray icon constants ──────────────────────────────────────────────────
    static const UINT WM_TRAYICON  = WM_APP + 1;
    static const UINT ID_TRAY_EXIT  = 1001;
    static const UINT ID_TRAY_TOGGLE = 1002;

    // ── Taskbar window ───────────────────────────────────────────────────────
    static HWND        g_taskbar_window = nullptr;
    static const char* g_taskbar_class  = "GlassWidgetApp";

    // ── Keyboard hook state ──────────────────────────────────────────────────
    static HHOOK   s_llkb_hook      = nullptr;
    static bool    s_llkb_prev_space = false;
    static HANDLE  s_llkb_thread    = nullptr;
    static DWORD   s_llkb_thread_id = 0;

    // ─────────────────────────────────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────────────────────────────────

    struct FindWndData { DWORD pid; HWND result; };

    static BOOL CALLBACK find_wnd_proc(HWND hwnd, LPARAM lparam)
    {
        FindWndData* d = reinterpret_cast<FindWndData*>(lparam);
        DWORD wnd_pid = 0;
        GetWindowThreadProcessId(hwnd, &wnd_pid);
        if (wnd_pid == d->pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd))
        {
            d->result = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    static HWND find_roblox_window()
    {
        DWORD pid = (DWORD)mem::process_id.load();
        if (!pid) return nullptr;
        FindWndData d{ pid, nullptr };
        EnumWindows(find_wnd_proc, reinterpret_cast<LPARAM>(&d));
        return d.result;
    }

    // When click_through is true the overlay ignores the mouse entirely so you
    // can keep playing.  When the menu is open we drop WS_EX_TRANSPARENT so it
    // can be used.
    static void set_clickthrough(bool click_through)
    {
        if (!g_state.window) return;

        LONG_PTR ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
        if (click_through) ex |= WS_EX_TRANSPARENT;

        SetWindowLongPtr(g_state.window, GWL_EXSTYLE, ex);
        SetWindowPos(g_state.window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  VK ↔ ImGui key mapping + text input helpers
    // ─────────────────────────────────────────────────────────────────────────

    static int VkToImGuiKey(int vk) {
        if (vk >= '0' && vk <= '9') return vk;
        if (vk >= 'A' && vk <= 'Z') return vk;
        if (vk == VK_BACK) return ImGuiKey_Backspace;
        if (vk == VK_DELETE) return ImGuiKey_Delete;
        if (vk == VK_RETURN) return ImGuiKey_Enter;
        if (vk == VK_ESCAPE) return ImGuiKey_Escape;
        if (vk == VK_TAB) return ImGuiKey_Tab;
        if (vk == VK_LEFT) return ImGuiKey_LeftArrow;
        if (vk == VK_RIGHT) return ImGuiKey_RightArrow;
        if (vk == VK_UP) return ImGuiKey_UpArrow;
        if (vk == VK_DOWN) return ImGuiKey_DownArrow;
        if (vk == VK_HOME) return ImGuiKey_Home;
        if (vk == VK_END) return ImGuiKey_End;
        if (vk == VK_SPACE) return ImGuiKey_Space;
        if (vk == VK_DECIMAL) return ImGuiKey_KeypadDecimal;
        if (vk == VK_OEM_MINUS) return ImGuiKey_Minus;
        if (vk == VK_OEM_PLUS) return ImGuiKey_Equal;
        if (vk == VK_OEM_1) return ImGuiKey_Semicolon;
        if (vk == VK_OEM_7) return ImGuiKey_Apostrophe;
        if (vk == VK_OEM_COMMA) return ImGuiKey_Comma;
        if (vk == VK_OEM_PERIOD) return ImGuiKey_Period;
        if (vk == VK_OEM_2) return ImGuiKey_Slash;
        if (vk == VK_OEM_4) return ImGuiKey_LeftBracket;
        if (vk == VK_OEM_5) return ImGuiKey_Backslash;
        if (vk == VK_OEM_6) return ImGuiKey_RightBracket;
        if (vk == VK_OEM_3) return ImGuiKey_GraveAccent;
        if (vk == VK_LSHIFT || vk == VK_RSHIFT) return ImGuiKey_ModShift;
        return ImGuiKey_None;
    }

    static bool IsVkPrintable(int vk) {
        if (vk >= '0' && vk <= '9') return true;
        if (vk >= 'A' && vk <= 'Z') return true;
        if (vk == VK_SPACE) return true;
        if (vk == VK_OEM_MINUS || vk == VK_OEM_PLUS || vk == VK_OEM_1) return true;
        if (vk == VK_OEM_7 || vk == VK_OEM_COMMA || vk == VK_OEM_PERIOD) return true;
        if (vk == VK_OEM_2 || vk == VK_OEM_3 || vk == VK_OEM_4) return true;
        if (vk == VK_OEM_5 || vk == VK_OEM_6) return true;
        return false;
    }

    static char VkToChar(int vk, bool shift) {
        if (vk >= '0' && vk <= '9') {
            if (!shift) return (char)vk;
            const char* shifted = ")!@#$%^&*(";
            return shifted[vk - '0'];
        }
        if (vk >= 'A' && vk <= 'Z') {
            char c = (char)(vk + 32);
            if (shift) c = (char)vk;
            return c;
        }
        if (vk == VK_SPACE) return ' ';
        if (shift) {
            if (vk == VK_OEM_MINUS) return '_';
            if (vk == VK_OEM_PLUS) return '+';
            if (vk == VK_OEM_1) return ':';
            if (vk == VK_OEM_7) return '"';
            if (vk == VK_OEM_COMMA) return '<';
            if (vk == VK_OEM_PERIOD) return '>';
            if (vk == VK_OEM_2) return '?';
            if (vk == VK_OEM_3) return '~';
            if (vk == VK_OEM_4) return '{';
            if (vk == VK_OEM_5) return '|';
            if (vk == VK_OEM_6) return '}';
        } else {
            if (vk == VK_OEM_MINUS) return '-';
            if (vk == VK_OEM_PLUS) return '=';
            if (vk == VK_OEM_1) return ';';
            if (vk == VK_OEM_7) return '\'';
            if (vk == VK_OEM_COMMA) return ',';
            if (vk == VK_OEM_PERIOD) return '.';
            if (vk == VK_OEM_2) return '/';
            if (vk == VK_OEM_3) return '`';
            if (vk == VK_OEM_4) return '[';
            if (vk == VK_OEM_5) return '\\';
            if (vk == VK_OEM_6) return ']';
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Input thread — polls keyboard/mouse for ImGui + menu toggle
    // ─────────────────────────────────────────────────────────────────────────

    static DWORD WINAPI input_thread(LPVOID)
    {
        static bool s_prev_keys[256] = {};

        while (true)
        {
            if (menu_toggle_keybind != 0 && (GetAsyncKeyState(menu_toggle_keybind) & 1))
            {
                g_state.menu_open = !g_state.menu_open;

                if (g_state.menu_open)
                {
                    HWND prev = GetForegroundWindow();
                    if (prev && prev != g_state.window)
                        g_state.prev_foreground = prev;

                    SetForegroundWindow(g_state.window);
                }
                else
                {
                    HWND restore = g_state.prev_foreground;
                    if (!restore || !IsWindow(restore))
                        restore = find_roblox_window();

                    if (restore && IsWindow(restore))
                    {
                        SetForegroundWindow(restore);
                        SetActiveWindow(restore);
                    }
                    g_state.prev_foreground = nullptr;
                }
            }

            ImGuiIO& io = ImGui::GetIO();

            POINT cursor{};
            GetCursorPos(&cursor);
            io.MousePos = ImVec2(static_cast<float>(cursor.x), static_cast<float>(cursor.y));

            io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

            if (g_state.menu_open) {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

                io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
                io.AddKeyEvent(ImGuiMod_Shift, shift);

                for (int vk = 1; vk < 256; ++vk) {
                    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == menu_toggle_keybind) continue;
                    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    bool was_down = s_prev_keys[vk];
                    if (down != was_down) {
                        ImGuiKey imgui_key = (ImGuiKey)VkToImGuiKey(vk);
                        if (imgui_key != ImGuiKey_None) {
                            io.AddKeyEvent(imgui_key, down);
                        }
                        if (down && !was_down && IsVkPrintable(vk)) {
                            char c = VkToChar(vk, shift);
                            if (ctrl && (c == 'c' || c == 'C' || c == 'v' || c == 'V' ||
                                         c == 'a' || c == 'A' || c == 'x' || c == 'X')) {
                                // let ctrl shortcuts pass through
                            } else if (c != 0) {
                                io.AddInputCharacter((unsigned int)c);
                            }
                        }
                    }
                    s_prev_keys[vk] = down;
                }
            }

            Sleep(1);
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Window procedure
    // ─────────────────────────────────────────────────────────────────────────

    static LRESULT WINAPI wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        // Raw mouse movement — freecam's mouselook reads this. RIDEV_INPUTSINK
        // (registered in create_overlay) delivers it even when the game has focus.
        case WM_INPUT:
        {
            RAWINPUT ri{};
            UINT size = sizeof(ri);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &ri, &size,
                                sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                ri.header.dwType == RIM_TYPEMOUSE)
            {
                if (ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                    static bool have_prev = false;
                    static LONG prev_x = 0, prev_y = 0;
                    if (have_prev)
                        input::AddRawMouse(ri.data.mouse.lLastX - prev_x,
                                           ri.data.mouse.lLastY - prev_y);
                    prev_x = ri.data.mouse.lLastX;
                    prev_y = ri.data.mouse.lLastY;
                    have_prev = true;
                } else if (ri.data.mouse.lLastX || ri.data.mouse.lLastY) {
                    input::AddRawMouse(ri.data.mouse.lLastX, ri.data.mouse.lLastY);
                }
            }
            return DefWindowProcA(hWnd, msg, wParam, lParam);
        }

        case WM_SIZE:
            if (g_state.swap_chain && wParam != SIZE_MINIMIZED)
            {
                if (g_state.rtv) { g_state.rtv->Release(); g_state.rtv = nullptr; g_render_target = nullptr; }

                g_state.swap_chain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                                  DXGI_FORMAT_UNKNOWN, 0);

                ID3D11Texture2D* back_buffer = nullptr;
                g_state.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
                if (back_buffer)
                {
                    g_device->CreateRenderTargetView(back_buffer, nullptr, &g_state.rtv);
                    g_render_target = g_state.rtv;
                    back_buffer->Release();
                }
            }
            return 0;

        case WM_DESTROY:
            g_state.window = nullptr;
            PostQuitMessage(0);
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT)  { g_request_exit = true; return 0; }
            if (LOWORD(wParam) == ID_TRAY_TOGGLE) { g_state.menu_open = !g_state.menu_open; return 0; }
            break;

        default:
            if (msg == WM_TRAYICON &&
                (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP))
            {
                POINT pt{};
                GetCursorPos(&pt);
                HMENU menu = CreatePopupMenu();
                AppendMenuA(menu, MF_STRING, ID_TRAY_TOGGLE, "show / hide menu");
                AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "exit");
                SetForegroundWindow(hWnd);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
                DestroyMenu(menu);
                return 0;
            }
            break;
        }

        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  System tray icon
    // ─────────────────────────────────────────────────────────────────────────

    static void add_tray_icon()
    {
        NOTIFYICONDATAA nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g_state.window;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        strncpy_s(nid.szTip, "Glass Widget - right click to exit", _TRUNCATE);
        Shell_NotifyIconA(NIM_ADD, &nid);
    }

    static void remove_tray_icon()
    {
        NOTIFYICONDATAA nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g_state.window;
        nid.uID = 1;
        Shell_NotifyIconA(NIM_DELETE, &nid);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Taskbar window — gives the app a real taskbar / Alt-Tab entry
    // ─────────────────────────────────────────────────────────────────────────

    static LRESULT WINAPI taskbar_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CLOSE:
            g_request_exit = true;
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_RESTORED) {
                g_state.menu_open = true;
                ShowWindow(hWnd, SW_MINIMIZE);
            }
            return 0;
        }
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    static void create_taskbar_window()
    {
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = taskbar_proc;
        wc.hInstance     = g_hinstance;
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = g_taskbar_class;
        RegisterClassExA(&wc);

        g_taskbar_window = CreateWindowExA(
            WS_EX_APPWINDOW,
            g_taskbar_class, "Glass Widget",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 420, 200,
            nullptr, nullptr, g_hinstance, nullptr);

        if (g_taskbar_window)
            ShowWindow(g_taskbar_window, SW_SHOWMINNOACTIVE);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Overlay window creation
    // ─────────────────────────────────────────────────────────────────────────

    static bool create_overlay()
    {
        SetProcessDPIAware();

        g_hinstance = GetModuleHandleA(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = wnd_proc;
        wc.hInstance     = g_hinstance;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = g_class_name;

        g_class_atom = RegisterClassExA(&wc);
        if (!g_class_atom)
            return false;

        int vx = 0, vy = 0;
        int vw = GetSystemMetrics(SM_CXSCREEN);
        int vh = GetSystemMetrics(SM_CYSCREEN);

        g_state.window = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            g_class_name, g_window_name,
            WS_POPUP,
            vx, vy, vw, vh,
            nullptr, nullptr, g_hinstance, nullptr);

        if (!g_state.window)
            return false;

        SetLayeredWindowAttributes(g_state.window, RGB(0, 0, 0), 255, LWA_ALPHA);

        MARGINS margin = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(g_state.window, &margin);

        ShowWindow(g_state.window, SW_SHOWNOACTIVATE);
        UpdateWindow(g_state.window);
        SetWindowPos(g_state.window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        // Raw mouse input.  INPUTSINK delivers events even when the overlay is
        // not in the foreground (it is WS_EX_NOACTIVATE and never takes focus).
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = g_state.window;
        input::g_raw_ok.store(RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE,
                              std::memory_order_relaxed);
        if (!input::g_raw_ok.load(std::memory_order_relaxed))
            LogLine("raw mouse input unavailable (error %lu) - freecam mouselook is off",
                    GetLastError());

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  D3D11 device init
    // ─────────────────────────────────────────────────────────────────────────

    static bool init_device()
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.RefreshRate.Numerator   = 60;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count                   = 1;
        desc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount                        = 2;
        desc.OutputWindow                       = g_state.window;
        desc.Windowed                           = TRUE;
        desc.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
        desc.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        D3D_FEATURE_LEVEL requested_levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0
        };

        D3D_FEATURE_LEVEL achieved_level{};
        ID3D11Device* device  = nullptr;
        ID3D11DeviceContext* context = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            requested_levels, _countof(requested_levels),
            D3D11_SDK_VERSION, &desc, &g_state.swap_chain,
            &device, &achieved_level, &context
        );

        if (FAILED(hr))
            return false;

        g_state.feature_level = achieved_level;
        g_device  = device;
        g_context = context;

        ID3D11Texture2D* back_buffer = nullptr;
        g_state.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));

        if (back_buffer)
        {
            device->CreateRenderTargetView(back_buffer, nullptr, &g_state.rtv);
            g_render_target = g_state.rtv;
            back_buffer->Release();
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  ImGui init (Glass Obsidian theme)
    // ─────────────────────────────────────────────────────────────────────────

    static bool init_imgui()
    {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();

        // The theme comes from the app's settings, not from a constant: config::Load
        // has already run by the time the overlay thread starts, so the first frame is
        // the user's theme rather than Axiom for one frame followed by their own.
        // menuui::SyncTheme() re-applies the font-dependent half from here on.
        obsidian::ThemeConfig cfg;
        cfg.ui_scale         = ui_scale;
        cfg.base_font_size   = ui_font_size;
        cfg.title_font_size  = ui_title_size;
        cfg.mono_font_size   = ui_font_size * 0.87f;
        cfg.move_from_title_bar_only = true;
        if (!obsidian::LoadFonts(cfg))
            LogLine("glass obsidian: no system font found, using the built-in fallback");
        obsidian::ApplyTheme(menuui::BuildPalette(), cfg);

        ImGui_ImplWin32_Init(g_state.window);
        ImGui_ImplDX11_Init(g_device, g_context);

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Low-level keyboard hook (space press queue — see globals.h for rationale)
    // ─────────────────────────────────────────────────────────────────────────

    static LRESULT CALLBACK low_level_keyboard_proc(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN ||
                                  wp == WM_KEYUP   || wp == WM_SYSKEYUP)) {
            const KBDLLHOOKSTRUCT& k = *(const KBDLLHOOKSTRUCT*)lp;
            if (k.vkCode == VK_SPACE) {
                const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
                if (down && !s_llkb_prev_space)
                    g_input_space_hook_presses.fetch_add(1, std::memory_order_relaxed);
                s_llkb_prev_space = down;
                g_input_space_hook_down.store(down, std::memory_order_relaxed);
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    static DWORD WINAPI keyboard_hook_thread(LPVOID)
    {
        s_llkb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, low_level_keyboard_proc,
                                        GetModuleHandleW(nullptr), 0);
        if (!s_llkb_hook) {
            g_input_hook_installed.store(false, std::memory_order_relaxed);
            LogLine("input: keyboard hook failed (error %lu) - falling back to "
                    "GetAsyncKeyState polling, fast space presses may miss",
                    GetLastError());
            return 0;
        }
        g_input_hook_installed.store(true, std::memory_order_relaxed);
        LogLine("input: keyboard hook installed on its own thread - space "
                "presses are queued, not polled");
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (s_llkb_hook) { UnhookWindowsHookEx(s_llkb_hook); s_llkb_hook = nullptr; }
        g_input_hook_installed.store(false, std::memory_order_relaxed);
        return 0;
    }

    static void install_keyboard_hook()
    {
        if (s_llkb_thread) return;
        s_llkb_thread = CreateThread(nullptr, 0, keyboard_hook_thread, nullptr,
                                     0, &s_llkb_thread_id);
    }

    static void remove_keyboard_hook()
    {
        if (s_llkb_thread) {
            PostThreadMessageW(s_llkb_thread_id, WM_QUIT, 0, 0);
            platform::JoinThread(s_llkb_thread, 2000);   // wait + close, Win32-layer safe
            s_llkb_thread = nullptr;
        }
        g_input_hook_installed.store(false, std::memory_order_relaxed);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Main render loop
    // ─────────────────────────────────────────────────────────────────────────

    void run()
    {
        if (!create_overlay()) { LogLine("failed to create overlay window"); return; }
        if (!init_device())    { LogLine("failed to create d3d11 device"); return; }
        if (!init_imgui())     { LogLine("failed to init imgui"); return; }

        create_taskbar_window();
        add_tray_icon();
        LogLine("overlay ready - right click the tray icon to exit");

        install_keyboard_hook();
        CreateThread(nullptr, 0, input_thread, nullptr, 0, nullptr);

        while (g_state.window && !g_request_exit)
        {
            MSG msg{};
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT) return;
            }

            if (!g_state.window) break;

            // Fonts cannot be re-baked inside a frame (the atlas is locked), so the
            // scale/font-size settings are picked up here, between frames. No-op on
            // every frame where nothing changed -- see menu_ui.h.
            //
            // When it DOES re-bake, the texture the DX11 backend uploaded from the old
            // atlas has to go with it: this backend creates the font texture in
            // CreateDeviceObjects() only, and leaves io.Fonts->TexID pointing at the
            // view it made there. Skip the pair below and every glyph after a ui_scale
            // or font-size change is sampled out of a bake that no longer exists - tofu,
            // or the previous size's letters stretched over the new UVs.
            if (menuui::SyncTheme())
            {
                ImGui_ImplDX11_InvalidateDeviceObjects();
                ImGui_ImplDX11_CreateDeviceObjects();
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            render_ui();

            ImGui::Render();

            {
                static bool s_click_through = true;
                bool want_mouse = g_state.menu_open && ImGui::GetIO().WantCaptureMouse;
                bool desired = !want_mouse;
                if (desired != s_click_through)
                {
                    set_clickthrough(desired);
                    s_click_through = desired;
                }
            }

            constexpr float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            g_context->OMSetRenderTargets(1, &g_state.rtv, nullptr);
            g_context->ClearRenderTargetView(g_state.rtv, clear);

            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_state.swap_chain->Present(0, 0);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Shutdown
    // ─────────────────────────────────────────────────────────────────────────

    void shutdown()
    {
        remove_keyboard_hook();
        remove_tray_icon();
        if (g_taskbar_window) { DestroyWindow(g_taskbar_window); g_taskbar_window = nullptr; }
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (g_state.rtv)        g_state.rtv->Release();
        if (g_state.swap_chain) g_state.swap_chain->Release();
        if (g_context)          g_context->Release();
        if (g_device)           g_device->Release();

        if (g_state.window) { DestroyWindow(g_state.window); g_state.window = nullptr; }
        if (g_class_atom)   { UnregisterClassA(g_class_name, g_hinstance); g_class_atom = 0; }
    }
}
