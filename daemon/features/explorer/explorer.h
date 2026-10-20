#pragma once
// -----------------------------------------------------------------------------
//  features/explorer/explorer.h  --  the DataModel object browser (dex)
// -----------------------------------------------------------------------------
//  The reference's misc/explorer is a full Dex clone: a background BFS over the
//  DataModel, a published snapshot, and a tree window. The parts of it that made
//  it a *browser* rather than a screenshot are kept here:
//
//    * the walk happens OFF the render thread, on its own thread, with a node cap
//      and a wall-clock budget - a 200k-instance showcase cannot stall a frame;
//    * the published snapshot is immutable (shared_ptr<const Snap>), so the
//      render thread never holds a lock while drawing and never sees a half-built
//      tree;
//    * names and class names are read when a row is DRAWN, not when the tree is
//      built. That is the difference between 20k string reads per sweep and ~30
//      per frame, and it also means a renamed object shows its new name without a
//      rebuild.
//
//  Dropped from the reference on purpose: winhttp (no backend), commdlg file
//  dialogs, and the embedded dex_icons asset table (no icon credentials). The
//  browser is the feature; the icons were decoration.
// -----------------------------------------------------------------------------

namespace features {
namespace explorer {

// Starts the background thread on first call and asks for a sweep when the
// published snapshot is stale. Safe to call every feature-loop pass.
void Run();

// The browser, drawn INTO the menu page that hosts it (the dex tab). No window,
// no Begin/End: the menu owns the frame. Also stamps "the tab is visible", which
// is what makes Run() auto-refresh only while somebody can see it.
void DrawPanel();

// Ask for a fresh walk (the window's Refresh button, a place change).
void RequestRefresh();

// Ask for a search of `text` to be resolved in the background. An empty string
// clears the results and returns the window to the tree.
void RequestSearch(const char* text);

}  // namespace explorer
}  // namespace features
