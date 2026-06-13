#pragma once

// Galaxy map overlay — a pseudo-3D isometric view of the sector grid showing
// the player's position, online players and nearby procedurally-generated
// stars. Opened with 'M' (or the `map` command). Rotated with arrow keys.

extern bool g_map_open;

void map_render(int W, int H, float dt);
bool map_on_key(int key, int action);
void map_on_scroll(double yoffset);

// Docked corner mini-map: auto-rotating, drawn into a rect. Always-on HUD.
void map_widget_render(float x, float y, float w, float h, float dt);
