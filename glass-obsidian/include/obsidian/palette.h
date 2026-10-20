// -----------------------------------------------------------------------------
//  obsidian/palette.h  --  design tokens for the "Glass Obsidian" UI language
// -----------------------------------------------------------------------------
//  Every colour/size the theme and the widget layer can consult lives here, so
//  re-skinning the whole UI is a matter of swapping one Palette struct.
//
//  Conventions
//    * Colours are authored as 8-bit hex (0xRRGGBB) + alpha, then normalised to
//      the ImVec4 (0..1) range ImGui expects.
//    * Surfaces are ordered by elevation:  backdrop < window < panel < field.
//      Higher elevation == lighter == closer to the viewer. Keeping that
//      monotonic is what makes the UI read as "layered glass" instead of noise.
//    * `sheen` is the specular highlight along the TOP edge of a surface
//      (light from above). `edge` is the hairline silhouette. Both are drawn at
//      very low alpha; they do the heavy lifting for the glass read.
//
//  Public domain / MIT -- see LICENSE.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

namespace obsidian {

// 0xRRGGBB -> ImVec4 with alpha
inline ImVec4 Hex(unsigned int rgb, float a = 1.0f)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f,
                  ((rgb >>  8) & 0xFF) / 255.0f,
                  ((rgb >>  0) & 0xFF) / 255.0f,
                  a);
}

// Hue -> RGB, so a "rainbow" theme does not need the ImGui colour converters (and
// so a host can build a palette from one float). `h` wraps: 1.25 == 0.25.
inline ImVec4 Hue(float h, float s = 1.0f, float v = 1.0f, float a = 1.0f)
{
    h = h - (float)(int)h;
    if (h < 0.0f) h += 1.0f;
    const float hh = h * 6.0f;
    const int   i  = (int)hh;
    const float f  = hh - (float)i;
    const float p  = v * (1.0f - s);
    const float q  = v * (1.0f - s * f);
    const float t  = v * (1.0f - s * (1.0f - f));
    float r = 0, g = 0, b = 0;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return ImVec4(r, g, b, a);
}

// Multiply only the alpha channel (used everywhere for the global opacity knob).
inline ImVec4 Fade(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, c.w * a); }

// Perceptual-ish lerp between two colours.
inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// Lighten (t>0) or darken (t<0) towards white/black without touching alpha.
inline ImVec4 Shade(const ImVec4& c, float t)
{
    return t >= 0.0f ? Mix(c, ImVec4(1, 1, 1, c.w), t)
                     : Mix(c, ImVec4(0, 0, 0, c.w), -t);
}

// Contrast ink for a filled surface: dark text on a light accent, light text on
// a dark one (Rec.709 luma). Lets a white or pastel accent stay legible without
// every palette having to hand-pick `text_on_accent`.
inline ImVec4 AutoContrast(const ImVec4& bg)
{
    const float luma = 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z;
    return luma > 0.58f ? Hex(0x06070A, 1.0f) : Hex(0xF3F5F9, 1.0f);
}

// -----------------------------------------------------------------------------
//  Material -- how the glass is POLISHED, as opposed to what COLOUR it is.
// -----------------------------------------------------------------------------
//  Everything in here is a strength, not a toggle, because "a little" is almost
//  always right and the amount is taste: a user dials 0.35 chroma and 0.8 sweep
//  and it reads as cut crystal, while 0 and 0 reads as a flat panel. 0 turns a
//  layer off entirely (no draw calls, no cost), 1 is the designer's full intent.
//
//  `animate` is the master switch for every time-driven layer, and `speed` scales
//  the clock those layers read. A tool drawn over a game must be able to stop
//  moving without losing the material: an animated shine that cannot be frozen is
//  a distraction, and a distraction in a competitive game is a reason to turn the
//  whole theme off.
struct Material
{
    float sheen    = 1.00f;  // top-edge specular highlight (the glass read)
    float edge     = 1.00f;  // hairline weight on every silhouette
    float bloom    = 1.00f;  // accent glow: pills, live cards, focus, nav indicator
    float chroma   = 0.00f;  // rim dispersion -- RGB split along the rounded edge
    float frost    = 0.00f;  // inner frost ring, simulates a blurred backdrop
    float sweep    = 0.00f;  // travelling specular band across a surface
    float aurora   = 0.00f;  // slow accent blooms drifting inside the window
    float shimmer  = 0.00f;  // travelling highlight on a filled slider/bar
    float lift     = 1.00f;  // hover emphasis (brighter + wider bloom, no move)
    float gradient = 1.00f;  // top->bottom ramp strength on every surface
    float speed    = 1.00f;  // multiplier on the clock the animated layers read
    bool  animate  = true;   // false freezes every animated layer

