// -----------------------------------------------------------------------------
//  src/palette.cpp  --  built-in Glass Obsidian palettes
// -----------------------------------------------------------------------------
#include "obsidian/palette.h"

#include <cstring>

namespace obsidian {
namespace {

// The shared obsidian surface ramp. Accents are layered on top of this so every
// preset keeps identical contrast between text and surface.
Palette BaseObsidian()
{
    Palette p;

    // surfaces, ascending elevation
    p.backdrop      = Hex(0x05060A, 1.00f);
    p.window_top    = Hex(0x141821, 0.97f);
    p.window_bottom = Hex(0x090C12, 0.98f);
    p.header        = Hex(0x0E121A, 0.99f);
    p.panel         = Hex(0x171D29, 0.52f);
    p.panel_hover   = Hex(0x1E2532, 0.66f);
    p.field         = Hex(0x0A0E15, 0.86f);
    p.field_hover   = Hex(0x131926, 0.92f);
    p.field_active  = Hex(0x182031, 0.96f);
    p.popup         = Hex(0x0C1017, 0.98f);
    p.shadow        = Hex(0x000000, 0.62f);

    // lines & light
    p.edge          = Hex(0x2E384A, 1.00f);
    p.edge_soft     = Hex(0x1D2431, 1.00f);
    p.sheen         = Hex(0xFFFFFF, 0.055f);
    p.inner_glow    = Hex(0xFFFFFF, 0.020f);

    // text
    p.text          = Hex(0xE8ECF4, 1.00f);
    p.text_dim      = Hex(0x99A2B4, 1.00f);
    p.text_faint    = Hex(0x5F6879, 1.00f);

    // semantics
    p.ok            = Hex(0x5BC98C, 1.00f);
    p.warn          = Hex(0xE5B84B, 1.00f);
    p.danger        = Hex(0xE4614F, 1.00f);
    p.info          = Hex(0x59A8E8, 1.00f);

    p.opacity       = 1.0f;
    return p;
}

// The polish the "Liquid" preset turns on: the layers that make the glass look
// like a material rather than a colour swatch. Everything here is a strength, so
// a host that only wants the tint of "Liquid" can dial these back to zero.
void Polish(Palette& p, float amount)
{
    p.mat.sheen    = 1.00f * amount;
    p.mat.edge     = 1.00f;
    p.mat.bloom    = 1.10f * amount;
    p.mat.chroma   = 0.45f * amount;
    p.mat.frost    = 0.55f * amount;
    p.mat.sweep    = 0.65f * amount;
    p.mat.aurora   = 0.60f * amount;
    p.mat.shimmer  = 0.70f * amount;
    p.mat.lift     = 1.00f * amount;
    p.mat.gradient = 1.00f;
}

// Finish a palette: derive the accent-dependent tokens from the accent pair.
Palette WithAccent(Palette p, unsigned int a1, unsigned int a2, unsigned int on_accent)
{
    p.accent         = Hex(a1, 1.00f);
    p.accent_2       = Hex(a2, 1.00f);
    p.accent_dim     = Fade(Mix(p.accent, p.text_faint, 0.55f), 0.75f);
    p.accent_glow    = Hex(a1, 0.30f);
    p.text_on_accent = Hex(on_accent, 1.00f);

    // A whisper of the accent inside the window so the glass feels lit by it.
    p.inner_glow     = Hex(a1, 0.045f);
    p.field_active   = Mix(p.field_active, p.accent, 0.10f);
    p.panel_hover    = Mix(p.panel_hover, p.accent, 0.05f);
    return p;
}

} // namespace

Palette PaletteEmber()
{
    // Molten gold over cold volcanic glass. Dark text on the accent fill.
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x15171E, 0.97f);   // faint warm shift
    p.window_bottom = Hex(0x0A0B10, 0.98f);
    return WithAccent(p, 0xE9A13B, 0xD9553F, 0x0A0C11);
}

Palette PaletteAurora()
{
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x121826, 0.97f);
    p.window_bottom = Hex(0x080A12, 0.98f);
    return WithAccent(p, 0x59D2FE, 0x8B7CF6, 0x05121A);
}

Palette PaletteVerdant()
{
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x121A19, 0.97f);
    p.window_bottom = Hex(0x070C0B, 0.98f);
    return WithAccent(p, 0x63E6A8, 0x3FBF8F, 0x04140D);
}

