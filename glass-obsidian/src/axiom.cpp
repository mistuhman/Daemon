// -----------------------------------------------------------------------------
//  src/axiom.cpp  --  the "Axiom" app shell (see obsidian/axiom.h)
// -----------------------------------------------------------------------------
//  Two rules govern everything in here, and both come from having a *fixed*
//  cell grid instead of a flowing layout:
//
//  1. A card reserves its exact rect, then positions its control inside it.
//     Because that rect is a child window, item widths, right-alignment and
//     clipping are all relative to the card -- a slider in a 320px card can
//     neither measure the page's width nor paint over its neighbour.
//
//  2. The accent colour is only ever used for STATE. A card edge, a pill, a
//     filled track or a rail indicator is accent-tinted when the setting behind
//     it is live -- never just because the mouse is nearby. That restraint is
//     what separates "designed" from "glowy".
// -----------------------------------------------------------------------------
#include "obsidian/axiom.h"
#include "obsidian/theme.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace obsidian {
namespace {

// -----------------------------------------------------------------------------
//  local helpers
// -----------------------------------------------------------------------------
inline ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

inline void Shout(const char* in, char* out, size_t n)
{
    size_t i = 0;
    if (n == 0) return;
    for (; in && in[i] && i + 1 < n; ++i) {
        char c = in[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i] = c;
    }
    out[i] = '\0';
}

float MicroTracking() { return S(1.05f); }
float MicroFontSz()   { return (FontBold() ? FontBold()->FontSize : S(15.0f)) * AM().micro_k; }

// Letterspaced text, drawn glyph by glyph (ImGui's only other tracking knob is a
// per-font config, which would mean baking a second font).
float DrawTracked(ImDrawList* d, ImFont* f, float size, ImVec2 pos, ImU32 col,
                  const char* text, float tracking)
{
    if (!f || !text) { if (text) d->AddText(pos, col, text); return text ? ImGui::CalcTextSize(text).x : 0.0f; }
    float x = pos.x;
    char ch[2] = { 0, 0 };
    for (const char* s = text; *s; ++s) {
        ch[0] = *s;
        d->AddText(f, size, ImVec2(x, pos.y), col, ch);
        x += f->CalcTextSizeA(size, FLT_MAX, -1.0f, ch).x + tracking;
    }
    return (x - pos.x) - tracking;
}

float MeasureTracked(ImFont* f, float size, const char* text, float tracking)
{
    if (!text || !*text) return 0.0f;
    if (!f) return ImGui::CalcTextSize(text).x;
    float w = 0.0f;
    char ch[2] = { 0, 0 };
    for (const char* s = text; *s; ++s) {
        ch[0] = *s;
        w += f->CalcTextSizeA(size, FLT_MAX, -1.0f, ch).x + tracking;
    }
    return w > 0.0f ? w - tracking : 0.0f;
}

// Truncates with an ellipsis so a long label can never run under a control.
void FitTruncated(char* io, float max_w, ImFont* f, float size, float tracking)
{
    if (max_w <= 0.0f) return;
    if (MeasureTracked(f, size, io, tracking) <= max_w) return;
    const size_t n = strlen(io);
    for (size_t k = n; k > 1; --k) {
        const size_t m = k - 1;
        if (m + 3 < 128) { io[m] = '.'; io[m + 1] = '.'; io[m + 2] = '.'; io[m + 3] = '\0'; }
        if (MeasureTracked(f, size, io, tracking) <= max_w) return;
    }
}

// A fully transparent interactive item: keeps native hit-testing, click and
// drag capture, and nav focus, while the widget paints itself on top.
void BeginGhost()
{
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}
void EndGhost()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
}

// -----------------------------------------------------------------------------
//  grid context (one grid at a time -- a menu page has exactly one)
// -----------------------------------------------------------------------------
struct GridCtx
{
    bool  active = false;
    int   cols   = 2;
    int   col    = 0;
    float gutter = 8.0f;
    float row_h  = 50.0f;
    float pad    = 11.0f;
    float cell_w = 0.0f;

    bool   have_pending = false;
    ImVec2 pending      = ImVec2(0, 0);
};

GridCtx& G() { static GridCtx g; return g; }

// -----------------------------------------------------------------------------
//  card context
// -----------------------------------------------------------------------------
struct CardCtx
{
    ImVec2  a, b;         // screen rect
    float   pad;
    float   r;
    ImGuiID hov_id;
    bool    hoverable;
    bool    hovered;
    bool    clicked;
    bool    live;
};

ImVector<CardCtx>& CardStack() { static ImVector<CardCtx> s; return s; }
CardCtx* TopCard() { return CardStack().empty() ? nullptr : &CardStack().back(); }

// The card IS the layout primitive of this shell, so it carries the material: a
// flat fill reads as a table cell, the same fill with a gradient, a sheen and a
// rim reads as a pane of glass with something under it. `lift` scales the whole
// hover response, and the rim light (chroma) is what makes the hover feel like the
// card moved toward you even though nothing moved -- moving a child window's
// surface would fight its clip rect, so the emphasis is all light.
void PaintCardSurface(const ImVec2& a, const ImVec2& b, float r, CardFlags flags,
                      float hover, float live_on)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float sa = p.SurfaceAlphaScale();
    const float ea = p.EdgeWeight();
    const float lift = p.mat.lift;

    const bool inset = (flags & Card_Inset) != 0;
    ImVec4 base = inset ? Shade(p.field, -0.05f)
                        : Mix(p.panel, p.window_top, 0.30f);
    base = Mix(base, Lift(p, base, 0.16f), hover * 0.80f * lift);
    if (live_on > 0.01f) base = Mix(base, p.accent, 0.11f * live_on);

    // A ramp, not a flat fill: the top stop catches the same light as the window.
    const float gk = p.mat.gradient;
    GradientFillRounded(d, a, b, Fade(Lift(p, base, 0.055f * gk), (inset ? 0.85f : 0.78f) * sa),
                        Fade(Lift(p, base, -0.075f * gk), (inset ? 0.85f : 0.78f) * sa), r, 6);

    if (!inset)
        StrokeSheen(d, a, b, Fade(p.sheen, (0.45f + 0.55f * hover) * (0.6f + 0.4f * lift)), r);
    if (!inset && p.mat.sweep > 0.002f)
        SpecularSweep(d, a, b, r, p, 0.55f + 0.9f * hover, anim::Clock() * 0.08f +
                    (a.x * 0.0011f + a.y * 0.0017f), 0.8f);
    if (p.mat.frost > 0.002f)
        FrostRing(d, a, b, r, p, 0.55f + 0.45f * hover, 3);

    ImVec4 edge = Fade(p.edge, (inset ? 0.50f : 0.70f) * ea);
    if (hover > 0.01f) edge = Mix(edge, Fade(p.accent, 0.55f), hover * 0.65f * lift);
    if (live_on > 0.01f) edge = Mix(edge, Fade(p.accent, 0.85f), 0.72f * live_on);
    StrokeEdge(d, a, b, edge, r);
    if (p.mat.chroma > 0.002f)
        ChromaRim(d, a, b, p, r, 0.55f + 0.45f * FMax(hover * lift, live_on));
}

// The ON/OFF pill. Paint-only: its card owns the click.
void PaintPill(ImDrawList* d, const ImVec2& pos, bool on, float anim_on, float hover)
{
    const Palette& p = ActivePalette();
    const float w = S(AM().pill_w), h = S(AM().pill_h);
    const float r = h * 0.5f;
    const ImVec2 a = pos, b(a.x + w, a.y + h);
    const float e = anim::Ease(anim_on);

    const ImVec4 off = Mix(p.field, p.field_hover, hover);
    GradientFillRounded(d, a, b, Fade(Mix(Lift(p, off, 0.05f), p.accent, 0.30f * e), p.SurfaceAlphaScale()),
                           Fade(Mix(Shade(off, -0.14f), p.accent_2, 0.30f * e), p.SurfaceAlphaScale()), r);
    StrokeEdge(d, a, b, Mix(Fade(p.edge, 0.85f * p.mat.edge), Fade(Shade(p.accent, 0.25f), 0.95f), e), r);
    if (e > 0.02f)
        d->AddShadowRect(a, b, U32(Fade(p.accent, 0.55f * e * p.mat.bloom)), S(10.0f), ImVec2(0, 0),
                         ImDrawFlags_RoundCornersAll, r);
    ChromaRim(d, a, b, p, r, 0.45f + 0.55f * e);

    // ON / OFF micro-caps, on the half the knob is not covering
    char up[16];
    Shout(on ? "ON" : "OFF", up, sizeof(up));
    ImFont* f = FontBold();
    const float fs = f ? f->FontSize * 0.68f : S(10.0f);
    const float tr = S(0.7f);
    const float tw = MeasureTracked(f, fs, up, tr);
    const float pad = S(8.0f);
    const float lx  = on ? a.x + pad : b.x - pad - tw;
    const ImVec4 tc = on ? p.text_on_accent : Mix(p.text_faint, p.text_dim, 0.35f + 0.4f * hover);
    DrawTracked(d, f, fs, ImVec2(lx, a.y + (h - fs) * 0.5f - S(0.5f)),
                U32(Fade(tc, p.TextAlphaScale())), up, tr);

    const float kd = h - S(7.0f);
    const float kx = a.x + S(3.5f) + e * (w - kd - S(7.0f));
    const ImVec2 kc(kx + kd * 0.5f, a.y + h * 0.5f);
    d->AddShadowRect(ImVec2(kc.x - kd * 0.5f, kc.y - kd * 0.5f),
                     ImVec2(kc.x + kd * 0.5f, kc.y + kd * 0.5f),
                     U32(ImVec4(0, 0, 0, 0.50f)), S(3.0f), ImVec2(0, S(1.0f)),
                     ImDrawFlags_RoundCornersAll, kd * 0.5f);
    const ImVec4 kcol = Mix(ImVec4(0.42f, 0.44f, 0.48f, 1.0f), ImVec4(1, 1, 1, 1), e);
    d->AddCircleFilled(kc, kd * 0.5f, U32(Fade(kcol, 0.98f)), 20);
    if (on)
        icon::Check(d, kc, kd * 0.72f, U32(Fade(p.text_on_accent, e)));
}

} // namespace

