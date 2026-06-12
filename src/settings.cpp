#include "settings.h"
#include "sound.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <fstream>
#include <algorithm>

// ─── Globals ──────────────────────────────────────────────────────────────────
ClientSettings g_settings;
bool           g_settings_open = false;

// ─── Persist ──────────────────────────────────────────────────────────────────
void settings_load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string key;
    int val;
    while (f >> key >> val) {
        if (key == "sounds")           g_settings.sounds_enabled  = (val != 0);
        else if (key == "sound_volume") g_settings.sound_volume   = std::clamp(val, 1, 10);
        else if (key == "scanlines")    g_settings.scanlines       = (val != 0);
        else if (key == "scanline_bright") g_settings.scanline_bright = std::clamp(val, 1, 10);
    }
}

void settings_save(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "sounds "           << (g_settings.sounds_enabled ? 1 : 0) << "\n";
    f << "sound_volume "     << g_settings.sound_volume              << "\n";
    f << "scanlines "        << (g_settings.scanlines ? 1 : 0)       << "\n";
    f << "scanline_bright "  << g_settings.scanline_bright            << "\n";
}

// ─── Overlay state ────────────────────────────────────────────────────────────
static int s_selected = 0;  // currently focused row

struct SettingRow {
    const char* label;
    enum Type { TOGGLE, RANGE } type;
    int min_val, max_val;
};

static const SettingRow ROWS[] = {
    { "Sounds",           SettingRow::TOGGLE, 0, 1  },
    { "Sound Volume",     SettingRow::RANGE,  1, 10 },
    { "Scanlines",        SettingRow::TOGGLE, 0, 1  },
    { "Scanline Bright",  SettingRow::RANGE,  1, 10 },
};
static constexpr int ROW_COUNT = 4;

static int get_value(int idx) {
    switch (idx) {
    case 0: return g_settings.sounds_enabled  ? 1 : 0;
    case 1: return g_settings.sound_volume;
    case 2: return g_settings.scanlines       ? 1 : 0;
    case 3: return g_settings.scanline_bright;
    default: return 0;
    }
}

static void set_value(int idx, int v) {
    switch (idx) {
    case 0: g_settings.sounds_enabled   = (v != 0); break;
    case 1: g_settings.sound_volume     = std::clamp(v, 1, 10); break;
    case 2: g_settings.scanlines        = (v != 0); break;
    case 3: g_settings.scanline_bright  = std::clamp(v, 1, 10); break;
    }
}

static void change_value(int idx, int delta) {
    const auto& row = ROWS[idx];
    int v = get_value(idx);
    if (row.type == SettingRow::TOGGLE)
        v = (v == 0) ? 1 : 0;
    else
        v = std::clamp(v + delta, row.min_val, row.max_val);
    set_value(idx, v);
    if (g_settings.sounds_enabled)
        sound_play(SoundEvent::KEY_TYPE);
}

// ─── Forward declarations for draw helpers (implemented in terminal.cpp) ──────
// We re-implement small local versions here to stay self-contained.

static float s_cell_h = 23.0f;  // approximate, matches terminal font size

// We can't call terminal's internal draw_string from here directly, so we use
// GLFW/OpenGL primitives. The font texture belongs to terminal.cpp. Instead we
// rely on the extern below — terminal exposes a simple public helper.

// Public helper declared in terminal.h — draws text and returns x advance.
extern float term_draw_string(float x, float y, const char* text,
                              float r, float g, float b, float a);
extern float term_string_width(const char* text);
extern float term_cell_h();

// ─── Geometry helpers ─────────────────────────────────────────────────────────
static void draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a = 1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glVertex2f(x,   y);   glVertex2f(x+w, y);
        glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}

static void draw_rect_outline(float x, float y, float w, float h,
                               float r, float g, float b, float lw = 1.2f) {
    glColor3f(r, g, b);
    glLineWidth(lw);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x,   y);   glVertex2f(x+w, y);
        glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}

// ─── Mouse hit tracking ───────────────────────────────────────────────────────
struct RowHit {
    float x, y, w, h;
    int   row;
    int   delta;  // -1 left arrow, +1 right arrow, 0 = row body
};

static RowHit  s_hits[ROW_COUNT * 3 + 4];  // left/right/body per row + close btn
static int     s_hit_count   = 0;
static int     s_hovered_hit = -1;  // index into s_hits