Palette PaletteCrimson()
{
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x191419, 0.97f);
    p.window_bottom = Hex(0x0C080B, 0.98f);
    // Dark red fill -> light text.
    return WithAccent(p, 0xF2637C, 0xB4304F, 0xFFF1F3);
}


// -----------------------------------------------------------------------------
//  Axiom: the reference's own ramp -- near-black glass, 1px hairlines, and an
//  accent that is only ever used for STATE. The surface values are lifted
//  straight from the design language it comes from: window #080808, rail
//  #050505, card #0A0A0A, hairlines #151515/#1A1A1A. Because elevation is still
//  monotonic (rail < window < card < field-on-hover) it keeps reading as layered
//  glass rather than a flat black box.
// -----------------------------------------------------------------------------
Palette PaletteAxiom()
{
    Palette p;

    p.backdrop      = Hex(0x000000, 1.00f);
    p.window_top    = Hex(0x0B0C0E, 0.965f);
    p.window_bottom = Hex(0x060708, 0.975f);
    p.header        = Hex(0x090A0C, 0.98f);
    p.panel         = Hex(0x101215, 0.62f);
    p.panel_hover   = Hex(0x181B1F, 0.78f);
    p.field         = Hex(0x0A0B0D, 0.92f);
    p.field_hover   = Hex(0x14171A, 0.95f);
    p.field_active  = Hex(0x1B1F24, 0.98f);
    p.popup         = Hex(0x0C0D10, 0.99f);
    p.shadow        = Hex(0x000000, 0.72f);

    p.edge          = Hex(0x22252A, 1.00f);
    p.edge_soft     = Hex(0x171A1E, 1.00f);
    p.sheen         = Hex(0xFFFFFF, 0.050f);
    p.inner_glow    = Hex(0xFFFFFF, 0.016f);

    p.text          = Hex(0xF1F3F6, 1.00f);
    p.text_dim      = Hex(0x9BA2AC, 1.00f);
    p.text_faint    = Hex(0x5D636C, 1.00f);

    p.ok            = Hex(0x4ADE80, 1.00f);
    p.warn          = Hex(0xFACC15, 1.00f);
    p.danger        = Hex(0xF87171, 1.00f);
    p.info          = Hex(0x60A5FA, 1.00f);

    // tighter corners than the obsidian presets: small cards want a small radius
    p.corner_lg = 11.0f;
    p.corner    =  7.0f;
    p.corner_sm =  4.5f;

    p.opacity       = 1.0f;

    // White accent: every filled control is a light surface, so its label has to
    // be dark. AutoContrast() is what makes that decision per-accent.
    ApplyAccent(p, Hex(0xFFFFFF, 1.0f), nullptr);
    p.text_on_accent = Hex(0x06070A, 1.00f);
    return p;
}

// -----------------------------------------------------------------------------
//  Liquid: the polished obsidian. Same ramp as Axiom, but the surfaces carry a
//  cold blue tint, sit at a lower alpha, and every material layer is on -- rim
//  light, chroma dispersion, a sweeping specular, drifting accent blooms. This
//  is the preset that answers "make it look like glass, not like a black box".
// -----------------------------------------------------------------------------
Palette PaletteLiquid()
{
    Palette p = BaseObsidian();

    p.backdrop      = Hex(0x04060D, 1.00f);
    p.window_top    = Hex(0x18203A, 0.80f);
    p.window_bottom = Hex(0x0A0E1C, 0.88f);
    p.header        = Hex(0x121A30, 0.86f);
    p.panel         = Hex(0x1E2942, 0.52f);
    p.panel_hover   = Hex(0x2A3A5C, 0.70f);
    p.field         = Hex(0x0C1220, 0.80f);
    p.field_hover   = Hex(0x16203A, 0.88f);
    p.field_active  = Hex(0x1C2A4A, 0.94f);
    p.popup         = Hex(0x101828, 0.94f);
    p.shadow        = Hex(0x01030A, 0.78f);

    p.edge          = Hex(0x54648C, 0.85f);
    p.edge_soft     = Hex(0x2B3550, 1.00f);
    p.sheen         = Hex(0xFFFFFF, 0.100f);

    p.text          = Hex(0xF0F4FE, 1.00f);
    p.text_dim      = Hex(0xAEBBD8, 1.00f);
    p.text_faint    = Hex(0x6F7E9E, 1.00f);

    p.ok            = Hex(0x57E39B, 1.00f);
    p.warn          = Hex(0xFFD36B, 1.00f);
    p.danger        = Hex(0xFF7A7A, 1.00f);
    p.info          = Hex(0x7FB8FF, 1.00f);

    p.corner_lg = 16.0f;
    p.corner    = 10.0f;
    p.corner_sm =  6.0f;

    Polish(p, 1.0f);
    return WithAccent(p, 0x7FD8FF, 0xB98CFF, 0x040913);
}

