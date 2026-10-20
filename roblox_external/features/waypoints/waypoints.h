#pragma once
namespace features {
void AddWaypointHere(const char* name);
void AddWaypointAt(const char* name, float x, float y, float z);
void FillWaypointEdit();
void GotoWaypoint(int i);
void RemoveWaypoint(int i);
void RenderWaypoints();
}