    // Master multiplier for the ten strengths above, applied once when a palette is
    // installed (see SetActivePalette / ApplyTheme, and Palette::Normalise). `animate`
    // and `speed` are deliberately NOT scaled: they are the clock, not the polish.
    //
    // This exists because "turn the polish off" is one question, and answering it by
    // zeroing ten sliders is ten questions that also throw away what the user tuned.
    // With a master switch, 0 is flat card paper and 1 is exactly what was dialled in.
    float polish   = 1.00f;

    // Every animated layer asks the same question, so they all freeze together.
    bool Moving() const { return animate && speed > 0.0f; }
    float Band(float strength) const { return animate ? strength : 0.0f; }

    // Folds `polish` into the strengths and resets it, so applying is a no-op the
    // second time round: a palette that has been through here once cannot be
    // flattened twice, whichever order the caller installs it in.
    void ApplyPolish()
    {
        float k = polish;
        polish = 1.00f;
        if (k >= 0.999f) return;
        if (k < 0.0f) k = 0.0f;
        sheen *= k; edge *= k; bloom *= k; chroma *= k; frost *= k;
        sweep *= k; aurora *= k; shimmer *= k; lift *= k; gradient *= k;
    }
};

struct Palette
{
    // ---- surfaces (ascending elevation) -------------------------------------
    ImVec4 backdrop;        // the void behind everything (desktop / clear colour)
    ImVec4 window_top;      // main window gradient, top stop
    ImVec4 window_bottom;   // main window gradient, bottom stop
    ImVec4 header;          // title bar
    ImVec4 panel;           // inset panel / group
    ImVec4 panel_hover;     // inset panel, hovered
    ImVec4 field;           // input / slider track
    ImVec4 field_hover;
    ImVec4 field_active;
    ImVec4 popup;           // combo & context menus
    ImVec4 shadow;          // drop shadow tint

    // ---- lines & light ------------------------------------------------------
    ImVec4 edge;            // hairline silhouette
    ImVec4 edge_soft;       // interior separators
    ImVec4 sheen;           // top-edge specular highlight
    ImVec4 inner_glow;      // faint accent wash inside the window

    // ---- text ---------------------------------------------------------------
    ImVec4 text;
    ImVec4 text_dim;
    ImVec4 text_faint;
    ImVec4 text_on_accent;  // label colour that sits on a filled accent surface

    // ---- accent -------------------------------------------------------------
    ImVec4 accent;          // primary
    ImVec4 accent_2;        // secondary -- gradients run accent -> accent_2
    ImVec4 accent_dim;      // unfocused / disabled accent
    ImVec4 accent_glow;     // bloom tint around active controls

    // ---- semantics ----------------------------------------------------------
    ImVec4 ok;
    ImVec4 warn;
    ImVec4 danger;
    ImVec4 info;

    // ---- geometry -----------------------------------------------------------
    float corner_lg = 12.0f;   // windows, popups
    float corner    =  8.0f;   // panels, buttons, fields
    float corner_sm =  5.0f;   // small controls, knobs, badges
    float corner_pill = 999.0f;// fully rounded (clamped to half-height at draw)

    // ---- material -----------------------------------------------------------
    // Polish, not colour. Every field is a 0..1 strength and 0 means the layer is
    // not drawn at all, so a theme can be turned from crystal to card paper
    // without touching a single colour.
    Material mat;

    // ---- polarity -----------------------------------------------------------
    // A LIGHT theme is not a dark theme with the surfaces inverted: elevation has
    // to go the other way. On near-black glass "closer to the viewer" is LIGHTER,
    // so hover and selection lift colours toward white. On near-white glass that
    // lift clips to nothing, so it has to darken. Draw sites that used to write
    // Shade(x, +t) call Lift(pal, x, t), which flips on this flag -- that is the
    // whole difference between a light theme and an inverted one.
    bool  light = false;

    // 0..1 high-contrast push: text goes to the extreme of its polarity, hairlines
    // harden, and the accent washes inside surfaces pull back so the ink is what
    // carries the meaning. BoostContrast() applies it once, to a finished palette.
    float contrast = 0.0f;

