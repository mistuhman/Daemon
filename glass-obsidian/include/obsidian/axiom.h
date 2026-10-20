// -----------------------------------------------------------------------------
//  obsidian/axiom.h  --  the "Axiom" app shell: nav rail, card grid, dense cards
// -----------------------------------------------------------------------------
//  glass.h is a *component* set (buttons, sliders, panels, chips). Axiom is the
//  layer that turns components into an application menu in the visual language
//  of a modern dark web app / production tool:
//
//    * NavSidebar()  a left rail: brand block, icon + micro-caps rows with a
//                    sliding accent indicator and a "live" dot per enabled
//                    feature, plus a pinned footer for the status/profile card.
//    * Grid + Card   a fixed-cell grid of small surfaces. Cells in a row share a
//                    height, so showing or hiding a setting's sub-controls can
//                    never make the layout ragged.
//    * Card helpers  ToggleCard / SliderCard / ComboCard / ... one call per
//                    setting, so the menu source reads like a table of settings
//                    instead of hand-placed widgets.
//
//  Design language: near-black glass, 1px hairlines, letter-tracked micro-caps
//  labels, and the accent colour reserved for *state* (selected row, filled
//  track, ON pill, focus glow). Colours always come from ActivePalette(), so a
//  palette or opacity change repaints the whole shell for free.
//
//  CARDS ARE CHILD WINDOWS, NOT GROUPS. A card needs an item-width context of
//  its own -- otherwise obsidian::Slider() would measure the *page*'s width and
//  a track in a 320px card would run 200px past its edge -- plus a clip rect, so
//  content that grows can never bleed over a neighbour. A child window gives
//  both, plus free hover/click semantics for the "whole card is a button" case.
//
//  Public ImGui API only -- no imgui_internal.h.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/glass.h"

