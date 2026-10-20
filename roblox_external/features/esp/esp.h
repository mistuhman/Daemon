#pragma once

namespace features {
    // The one projection in the app. Anything that draws a world point on the
    // overlay must go through this, not build its own: it is the only version
    // that also converts Roblox's CLIENT-space pixels into the overlay's
    // desktop-space ones. A second copy (the waypoint markers had one) is
    // correct only when the game happens to be borderless-fullscreen at the
    // desktop's own resolution, and drifts by the window origin otherwise.
    // Returns false when the point is behind the camera or unprojectable.
    bool ProjectToOverlay(float wx, float wy, float wz, float& out_x, float& out_y);

    void RenderESP();
    void RenderAimViewer();
    void RenderSkeletonESP();
    void RenderChinaHatESP();
    void RenderChams();
    void RenderExpandedHitbox();
}

