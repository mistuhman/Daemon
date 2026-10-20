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

    const ImVec4* key = nullptr; (void)key;
    if (strcmp(name, "Ember")   == 0) return &ember;
    if (strcmp(name, "Aurora")  == 0) return &aurora;
    if (strcmp(name, "Verdant") == 0) return &verdant;
    if (strcmp(name, "Crimson") == 0) return &crimson;
    if (strcmp(name, "Axiom")   == 0) return &axiom;
    return nullptr;
}

const char* const* PaletteNameList(int* out_count)
{
    static const char* names[] = { "Ember", "Aurora", "Verdant", "Crimson", "Axiom" };
    if (out_count) *out_count = (int)IM_ARRAYSIZE(names);
    return names;
}

} // namespace obsidian