// The public pill entry point shares the private painter, so the geometry lives
// in exactly one place and a hand-composed hero card can never drift from a
// card switch.
void DrawPill(ImDrawList* d, ImVec2 pos, bool on, float t, float hover)
{
    PaintPill(d, pos, on, t, hover);
}

// =============================================================================
//  tuning + helpers
// =============================================================================
AxiomMetrics& AM()
{
    static AxiomMetrics m;
    return m;
}
void SetAxiomMetrics(const AxiomMetrics& m) { AM() = m; }

NavStyle& NavStyleStore()
{
    static NavStyle s;
    return s;
}
void SetNavStyle(const NavStyle& v) { NavStyleStore() = v; }
const NavStyle& NavSty() { return NavStyleStore(); }

float MicroCaps(ImDrawList* d, ImVec2 pos, const char* text, ImU32 col,
                float size, float tracking)
{
    if (!text || !*text) return 0.0f;
    char up[160];
    Shout(VisibleLabel(text), up, sizeof(up));
    const float fs = size > 0.0f ? size : MicroFontSz();
    const float tr = tracking >= 0.0f ? tracking : MicroTracking();
    return DrawTracked(d, FontBold(), fs, pos, col, up, tr);
}

float MeasureMicroCaps(const char* text, float size, float tracking)
{
    char up[160];
    Shout(VisibleLabel(text), up, sizeof(up));
    const float fs = size > 0.0f ? size : MicroFontSz();
    const float tr = tracking >= 0.0f ? tracking : MicroTracking();
    return MeasureTracked(FontBold(), fs, up, tr);
}

// Both the measurement and the painter uppercase, so the pill is exactly as wide
// as the text it draws. (Chip labels are micro-caps in this language; shouting
// here means no caller has to remember to.)
float ChipWidth(const char* text)
{
    char up[96];
    Shout(text ? text : "", up, sizeof(up));
    return TextWidth(FontBold(), up) + S(28.0f);
}

void DrawChip(ImDrawList* d, ImVec2 pos, const char* text, const ImVec4& tint,
              bool pulse, float right_x)
{
    const Palette& p = ActivePalette();
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float w = ChipWidth(text);
    const float h = line_h + S(4.0f);
    if (right_x > 0.0f) pos.x = right_x - w;
    const ImVec2 a = pos, b(a.x + w, a.y + h);
    const float r = h * 0.5f;
    const float g = pulse ? (0.55f + 0.45f * anim::Pulse(0.75f)) : 1.0f;

    d->AddRectFilled(a, b, U32(Fade(Mix(p.field, tint, 0.12f), 0.90f * p.SurfaceAlphaScale())), r);
    StrokeEdge(d, a, b, Fade(tint, 0.42f + 0.28f * (pulse ? g : 0.0f)), r);
    const float dot_r = S(2.8f);
    const ImVec2 dc(a.x + S(10.0f) + dot_r, a.y + h * 0.5f);
    d->AddCircleFilled(dc, dot_r, U32(Fade(tint, g)), 16);
    if (pulse && g > 0.8f)
        d->AddCircle(dc, dot_r + S(2.6f) * g, U32(Fade(tint, 0.30f * (g - 0.8f) / 0.2f)), 20, S(1.0f));

    ImFont* f = FontBold();
    const float fs = f ? f->FontSize * 0.80f : ImGui::GetTextLineHeight();
    const ImVec2 tp(dc.x + dot_r + S(6.0f), a.y + (h - fs) * 0.5f);
    const ImVec4 tc = Mix(p.text_dim, tint, 0.35f);
    char up[96];
    Shout(text ? text : "", up, sizeof(up));
    if (f) d->AddText(f, fs, tp, U32(Fade(tc, p.TextAlphaScale())), up);
    else   d->AddText(tp, U32(Fade(tc, p.TextAlphaScale())), up);
}

float Chip(const char* text, const ImVec4& tint, bool pulse)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ChipWidth(text);
    const float h = ImGui::GetTextLineHeightWithSpacing() + S(4.0f);
    DrawChip(ImGui::GetWindowDrawList(), pos, text, tint, pulse, 0.0f);
    ImGui::Dummy(ImVec2(w, h));
    return w;
}

// =============================================================================
//  Nav sidebar
// =============================================================================
void SidebarBegin(const char* id, const ImVec2& size, float width)
{
    const Palette& p = ActivePalette();
    const float w = width > 0.0f ? S(width) : S(AM().nav_w);

    ImGui::PushID(id);
    ImGui::BeginGroup();          // so the caller can SameLine() the page beside it

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14.0f), S(14.0f)));
    // Negative y is handed straight to BeginChild, which resolves it against the
    // parent rect -- resolving it here would be wrong inside a SameLine row.
    ImGui::BeginChild("##rail", ImVec2(w, size.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoSavedSettings);

    // The dock is one step DARKER than the window, so it reads as recessed.
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(a, b, U32(Fade(Shade(p.window_bottom, p.light ? 0.16f : -0.38f),
                                    p.SurfaceAlphaScale())));
    d->AddLine(ImVec2(b.x - 0.75f, a.y), ImVec2(b.x - 0.75f, b.y),
               U32(Fade(p.edge, 0.80f * p.EdgeWeight())), 1.0f);
    d->AddLine(ImVec2(a.x + 1.0f, a.y + 0.75f), ImVec2(b.x - 1.0f, a.y + 0.75f),
               U32(Fade(p.sheen, 0.30f)), 1.0f);
    // The rail is one pane set INTO the window, so it gets the sweep on its own
    // phase -- a highlight that crosses the rail and the page at different times is
    // what sells them as separate pieces of glass, which is the whole idea.
    if (p.mat.sweep > 0.002f)
        SpecularSweep(d, a, b, 0.0f, p, 0.5f, anim::Clock() * 0.08f + 0.4f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(9.0f), S(4.0f)));
}

void SidebarEnd()
{
    ImGui::PopStyleVar(2);            // ItemSpacing + WindowPadding
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::PopID();
}

void SidebarBrand(const char* name, const char* accent_suffix,
                  const char* subtitle, const char* chip, const ImVec4* chip_tint)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImFont* tf = FontTitle();
    // Slightly under the page title's size on purpose: the rail header names the
    // session, the page header names what you are looking at, and only one of them
    // can be the loudest thing on the screen.
    const float fs = tf ? tf->FontSize * 0.92f : S(17.0f);
    const float tr = S(0.75f);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;

    float x = base.x;
    const float y = base.y;
    // The chip is right-aligned inside `avail`; the wordmark has to stop short of
    // it, or a long place name and the status pill draw on top of each other.
    const bool has_sub = (subtitle && *subtitle);
    char up[96], up2[48];
    Shout(name && *name ? name : "", up, sizeof(up));
    Shout(accent_suffix && *accent_suffix ? accent_suffix : "", up2, sizeof(up2));
    const float want = (up[0] ? MeasureTracked(tf, fs, up, tr) : 0.0f) +
                       (up2[0] ? S(5.0f) + MeasureTracked(tf, fs, up2, tr) : 0.0f);
    float chip_w = (chip && *chip) ? ChipWidth(chip) + S(10.0f) : 0.0f;
    // The status chip is decoration, the wordmark is the identity: when the two
    // cannot share the line, the chip steps out rather than the name getting an
    // ellipsis. (A menu's header shows the same state, so nothing is lost.)
    bool show_chip = (chip && *chip);
    if (show_chip && want > avail - chip_w) { show_chip = false; chip_w = 0.0f; }
    const float brand_room = FMax(S(40.0f), avail - chip_w);
    if (up[0]) {
        FitTruncated(up, brand_room, tf, fs, tr);
        x += DrawTracked(d, tf, fs, ImVec2(x, y), U32(Fade(p.text, p.TextAlphaScale())), up, tr);
        x += S(5.0f);
    }
    if (up2[0]) {
        const float tw = FMin(MeasureTracked(tf, fs, up2, tr), FMax(0.0f, brand_room - (x - base.x)));
        if (tw < S(12.0f)) { up2[0] = '\0'; }
        DrawTracked(d, tf, fs, ImVec2(x, y), U32(p.accent), up2, tr);
        x += tw;
    }

    const float chip_h = ImGui::GetTextLineHeightWithSpacing() + S(4.0f);
    const float mfs = MicroFontSz();
    // The chip rides the wordmark line (vertically centred on it, right-aligned to
    // the rail's content edge), the subtitle gets the line below at full width.
    float h = FMax(fs, show_chip ? chip_h : 0.0f);
    if (show_chip) {
        const ImVec4 tc = chip_tint ? *chip_tint : p.accent;
        // With no wordmark on the line there is nothing to sit beside, so the chip
        // takes the line from the left instead of floating alone at the right edge.
        const bool alone = (!up[0] && !up2[0]);
        DrawChip(d, ImVec2(alone ? base.x : 0.0f, y + (h - chip_h) * 0.5f), chip, tc, false,
                 alone ? 0.0f : base.x + avail);
    }
    if (has_sub) {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s", subtitle);
        char up[128];
        Shout(sub, up, sizeof(up));
        FitTruncated(up, avail, FontBold(), mfs, MicroTracking());
        MicroCaps(d, ImVec2(base.x, y + fs + S(5.0f)), up,
                  U32(Fade(p.text_faint, p.TextAlphaScale())), mfs, MicroTracking());
        h = FMax(h, fs + S(5.0f) + mfs + S(3.0f));
    }
    (void)has_sub;
    ImGui::Dummy(ImVec2(0.0f, h));
    ImGui::Dummy(ImVec2(0.0f, S(12.0f)));
}

