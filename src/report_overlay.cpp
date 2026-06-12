#include "report_overlay.h"
#include "state.h"
#include "terminal.h"
#include "sound.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <algorithm>
#include <vector>
#include <string>
#include <mutex>

bool g_report_open = false;

// One parsed report record.
struct ReportRow {
    std::string id;
    std::string nick;     // staff mode only
    std::string date;
    std::string status;   // player mode only ("PENDING"/"ANSWERED")
    std::string text;
    std::string answer;   // player mode only
};

static int                    g_mode = 0;       // 0=staff, 1=player
static std::vector<ReportRow> g_rows;
static int                    g_sel = 0;
static int                    g_scroll = 0;     // first visible row index

// Split helper on a single-byte delimiter.
static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == d) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

void report_set_data(int mode, const std::string& blob) {
    g_mode = mode;
    g_rows.clear();
    g_sel = 0;
    g_scroll = 0;

    // Records separated by 0x1E. A trailing 0x1E leaves an empty final token.
    for (auto& rec : split(blob, '\x1E')) {
        if (rec.empty()) continue;
        auto f = split(rec, '\x1F');
        ReportRow row;
        if (mode == 0) {
            // id, nick, date, text
            if (f.size() >= 1) row.id   = f[0];
            if (f.size() >= 2) row.nick = f[1];
            if (f.size() >= 3) row.date = f[2];
            if (f.size() >= 4) row.text = f[3];
        } else {
            // id, date, status, text, answer
            if (f.size() >= 1) row.id     = f[0];
            if (f.size() >= 2) row.date   = f[1];
            if (f.size() >= 3) row.status = f[2];
            if (f.size() >= 4) row.text   = f[3];
            if (f.size() >= 5) row.answer = f[4];
        }
        g_rows.push_back(row);
    }
    g_report_open = true;
}

// Word-wrap `text` into lines no wider than `max_w` pixels.
static void wrap_text(const std::string& text, float max_w, std::vector<std::string>& out) {
    std::string line;
    std::string word;
    auto flush_word = [&]() {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (term_string_width(candidate.c_str()) > max_w && !line.empty()) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush_word();
        else if (c == '\n') { flush_word(); out.push_back(line); line.clear(); }
        else word += c;
    }
    flush_word();
    if (!line.empty()) out.push_back(line);
    if (out.empty()) out.push_back("");
}