static void register_hit(float x, float y, float w, float h, int row, int delta) {
    if (s_hit_count >= (int)(sizeof(s_hits)/sizeof(s_hits[0]))) return;
    s_hits[s_hit_count++] = {x, y, w, h, row, delta};
}

static int find_hit(float mx, float my) {
    for (int i = 0; i < s_hit_count; ++i) {
        const auto& h = s_hits[i];
        if (mx >= h.x && mx <= h.x+h.w && my >= h.y && my <= h.y+h.h)
            return i;
    }
    return -1;
}

// ─── Render ───────────────────────────────────────────────────────────────────
void settings_render(int W, int H) {
    s_hit_count   = 0;
    s_hovered_hit = -1;

    float ch = term_cell_h();

    // Window size
    const float WIN_W = 420.0f;
    const float ROW_H = ch + 12.0f;
    const float HDR_H = ch + 16.0f;
    const float PAD   = 20.0f;
    const float WIN_H = HDR_H + PAD + ROW_COUNT * ROW_H + PAD + ROW_H; // +1 for close btn row
    const float WX    = (W - WIN_W) * 0.5f;
    const float WY    = (H - WIN_H) * 0.5f;

    // Dark backdrop (dim the terminal behind)
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f((float)W,0);
        glVertex2f((float)W,(float)H); glVertex2f(0,(float)H);
    glEnd();

    // Window background
    draw_rect(WX, WY, WIN_W, WIN_H, 0.04f, 0.04f, 0.12f);
    // Outer glow border
    draw_rect_outline(WX-1, WY-1, WIN_W+2, WIN_H+2, 0.10f, 0.15f, 0.50f, 1.0f);
    draw_rect_outline(WX,   WY,   WIN_W,   WIN_H,   0.22f, 0.32f, 0.90f, 1.8f);

    // Header
    draw_rect(WX, WY, WIN_W, HDR_H, 0.06f, 0.06f, 0.18f);
    const char* title = "//  SETTINGS  //";
    float tw = term_string_width(title);
    term_draw_string(WX + (WIN_W - tw)*0.5f, WY + 6.0f, title,
                     0.45f, 0.65f, 1.0f, 1.0f);

    // Separator under header
    glColor4f(0.18f, 0.22f, 0.60f, 0.9f); glLineWidth(1.0f);
    glBegin(GL_LINES);
        glVertex2f(WX, WY+HDR_H); glVertex2f(WX+WIN_W, WY+HDR_H);
    glEnd();

    // Rows
    for (int i = 0; i < ROW_COUNT; ++i) {
        float ry = WY + HDR_H + PAD + i * ROW_H;
        bool focused = (i == s_selected);

        // Row highlight
        if (focused) {
            draw_rect(WX+4, ry-2, WIN_W-8, ROW_H-2,
                      0.08f, 0.12f, 0.30f, 1.0f);
            draw_rect_outline(WX+4, ry-2, WIN_W-8, ROW_H-2,
                               0.25f, 0.40f, 0.90f, 1.2f);
        }

        // Register body hit for row focus
        register_hit(WX+4, ry-2, WIN_W-8, ROW_H-2, i, 0);

        // Label
        float lr = focused ? 0.85f : 0.55f;
        float lg = focused ? 0.92f : 0.65f;
        float lb = focused ? 1.00f : 0.85f;
        term_draw_string(WX + 16.0f, ry + 2.0f, ROWS[i].label, lr, lg, lb, 1.0f);

        // Value display
        const auto& row = ROWS[i];
        int v = get_value(i);
        char val_str[32];
        if (row.type == SettingRow::TOGGLE) {
            std::snprintf(val_str, sizeof(val_str), "%s", v ? "On" : "Off");
        } else {
            std::snprintf(val_str, sizeof(val_str), "%d", v);
        }

        // ← arrow
        const float ARR_W = 28.0f;
        const float VAL_AREA_W = 130.0f;
        float vx = WX + WIN_W - VAL_AREA_W - 16.0f;
        float vy = ry + 2.0f;

        // Left arrow button
        bool l_hover = false;
        float l_ax = vx, l_ay = ry - 2, l_aw = ARR_W, l_ah = ROW_H - 2;
        register_hit(l_ax, l_ay, l_aw, l_ah, i, -1);
        if (focused) {
            float fc = l_hover ? 0.50f : 0.30f;
            draw_rect(l_ax, l_ay, l_aw, l_ah, fc*0.4f, fc*0.6f, fc, 0.8f);
            draw_rect_outline(l_ax, l_ay, l_aw, l_ah, 0.20f, 0.30f, 0.70f);
        }
        term_draw_string(vx + 7.0f, vy, "<", focused ? 0.6f : 0.3f, focused ? 0.8f : 0.4f, 1.0f, 1.0f);

        // Value text (centered)
        float vw = term_string_width(val_str);
        float vcx = vx + ARR_W + (VAL_AREA_W - 2*ARR_W - vw) * 0.5f;
        float vc = focused ? 1.0f : 0.7f;
        if (row.type == SettingRow::TOGGLE) {
            float vr = v ? 0.30f*vc : 0.70f*vc;
            float vg = v ? 0.90f*vc : 0.35f*vc;
            float vb = v ? 0.40f*vc : 0.35f*vc;
            term_draw_string(vcx, vy, val_str, vr, vg, vb, 1.0f);
        } else {
            term_draw_string(vcx, vy, val_str, 0.85f*vc, 0.92f*vc, 1.0f*vc, 1.0f);
        }

        // Right arrow button
        float r_ax = vx + VAL_AREA_W - ARR_W, r_ay = ry - 2, r_aw = ARR_W, r_ah = ROW_H - 2;
        register_hit(r_ax, r_ay, r_aw, r_ah, i, +1);
        if (focused) {
            draw_rect(r_ax, r_ay, r_aw, r_ah, 0.12f, 0.18f, 0.40f, 0.8f);
            draw_rect_outline(r_ax, r_ay, r_aw, r_ah, 0.20f, 0.30f, 0.70f);
        }
        term_draw_string(r_ax + 8.0f, vy, ">", focused ? 0.6f : 0.3f, focused ? 0.8f : 0.4f, 1.0f, 1.0f);
    }

    // Hint bar at bottom
    const char* hint = "\xe2\x86\x91\xe2\x86\x93 select   \xe2\x86\x90\xe2\x86\x92 / Enter change   Esc close";
    float hw = term_string_width(hint);
    float hy = WY + WIN_H - ROW_H + 4.0f;

    // Separator above hint
    glColor4f(0.12f, 0.16f, 0.45f, 0.7f); glLineWidth(1.0f);
    glBegin(GL_LINES);
        glVertex2f(WX+8, hy-8); glVertex2f(WX+WIN_W-8, hy-8);
    glEnd();

    term_draw_string(WX + (WIN_W - hw)*0.5f, hy, hint, 0.28f, 0.33f, 0.55f, 1.0f);
}