namespace obsidian {

// =============================================================================
//  Tuning
// =============================================================================
struct AxiomMetrics
{
    float card_h     = 50.0f;  // default cell height
    float card_pad   = 11.0f;  // padding inside a card
    float gutter     = 8.0f;   // gap between cells, both axes
    float nav_row_h  = 36.0f;  // sidebar row height
    float nav_w      = 220.0f; // sidebar width
    float pill_w     = 46.0f;  // ON/OFF pill
    float pill_h     = 21.0f;
    float micro_k    = 0.78f;  // micro-caps size as a fraction of the bold font
};
AxiomMetrics&       AM();                  // live values (unscaled; use sites apply S())
void                SetAxiomMetrics(const AxiomMetrics& m);

// =============================================================================
//  Type / colour helpers
// =============================================================================

// Letterspaced caps -- the label style of this language. Returns the width.
float MicroCaps(ImDrawList* d, ImVec2 pos, const char* text, ImU32 col,
                float size = 0.0f, float tracking = -1.0f);
float MeasureMicroCaps(const char* text, float size = 0.0f, float tracking = -1.0f);

// Rounded status chip (dot + label). DrawChip with `right_x` > 0 right-aligns
// its right edge to that x. Returns the chip's width.
float ChipWidth(const char* text);
void  DrawChip(ImDrawList* d, ImVec2 pos, const char* text, const ImVec4& tint,
               bool pulse = false, float right_x = 0.0f);
float Chip(const char* text, const ImVec4& tint, bool pulse = false);  // laid-out form

// =============================================================================
//  Nav sidebar
// =============================================================================
struct NavDef
{
    const char* label;
    IconDrawFn  icon     = nullptr;
    const bool* live     = nullptr;  // non-null && true  -> accent dot + brighter row
    const char* trailing = nullptr;  // dim mono hint on the right (e.g. "3 on")
};

// Left rail: opens a group (so the caller can SameLine() the page next to it)
// and a child window inside it (so the footer can be pinned to the bottom edge).
//   size.y < 0 => fill the parent region's remaining height.
void SidebarBegin(const char* id, const ImVec2& size, float width = 0.0f);
void SidebarEnd();

// Big name + accent suffix + dim subtitle, with an optional chip on the right.
void SidebarBrand(const char* name, const char* accent_suffix,
                  const char* subtitle, const char* chip = nullptr,
                  const ImVec4* chip_tint = nullptr);

// The rows. Returns true when the selection changed.
bool SidebarNav(const char* id, const NavDef* items, int count, int* selected);

// Pins whatever comes next to the rail's bottom edge, reserving `height`.
void SidebarFooter(float height);
void SidebarRule();                      // hairline across the rail

// Footer content.
void RailStat(const char* key, const char* value, const ImVec4* value_col = nullptr);
bool RailButton(const char* label, const ImVec2& size = ImVec2(0, 0),
                ButtonKind kind = ButtonKind_Default);

// The ON/OFF pill, painted (not interactive) at `pos`. `t` is the animated
// 0..1 "on" value and `hover` the animated hover value -- expose both so app
// code can build its own hero cards out of the same switch.
void DrawPill(ImDrawList* d, ImVec2 pos, bool on, float t, float hover);

// =============================================================================
//  Card grid
// =============================================================================
struct GridCfg
{
    int   columns    = 2;
    float gutter     = -1.0f;   // <0 => AM().gutter
    float row_height = -1.0f;   // <0 => AM().card_h
    float pad        = -1.0f;   // <0 => AM().card_pad
};

// One grid at a time (a menu page has one). GridEnd() is unconditional.
void GridBegin(const GridCfg& cfg = GridCfg());
void GridEnd();

// Claims the next cell. `span` merges columns; `height` (already-scaled pixels,
// e.g. from ImGui::GetTextLineHeight() or S(...)) overrides the row height for
// this cell -- use the same value for both cells of a row if you want it aligned.
void GridCell(int span = 1, float height = 0.0f);
int  GridColumnsLeft();

enum CardFlags_
{
    Card_None      = 0,
    Card_Live      = 1 << 0,  // the setting inside is enabled: accent edge + bloom
    Card_Inset     = 1 << 1,  // recessed read-only surface, no sheen
    Card_Hoverable = 1 << 2,  // the WHOLE card is one button (no controls inside)
    Card_Clipped   = 1 << 3,  // draw a scroll indicator-less clip (default on)
    Card_NoPad     = 1 << 4,  // caller positions its own content
};
typedef int CardFlags;

// size == (0,0) => take the size GridCell() reserved. Must be paired with
// CardEnd() even if it returns false, exactly like BeginChild().
bool CardBegin(const char* id, const ImVec2& size = ImVec2(0, 0), CardFlags flags = Card_None);
void CardEnd();

// Inside a card.
ImVec2 CardOrigin();                 // screen rect of the card
ImVec2 CardInner();                  // origin + padding
float  CardInnerWidth();
float  CardHeight();
bool   CardHovered();
bool   CardClicked();
float  CardMicroY(float line_h);     // y that vertically centres a micro-caps line

// Micro-caps title. `trailing_w` reserves room on the right for the control so
// a long label ellipsizes instead of sliding under it.
void CardTitle(const char* label, float trailing_w = 0.0f, bool top = false);
void CardKeyValue(const char* key, const char* value, const ImVec4* tint = nullptr);
void CardText(const char* text, bool dim = true);
void CardSpace(float h);

// =============================================================================
//  Card-shaped settings (each owns its grid cell)
// =============================================================================
// `span` merges grid columns, exactly like GridCell()'s: pass 2 to let a lone
// card in a group fill its row instead of leaving a hole beside it.
bool ToggleCard(const char* label, bool* v, const char* hint = nullptr, int span = 1);
// `live` marks the card as driven by an enabled feature (accent edge + bloom).
bool SliderCard(const char* label, float* v, float vmin, float vmax,
                const char* fmt = "%.0f", float power = 1.0f,
                float default_value = 0.0f, bool has_default = false, bool live = false,
                int span = 1);
bool SliderCardInt(const char* label, int* v, int vmin, int vmax,
                   const char* fmt = "%d", int default_value = 0, int span = 1);
bool ComboCard(const char* label, int* current, const char* const* items, int count,
               int span = 1);
bool ButtonCard(const char* label, const char* button_text,
                ButtonKind kind = ButtonKind_Default, const char* value_text = nullptr,
                int span = 1);
// `live` marks the card the way a toggle card does when its setting is on: the
// accent edge. A bind is only worth a glance if it says whether the feature it
// belongs to is actually armed, so the caller passes that switch in.
bool KeyCard(const char* label, const char* key_name, bool waiting = false,
             bool live = false, int span = 1);
bool ColorCard(const char* label, float col[4], bool has_alpha = true, int span = 1);
// `span` merges grid columns (2 = full width, the default; 1 to sit beside
// another card in a two-column grid).
void StatCard(const char* title, const char* const* keys, const char* const* values,
              int count, const ImVec4* tints = nullptr, int span = 2);
void NoteCard(const char* title, const char* body);
bool ScrollCardBegin(const char* title, float height);
void ScrollCardEnd();

// =============================================================================
//  Page furniture
// =============================================================================
// Title block: big tracked caps, dim subtitle, optional chip on the right.
void PageTitle(const char* title, const char* subtitle,
               const char* chip = nullptr, const ImVec4* chip_tint = nullptr);

// Divider with a micro-caps label, running across the content area.
void GroupLabel(const char* text);

// Preset accent swatches: ring, glow, hover bloom, check when selected.
// Returns true when *selected changed.
bool AccentDots(const char* const* names, const unsigned int* rgbs, int count,
                int* selected, float size = 30.0f, bool show_names = true);

} // namespace obsidian