// -----------------------------------------------------------------------------
//  Ink: the "very high contrast" dark theme. Pure black glass, pure white ink,
//  and the ONLY decoration is a hairline. Nothing is washed with the accent,
//  because at this contrast a tint is noise; the material layers are set to
//  exactly what survives being read at a glance in a dark room.
// -----------------------------------------------------------------------------
Palette PaletteInk()
{
    Palette p;

    p.backdrop      = Hex(0x000000, 1.00f);
    p.window_top    = Hex(0x000000, 0.98f);
    p.window_bottom = Hex(0x000000, 1.00f);
    p.header        = Hex(0x000000, 1.00f);
    p.panel         = Hex(0x050505, 0.92f);
    p.panel_hover   = Hex(0x121212, 0.95f);
    p.field         = Hex(0x0A0A0A, 0.96f);
    p.field_hover   = Hex(0x161616, 0.96f);
    p.field_active  = Hex(0x1E1E1E, 0.96f);
    p.popup         = Hex(0x000000, 0.99f);
    p.shadow        = Hex(0x000000, 0.00f);

    p.edge          = Hex(0xFFFFFF, 0.34f);
    p.edge_soft     = Hex(0xFFFFFF, 0.14f);
    p.sheen         = Hex(0xFFFFFF, 0.070f);
    p.inner_glow    = Hex(0xFFFFFF, 0.000f);

    p.text          = Hex(0xFFFFFF, 1.00f);
    p.text_dim      = Hex(0xC9CDD4, 1.00f);
    p.text_faint    = Hex(0x8A9099, 1.00f);
    p.text_on_accent= Hex(0x000000, 1.00f);

    p.ok            = Hex(0x66FF9C, 1.00f);
    p.warn          = Hex(0xFFE066, 1.00f);
    p.danger        = Hex(0xFF6B6B, 1.00f);
    p.info          = Hex(0x7FC4FF, 1.00f);

    p.corner_lg = 10.0f;
    p.corner    =  6.0f;
    p.corner_sm =  3.0f;

    p.mat.sheen    = 1.00f;
    p.mat.edge     = 1.30f;   // the hairline IS the theme
    p.mat.bloom    = 0.55f;
    p.mat.chroma   = 0.00f;
    p.mat.frost    = 0.00f;
    p.mat.sweep    = 0.28f;
    p.mat.aurora   = 0.00f;
    p.mat.shimmer  = 0.45f;
    p.mat.lift     = 0.80f;
    p.mat.gradient = 0.35f;

    ApplyAccent(p, Hex(0xFFFFFF, 1.0f), nullptr);
    return p;
}