// ─── Input ────────────────────────────────────────────────────────────────────
bool settings_on_key(int key, int action) {
    if (!g_settings_open) return false;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return true; // consume but ignore

    if (key == GLFW_KEY_ESCAPE) {
        g_settings_open = false;
        settings_save("settings.cfg");
        if (g_settings.sounds_enabled) sound_play(SoundEvent::CMD_CLEAR);
        return true;
    }
    if (key == GLFW_KEY_UP) {
        s_selected = (s_selected - 1 + ROW_COUNT) % ROW_COUNT;
        if (g_settings.sounds_enabled) sound_play(SoundEvent::KEY_TYPE);
        return true;
    }
    if (key == GLFW_KEY_DOWN) {
        s_selected = (s_selected + 1) % ROW_COUNT;
        if (g_settings.sounds_enabled) sound_play(SoundEvent::KEY_TYPE);
        return true;
    }
    if (key == GLFW_KEY_LEFT) {
        change_value(s_selected, -1);
        settings_save("settings.cfg");
        return true;
    }
    if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        change_value(s_selected, +1);
        settings_save("settings.cfg");
        return true;
    }
    return true; // consume all keys while open
}

bool settings_on_cursor(float mx, float my) {
    if (!g_settings_open) return false;
    s_hovered_hit = find_hit(mx, my);
    return true;
}

bool settings_on_mouse_button(float mx, float my, int button, int action) {
    if (!g_settings_open) return false;
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return true;

    int hit = find_hit(mx, my);
    if (hit < 0) {
        // Click outside window — close
        g_settings_open = false;
        settings_save("settings.cfg");
        if (g_settings.sounds_enabled) sound_play(SoundEvent::CMD_CLEAR);
        return true;
    }

    const auto& h = s_hits[hit];
    s_selected = h.row;
    if (h.delta != 0) {
        change_value(h.row, h.delta);
        settings_save("settings.cfg");
    }
    return true;
}
