#pragma once
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
// SDL_VIDEO_X11_WMCLASS before SDL_Init — "roblox-external"). Must succeed
// before SetDockHints / SetClickThrough / WindowDepth.
bool AttachSelf(const char* wm_class);

// _NET_WM_STATE_ABOVE + SKIP_TASKBAR + SKIP_PAGER + window type UTILITY,
// via the EWMH client-message path (the window is already mapped).
bool SetDockHints();

// enable=true  -> empty input shape (clicks pass through to what is below)
// enable=false -> full-window input shape (normal interactive window)
// Needs HAS_XFIXES; returns false and StatusLine() says so otherwise.
bool SetClickThrough(bool enable);

// Passive grab of HOME + F8 on the root window (AnyModifier). Each press calls
// on_toggle() from the hotkey thread — keep the callback trivial (set a flag).
// Idempotent; returns false when not built with X11.
bool StartToggleHotkeys(void (*on_toggle)());

// X visual depth of our window (32 => ARGB, the compositor honours per-pixel
// alpha; 24 => opaque-only fallback). 0 when unknown.
int WindowDepth();

// True when the default screen offers a 32-bit TrueColor visual - the
// precondition for per-pixel alpha. Decided BEFORE window creation so the
// overlay can pick borderless-always-on-top flags up front instead of
// reshuffling after the fact.
bool ArgbVisualAvailable();

// The game window's rect in screen coordinates. Prefers the window whose
// _NET_WM_PID is the pid passed to platform::SetGamePid(); falls back to any
// viewable top-level whose title contains "roblox" or "sober".
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
