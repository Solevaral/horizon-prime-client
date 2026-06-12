#pragma once
#include <string>

// ─── Client settings (persisted to settings.cfg) ──────────────────────────────
struct ClientSettings {
    bool sounds_enabled  = true;
    int  sound_volume    = 5;    // 1..10
    bool scanlines       = true;
    int  scanline_bright = 3;    // 1..10
};

extern ClientSettings g_settings;
extern bool           g_settings_open;  // overlay visible?

void settings_load(const std::string& path);
void settings_save(const std::string& path);

// Render the settings overlay — call from terminal_render when g_settings_open
void settings_render(int W, int H);

// Key/mouse input for overlay
// Returns true if the event was consumed (overlay handled it)
bool settings_on_key(int key, int action);
bool settings_on_mouse_button(float mx, float my, int button, int action);
bool settings_on_cursor(float mx, float my);
