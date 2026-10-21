#pragma once

#include <cstdint>
// -----------------------------------------------------------------------------
//  platform/overlay_x11.h — Linux X11 window control for the overlay
// -----------------------------------------------------------------------------
//  The third leg of P3 (after the menu window and the SDL2+GL shell): make the
//  overlay behave like an overlay —
//
//    * EWMH game rect: find the game window (_NET_WM_PID match on the pid the
//      attach path stored via platform::SetGamePid, title match fallback) and
//      report its screen rect so the overlay can cover exactly the game.
//    * XFixes click-through: an empty XFixes input shape lets clicks fall
//      through to the game; restoring the full window shape brings input back
//      (menu open). This is the only reliable click-through on X11 —
//      SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH only changes focus semantics.
//    * Root-grabbed HOME/F8: once the input shape is empty the window cannot
//      be focused by clicking, so the menu toggle is a passive XGrabKey on the
//      root window (ancestor of the focus window, so it fires no matter who
//      has focus) pumped by a tiny thread with its own Display connection.
//
//  All Xlib lives in platform_linux.cpp behind this header, compiled only when
//  CMake finds X11 and defines HAS_X11 (plus HAS_XFIXES when XFixes is present).
//  Without them every call here is a stub that returns false and StatusLine() says
//  why — the windowed menu still works exactly as before. No SDL types leak
//  through this header and the Windows build never sees it (not in the vcxproj).
// -----------------------------------------------------------------------------

namespace x11overlay {

// True when this build has X11 and a DISPLAY is reachable.
bool Available();

// Locate our own overlay window by WM_CLASS (the class set via
// SDL_VIDEO_X11_WMCLASS before SDL_Init — "daemon"). Must succeed
// before SetDockHints / SetClickThrough / WindowDepth.
bool AttachSelf(const char* wm_class);

// Attach by exact X11 window id — the honest path for an override-redirect
// overlay: the WM never manages OR windows, so they never appear in
// _NET_CLIENT_LIST and a WM_CLASS scan over that list cannot find them.
// The id comes straight from SDL_GetWindowWMInfo; no scanning, no guessing.
bool AttachWindow(unsigned long xid);

// _NET_WM_STATE_ABOVE + SKIP_TASKBAR + SKIP_PAGER + window type UTILITY,
// via the EWMH client-message path (the window is already mapped).
bool SetDockHints();

// enable=true  -> empty input shape (clicks pass through to what is below)
// enable=false -> full-window input shape (normal interactive window)
// Needs HAS_XFIXES; returns false and StatusLine() says so otherwise.
bool SetClickThrough(bool enable);

// The precise form of the same idea: the input shape becomes EXACTLY the given
// window-local rectangles (the menu's own windows — main column plus the open
// feature windows), so while the menu is open, clicks on every other pixel of
// the overlay (all the transparent area over the game) still fall through.
// rects is n quads x,y,w,h; n <= 0 means the empty shape (click-through).
// Coordinates are clamped to the window. Needs HAS_XFIXES.
bool SetInputRects(const int* rects, int n);

// Passive grab of HOME + F8 on the root window (AnyModifier). Each press calls
// on_toggle() from the hotkey thread — keep the callback trivial (set a flag).
// Idempotent; returns false when not built with X11.
bool StartToggleHotkeys(void (*on_toggle)());

// X visual depth of our window (32 => ARGB, the compositor honours per-pixel
// alpha; 24 => opaque-only fallback). 0 when unknown.
int WindowDepth();

// True when a compositing manager owns the _NET_WM_CM_Sn selection for this
// screen — the EWMH way to ask "is anything actually blending ARGB windows
// right now". The 013 receipt printed xfwm4 use_compositing=false from the
// config while the overlay visibly blended, so the overlay logs this runtime
// fact alongside: config and reality are not always the same thing.
bool CompositorActive();

// True when the default screen offers a 32-bit TrueColor visual - the
// precondition for per-pixel alpha. Decided BEFORE window creation so the
// overlay can pick borderless-always-on-top flags up front instead of
// reshuffling after the fact.
bool ArgbVisualAvailable();

// The visualid of that 32-bit visual (0 when there is none). The overlay feeds
// it to SDL's SDL_VIDEO_X11_WINDOW_VISUALID hint BEFORE SDL_CreateWindow, which
// is SDL2's supported way to force the window onto a specific visual - the
// alpha-8 GLX request alone still produced depth-24 windows in the field.
uint64_t ArgbVisualId();

// Re-assert being above the game window. The check reads the REAL stacking
// order (XQueryTree children are listed bottom-to-top) — not
// _NET_CLIENT_LIST_STACKING, which only knows WM-managed windows and is
// blind to our override-redirect overlay. When the game is above us we
// XRaiseWindow ourselves: for an OR window the WM cannot veto or re-layer
// that (the 012 receipt: a managed ABOVE window lost to the focused
// fullscreen game every time). Called from the follow tick; returns true
// when we are above the game (or no game window exists).
bool EnsureAboveGame();

// Hand keyboard focus to the game window (used when our menu closes and we
// held focus for text input). Fails softly — focus is a courtesy, not a
// contract.
bool FocusGameWindow();

// The game window's rect in screen coordinates. Candidates come from (1) the
// window whose _NET_WM_PID is the pid passed to platform::SetGamePid(), then
// (2) any Sober-named process's window, then (3) WM_CLASS-gated windows —
// never title alone, so a browser tab titled "Roblox" cannot match. When more
// than one candidate is mapped (Sober's launcher AND the Roblox client), the
// LARGEST viewable window wins: the client, not the launcher.
bool GameWindowRect(int& x, int& y, int& w, int& h);

// Which window currently has _NET_ACTIVE_STATE focus: 'g'ame, 'u's (our
// overlay), 'o'ther, or 0 unknown. (GameWindowForeground seam.)
char ActiveWindowKind();

// Cursor position in root/screen coordinates.
bool CursorScreen(int& x, int& y);

// Screen DPI from the X screen geometry (mm width), 96 when unknown.
int ScreenDpi();

// Steady-state human line for the debug page, never null, never empty:
// "x11 click-through (xfixes), depth 32" / "no DISPLAY" / "no x11 in build" ...
const char* StatusLine();

// The pid stored via platform::SetGamePid — shared with the game-rect lookup.
void SetGamePid(uint32_t pid);

} // namespace x11overlay