// -----------------------------------------------------------------------------
//  Frost: LIGHT glass. Authored, not inverted -- the elevation ramp runs the
//  other way (nearer = darker), the specular stays white and gets stronger, and
//  the hairline turns from a light seam into a real grey line, because a white
//  edge on a white panel is invisible. `light = true` is what flips every
//  Lift() call site in the widget layer.
// -----------------------------------------------------------------------------
Palette PaletteFrost()
{
    Palette p;

    p.backdrop      = Hex(0xDCE4F0, 1.00f);
    p.window_top    = Hex(0xFFFFFF, 0.82f);
    p.window_bottom = Hex(0xE6EBF4, 0.90f);
    p.header        = Hex(0xF6F9FD, 0.90f);
    p.panel         = Hex(0xFFFFFF, 0.52f);
    p.panel_hover   = Hex(0xFFFFFF, 0.74f);
    p.field         = Hex(0xD8E0EC, 0.80f);
    p.field_hover   = Hex(0xC9D5E6, 0.88f);
    p.field_active  = Hex(0xBDCAE0, 0.92f);
    p.popup         = Hex(0xFDFEFF, 0.96f);
    p.shadow        = Hex(0x1B2436, 0.34f);

    p.edge          = Hex(0x8D9BB2, 0.85f);
    p.edge_soft     = Hex(0xB9C4D6, 1.00f);
    p.sheen         = Hex(0xFFFFFF, 0.850f);   // white specular on white glass: loud
    p.inner_glow    = Hex(0xFFFFFF, 0.350f);

    p.text          = Hex(0x0A0E16, 1.00f);
    p.text_dim      = Hex(0x3D4759, 1.00f);
    p.text_faint    = Hex(0x6C788E, 1.00f);
    p.text_on_accent= Hex(0xFFFFFF, 1.00f);

    p.ok            = Hex(0x0F8A4C, 1.00f);
    p.warn          = Hex(0xA9750A, 1.00f);
    p.danger        = Hex(0xC4322A, 1.00f);
    p.info          = Hex(0x1E6FC4, 1.00f);

    p.corner_lg = 16.0f;
    p.corner    = 10.0f;
    p.corner_sm =  6.0f;

    p.light = true;

    Polish(p, 0.75f);
    p.mat.frost = 0.20f;      // frost rings are a dark-glass trick
    p.mat.aurora = 0.35f;
    return WithAccent(p, 0x2F7BE0, 0x8E5BE8, 0xFFFFFF);
}

// -----------------------------------------------------------------------------
//  Paper: the light theme for people who want contrast, not glass. Opaque white,
//  black ink, 1px grey rules, no specular at all. This is the preset that proves
//  the material layer is orthogonal to the palette: same widgets, 0 polish.
// -----------------------------------------------------------------------------
Palette PalettePaper()
{
    Palette p = PaletteFrost();

    p.backdrop      = Hex(0xF2F4F8, 1.00f);
    p.window_top    = Hex(0xFFFFFF, 1.00f);
    p.window_bottom = Hex(0xFAFBFD, 1.00f);
    p.header        = Hex(0xF4F6FA, 1.00f);
    p.panel         = Hex(0xF6F8FB, 1.00f);
    p.panel_hover   = Hex(0xECF0F7, 1.00f);
    p.field         = Hex(0xEDEFF4, 1.00f);
    p.field_hover   = Hex(0xE2E6EF, 1.00f);
    p.field_active  = Hex(0xD8DEEA, 1.00f);
    p.popup         = Hex(0xFFFFFF, 1.00f);
    p.shadow        = Hex(0x10182A, 0.16f);

    p.edge          = Hex(0x8A94A6, 1.00f);
    p.edge_soft     = Hex(0xC3CAD6, 1.00f);
    p.sheen         = Hex(0xFFFFFF, 0.000f);
    p.inner_glow    = Hex(0x000000, 0.000f);

    p.text          = Hex(0x000000, 1.00f);
    p.text_dim      = Hex(0x2C333F, 1.00f);
    p.text_faint    = Hex(0x5C6472, 1.00f);

    p.corner_lg = 8.0f;
    p.corner    = 5.0f;
    p.corner_sm = 3.0f;

    p.mat.sheen = 0.0f; p.mat.bloom = 0.35f; p.mat.chroma = 0.0f;
    p.mat.frost = 0.0f; p.mat.sweep   = 0.0f; p.mat.aurora = 0.0f;
    p.mat.shimmer = 0.0f; p.mat.lift  = 0.6f; p.mat.gradient = 0.25f;
    p.mat.edge  = 1.2f;

    return WithAccent(p, 0x141414, 0x444444, 0xFFFFFF);
}

void ApplyAccent(Palette& p, const ImVec4& accent, const ImVec4* secondary)
{
    p.accent      = accent;
    p.accent_2    = secondary ? *secondary : Shade(accent, -0.26f);
    p.accent_dim  = Fade(Mix(accent, p.text_faint, 0.55f), 0.75f);
    p.accent_glow = Fade(accent, 0.30f);
    p.inner_glow  = Fade(accent, 0.045f);
    p.field_active  = Mix(p.field_active,  accent, 0.10f);
    p.panel_hover   = Mix(p.panel_hover,   accent, 0.05f);
    p.text_on_accent = AutoContrast(accent);
}

void ApplyAccent(Palette& p, unsigned int rgb, const ImVec4* secondary)
{
    ApplyAccent(p, Hex(rgb, 1.0f), secondary);
}

