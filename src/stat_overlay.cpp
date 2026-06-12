#include "stat_overlay.h"
#include "state.h"
#include "terminal.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <algorithm>
#include <string>

bool g_stat_open = false;

// Player stats structure matching server's PlayerStats
struct DisplayStats {
    int played_minutes;
    int ships_destroyed;
    int npcs_destroyed;
    int quests_completed;
    int jumps_made;
    int pms_received;
    std::string created_at;
    std::string last_online;
};

static DisplayStats g_display_stats = {};
static bool g_stat_needs_update = false;

void stat_open() {
    g_stat_open = true;
    g_stat_needs_update = true;
}

void stat_set_data(int played_min, int ships, int npcs, int quests, int jumps, int pms,
                    const std::string& created, const std::string& online) {
    g_display_stats.played_minutes = played_min;
    g_display_stats.ships_destroyed = ships;
    g_display_stats.npcs_destroyed = npcs;
    g_display_stats.quests_completed = quests;
    g_display_stats.jumps_made = jumps;
    g_display_stats.pms_received = pms;
    g_display_stats.created_at = created;
    g_display_stats.last_online = online;
    g_stat_needs_update = false;
}

void stat_render(int W, int H) {
    float cx = W * 0.5f, cy = H * 0.5f;

    // Dark semi-transparent backdrop
    glColor4f(0, 0, 0, 0.7f);
    glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
    glEnd();

    // Window box (centered)
    float win_w = 400.0f, win_h = 360.0f;
    float win_x = cx - win_w * 0.5f, win_y = cy - win_h * 0.5f;

    // Background
    glColor4f(0.05f, 0.05f, 0.15f, 0.95f);
    glBegin(GL_QUADS);
        glVertex2f(win_x, win_y); glVertex2f(win_x + win_w, win_y);
        glVertex2f(win_x + win_w, win_y + win_h); glVertex2f(win_x, win_y + win_h);
    glEnd();

    // Blue border with glow
    glColor4f(0.2f, 0.4f, 1.0f, 0.8f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(win_x, win_y); glVertex2f(win_x + win_w, win_y);
        glVertex2f(win_x + win_w, win_y + win_h); glVertex2f(win_x, win_y + win_h);
    glEnd();

    // Glow effect (outer line)
    glColor4f(0.3f, 0.5f, 1.0f, 0.3f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(win_x - 2, win_y - 2); glVertex2f(win_x + win_w + 2, win_y - 2);
        glVertex2f(win_x + win_w + 2, win_y + win_h + 2); glVertex2f(win_x - 2, win_y + win_h + 2);
    glEnd();

    // Title
    float ty = win_y + 16.0f;
    term_draw_string(win_x + 16.0f, ty, "// STATISTICS //", 0.35f, 0.80f, 1.0f, 1.0f);

    ty += term_cell_h() + 8.0f;

    // Stats lines
    char buf[128];
    std::snprintf(buf, sizeof(buf), "  Playtime: %d min", g_display_stats.played_minutes);
    term_draw_string(win_x + 20.0f, ty, buf, 0.85f, 0.92f, 1.0f, 1.0f);
    ty += term_cell_h();

    std::snprintf(buf, sizeof(buf), "  Ships destroyed: %d", g_display_stats.ships_destroyed);
    term_draw_string(win_x + 20.0f, ty, buf, 0.85f, 0.92f, 1.0f, 1.0f);
    ty += term_cell_h();

    std::snprintf(buf, sizeof(buf), "  NPCs destroyed: %d", g_display_stats.npcs_destroyed);
    term_draw_string(win_x + 20.0f, ty, buf, 0.85f, 0.92f, 1.0f, 1.0f);
    ty += term_cell_h();

    std::snprintf(buf, sizeof(buf), "  Quests completed: %d", g_display_stats.quests_completed);
    term_draw_string(win_x + 20.0f, ty, buf, 0.85f, 0.92f, 1.0f, 1.0f);
    ty += term_cell_h();

    std::snprintf(buf, sizeof(buf), "  Jumps made: %d", g_display_stats.jumps_made);
    term_draw_string(win_x + 20.0f, ty, buf, 0.85f, 0.92f, 1.0f, 1.0f);
    ty += term_cell_h();

    std::snprintf(buf, sizeof(buf), "  PMs received: %d", g_display_stats.pms_received);
    term_draw_string(win_x + 20.0f, ty, buf, 0.85f, 0.92f, 1.0f, 1.0f);
    ty += term_cell_h() + 6.0f;

    // Dates
    std::snprintf(buf, sizeof(buf), "  Created: %s", g_display_stats.created_at.c_str());
    term_draw_string(win_x + 20.0f, ty, buf, 0.70f, 0.75f, 0.85f, 1.0f);
    ty += term_cell_h();

    std::snprintf(buf, sizeof(buf), "  Last online: %s", g_display_stats.last_online.c_str());
    term_draw_string(win_x + 20.0f, ty, buf, 0.70f, 0.75f, 0.85f, 1.0f);

    // Hint at bottom
    ty = win_y + win_h - 20.0f;
    term_draw_string(win_x + 16.0f, ty, "Esc/Enter - close", 0.35f, 0.50f, 0.75f, 1.0f);
}

bool stat_on_key(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        g_stat_open = false;
        return true;
    }
    return false;
}
