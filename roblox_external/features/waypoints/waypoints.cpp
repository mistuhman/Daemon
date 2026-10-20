#include "waypoints.h"
#include "features/players/players.h"
#include "features/esp/esp.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"
#include "imgui/imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace features {

void AddWaypointAt(const char* name, float x, float y, float z)
{
    if (waypoint_n >= kMaxWaypoints) {
        snprintf(players_status_msg, sizeof(players_status_msg), "Waypoint list is full");
        return;
    }
    int i = waypoint_n++;
    if (name && name[0]) snprintf(waypoint_names[i], kWaypointNameLen, "%s", name);
    else snprintf(waypoint_names[i], kWaypointNameLen, "wp %d", i + 1);
    waypoint_x[i] = x;
    waypoint_y[i] = y;
    waypoint_z[i] = z;
    snprintf(players_status_msg, sizeof(players_status_msg),
             "Pinned %s at %.0f, %.0f, %.0f", waypoint_names[i], x, y, z);
}

void FillWaypointEdit()
{
    const cache::LocalPlayerData lp = cache::GetLocalPlayer();
    if (!lp.valid || !is_valid_address(lp.hrp_primitive)) {
        snprintf(players_status_msg, sizeof(players_status_msg), "No character to read xyz from");
        return;
    }
    float pos[3] = { lp.x, lp.y, lp.z };
    read_raw(lp.hrp_primitive + Offsets::Primitive::Position, pos, sizeof(pos));
    waypoint_edit_x = pos[0];
    waypoint_edit_y = pos[1];
    waypoint_edit_z = pos[2];
    snprintf(players_status_msg, sizeof(players_status_msg),
             "xyz %.1f, %.1f, %.1f", pos[0], pos[1], pos[2]);
}

void AddWaypointHere(const char* name)
{
    const cache::LocalPlayerData lp = cache::GetLocalPlayer();
    if (!lp.valid || !is_valid_address(lp.hrp_primitive)) {
        snprintf(players_status_msg, sizeof(players_status_msg), "No character to pin a waypoint from");
        return;
    }
    float pos[3] = { lp.x, lp.y, lp.z };
    read_raw(lp.hrp_primitive + Offsets::Primitive::Position, pos, sizeof(pos));
    waypoint_edit_x = pos[0];
    waypoint_edit_y = pos[1];
    waypoint_edit_z = pos[2];
    AddWaypointAt(name, pos[0], pos[1], pos[2]);
}

void RemoveWaypoint(int i)
{
    if (i < 0 || i >= waypoint_n) return;
    for (int k = i; k < waypoint_n - 1; ++k) {
        memcpy(waypoint_names[k], waypoint_names[k + 1], kWaypointNameLen);
        waypoint_x[k] = waypoint_x[k + 1];
        waypoint_y[k] = waypoint_y[k + 1];
        waypoint_z[k] = waypoint_z[k + 1];
    }
    --waypoint_n;
    waypoint_names[waypoint_n][0] = '\0';
}

void GotoWaypoint(int i)
{
    if (i < 0 || i >= waypoint_n) return;
    // WriteRootTo, not TeleportToWorld: a waypoint already IS a root-part
    // position (that is what PIN recorded), so the "land above by" lift that
    // exists for standing on top of another player's feet does not apply. With
    // it applied, pinning where you stand and going back put you 2 studs higher
    // every round trip, and each of those was a short fall the server had to
    // reconcile - the drift people read as the waypoint not working.
    WriteRootTo(waypoint_x[i], waypoint_y[i], waypoint_z[i], waypoint_names[i]);
}

void RenderWaypoints()
{
    if (!show_waypoints || waypoint_n < 1) return;
    if (!g_base_address) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    const cache::LocalPlayerData lp = cache::GetLocalPlayer();

    for (int i = 0; i < waypoint_n; ++i) {
        // ESP's projection, not a private copy of it: the old one here skipped
        // the client-space -> desktop-space step, so every marker was off by the
        // Roblox window's origin (and by the DPI/render-scale factor) whenever
        // the game was not borderless at desktop resolution. That is why a
        // waypoint could sit nowhere near the place it was pinned.
        float x = 0.0f, y = 0.0f;
        if (!ProjectToOverlay(waypoint_x[i], waypoint_y[i], waypoint_z[i], x, y)) continue;
        float dist = 0;
        if (lp.valid) {
            float ax = waypoint_x[i] - lp.x, ay = waypoint_y[i] - lp.y, az = waypoint_z[i] - lp.z;
            dist = sqrtf(ax*ax+ay*ay+az*az);
        }
        char lab[64];
        snprintf(lab, sizeof(lab), "%s  %.0f", waypoint_names[i], dist);
        ImVec2 ts = ImGui::CalcTextSize(lab);
        dl->AddRectFilled(ImVec2(x - ts.x * 0.5f - 4, y - ts.y - 2),
                          ImVec2(x + ts.x * 0.5f + 4, y + 2), IM_COL32(0, 0, 0, 160), 3.0f);
        dl->AddCircleFilled(ImVec2(x, y + 6), 3.0f, IM_COL32(255, 220, 80, 230));
        dl->AddText(ImVec2(x - ts.x * 0.5f, y - ts.y - 1), IM_COL32(255, 255, 255, 240), lab);
    }
}

} // namespace features