const Palette* PaletteByName(const char* name)
{
    if (!name) return nullptr;
    static const Palette ember   = PaletteEmber();
    static const Palette aurora  = PaletteAurora();
    static const Palette verdant = PaletteVerdant();
    static const Palette crimson = PaletteCrimson();
    static const Palette axiom   = PaletteAxiom();
    static const Palette liquid  = PaletteLiquid();
    static const Palette ink     = PaletteInk();
    static const Palette frost   = PaletteFrost();
    static const Palette paper   = PalettePaper();

    const ImVec4* key = nullptr; (void)key;
    if (strcmp(name, "Ember")   == 0) return &ember;
    if (strcmp(name, "Aurora")  == 0) return &aurora;
    if (strcmp(name, "Verdant") == 0) return &verdant;
    if (strcmp(name, "Crimson") == 0) return &crimson;
    if (strcmp(name, "Axiom")   == 0) return &axiom;
    if (strcmp(name, "Liquid")  == 0) return &liquid;
    if (strcmp(name, "Ink")     == 0) return &ink;
    if (strcmp(name, "Frost")   == 0) return &frost;
    if (strcmp(name, "Paper")   == 0) return &paper;
    return nullptr;
}

const char* const* PaletteNameList(int* out_count)
{
    static const char* names[] = { "Axiom", "Liquid", "Ink", "Frost", "Paper",
                                   "Ember", "Aurora", "Verdant", "Crimson" };
    if (out_count) *out_count = (int)IM_ARRAYSIZE(names);
    return names;
}

bool PaletteIsLight(const char* name)
{
    if (!name) return false;
    return strcmp(name, "Frost") == 0 || strcmp(name, "Paper") == 0;
}

// -----------------------------------------------------------------------------
//  The two edits a user makes ON a preset
// -----------------------------------------------------------------------------
namespace {
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Mix without disturbing alpha. Every token carries its own alpha on purpose -- a
// field is translucent in a way the panel behind it is not -- and a tint that
// flattens all of them to 1.0 is a tint that deleted the depth of the UI.
void TintOne(ImVec4& v, const ImVec4& c, float k)
{
    const ImVec4 m = Mix(v, c, k);
    v = ImVec4(m.x, m.y, m.z, v.w);
}

// Push `v` a little further from `ref`, along the vector that already separates them.
void Separate(ImVec4& v, const ImVec4& ref, float k)
{
    v = ImVec4(ref.x + (v.x - ref.x) * (1.0f + k),
               ref.y + (v.y - ref.y) * (1.0f + k),
               ref.z + (v.z - ref.z) * (1.0f + k),
               Mix(v, ref, k).w);
}
} // namespace

// "Smoky violet glass" without a tenth hard-coded palette: the colour goes into the
// surfaces only. Text, hairlines and the accent pair are left alone deliberately --
// tinting ink is how a theme picker produces a palette that is pretty in the swatch
// and unreadable in the window, and tinting the accent would fight whatever the user
// just picked in the accent row.
void TintSurfaces(Palette& p, const ImVec4& tint, float amount)
{
    const float k = Clamp01(amount);
    if (k <= 0.001f) return;
    const ImVec4 c(tint.x, tint.y, tint.z, 1.0f);

    TintOne(p.backdrop,    c, k * 0.85f);
    TintOne(p.window_top,  c, k);
    TintOne(p.window_bottom, c, k);
    TintOne(p.header,      c, k * 0.9f);
    TintOne(p.panel,       c, k);
    TintOne(p.panel_hover, c, k);
    TintOne(p.field,       c, k);
    TintOne(p.field_hover, c, k);
    TintOne(p.field_active, c, k);
    TintOne(p.popup,       c, k);
    TintOne(p.shadow,      c, k * 0.5f);

    // The two layers that are LIGHT rather than material -- a highlight, not a
    // colour -- take a third of the amount, so a saturated tint still leaves the
    // glass something to reflect.
    TintOne(p.sheen,      c, k * 0.35f);
    TintOne(p.inner_glow, c, k * 0.35f);
}