void SidebarRule()
{
    const Palette& p = ActivePalette();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + S(4.0f)),
                                        ImVec2(pos.x + w, pos.y + S(4.0f)),
                                        U32(Fade(p.edge, 0.55f * p.EdgeAlphaScale())), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, S(9.0f)));
}

bool SidebarNav(const char* id, const NavDef* items, int count, int* selected)
{
    if (count <= 0 || !items) return false;
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ta = p.TextAlphaScale();
    const float row_h = S(AM().nav_row_h);
    const float w = ImGui::GetContentRegionAvail().x;
    const float fs = MicroFontSz();
    const float tr = S(1.15f);

    struct Row { ImVec2 a, b; ImGuiID id; bool hov, foc, pressed; };
    Row rows[32];
    IM_ASSERT(count <= 32);

    bool changed = false;
    const int sel = IClamp(*selected, 0, count - 1);

    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        BeginGhost();
        const bool pressed = ImGui::Button("##nav", ImVec2(w, row_h));
        EndGhost();
        rows[i].a = a;
        rows[i].b = ImVec2(a.x + w, a.y + row_h);
        rows[i].id = ImGui::GetItemID();
        rows[i].hov = ImGui::IsItemHovered();
        rows[i].foc = ImGui::IsItemFocused();
        rows[i].pressed = pressed;
        ImGui::PopID();
        if (i + 1 < count) ImGui::Dummy(ImVec2(0.0f, S(3.0f)));
    }

    for (int i = 0; i < count; ++i) {
        const Row& r = rows[i];
        const NavDef& it = items[i];
        const bool is_sel = (i == sel);
        const bool live = it.live && *it.live;
        const float ht = anim::Step(r.id, r.hov ? 1.0f : 0.0f, 17.0f);
        const float st = anim::Step(r.id ^ 0x51F3B3A7u, is_sel ? 1.0f : 0.0f, 15.0f);

        if (ht > 0.01f || st > 0.01f) {
            const ImVec2 qa(r.a.x + S(3.0f), r.a.y + S(1.0f));
            const ImVec2 qb(r.b.x - S(3.0f), r.b.y - S(1.0f));
            const ImVec4 wash = Mix(Fade(p.panel_hover, 0.55f * ht * p.SurfaceAlphaScale()),
                                    Fade(p.accent, (0.10f + 0.20f * ht) * p.SurfaceAlphaScale()), st);
            d->AddRectFilled(qa, qb, U32(wash), p.corner_sm);
            // The selected row is a lit tile, not just a tinted one: a second,
            // brighter gradient at its top edge is what makes it read as raised.
            if (st > 0.02f) {
                GradientFillRounded(d, qa, qb,
                                    Fade(Lift(p, wash, 0.22f), st * 0.9f), Fade(wash, 0.0f),
                                    p.corner_sm, 5);
                StrokeEdge(d, qa, qb, Fade(p.accent, 0.42f * st * p.mat.edge), p.corner_sm);
            }
        }

        const NavStyle& sty = NavSty();
        const float icon_sz = sty.icon_size > 0.0f ? S(sty.icon_size) : S(16.0f);
        const float pad = S(9.0f);
        // Icons-only: the glyph is centred in the row instead of hanging off the
        // left edge of a label that is not there.
        const float lx0 = sty.labels ? r.a.x + pad + icon_sz + S(11.0f) : 0.0f;
        const ImVec2 ic(!sty.labels ? ImVec2((r.a.x + r.b.x) * 0.5f - (sty.trailing && it.trailing ? S(9.0f) : 0.0f),
                                             (r.a.y + r.b.y) * 0.5f)
                                    : ImVec2(r.a.x + pad + icon_sz * 0.5f, (r.a.y + r.b.y) * 0.5f));
        // State has to be legible as a SHAPE before it is legible as a colour, but this
        // is only worth drawing when the row has no label to carry it: in icons-only
        // mode the glyph is the entire row. Labelled rows already say "selected" with
        // the lit tile, the sliding indicator and the accent-tinted text, so drawing a
        // wash behind their icon too just puts a shape in the way of the eye.
        //
        // It used to be a filled CIRCLE at the radius of the whole icon box, plus a
        // ring, which appeared the moment a row was clicked (st is the selection
        // animation) and read as a stray notification blob rather than as state. A
        // rounded square sized to the glyph reads as the button it is sitting in, and
        // it inherits `mat.bloom`, so "polish off" removes it.
        const float halo = (sty.labels ? 0.0f : FMax(st, live ? 0.55f : 0.0f)) * p.mat.bloom;
        if (halo > 0.02f) {
            const float hb = icon_sz * 0.92f;
            const ImVec2 ha(ic.x - hb, ic.y - hb), hz(ic.x + hb, ic.y + hb);
            GradientFillRounded(d, ha, hz,
                                Fade(p.accent, 0.22f * halo * p.SurfaceAlphaScale()),
                                Fade(p.accent_2, 0.07f * halo * p.SurfaceAlphaScale()),
                                p.corner_sm);
            StrokeEdge(d, ha, hz, Fade(p.accent, 0.34f * halo * p.mat.edge), p.corner_sm);
        }
        const ImVec4 ic_col = is_sel ? Mix(p.text, p.accent, 0.42f)
                          : live     ? Mix(p.text_dim, p.accent, 0.45f)
                                     : Mix(p.text_faint, p.text_dim, ht * 0.8f);
        if (it.icon) it.icon(d, ic, icon_sz, U32(Fade(ic_col, ta)));

        char up[64];
        Shout(VisibleLabel(it.label), up, sizeof(up));
        const float lx = lx0;
        // measure the trailing hint first: the label gets whatever is left
        ImFont* mono = FontMono();
        const float vsz = mono ? mono->FontSize * 0.92f : ImGui::GetTextLineHeight() * 0.8f;
        const bool show_trail = sty.trailing && it.trailing && *it.trailing;
        const float trail_w = show_trail
                            ? MeasureTracked(mono, vsz, it.trailing, 0.0f) + S(8.0f) : 0.0f;
        const float room = (r.b.x - pad) - lx - trail_w -
                           ((live && sty.labels) ? S(14.0f) : 0.0f);
        const ImVec4 tx_col = is_sel ? Mix(p.text, p.accent, 0.14f)
                                     : Mix(Mix(p.text_faint, p.text_dim, ht), p.text, live ? 0.5f : 0.0f);
        if (sty.labels) {
            FitTruncated(up, FMax(S(30.0f), room), FontBold(), fs, tr);
            DrawTracked(d, FontBold(), fs, ImVec2(lx, (r.a.y + r.b.y) * 0.5f - fs * 0.5f - S(0.5f)),
                        U32(Fade(tx_col, ta)), up, tr);
        }

        float rx = r.b.x - pad;
        if (show_trail) {
            const float vw = trail_w - S(8.0f);
            const ImVec4 vc = live ? Mix(p.text_faint, p.accent, 0.6f) : p.text_faint;
            if (mono) d->AddText(mono, vsz, ImVec2(rx - vw, (r.a.y + r.b.y) * 0.5f - vsz * 0.5f),
                                 U32(Fade(vc, ta)), it.trailing);
            rx -= vw + S(8.0f);
        }
        if (live && sty.live_dot) {
            const float pulse = 0.72f + 0.28f * anim::Pulse(0.55f);
            // Icons-only has no text column to hang the dot off, so it sits on the
            // icon's corner like a notification badge -- which is the idiom the
            // dot came from anyway.
            const ImVec2 c(sty.labels ? ImVec2(rx - S(2.0f), (r.a.y + r.b.y) * 0.5f)
                                      : ImVec2(ic.x + icon_sz * 0.62f, r.a.y + S(6.0f)));
            d->AddCircleFilled(c, S(2.5f), U32(Fade(p.accent, pulse)), 14);
            d->AddCircle(c, S(4.4f), U32(Fade(p.accent, 0.20f * pulse * p.mat.bloom)), 16, S(1.0f));
        }
        if (r.foc)
            StrokeEdge(d, ImVec2(r.a.x + S(1.0f), r.a.y), ImVec2(r.b.x - S(1.0f), r.b.y),
                       Fade(p.accent, 0.7f), p.corner_sm, S(1.3f), false);
    }

    // The indicator: a 3px accent bar that slides between rows. Its own soft
    // bloom is what makes the selection feel lit rather than marked.
    const float tgt_y = rows[sel].a.y + S(7.0f);
    const float tgt_h = row_h - S(14.0f);
    char ky[160], kh[160];
    snprintf(ky, sizeof(ky), "obs_axiom_nav_%s_y", id);
    snprintf(kh, sizeof(kh), "obs_axiom_nav_%s_h", id);
    const float iy = anim::StepGlobal(ky, tgt_y, 18.0f);
    const float ih = anim::StepGlobal(kh, tgt_h, 18.0f);
    const float iw = S(3.0f);
    const ImVec2 ia(rows[sel].a.x - S(10.0f), iy);
    const ImVec2 ib(ia.x + iw, iy + ih);
    d->AddShadowRect(ia, ib, U32(Fade(p.accent, 0.80f)), S(10.0f), ImVec2(0, 0),
                     ImDrawFlags_RoundCornersAll, iw * 0.5f);
    GradientFillRounded(d, ia, ib, Shade(p.accent, 0.20f), p.accent_2, iw * 0.5f, 8);

    for (int i = 0; i < count; ++i)
        if (rows[i].pressed && i != *selected) { *selected = i; changed = true; }

    ImGui::PopID();
    return changed;
}

