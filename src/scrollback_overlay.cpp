#include "scrollback_overlay.h"
#include "state.h"
#include "terminal.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>

int  g_scr_scroll_offset = 0;

// Layout constants
static const float WIN_MARGIN_X  = 80.0f;
static const float WIN_MARGIN_Y  = 60.0f;
static const float TITLE_H       = 36.0f;
static const float FOOTER_H      = 28.0f;
static const float TEXT_PAD_X    = 20.0f;
static const float SCROLLBAR_W   = 10.0f;
static const float SCROLLBAR_PAD =  4.0f;

void scr_render(int W, int H) {
    float win_x = WIN_MARGIN_X;
    float win_y = WIN_MARGIN_Y;
    float win_w = (float)W - WIN_MARGIN_X * 2.0f;
    float win_h = (float)H - WIN_MARGIN_Y * 2.0f;

    // Dim background
    glColor4f(0.0f, 0.0f, 0.0f, 0.72f);
    glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
    glEnd();

    // Window background
    glColor4f(0.02f, 0.02f, 0.08f, 0.97f);
    glBegin(GL_QUADS);
        glVertex2f(win_x, win_y); glVertex2f(win_x + win_w, win_y);
        glVertex2f(win_x + win_w, win_y + win_h); glVertex2f(win_x, win_y + win_h);
    glEnd();

    // Border
    glColor4f(0.2f, 0.3f, 0.6f, 0.8f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(win_x, win_y); glVertex2f(win_x + win_w, win_y);
        glVertex2f(win_x + win_w, win_y + win_h); glVertex2f(win_x, win_y + win_h);
    glEnd();

    // Title bar separator
    float title_sep = win_y + TITLE_H;
    glColor4f(0.15f, 0.22f, 0.50f, 0.6f);
    glBegin(GL_LINES);
        glVertex2f(win_x + 1.0f, title_sep); glVertex2f(win_x + win_w - 1.0f, title_sep);
    glEnd();

    // Title
    term_draw_string(win_x + TEXT_PAD_X, win_y + 10.0f, "// SCROLLBACK //",
                     0.35f, 0.70f, 0.95f, 1.0f);

    // Footer separator
    float footer_sep = win_y + win_h - FOOTER_H;
    glColor4f(0.15f, 0.22f, 0.50f, 0.6f);
    glBegin(GL_LINES);
        glVertex2f(win_x + 1.0f, footer_sep); glVertex2f(win_x + win_w - 1.0f, footer_sep);
    glEnd();

    // Text area geometry
    float text_x     = win_x + TEXT_PAD_X;
    float text_y_top = title_sep + 6.0f;
    float text_y_bot = footer_sep - 4.0f;
    float text_h     = text_y_bot - text_y_top;
    float cell_h     = term_cell_h();
    int   visible    = std::max(1, (int)(text_h / cell_h));
    float max_text_w = win_w - TEXT_PAD_X * 2.0f - SCROLLBAR_W - SCROLLBAR_PAD * 2.0f;

    int total_lines = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        total_lines = (int)g_lines.size();
    }
    int max_offset = std::max(0, total_lines - visible);
    g_scr_scroll_offset = std::clamp(g_scr_scroll_offset, 0, max_offset);

    // Lines
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        int end_idx   = total_lines - g_scr_scroll_offset;
        int start_idx = std::max(0, end_idx - visible);

        float ty = text_y_top;
        for (int i = start_idx; i < end_idx; ++i) {
            const auto& ln = g_lines[i];
            float r = ln.r / 255.0f;
            float g = ln.g / 255.0f;
            float b = ln.b / 255.0f;

            // Clip text to available width
            std::string text = ln.text;
            while (!text.empty() && term_string_width(text.c_str()) > max_text_w) {
                // remove last UTF-8 char
                size_t n = text.size();
                if ((uint8_t)text[n-1] < 0x80) { text.resize(n-1); }
                else {
                    while (n > 0 && ((uint8_t)text[n-1] & 0xC0) == 0x80) n--;
                    if (n > 0) n--;
                    text.resize(n);
                }
            }

            term_draw_string(text_x, ty, text.c_str(), r, g, b, 1.0f);
            ty += cell_h;
        }
    }

    // ── Scrollbar ─────────────────────────────────────────────────────────────
    float sb_x      = win_x + win_w - SCROLLBAR_W - SCROLLBAR_PAD;
    float sb_y_top  = text_y_top;
    float sb_h      = text_h;

    // Track
    glColor4f(0.08f, 0.10f, 0.20f, 1.0f);
    glBegin(GL_QUADS);
        glVertex2f(sb_x, sb_y_top);
        glVertex2f(sb_x + SCROLLBAR_W, sb_y_top);
        glVertex2f(sb_x + SCROLLBAR_W, sb_y_top + sb_h);
        glVertex2f(sb_x, sb_y_top + sb_h);
    glEnd();

    if (total_lines > visible) {
        float ratio     = (float)visible / (float)total_lines;
        float thumb_h   = std::max(20.0f, sb_h * ratio);
        // scroll position: 0 = bottom (newest), max_offset = top (oldest)
        float scroll_frac = (max_offset > 0) ? (float)g_scr_scroll_offset / (float)max_offset : 0.0f;
        float thumb_y = sb_y_top + (sb_h - thumb_h) * scroll_frac;

        glColor4f(0.3f, 0.45f, 0.80f, 0.85f);
        glBegin(GL_QUADS);
            glVertex2f(sb_x + 1.0f, thumb_y);
            glVertex2f(sb_x + SCROLLBAR_W - 1.0f, thumb_y);
            glVertex2f(sb_x + SCROLLBAR_W - 1.0f, thumb_y + thumb_h);
            glVertex2f(sb_x + 1.0f, thumb_y + thumb_h);
        glEnd();
    }

    // Footer info
    char info[96];
    int show_start = total_lines > 0 ? (std::max(0, total_lines - visible - g_scr_scroll_offset) + 1) : 0;
    int show_end   = std::max(0, total_lines - g_scr_scroll_offset);
    std::snprintf(info, sizeof(info), "↑↓ scroll | wheel | Esc close  [%d-%d / %d lines]",
        show_start, show_end, total_lines);
    term_draw_string(win_x + TEXT_PAD_X, footer_sep + 6.0f, info, 0.40f, 0.52f, 0.72f, 1.0f);
}

bool scr_on_key(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;

    int total_lines = (int)g_lines.size();
    int visible     = 16; // rough — exact clamping happens in scr_render
    int max_offset  = std::max(0, total_lines - visible);

    if (key == GLFW_KEY_UP || key == GLFW_KEY_PAGE_UP) {
        int step = (key == GLFW_KEY_PAGE_UP) ? 5 : 1;
        g_scr_scroll_offset = std::min(g_scr_scroll_offset + step, max_offset);
        return true;
    }
    if (key == GLFW_KEY_DOWN || key == GLFW_KEY_PAGE_DOWN) {
        int step = (key == GLFW_KEY_PAGE_DOWN) ? 5 : 1;
        g_scr_scroll_offset = std::max(g_scr_scroll_offset - step, 0);
        return true;
    }
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER) {
        g_scr_open = false;
        g_scr_scroll_offset = 0;
        return true;
    }
    return false;
}

void scr_on_scroll(double yoffset) {
    int total_lines = (int)g_lines.size();
    int max_offset  = std::max(0, total_lines - 1);
    int delta = (yoffset > 0) ? 3 : -3;
    g_scr_scroll_offset = std::clamp(g_scr_scroll_offset + delta, 0, max_offset);
}
