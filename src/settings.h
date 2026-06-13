#pragma once
#include <string>

// ─── Client settings (persisted to settings.cfg) ──────────────────────────────
struct ClientSettings {
    bool sounds_enabled    = false;  // Off by default (Beta feature)
    int  sound_volume      = 5;      // 1..10
    bool scanlines         = true;
    int  scanline_bright   = 3;      // 1..10
    int  term_buffer_size  = 128;    // 30..500 lines
    int  language          = 1;      // 0 = ENG, 1 = RU (RU community by default)
    bool welcome_logo      = true;   // show the daily logo on every login
    int  fps_limit         = 60;     // 15, 30 or 60 — frame cap (vsync stays on at 60)
    bool remember_login    = false;  // remember nickname on the login screen
    bool remember_pass     = false;  // remember password (implies remember_login)
};

// Remembered login credentials (persisted to login.cfg, separate from the
// int-only settings.cfg). Loaded at startup into the login fields.
struct SavedLogin {
    std::string nick;
    std::string pass;
};
extern SavedLogin g_saved_login;

void saved_login_load(const std::string& path);
void saved_login_save(const std::string& path);

extern ClientSettings g_settings;
extern bool           g_settings_open;  // overlay visible?
extern int            g_term_buf_size;  // sync from g_settings.term_buffer_size

void settings_load(const std::string& path);
void settings_save(const std::string& path);

// Render the settings overlay — call from terminal_render when g_settings_open
void settings_render(int W, int H);

// Key/mouse input for overlay
// Returns true if the event was consumed (overlay handled it)
bool settings_on_key(int key, int action);
bool settings_on_mouse_button(float mx, float my, int button, int action);
bool settings_on_cursor(float mx, float my);