    // ---- global opacity -----------------------------------------------------
    // Scales the alpha of every surface. 1.0 = opaque card, 0.35 = see-through
    // glass. Text/edge alpha is scaled by a gentler curve so the UI stays
    // legible when the surfaces go transparent.
    float opacity = 1.0f;

    float SurfaceAlphaScale() const { return opacity; }
    // Text normally dims with the surfaces, which is the glassy look -- but it is
    // also what makes a translucent menu unreadable over a bright world. A tool
    // that is meant to be *read* through sets `text_stays_opaque`.
    bool  text_stays_opaque = false;
    float TextAlphaScale()    const { return text_stays_opaque ? 1.0f : 0.35f + 0.65f * opacity; }
    // Contrast hardens the lines: a 0.55-alpha hairline is invisible over a bright
    // skybox, and "high contrast" has to mean something the user can dial up.
    float EdgeAlphaScale()    const
    {
        const float v = 0.55f + 0.45f * opacity + contrast * 0.30f;
        return v > 1.0f ? 1.0f : v;
    }
    // The many places that scale a hairline by both the opacity and the material's
    // own edge weight.
    float EdgeWeight()        const { return EdgeAlphaScale() * mat.edge; }
    // Dispersion reads much stronger on dark glass (the split has somewhere to go);
    // on a light theme the rim is mostly a bright specular line, so damp it.
    float Chroma()            const { return mat.chroma * (light ? 0.55f : 1.0f); }
    bool  Moving()            const { return mat.Moving(); }
};

// Shade toward the viewer. Lighter on dark glass, darker on light glass -- which
// is why call sites use this instead of picking a sign themselves.
inline ImVec4 Lift(const Palette& p, const ImVec4& c, float t)
{
    return Shade(c, p.light ? -t : t);
}

// -----------------------------------------------------------------------------
//  Built-in palettes.
//  The dark presets share one obsidian surface ramp; only the accent pair (and a
//  hint of tint in the surfaces) changes, so switching preset never breaks the
//  contrast ratios for text. The light pair is authored separately, because a
//  light theme needs its OWN ramp (see `Palette::light`), not an inverted one.
// -----------------------------------------------------------------------------
Palette PaletteEmber();     // molten gold over cold volcanic glass  (default)
Palette PaletteAurora();    // cyan -> violet iridescence
Palette PaletteVerdant();   // jade / mint
Palette PaletteCrimson();   // garnet / rose
Palette PaletteAxiom();     // near-black, white accent, hairline edges (default)
Palette PaletteLiquid();    // obsidian, polished: rim light, chroma, sweep, bloom
Palette PaletteInk();       // pure black + pure white, maximum contrast, no wash
Palette PaletteFrost();     // LIGHT: frosted ice glass over dark ink
Palette PalettePaper();     // LIGHT: opaque white, black ink, hard hairlines

// Colouring a surface without re-authoring it: mixes `tint` into every surface
// token (window, header, panel, field, popup, backdrop) at `amount`, leaving text
// and accents alone. This is what makes "smoky violet glass" a slider instead of a
// new palette, and why a theme page can offer a tint picker at all.
void TintSurfaces(Palette& p, const ImVec4& tint, float amount);

// Push a finished palette toward maximum contrast: text to the extremes of its
// polarity, hairlines up, the accent washes inside surfaces down. `k` is 0..1 and
// BoostContrast(p, 0) is a no-op, so callers can apply it unconditionally.
void BoostContrast(Palette& p, float k);

// Re-derive every accent-dependent token from one colour, including whether text
// sitting ON the accent should be dark or light. Call this after changing
// `accent` by hand, or pass colours you picked from a picker.
// `secondary` (optional) overrides the gradient's far stop.
void ApplyAccent(Palette& p, const ImVec4& accent, const ImVec4* secondary = nullptr);
void ApplyAccent(Palette& p, unsigned int rgb, const ImVec4* secondary = nullptr);

// Named lookup, handy for a settings dropdown. Returns nullptr if unknown.
const Palette* PaletteByName(const char* name);
const char* const* PaletteNameList(int* out_count);
// True when the named preset is a light one, for hosts that need to know without
// building the palette first (a HUD must not keep drawing white text the frame
// after the user picked "Frost").
bool PaletteIsLight(const char* name);

} // namespace obsidian