void report_render(int W, int H) {
    // Backdrop
    glColor4f(0, 0, 0, 0.7f);
    glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
    glEnd();

    // Wide window: 80% width, 80% height, centered.
    float win_w = W * 0.80f, win_h = H * 0.80f;
    float win_x = (W - win_w) * 0.5f, win_y = (H - win_h) * 0.5f;

    glColor4f(0.05f, 0.05f, 0.15f, 0.96f);
    glBegin(GL_QUADS);
        glVertex2f(win_x, win_y); glVertex2f(win_x + win_w, win_y);
        glVertex2f(win_x + win_w, win_y + win_h); glVertex2f(win_x, win_y + win_h);
    glEnd();

    glColor4f(0.2f, 0.4f, 1.0f, 0.8f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(win_x, win_y); glVertex2f(win_x + win_w, win_y);
        glVertex2f(win_x + win_w, win_y + win_h); glVertex2f(win_x, win_y + win_h);
    glEnd();

    float pad = 18.0f;
    float ch = term_cell_h();
    float ty = win_y + 14.0f;

    // Title + hint
    const char* title = (g_mode == 0) ? "// REPORT INBOX (staff) //" : "// MY REPORTS //";
    term_draw_string(win_x + pad, ty, title, 0.35f, 0.80f, 1.0f, 1.0f);
    const char* hint = (g_mode == 0)
        ? "Up/Down - select   Enter - reply in terminal   Esc - close"
        : "Up/Down - select   Esc - close";
    float hint_w = term_string_width(hint);
    term_draw_string(win_x + win_w - pad - hint_w, ty, hint, 0.35f, 0.50f, 0.75f, 1.0f);
    ty += ch + 8.0f;

    if (g_rows.empty()) {
        term_draw_string(win_x + pad, ty,
            (g_mode == 0) ? "  No open reports." : "  You have no reports yet.",
            0.6f, 0.6f, 0.6f, 1.0f);
        return;
    }

    float text_max_w = win_w - pad * 2 - 24.0f;
    float bottom = win_y + win_h - pad;

    // Render rows starting at g_scroll, each may span multiple wrapped lines.
    for (int i = g_scroll; i < (int)g_rows.size() && ty < bottom; ++i) {
        const ReportRow& row = g_rows[i];
        bool selected = (i == g_sel);

        // Header line: highlighted bar for selection.
        char head[256];
        if (g_mode == 0)
            std::snprintf(head, sizeof(head), "#%s  %s  %s",
                row.id.c_str(), row.nick.c_str(), row.date.c_str());
        else
            std::snprintf(head, sizeof(head), "#%s  %s  [%s]",
                row.id.c_str(), row.date.c_str(), row.status.c_str());

        if (selected) {
            // Selection highlight bar across the row width.
            glColor4f(0.15f, 0.30f, 0.55f, 0.55f);
            glBegin(GL_QUADS);
                glVertex2f(win_x + pad - 6, ty - 2);
                glVertex2f(win_x + win_w - pad + 6, ty - 2);
                glVertex2f(win_x + win_w - pad + 6, ty + ch);
                glVertex2f(win_x + pad - 6, ty + ch);
            glEnd();
        }

        float hr = selected ? 1.0f  : 0.6f;
        float hg = selected ? 0.85f : 0.7f;
        float hb = selected ? 0.4f  : 0.85f;
        term_draw_string(win_x + pad, ty, head, hr, hg, hb, 1.0f);
        ty += ch;

        // Wrapped body text.
        std::vector<std::string> wrapped;
        wrap_text(row.text, text_max_w, wrapped);
        for (auto& wl : wrapped) {
            if (ty >= bottom) break;
            std::string indented = "    " + wl;
            term_draw_string(win_x + pad, ty, indented.c_str(), 0.88f, 0.92f, 1.0f, 1.0f);
            ty += ch;
        }

        // Player mode: show the staff answer if present.
        if (g_mode == 1 && !row.answer.empty() && ty < bottom) {
            std::vector<std::string> awrapped;
            wrap_text(std::string("\xE2\x86\xB3 ") + row.answer, text_max_w, awrapped);
            for (auto& al : awrapped) {
                if (ty >= bottom) break;
                std::string indented = "    " + al;
                term_draw_string(win_x + pad, ty, indented.c_str(), 0.5f, 0.9f, 0.65f, 1.0f);
                ty += ch;
            }
        }

        ty += 6.0f;  // gap between reports
    }
}

bool report_on_key(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;

    if (key == GLFW_KEY_ESCAPE) {
        g_report_open = false;
        return true;
    }
    if (g_rows.empty()) return true;

    if (key == GLFW_KEY_UP) {
        if (g_sel > 0) --g_sel;
        if (g_sel < g_scroll) g_scroll = g_sel;
        return true;
    }
    if (key == GLFW_KEY_DOWN) {
        if (g_sel < (int)g_rows.size() - 1) ++g_sel;
        // Keep selection roughly visible: nudge scroll forward.
        if (g_sel > g_scroll + 6) g_scroll = g_sel - 6;
        return true;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (g_mode == 0 && g_sel >= 0 && g_sel < (int)g_rows.size()) {
            // Pull the selected report into the terminal so staff can answer.
            const ReportRow& row = g_rows[g_sel];
            {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_lines.push_back({std::string("  Report #") + row.id + " from " +
                                   row.nick + ":", 120, 180, 255});
                g_lines.push_back({std::string("    ") + row.text, 200, 220, 255});
                if ((int)g_lines.size() > g_term_buf_size) g_lines.erase(g_lines.begin());
                g_input_buf = "reply " + row.id + " ";
            }
            g_report_open = false;
            sound_play(SoundEvent::CMD_SEND);
        }
        return true;
    }
    return true;  // overlay consumes all keys while open
}