// The accessibility knob and the OLED knob at once. `k` is 0..1 and 0 is a no-op,
// which is why every host can call this unconditionally instead of branching.
//
// What it does, in order of how much it matters: ink goes to the extreme of its own
// polarity (the whole point), the two dim steps follow at half rate so a label is
// still quieter than a title, hairlines get their alpha up because a 0.55-alpha
// edge over a bright skybox is not a hairline, and the accent WASHES inside surfaces
// pull back -- on a high-contrast theme the colour belongs on the controls, not in
// the background.
//
// What it does NOT do: touch `opacity`. Transparency is a dial the user set, and a
// second knob that quietly overrides it is why "high contrast" menus often feel like
// they also stole your glass.
void BoostContrast(Palette& p, float k)
{
    const float amt = Clamp01(k);
    if (amt <= 0.001f) { p.contrast = 0.0f; return; }
    p.contrast = amt;

    // Which end "the extreme" is depends on polarity, and both directions have to be
    // authored or the light themes get a contrast knob that does nothing.
    const ImVec4 ink_target = p.light ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
                                      : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    p.text      = Mix(p.text,    ink_target, amt);
    p.text_dim  = Mix(p.text_dim,  ink_target, amt * 0.55f);
    p.text_faint= Mix(p.text_faint,ink_target, amt * 0.30f);

    // Edges: harder, and toward the ink so they read as drawn rather than as fog.
    const float ea = Clamp01(p.edge.w + amt * 0.45f);
    p.edge      = ImVec4(Mix(p.edge, p.text, amt * 0.5f).x,
                         Mix(p.edge, p.text, amt * 0.5f).y,
                         Mix(p.edge, p.text, amt * 0.5f).z, ea);
    p.edge_soft = ImVec4(p.edge.x, p.edge.y, p.edge.z, Clamp01(ea * 0.65f));
    p.sheen     = ImVec4(p.sheen.x, p.sheen.y, p.sheen.z, Clamp01(p.sheen.w + amt * 0.30f));

    // At the top of the knob the glass goes FLAT: every surface heads for the extreme
    // of its own polarity -- black on a dark theme, white on a light one. That is the
    // part a "high contrast" setting usually forgets: bright text over a washed,
    // tinted, gradient background is still a low-contrast UI, it just has a bright
    // foreground. It is also what makes this knob worth having on an overlay at all,
    // since a flat near-black plate is the one thing that stays legible over a
    // skybox, and on an OLED panel those pixels are simply off.
    //
    // The steps between the tokens then go WIDER instead of disappearing: a field is
    // a recess, and whether a recess reads as darker or lighter is the preset's own
    // call, so each step is scaled away from its surface rather than re-signed here.
    // That is what keeps a control pressable once the accent wash that used to say so
    // has been dialled down.
    const ImVec4 flat = p.light ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                : ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    auto Flatten = [&](ImVec4& v, float k) {
        const ImVec4 m = Mix(v, ImVec4(flat.x, flat.y, flat.z, v.w), k);
        v = m;
    };
    Flatten(p.backdrop,      amt * 0.70f);
    Flatten(p.window_top,    amt * 0.55f);
    Flatten(p.window_bottom, amt * 0.55f);
    Flatten(p.header,        amt * 0.50f);
    Flatten(p.popup,         amt * 0.50f);
    Flatten(p.panel,         amt * 0.45f);
    Flatten(p.field,         amt * 0.35f);

    Separate(p.panel_hover, p.panel, amt * 0.45f);
    Separate(p.field, p.panel, amt * 0.55f);
    Separate(p.field_hover, p.field, amt * 0.50f);
    Separate(p.field_active, p.field, amt * 0.60f);

    // Washes down, state colours up: contrast is about information, and "on" has to
    // stay findable when everything else is being flattened.
    p.accent_glow = Fade(p.accent_glow, 1.0f - amt * 0.45f);
    p.inner_glow  = Fade(p.inner_glow,  1.0f - amt * 0.70f);
    p.ok    = Mix(p.ok,    ImVec4(0.55f, 1.0f, 0.62f, 1.0f), amt * 0.35f);
    p.warn  = Mix(p.warn,  ImVec4(1.0f, 0.82f, 0.35f, 1.0f), amt * 0.35f);
    p.danger= Mix(p.danger,ImVec4(1.0f, 0.36f, 0.32f, 1.0f), amt * 0.35f);
    p.info  = Mix(p.info,  ImVec4(0.45f, 0.80f, 1.0f, 1.0f), amt * 0.35f);
}

} // namespace obsidian
