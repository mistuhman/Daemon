#pragma once

namespace cache { struct EspEntity; }   // only ever passed by reference here

namespace features {
    // The one projection in the app. Anything that draws a world point on the
    // overlay must go through this, not build its own: it is the only version
    // that also converts Roblox's CLIENT-space pixels into the overlay's
    // desktop-space ones. A second copy (the waypoint markers had one) is
    // correct only when the game happens to be borderless-fullscreen at the
    // desktop's own resolution, and drifts by the window origin otherwise.
    // Returns false when the point is behind the camera or unprojectable.
    bool ProjectToOverlay(float wx, float wy, float wz, float& out_x, float& out_y);

    // Canonical Roblox body-part size in studs, by part name. This client reports
    // ~0 from Primitive::Size, so the whole tool draws (and raycasts against)
    // parts at their canonical sizes instead of collapsing them to a point.
    // Shared with features/raycast/world_ray.cpp: "the box I see" and "the box
    // that blocks" must be the same box.
    void CanonicalPartSize(const char* part_name, float& out_x, float& out_y, float& out_z);

    // The screen box the ESP drew for this entity this frame, AFTER the far-range
    // fallback (tiny projections are inflated to a legible marker). This is the
    // one place that decides what "that player's box" means, so the triggerbot's
    // crosshair test and the pixels on screen cannot disagree.
    bool EntityScreenBox(const cache::EspEntity& entity, float& x1, float& y1, float& x2, float& y2);

    // Camera world position, read live. Same source the ESP projects with.
    bool CameraPosition(float& out_x, float& out_y, float& out_z);

    // A marker on the head (reference: visual/headdot) and screen-edge arrows for
    // players the game is not showing (reference: visual/oof). Both are ESP draws
    // and live in esp.cpp: they need the same camera, the same projection and the
    // same range gate as everything else in this file, and a second copy of that
    // plumbing is how the overlay and the game's window drift apart.
    void RenderHeadDot();
    void RenderOof();

    void RenderESP();
    void RenderAimViewer();
    void RenderSkeletonESP();
    void RenderChinaHatESP();
    void RenderChams();
    // Solid, backface-culled chams of the character's own parts ("mesh chams").
    void RenderMeshChams();
    void RenderExpandedHitbox();
}