void SidebarFooter(float height)
{
    const float want = S(height) + S(14.0f);
    const float target = ImGui::GetWindowHeight() - want - ImGui::GetStyle().WindowPadding.y;
    if (target > ImGui::GetCursorPosY())
        ImGui::SetCursorPosY(target);
}

void RailStat(const char* key, const char* value, const ImVec4* value_col)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const float ta = p.TextAlphaScale();
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float fs = MicroFontSz() * 0.94f;

    MicroCaps(d, ImVec2(pos.x, pos.y + (line_h - fs) * 0.5f), key,
              U32(Fade(p.text_faint, ta)), fs);

    ImFont* mono = FontMono();
    const float vsz = mono ? mono->FontSize * 0.96f : line_h;
    // The key is six tracked caps and does not shrink, so the value is fitted into
    // what is left of it -- right-aligned strings have a habit of walking over the
    // label they describe once a Windows build number or a base address is longer
    // than the card expects.
    char vbuf[96];
    snprintf(vbuf, sizeof(vbuf), "%s", value ? value : "");
    const float room = FMax(S(24.0f), w - MeasureMicroCaps(key, fs) - S(14.0f));
    FitTruncated(vbuf, room, mono ? mono : ImGui::GetFont(), vsz, 0.0f);
    const float vw = TextWidth(mono, vbuf);
    const ImVec4 vc = value_col ? *value_col : Mix(p.text_dim, p.text, 0.55f);
    if (mono) d->AddText(mono, vsz, ImVec2(pos.x + w - vw, pos.y + (line_h - vsz) * 0.5f),
                         U32(Fade(vc, ta)), vbuf);
    else      d->AddText(ImVec2(pos.x + w - vw, pos.y), U32(Fade(vc, ta)), vbuf);
    ImGui::Dummy(ImVec2(w, line_h + S(1.0f)));
}

bool RailButton(const char* label, const ImVec2& size, ButtonKind kind)
{
    const float w = size.x > 0.0f ? S(size.x) : ImGui::GetContentRegionAvail().x;
    const float h = size.y > 0.0f ? S(size.y) : 0.0f;
    return Button(label, ImVec2(w, h), kind);
}

// =============================================================================
//  Card grid
// =============================================================================
void GridBegin(const GridCfg& cfg)
{
    GridCtx& g = G();
    g = GridCtx();
    g.active  = true;
    g.cols    = cfg.columns > 0 ? cfg.columns : 1;
    g.gutter = S(cfg.gutter  < 0.0f ? AM().gutter   : cfg.gutter);
    g.row_h  = S(cfg.row_height < 0.0f ? AM().card_h : cfg.row_height);
    g.pad    = S(cfg.pad      < 0.0f ? AM().card_pad : cfg.pad);
    const float avail = ImGui::GetContentRegionAvail().x;
    g.cell_w  = (avail - g.gutter * (float)(g.cols - 1)) / (float)g.cols;
}

void GridEnd()
{
    GridCtx& g = G();
    g.active = false;
    g.have_pending = false;
}

int GridColumnsLeft()
{
    GridCtx& g = G();
    return g.active ? g.cols - g.col : 1;
}

void GridCell(int span, float height)
{
    GridCtx& g = G();
    if (!g.active) {
        // Outside a grid a card owns the whole content width at whatever height it
        // asked for. A page that mixes plain columns with a stat card or a note
        // relies on this -- ignoring the request would silently shrink those cards
        // to the default row height and clip their contents.
        g.have_pending = true;
        g.pending = ImVec2(FMax(1.0f, ImGui::GetContentRegionAvail().x),
                           height > 0.0f ? height : S(AM().card_h));
        return;
    }

    int sp = span < 1 ? 1 : (span > g.cols ? g.cols : span);
    if (g.col >= g.cols) g.col = 0;                 // wrapped already: new row
    if (g.col > 0) {
        if (g.col + sp > g.cols) sp = g.cols - g.col;
        ImGui::SameLine(0.0f, g.gutter);
    }
    const float w = g.cell_w * (float)sp + g.gutter * (float)(sp - 1);
    const float h = height > 0.0f ? height : g.row_h;   // height is in pixels
    g.have_pending = true;
    g.pending = ImVec2(w, h);
    g.col += sp;
    if (g.col >= g.cols) g.col = 0;
    if (g.col != 0)      ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// =============================================================================
//  Card surface
// =============================================================================
bool CardBegin(const char* id, const ImVec2& size, CardFlags flags)
{
    GridCtx& g = G();
    ImVec2 sz = g.have_pending ? g.pending
                               : ImVec2(ImGui::GetContentRegionAvail().x, S(AM().card_h));
    if (size.x > 0.0f) sz.x = size.x;
    if (size.y > 0.0f) sz.y = size.y;
    g.have_pending = false;
    const float pad = g.active ? g.pad : S(AM().card_pad);

    const Palette& p = ActivePalette();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + sz.x, a.y + sz.y);
    const float r = FMin(p.corner, sz.y * 0.5f);

    CardCtx c;
    c.a = a; c.b = b; c.pad = pad; c.r = r;
    c.hov_id = 0;
    c.hoverable = (flags & Card_Hoverable) != 0;
    c.hovered = false; c.clicked = false;
    c.live = (flags & Card_Live) != 0;
    CardStack().push_back(c);

    // The bloom has to be painted on the CONTAINER's draw list (a child's list is
    // clipped to the child rect, which would slice the glow off at the card
    // edge). Holding the pointer across BeginChild is what lets us emit it *after*
    // hover is known while still landing behind the card's own surface.
    ImDrawList* outer = ImGui::GetWindowDrawList();

    ImGui::PushID(id);
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        (flags & Card_NoPad) ? ImVec2(0.0f, 0.0f) : ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(4.0f)));
    ImGui::BeginChild("##card", sz, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoSavedSettings);

    const bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    TopCard()->hovered = hov;
    TopCard()->hov_id  = ImGui::GetID("##card");

    // A hoverable card IS its button: no child control may be in here, or the
    // first-submitted item would steal that control's hover.
    if (c.hoverable) {
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        BeginGhost();
        const bool pressed = ImGui::Button("##hit", sz);
        EndGhost();
        TopCard()->clicked = pressed;
        TopCard()->hov_id = ImGui::GetItemID();
    }

    const float ht = anim::Step(TopCard()->hov_id, hov ? 1.0f : 0.0f, 17.0f);
    const float on = anim::Step(TopCard()->hov_id ^ 0x9E3779B9u, c.live ? 1.0f : 0.0f, 13.0f);

    if (on > 0.02f || ht > 0.02f) {
        const float ga = (0.26f * on + 0.12f * ht) * p.SurfaceAlphaScale()
                       * p.mat.bloom * p.mat.lift;
        outer->AddShadowRect(a, b, U32(Fade(p.accent, ga)), S(16.0f) * p.mat.bloom,
                             ImVec2(0, S(3.0f)), ImDrawFlags_RoundCornersAll, r);
    }
    // Screen space, always: a child window's draw list is not translated, so the
    // surface has to use the card's screen rect. Painting it at (0,0)-sz would put
    // every card's background in the top-left corner of the display, where the
    // child's clip rect hides it -- surfaces would silently vanish.
    PaintCardSurface(a, b, r, flags, ht, on);

    // A hero card is a page's master switch: it is the one card that is allowed to
    // be lit from the accent while it is ON, so it gets a gradient rule across its
    // top edge and a wider bloom than a setting card.
    if (flags & Card_Hero) {
        const float k = FMax(0.35f, on);
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->PushClipRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, a.y + S(3.0f)), true);
        GradientFillRoundedH(d, a, ImVec2(b.x, a.y + S(2.0f)),
                             Fade(p.accent, 0.06f * k), Fade(p.accent_2, 0.9f * k), r, 12);
        d->PopClipRect();
        ShimmerBar(d, a, ImVec2(b.x, a.y + S(2.0f)), 1.0f, p, k, anim::Clock() * 0.15f);
    }

    if (!(flags & Card_NoPad))
        ImGui::SetCursorPos(ImVec2(pad, (sz.y - S(ImGui::GetTextLineHeight())) * 0.5f));
    ImGui::PushItemWidth(FMax(1.0f, sz.x - pad * 2.0f));
    return true;
}

