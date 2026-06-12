#include "scrollback_overlay.h"
#include "state.h"
#include "terminal.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>

int  g_scr_scroll_offset = 0;

void scr_render(int W, int H) {
    float cx = W * 0.5f, cy = H * 0.5f;

    glColor4f(0, 0, 0, 0.7f);
    glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
    glEnd();

    // Window box
    float win_w = 600.0f, win_h = 400.0f;
    float win_x = cx - win_w * 0.5f, win_y = cy - win_h * 0.5f;

    // Background
    glColor4f(0.02f, 0.02f, 0.08f, 0.95f);
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

    // Title
    float ty = win_y + 16.0f;
    term_draw_string(win_x + 20.0f, ty, "// SCROLLBACK //", 0.35f, 0.70f, 0.95f, 1.0f);

    ty += term_cell_h() + 8.0f;

    int total_lines = (int)g_lines.size();
    int visible_per_screen = 16;  // approximate lines visible

    {
        std::lock_guard<std::mutex> lock(g_state_mutex);

        // Show lines from oldest (offset up) to newest (offset 0)
        int start_idx = std::max(0, total_lines - visible_per_screen - g_scr_scroll_offset);
        int end_idx = std::min(total_lines, total_lines - g_scr_scroll_offset);

        for (int i = start_idx; i < end_idx && i >= 0 && i < total_lines; ++i) {
            auto& ln = g_lines[i];
            float r = ln.r / 255.0f;
            float g = ln.g / 255.0f;
            float b = ln.b / 255.0f;
            term_draw_string(win_x + 20.0f, ty, ln.text.c_str(), r, g, b, 1.0f);
            ty += term_cell_h();
        }
    }

    // Info line at bottom
    ty = win_y + win_h - 20.0f;
    char info[96];
    std::snprintf(info, sizeof(info), "↑↓ scroll | Esc close  [%d-%d / %d lines]",
        std::max(0, total_lines - visible_per_screen - g_scr_scroll_offset),
        std::min(total_lines, total_lines - g_scr_scroll_offset), total_lines);
    term_draw_string(win_x + 20.0f, ty, info, 0.5f, 0.6f, 0.75f, 1.0f);
}

bool scr_on_key(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;

    int total_lines = (int)g_lines.size();
    int max_offset = std::max(0, total_lines - 16);

    if (key == GLFW_KEY_UP) {
        g_scr_scroll_offset = std::min(g_scr_scroll_offset + 1, max_offset);
        return true;
    }
    if (key == GLFW_KEY_DOWN) {
        g_scr_scroll_offset = std::max(g_scr_scroll_offset - 1, 0);
        return true;
    }
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER) {
        g_scr_open = false;
        g_scr_scroll_offset = 0;
        return true;
    }
    return false;
}