void CardEnd()
{
    if (CardStack().empty()) return;
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::PopID();
    CardStack().pop_back();
}

ImVec2 CardOrigin()
{
    const CardCtx* c = TopCard();
    return c ? c->a : ImGui::GetCursorScreenPos();
}
ImVec2 CardInner()
{
    const CardCtx* c = TopCard();
    return c ? ImVec2(c->a.x + c->pad, c->a.y + c->pad) : ImGui::GetCursorScreenPos();
}
float CardInnerWidth()
{
    const CardCtx* c = TopCard();
    return c ? (c->b.x - c->a.x) - c->pad * 2.0f : ImGui::GetContentRegionAvail().x;
}
float CardHeight()
{
    const CardCtx* c = TopCard();
    return c ? (c->b.y - c->a.y) : 0.0f;
}
bool CardHovered() { const CardCtx* c = TopCard(); return c && c->hovered; }
bool CardClicked() { const CardCtx* c = TopCard(); return c && c->clicked; }

float CardMicroY(float line_h)
{
    const CardCtx* c = TopCard();
    if (!c) return ImGui::GetCursorScreenPos().y;
    return c->a.y + (CardHeight() - line_h) * 0.5f;
}

void CardTitle(const char* label, float trailing_w, bool top)
{
    const Palette& p = ActivePalette();
    CardCtx* c = TopCard();
    if (!c || !label) return;
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float fs = MicroFontSz();
    const float tr = MicroTracking();
    const float inner_w = (c->b.x - c->a.x) - c->pad * 2.0f;
    const float room = FMax(S(24.0f), inner_w - trailing_w - S(10.0f));
    const float y = top ? (c->a.y + c->pad - S(1.0f))
                        : (c->a.y + (CardHeight() - fs) * 0.5f - S(1.0f));

    char up[128];
    Shout(VisibleLabel(label), up, sizeof(up));
    FitTruncated(up, room, FontBold(), fs, tr);

    const ImVec4 col = c->live ? Mix(p.text_dim, p.text, 0.55f)
                               : Mix(p.text_faint, p.text_dim, c->hovered ? 0.55f : 0.10f);
    DrawTracked(d, FontBold(), fs, ImVec2(c->a.x + c->pad, y),
                U32(Fade(col, p.TextAlphaScale())), up, tr);
}

void CardKeyValue(const char* key, const char* value, const ImVec4* tint)
{
    const Palette& p = ActivePalette();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ta = p.TextAlphaScale();

    // Keys are labels, not prose: same tracked caps the rail's key/value rows use,
    // so one ledger reads like the other. (Values stay exactly as given -- they are
    // data, and shouting "0X00007FF6A1C20000" would be worse, not better.)
    const float kfs = MicroFontSz() * 0.94f;
    MicroCaps(d, ImVec2(pos.x, pos.y + (line_h - kfs) * 0.5f), key,
              U32(Fade(p.text_faint, ta)), kfs);
    ImFont* mono = FontMono();
    const float vsz = mono ? mono->FontSize * 0.98f : line_h;
    const float vw = TextWidth(mono, value);
    const ImVec4 vc = tint ? *tint : Mix(p.text_dim, p.text, 0.65f);
    if (mono) d->AddText(mono, vsz, ImVec2(pos.x + w - vw, pos.y + (line_h - vsz) * 0.5f),
                         U32(Fade(vc, ta)), value);
    else      d->AddText(ImVec2(pos.x + w - vw, pos.y), U32(Fade(vc, ta)), value);

    const float kx = pos.x + MeasureMicroCaps(key, kfs) + S(8.0f);
    const float vx = pos.x + w - vw - S(8.0f);
    if (vx - kx > S(20.0f)) {
        const float y = pos.y + line_h * 0.60f;
        for (float x = kx; x < vx; x += S(5.0f))
            d->AddRectFilled(ImVec2(x, y), ImVec2(x + S(1.3f), y + S(1.3f)),
                             U32(Fade(p.text_faint, 0.40f * ta)));
    }
    ImGui::Dummy(ImVec2(w, line_h));
}

void CardText(const char* text, bool dim)
{
    const Palette& p = ActivePalette();
    ImGui::PushStyleColor(ImGuiCol_Text,
                          Fade(dim ? p.text_faint : p.text_dim, p.TextAlphaScale()));
    // 0.0f means "wrap at this window's content right edge". Do NOT pass a screen x
    // here: PushTextWrapPos adds window->Pos.x to its argument, so the "obvious"
    // cursor + avail version lands a whole window-width to the right of the card --
    // which reads as text that never wraps, runs to the card's edge and is clipped
    // there. Every hint and note in the shell was quietly one clipped line.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void CardSpace(float h) { ImGui::Dummy(ImVec2(0.0f, S(h))); }

// =============================================================================
//  Card-shaped settings
// =============================================================================
bool ToggleCard(const char* label, bool* v, const char* hint, int span)
{
    if (!v) return false;
    const bool before = *v;
    GridCell(span);
    CardBegin(label, ImVec2(0, 0), (CardFlags)(Card_Hoverable | (*v ? Card_Live : 0)));

    CardCtx* c = TopCard();
    if (c && c->clicked) *v = !*v;
    if (c) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const Palette& p = ActivePalette();
        const float pw = S(AM().pill_w), ph = S(AM().pill_h);
        const bool two_line = (hint && *hint);
        // With a hint the card goes two-line -- label on top, explanation under it.
        // Squeezing "label ... hint" onto one line is how a switch card ends up
        // with its text running under the pill at narrow widths.
        CardTitle(label, pw + S(6.0f), two_line);
        if (two_line) {
            const float inner_w = (c->b.x - c->a.x) - c->pad * 2.0f;
            ImFont* f = FontBold();
            const float hfs = f ? f->FontSize * 0.74f : S(9.5f);
            char buf[192];
            snprintf(buf, sizeof(buf), "%s", hint);
            FitTruncated(buf, FMax(S(30.0f), inner_w - pw - S(14.0f)), f, hfs, 0.0f);
            const float ty = c->a.y + c->pad + MicroFontSz() + S(4.0f);
            if (f) d->AddText(f, hfs, ImVec2(c->a.x + c->pad, ty),
                              U32(Fade(p.text_faint, p.TextAlphaScale())), buf);
            else   d->AddText(ImVec2(c->a.x + c->pad, ty),
                              U32(Fade(p.text_faint, p.TextAlphaScale())), buf);
        }
        const float on = anim::Step(c->hov_id ^ 0x2545F491u, *v ? 1.0f : 0.0f, 18.0f);
        const float hv = anim::Step(c->hov_id, c->hovered ? 1.0f : 0.0f, 17.0f);
        PaintPill(d, ImVec2(c->b.x - c->pad - pw, c->a.y + (CardHeight() - ph) * 0.5f - S(0.5f)),
                  *v, on, hv);
    }
    CardEnd();
    return (*v != before);
}

bool SliderCard(const char* label, float* v, float vmin, float vmax,
                const char* fmt, float power, float default_value, bool has_default,
                bool live, int span)
{
    GridCell(span, S(AM().card_h + 20.0f));     // taller row: title line + track
    CardBegin(label, ImVec2(0, 0), live ? Card_Live : Card_None);
    CardCtx* c = TopCard();

    char vbuf[64];
    snprintf(vbuf, sizeof(vbuf), fmt ? fmt : "%.0f", (double)*v);

    if (c) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        ImFont* mono = FontMono();
        const float vsz = mono ? mono->FontSize : ImGui::GetTextLineHeight();
        const float vw = TextWidth(mono, vbuf);
        const float iw = (c->b.x - c->a.x) - c->pad * 2.0f;
        const ImVec2 vp(c->a.x + c->pad + iw - vw, c->a.y + c->pad - S(1.0f));
        const Palette& p = ActivePalette();
        const ImVec4 vc = Mix(p.text_dim, p.accent, c->hovered ? 0.85f : 0.55f);
        if (mono) d->AddText(mono, vsz, vp, U32(Fade(vc, p.TextAlphaScale())), vbuf);
        else      d->AddText(vp, U32(Fade(vc, p.TextAlphaScale())), vbuf);
        CardTitle(label, vw + S(10.0f), true);
        ImGui::SetCursorPos(ImVec2(c->pad, c->pad + S(21.0f)));
    }

    SliderOpts o;
    o.show_label = false;
    o.show_value = false;
    o.power = power;
    const bool changed = Slider("##s", v, vmin, vmax, fmt, 0.0f,
                                has_default ? default_value : NAN, o);
    CardEnd();
    return changed;
}

bool SliderCardInt(const char* label, int* v, int vmin, int vmax,
                   const char* fmt, int default_value, int span)
{
    if (!v) return false;
    float f = (float)*v;
    const bool ch = SliderCard(label, &f, (float)vmin, (float)vmax,
                               fmt ? fmt : "%d", 1.0f, (float)default_value, true,
                               false, span);
    if (ch) {
        int iv = (int)std::lround(f);
        iv = iv < vmin ? vmin : (iv > vmax ? vmax : iv);
        if (iv != *v) { *v = iv; return true; }
    }
    return false;
}

bool ComboCard(const char* label, int* current, const char* const* items, int count, int span)
{
    GridCell(span);
    CardBegin(label);
    CardCtx* c = TopCard();

    float widest = 0.0f;
    for (int i = 0; i < count; ++i) widest = FMax(widest, TextWidth(nullptr, items[i]));
    const float iw = CardInnerWidth();
    float bw = FMin(widest + S(38.0f), iw * 0.62f);
    bw = FMax(bw, S(80.0f));

    CardTitle(label, bw + S(8.0f));

    const Palette& p = ActivePalette();
    if (c) {
        const float bh = S(23.0f);
        ImGui::SetCursorPos(ImVec2((c->b.x - c->a.x) - c->pad - bw,
                                   (CardHeight() - bh) * 0.5f - S(1.0f)));
    }
    ImGui::PushItemWidth(bw);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        Fade(p.field, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Fade(p.field_hover, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  Fade(p.field_active, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,           Fade(Mix(p.text_dim, p.text, 0.7f), p.TextAlphaScale()));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(S(8.0f), S(3.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, p.corner_sm);
    const char* preview = (*current >= 0 && *current < count) ? items[*current] : "";
    bool changed = false;
    // NoArrowButton: the vendored ImGui draws a +/- expander of its own, and this card
    // already has a chevron that lights with the hover. Two arrows, one field.
    if (ImGui::BeginCombo("##cb", preview, ImGuiComboFlags_NoArrowButton)) {
        for (int i = 0; i < count; ++i) {
            const bool sel = (i == *current);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
            if (ImGui::Selectable(items[i], sel)) { *current = i; changed = true; }
            if (sel) { ImGui::PopStyleColor(); ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    const ImVec2 ra = ImGui::GetItemRectMin(), rb = ImGui::GetItemRectMax();
    const bool hot = ImGui::IsItemHovered();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    ImGui::PopItemWidth();
    if (c) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        StrokeEdge(d, ra, rb, Mix(Fade(p.edge, 0.75f), Fade(p.accent, 0.85f), hot ? 1.0f : 0.0f),
                   p.corner_sm);
        icon::Chevron(d, ImVec2(rb.x - S(12.0f), (ra.y + rb.y) * 0.5f), S(10.0f),
                      U32(Fade(Mix(p.text_faint, p.accent, hot ? 0.9f : 0.35f), p.TextAlphaScale())),
                      icon::Down, S(1.4f));
    }
    CardEnd();
    return changed;
}

bool ButtonCard(const char* label, const char* button_text, ButtonKind kind,
                const char* value_text, int span)
{
    GridCell(span);
    CardBegin(label);
    CardCtx* c = TopCard();
    const float bw = FMax(S(80.0f), TextWidth(FontBold(), button_text) + S(26.0f));
    const bool has_value = (value_text && *value_text);
    // Reserve exactly what the value needs (measured, then truncated to fit) so a
    // long value can never slide under the label.
    char vbuf[128];
    vbuf[0] = '\0';
    float vw = 0.0f;
    ImFont* mono = FontMono();
    const float vsz = mono ? mono->FontSize : ImGui::GetTextLineHeight();
    if (has_value && c) {
        snprintf(vbuf, sizeof(vbuf), "%s", value_text);
        // The label truncates itself inside CardTitle, so the value only has to
        // avoid the button plus a reasonable label budget -- otherwise a value
        // like "2 saved" and the label would draw over each other.
        const float inner_w = (c->b.x - c->a.x) - c->pad * 2.0f;
        char lab[128];
        snprintf(lab, sizeof(lab), "%s", VisibleLabel(label));
        Shout(lab, lab, sizeof(lab));
        const float room = FMax(S(24.0f), inner_w - bw - S(16.0f) -
                                         MeasureTracked(FontBold(), MicroFontSz(), lab,
                                                        MicroTracking()));
        if (mono) FitTruncated(vbuf, room, mono, vsz, 0.0f);
        vw = TextWidth(mono, vbuf);
    }
    CardTitle(label, bw + (has_value ? vw + S(14.0f) : S(8.0f)));

    bool pressed = false;
    if (c) {
        if (has_value) {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const Palette& p = ActivePalette();
            const ImVec2 vp(c->b.x - c->pad - bw - S(12.0f) - vw,
                            c->a.y + (CardHeight() - vsz) * 0.5f);
            if (mono) d->AddText(mono, vsz, vp, U32(Fade(Mix(p.text_dim, p.text, 0.5f),
                                                          p.TextAlphaScale())), vbuf);
            else      d->AddText(vp, U32(Fade(p.text_dim, p.TextAlphaScale())), vbuf);
        }
        const float bh = S(23.0f);
        ImGui::SetCursorPos(ImVec2((c->b.x - c->a.x) - c->pad - bw, (CardHeight() - bh) * 0.5f - S(1.0f)));
        pressed = Button(button_text, ImVec2(bw, bh), kind);
    }
    CardEnd();
    return pressed;
}

bool KeyCard(const char* label, const char* key_name, bool waiting, bool live, int span)
{
    GridCell(span);
    // waiting owns the keycap (accent fill, "press a key"), so `live` is expressed on
    // the card alone - the same accent edge a toggle card gets while its switch is on.
    CardBegin(label, ImVec2(0, 0),
              (CardFlags)(((waiting || live) ? Card_Live : 0) | Card_Hoverable));
    CardCtx* c = TopCard();
    const float bw = S(108.0f);
    CardTitle(label, bw + S(8.0f));
    bool clicked = false;
    if (c) {
        // The card owns the click, so the key "button" is painted, not submitted:
        // an interactive widget inside a Card_Hoverable would lose the hover race
        // to the card's own hit button.
        clicked = c->clicked;
        {
            const float bh = S(23.0f);
            ImDrawList* d = ImGui::GetWindowDrawList();
            const Palette& p = ActivePalette();
            const ImVec2 a(c->b.x - c->pad - bw, c->a.y + (CardHeight() - bh) * 0.5f - S(1.0f));
            const ImVec2 b(a.x + bw, a.y + bh);
            const char* txt = waiting ? "press a key"
                            : (key_name && *key_name ? key_name : "unbound");
            char up[64];
            Shout(txt, up, sizeof(up));
            const float fs = MicroFontSz() * 0.94f;
            const float tw = MeasureTracked(FontBold(), fs, up, S(0.9f));
            const ImVec4 tc = waiting ? p.text_on_accent : Mix(p.text_dim, p.text, 0.5f);
            d->AddRectFilled(a, b, U32(Fade(waiting ? p.accent : p.field,
                                              p.SurfaceAlphaScale() * (waiting ? 0.95f : 0.8f))),
                             p.corner_sm);
            StrokeEdge(d, a, b, waiting ? Fade(Shade(p.accent, 0.3f), 0.9f)
                                        : Fade(p.edge, 0.8f), p.corner_sm);
            if (waiting)
                d->AddShadowRect(a, b, U32(Fade(p.accent, 0.45f)), S(10.0f), ImVec2(0, 0),
                                 ImDrawFlags_RoundCornersAll, p.corner_sm);
            DrawTracked(d, FontBold(), fs, ImVec2(a.x + (bw - tw) * 0.5f, a.y + (bh - fs) * 0.5f - S(0.5f)),
                        U32(Fade(tc, p.TextAlphaScale())), up, S(0.9f));
        }
    }
    CardEnd();
    return clicked;
}

bool ColorCard(const char* label, float col[4], bool has_alpha, int span)
{
    GridCell(span);
    CardBegin(label);
    CardCtx* c = TopCard();
    const float bw = S(48.0f), bh = S(20.0f);
    CardTitle(label, bw + S(8.0f));
    bool changed = false;
    if (c) {
        const Palette& p = ActivePalette();
        ImGui::SetCursorPos(ImVec2((c->b.x - c->a.x) - c->pad - bw, (CardHeight() - bh) * 0.5f - S(1.0f)));
        ImGui::PushItemWidth(bw);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(S(6.0f), S(2.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  p.corner_sm);
        ImGui::PushStyleColor(ImGuiCol_Border, Fade(p.edge, 0.85f));
        ImGuiColorEditFlags fl = (ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoOptions |
                                   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf |
                                   ImGuiColorEditFlags_PickerHueBar);
        if (!has_alpha) fl |= ImGuiColorEditFlags_NoAlpha;
        changed = ImGui::ColorEdit4("##c", col, fl);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::PopItemWidth();
        ImDrawList* d = ImGui::GetWindowDrawList();
        StrokeEdge(d, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                   Fade(p.edge, ImGui::IsItemHovered() ? 1.0f : 0.75f), p.corner_sm);
    }
    CardEnd();
    return changed;
}

void StatCard(const char* title, const char* const* keys, const char* const* values,
              int count, const ImVec4* tints, int span)
{
    if (count <= 0) return;
    const float h = S(AM().card_pad) * 2.0f + ImGui::GetTextLineHeight() + S(2.0f)
                  + (float)count * (ImGui::GetTextLineHeight() + S(1.0f))
                  + (title && *title ? S(16.0f) : 0.0f);
    GridCell(span < 1 ? 1 : span, h);
    CardBegin(title && *title ? title : "stat", ImVec2(0, 0), Card_Inset | Card_NoPad);
    CardCtx* c = TopCard();
    if (c) {
        ImGui::SetCursorPos(ImVec2(c->pad, c->pad - S(2.0f)));
        if (title && *title) {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const Palette& p = ActivePalette();
            const float fs = MicroFontSz() * 0.94f;
            MicroCaps(d, ImVec2(c->a.x + c->pad, c->a.y + S(4.0f)), title,
                      U32(Fade(p.text_faint, p.TextAlphaScale())), fs);
            ImGui::SetCursorPos(ImVec2(c->pad, c->pad + S(14.0f)));
        }
        for (int i = 0; i < count; ++i)
            CardKeyValue(keys[i], values[i], tints ? &tints[i] : nullptr);
    }
    CardEnd();
}

void NoteCard(const char* title, const char* body)
{
    // The width the body will wrap at is the card's own, and the card's width is
    // whatever cell GridCell just queued -- not cell_w * 2, which is a guess about
    // the grid having two columns. Get it wrong and the height measured for the text
    // is shorter than the height it draws at, and the tail of the note falls off the
    // bottom of its own card, which reads as a truncated sentence.
    const float card_w = G().have_pending ? G().pending.x : ImGui::GetContentRegionAvail().x;
    // One inset, not two: this card is Card_NoPad, so its child's content area runs
    // from the left pad to the right edge, which is what the body below wraps at. The
    // height has to be measured at that same width or the last line falls off the card.
    const float inner_w = FMax(S(60.0f), card_w) - S(AM().card_pad);
    const float wrap = FMax(S(80.0f), inner_w);
    const ImVec2 ts = ImGui::CalcTextSize(body, nullptr, false, wrap);
    const float h = S(AM().card_pad) * 2.0f + ts.y + (title && *title ? S(18.0f) : S(0.0f));
    GridCell(2, h);
    CardBegin(title && *title ? title : "note", ImVec2(0, 0), Card_Inset | Card_NoPad);
    CardCtx* c = TopCard();
    if (c) {
        float y = c->pad;
        if (title && *title) {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const Palette& p = ActivePalette();
            MicroCaps(d, ImVec2(c->a.x + c->pad, c->a.y + c->pad - S(2.0f)), title,
                      U32(Fade(p.warn, 0.9f * p.TextAlphaScale())), 0.0f);
            y += S(18.0f);
        }
        ImGui::SetCursorPos(ImVec2(c->pad, y));
        CardText(body, true);
    }
    CardEnd();
}

bool ScrollCardBegin(const char* title, float height)
{
    GridCell(2, height);
    CardBegin(title && *title ? title : "scroll", ImVec2(0, 0), Card_Inset | Card_NoPad);
    CardCtx* c = TopCard();
    if (!c) return false;
    float top = c->pad;
    if (title && *title) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const Palette& p = ActivePalette();
        MicroCaps(d, ImVec2(c->a.x + c->pad, c->a.y + S(5.0f)), title,
                  U32(Fade(p.text_faint, p.TextAlphaScale())), 0.0f);
        top = c->pad + S(17.0f);
    }
    ImGui::SetCursorPos(ImVec2(c->pad - S(2.0f), top));
    const ImVec2 sz((c->b.x - c->a.x) - (c->pad - S(2.0f)) * 2.0f,
                    (c->b.y - c->a.y) - top - c->pad);
    const Palette& p = ActivePalette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Fade(Shade(p.field, -0.10f), 0.55f * p.SurfaceAlphaScale()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(9.0f), S(7.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(3.0f)));
    ImGui::BeginChild("##scroll", sz, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    return true;
}

void ScrollCardEnd()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    CardEnd();
}

// =============================================================================
//  Page furniture
// =============================================================================
void PageTitle(const char* title, const char* subtitle, const char* chip, const ImVec4* chip_tint)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImFont* tf = FontTitle();
    const float fs = tf ? tf->FontSize * 1.16f : S(19.0f);
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float line_h = ImGui::GetTextLineHeight();

    float used = S(2.0f);
    float title_w = 0.0f;
    if (title && *title) {
        char up[96];
        Shout(title, up, sizeof(up));
        title_w = DrawTracked(d, tf, fs, ImVec2(pos.x, pos.y + S(2.0f)),
                              U32(Fade(p.text, p.TextAlphaScale())), up, S(2.0f));
        used = fs + S(2.0f);
    }

    const float cw = (chip && *chip) ? ChipWidth(chip) : 0.0f;
    if (chip && *chip) {
        const ImVec4 tc = chip_tint ? *chip_tint : p.accent;
        DrawChip(d, ImVec2(0, pos.y + S(1.0f)), chip, tc, false, pos.x + avail);
    }

    if (subtitle && *subtitle) {
        const float sy = pos.y + used + S(6.0f);
        const float wrap_w = FMax(S(60.0f), avail - (chip ? cw + S(14.0f) : 0.0f));
        // keep the subtitle clear of the chip on the same line
        if (chip && title_w + S(20.0f) + cw < avail) {
            // Same line as the chip, so the subtitle is the flexible one: it gets
            // the space between the title and the chip and is truncated there. The
            // alternative is a sentence sliding under a status pill, which reads as
            // an overlap bug on every page whose hint is longer than the title.
            char sub[192];
            snprintf(sub, sizeof(sub), "%s", subtitle);
            ImFont* sf = ImGui::GetFont();
            FitTruncated(sub, FMax(S(40.0f), avail - title_w - S(20.0f) - cw - S(14.0f)),
                         sf, sf->FontSize, 0.0f);
            d->AddText(ImVec2(pos.x + title_w + S(20.0f), pos.y + (fs - line_h) * 0.5f + S(3.0f)),
                       U32(Fade(p.text_faint, p.TextAlphaScale())), sub);
            used = fs + S(2.0f);
        } else {
            d->PushClipRect(ImVec2(pos.x, sy - S(2.0f)),
                            ImVec2(pos.x + wrap_w, sy + line_h * 2.0f), true);
            d->AddText(ImVec2(pos.x, sy), U32(Fade(p.text_faint, p.TextAlphaScale())), subtitle);
            d->PopClipRect();
            used = used + S(6.0f) + line_h;
        }
    }
    // The lit rule under a page title. It is the one place in a page where the
    // accent is allowed to be decoration rather than state -- a header is already
    // telling you which page you are on, so the rule carries the theme's colour
    // instead of a meaning, and it is the surface that shows off the shimmer: a
    // highlight travelling along it is what makes a wide header read as glass with
    // light behind it rather than as a caption.
    {
        const float ry = pos.y + used + S(7.0f);
        const float rw = FMin(avail, FMax(S(120.0f), title_w + S(28.0f)));
        GradientFillRoundedH(d, ImVec2(pos.x, ry), ImVec2(pos.x + rw, ry + S(1.6f)),
                             Fade(p.accent, 0.75f), Fade(p.accent_2, 0.0f), 0.0f, 10);
        if (p.mat.shimmer > 0.002f)
            ShimmerBar(d, ImVec2(pos.x, ry), ImVec2(pos.x + rw, ry + S(1.6f)), 1.0f, p,
                       1.4f, anim::Clock() * 0.18f);
        used += S(11.0f);
    }

    ImGui::Dummy(ImVec2(avail, used + S(13.0f)));
}

float GroupLabelHeight()
{
    return ImGui::GetTextLineHeight() + S(7.0f);
}

// The shared painter for both forms of the rule: micro-caps label on the left, a
// hairline that fades out to the right, and (for the interactive one) a chevron and
// a summary read-out. Kept in one function because a group divider that only
// half-matches its collapsible twin is exactly the kind of 1px drift that makes a
// menu look patched rather than designed.
static float GroupLabelImpl(const char* id, const char* text, bool* open,
                            const char* summary, float hit_w)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float fs = MicroFontSz() * 0.92f;
    const float tr = S(1.35f);
    const float line_h = ImGui::GetTextLineHeight();
    const float h = line_h + S(7.0f);

    const bool interactive = (id && open);
    const float chev = interactive ? S(13.0f) : 0.0f;
    const float x_text = pos.x + chev;

    // The rule starts after the widest of (label, summary): the label is the name
    // of the group and the summary is its state, and neither may be crossed by a
    // line that thinks it is decoration.
    float tw = 0.0f;
    float sum_w = 0.0f;
    if (interactive) {
        // A ghost button over the whole row: the rule is a control, so it gets
        // hover, focus and the click-through for free instead of a hand-rolled
        // IsMouseHoveringRect that forgets the scroll parent.
        ImGui::PushID(id);
        BeginGhost();
        const bool pressed = ImGui::Button("##tgl", ImVec2(FMax(1.0f, avail - hit_w), h));
        EndGhost();
        const ImGuiID hid = ImGui::GetItemID();
        const bool hov = ImGui::IsItemHovered();
        if (pressed) *open = !*open;
        const float ht = anim::Step(hid, hov ? 1.0f : 0.0f, 18.0f);
        // The chevron is the state, so it turns with it -- and the accent only ever
        // means "something is live in here", which for a group means open.
        const float ang = anim::Step(hid ^ 0x5EEDu, *open ? 1.0f : 0.0f, 20.0f);
        const ImVec2 cc(pos.x + chev * 0.5f, pos.y + line_h * 0.5f);
        const float cs = chev * 0.42f;
        const ImU32 icol = U32(Fade(Mix(p.text_faint, p.accent, *open ? 0.55f : 0.0f),
                                    p.TextAlphaScale()));
        // A chevron that rotates by drawing its two arms at an eased angle: down
        // when closed, up when open, and a flat line halfway, which is the shape
        // both states agree on.
        const float k = -1.0f + 2.0f * ang;
        d->AddLine(ImVec2(cc.x - cs, cc.y + cs * k * 0.5f - cs * 0.35f),
                   ImVec2(cc.x, cc.y - cs * 0.55f * k), icol, S(1.4f));
        d->AddLine(ImVec2(cc.x, cc.y - cs * 0.55f * k),
                   ImVec2(cc.x + cs, cc.y + cs * k * 0.5f - cs * 0.35f), icol, S(1.4f));
        if (ht > 0.01f) {
            d->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + avail, pos.y + h), true);
            d->AddRectFilled(ImVec2(pos.x - S(6.0f), pos.y), ImVec2(pos.x + avail, pos.y + h),
                             U32(Fade(p.panel_hover, 0.35f * ht * p.SurfaceAlphaScale())), p.corner_sm);
            d->PopClipRect();
        }
        // The label is drawn after the row wash so the highlight sits behind it.
        tw = MicroCaps(d, ImVec2(x_text, pos.y + (line_h - fs) * 0.5f), text,
                       U32(Fade(Mix(p.text_faint, p.text, hov * 0.55f + (*open ? 0.15f : 0.0f)),
                                p.TextAlphaScale() * 0.95f)), fs, tr) + chev;
        // Popped here rather than at the end of the scope: the Dummy that reserves
        // the row's height submits an item, and an item still inside this scope would
        // be given the toggle's id -- "same id" errors are how a collapsible header
        // eats its own row.
        ImGui::PopID();
    } else {
        tw = MicroCaps(d, ImVec2(x_text, pos.y + (line_h - fs) * 0.5f), text,
                       U32(Fade(p.text_faint, p.TextAlphaScale() * 0.95f)), fs, tr);
        tw += x_text - pos.x;
    }

    const ImFont* mono = FontMono();
    const float vsz = mono ? mono->FontSize * 0.88f : line_h * 0.8f;
    if (summary && *summary)
        sum_w = TextWidth((ImFont*)mono, summary) + S(12.0f);

    const float ry = pos.y + line_h * 0.5f + S(0.5f);
    const float x0 = pos.x + FMax(tw, S(24.0f)) + S(11.0f);
    const float x1 = pos.x + avail - sum_w;
    if (x1 - x0 > S(24.0f)) {
        const ImU32 c0 = U32(Fade(p.edge, 0.80f * p.EdgeWeight()));
        const ImU32 c1 = U32(Fade(p.edge, 0.0f));
        d->AddRectFilledMultiColor(ImVec2(x0, ry), ImVec2(x1, ry + 1.0f), c0, c1, c1, c0);
        // The rule keeps a short accent at its start. A divider that is grey the
        // whole way across is a table; the one lit bit is what says "this group is
        // part of a themed surface".
        const float seg = FMin(S(46.0f), (x1 - x0) * 0.5f);
        GradientFillRoundedH(d, ImVec2(x0, ry - S(0.5f)), ImVec2(x0 + seg, ry + S(0.9f)),
                             Fade(p.accent, 0.55f), Fade(p.accent, 0.0f), 0.0f, 6);
    }
    if (summary && *summary) {
        const ImVec2 sp(pos.x + avail - TextWidth((ImFont*)mono, summary), pos.y + (line_h - vsz) * 0.5f);
        if (mono) d->AddText((ImFont*)mono, vsz, sp, U32(Fade(p.text_faint, p.TextAlphaScale())), summary);
        else      d->AddText(sp, U32(Fade(p.text_faint, p.TextAlphaScale())), summary);
    }
    ImGui::Dummy(ImVec2(avail, h));
    return h;
}

void GroupLabel(const char* text)
{
    GroupLabelImpl(nullptr, text, nullptr, nullptr, 0.0f);
}

bool GroupLabelCollapsible(const char* id, const char* text, bool* open,
                           const char* summary, float hit_w)
{
    if (!open) { GroupLabelImpl(nullptr, text, nullptr, summary, hit_w); return true; }
    GroupLabelImpl(id, text, open, summary, hit_w);
    return *open;
}

bool AccentDots(const char* const* names, const unsigned int* rgbs, int count,
                int* selected, float size, bool show_names)
{
    if (count <= 0 || !rgbs || !selected) return false;
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float sz = S(size);
    const float gap = S(8.0f);
    const int n = IClamp(count, 1, 16);
    float cw = (avail - gap * (float)(n - 1)) / (float)n;
    const float cap = sz * 2.4f + S(10.0f);
    if (cw > cap) cw = cap;

    bool changed = false;
    const float line_h = ImGui::GetTextLineHeight();
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        BeginGhost();
        const bool clicked = ImGui::Button("##dot", ImVec2(cw, sz));
        EndGhost();
        const bool hov = ImGui::IsItemHovered();
        const ImGuiID id = ImGui::GetItemID();
        if (hov && names && names[i]) ImGui::SetTooltip("%s", names[i]);
        ImGui::PopID();

        const ImVec2 b(a.x + cw, a.y + sz);
        const float ht = anim::Step(id, hov ? 1.0f : 0.0f, 18.0f);
        const bool sel = (*selected == i);
        const ImVec4 col = Hex(rgbs[i], 1.0f);
        const float grow = ht * S(2.0f);
        const ImVec2 qa(a.x - grow * 0.5f, a.y - grow * 0.5f);
        const ImVec2 qb(b.x + grow * 0.5f, b.y + grow * 0.5f);
        const float r = p.corner_sm;

        GradientFillRounded(d, qa, qb, Shade(col, 0.16f + 0.10f * ht), Shade(col, -0.16f), r);
        d->AddShadowRect(qa, qb, U32(Fade(col, 0.42f * (0.30f + 0.70f * ht))), S(12.0f),
                         ImVec2(0, S(2.0f)), ImDrawFlags_RoundCornersAll, r);
        StrokeEdge(d, qa, qb, Fade(Shade(col, 0.5f), 0.5f + 0.5f * ht), r);
        if (sel) {
            StrokeEdge(d, ImVec2(qa.x - S(2.5f), qa.y - S(2.5f)), ImVec2(qb.x + S(2.5f), qb.y + S(2.5f)),
                       Fade(AutoContrast(col), 0.9f), r + S(2.5f), S(1.3f), false);
            icon::Check(d, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), sz * 0.5f,
                        U32(AutoContrast(col)));
        }
        if (show_names && names && names[i]) {
            // caption in the same voice as every other label (MicroCaps shouts and
            // letterspaces), measured the same way so it stays centred.
            const float tw = MeasureMicroCaps(names[i], 0.0f);
            MicroCaps(d, ImVec2(a.x + (cw - tw) * 0.5f, b.y + S(4.0f)), names[i],
                      U32(Fade(sel ? p.text : p.text_faint, p.TextAlphaScale())), 0.0f);
        }
        if (clicked && *selected != i) { *selected = i; changed = true; }
    }
    ImGui::Dummy(ImVec2(avail, sz + (show_names ? S(4.0f) + line_h : 0.0f)));
    return changed;
}

} // namespace obsidian
