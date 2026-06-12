#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "terminal.h"
#include "state.h"
#include "net.h"
#include "sound.h"
#include "settings.h"
#include "stat_overlay.h"
#include "scrollback_overlay.h"
#include "report_overlay.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

static const float PI = 3.14159265f;

// ─── Font atlas ───────────────────────────────────────────────────────────────
static stbtt_packedchar g_cdata_ascii[224];    // ASCII 32..255
static stbtt_packedchar g_cdata_cyrillic[256]; // U+0400..U+04FF
static GLuint           g_font_tex = 0;
static float            g_font_size   = 17.0f;
static float            g_cell_w      = 0.0f;
static float            g_cell_h      = 0.0f;

static const int ATLAS_W = 1024, ATLAS_H = 1024;

bool terminal_init(const char* font_path) {
    FILE* f = fopen(font_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> ttf_buf(sz);
    fread(ttf_buf.data(), 1, sz, f);
    fclose(f);

    std::vector<uint8_t> bitmap(ATLAS_W * ATLAS_H);

    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap.data(), ATLAS_W, ATLAS_H, 0, 1, nullptr))
        return false;
    stbtt_PackSetOversampling(&pc, 1, 1);

    stbtt_pack_range ranges[2];
    ranges[0].font_size                = g_font_size;
    ranges[0].first_unicode_codepoint_in_range = 32;
    ranges[0].num_chars                = 224;
    ranges[0].chardata_for_range       = g_cdata_ascii;
    ranges[0].array_of_unicode_codepoints = nullptr;

    ranges[1].font_size                = g_font_size;
    ranges[1].first_unicode_codepoint_in_range = 0x0400;
    ranges[1].num_chars                = 256;
    ranges[1].chardata_for_range       = g_cdata_cyrillic;
    ranges[1].array_of_unicode_codepoints = nullptr;

    if (!stbtt_PackFontRanges(&pc, ttf_buf.data(), 0, ranges, 2))
        return false;
    stbtt_PackEnd(&pc);

    glGenTextures(1, &g_font_tex);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, ATLAS_H, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Measure cell size from 'M'
    stbtt_aligned_quad q;
    float cx = 0, cy = 0;
    stbtt_GetPackedQuad(g_cdata_ascii, ATLAS_W, ATLAS_H, 'M'-32, &cx, &cy, &q, 1);
    g_cell_w = cx;
    g_cell_h = g_font_size * 1.35f;
    return true;
}

void terminal_shutdown() {
    if (g_font_tex) { glDeleteTextures(1, &g_font_tex); g_font_tex = 0; }
}

// ─── 2D helpers ───────────────────────────────────────────────────────────────
static void set_ortho(int W, int H) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a=1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glVertex2f(x,   y);   glVertex2f(x+w, y);
        glVertex2f(x+w, y+h); glVertex2f(x,   y+h);
    glEnd();
}

// Draw UTF-8 string using TTF atlas. Returns x advance.
static float draw_string(float x, float y, const char* text,
                         float r, float g, float b, float a=1.0f) {
    if (!g_font_tex) return x;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    float cx = x, baseline_y = y + g_font_size;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(text);
    while (*s) {
        uint32_t cp;
        if (*s < 0x80) {
            cp = *s++;
        } else if ((*s & 0xE0) == 0xC0) {
            cp = (*s++ & 0x1F) << 6;
            if (*s) cp |= (*s++ & 0x3F);
        } else if ((*s & 0xF0) == 0xE0) {
            cp = (*s++ & 0x0F) << 12;
            if (*s) cp |= (*s++ & 0x3F) << 6;
            if (*s) cp |= (*s++ & 0x3F);
        } else if ((*s & 0xF8) == 0xF0) {
            cp = (*s++ & 0x07) << 18;
            if (*s) cp |= (*s++ & 0x3F) << 12;
            if (*s) cp |= (*s++ & 0x3F) << 6;
            if (*s) cp |= (*s++ & 0x3F);
        } else {
            cp = *s++; // fallback
        }
        stbtt_packedchar* cdata = nullptr;
        int idx = -1;
        if (cp >= 32 && cp < 256) {
            cdata = g_cdata_ascii; idx = cp - 32;
        } else if (cp >= 0x0400 && cp < 0x0500) {
            cdata = g_cdata_cyrillic; idx = cp - 0x0400;
        } else {
            cx += g_cell_w; continue;
        }
        stbtt_aligned_quad q;
        float bx = cx, by = baseline_y;
        stbtt_GetPackedQuad(cdata, ATLAS_W, ATLAS_H, idx, &bx, &by, &q, 1);
        glTexCoord2f(q.s0,q.t0); glVertex2f(q.x0,q.y0);
        glTexCoord2f(q.s1,q.t0); glVertex2f(q.x1,q.y0);
        glTexCoord2f(q.s1,q.t1); glVertex2f(q.x1,q.y1);
        glTexCoord2f(q.s0,q.t1); glVertex2f(q.x0,q.y1);
        cx = bx;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    return cx;
}

static float string_width(const char* text) {
    float cx = 0, dummy = 0;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(text);
    while (*s) {
        uint32_t cp;
        if (*s < 0x80) { cp = *s++; }
        else if ((*s & 0xE0) == 0xC0) { cp = (*s++ & 0x1F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF0) == 0xE0) { cp = (*s++ & 0x0F) << 12; if (*s) cp |= (*s++ & 0x3F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF8) == 0xF0) { cp = (*s++ & 0x07) << 18; if (*s) cp |= (*s++ & 0x3F) << 12; if (*s) cp |= (*s++ & 0x3F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else { cp = *s++; }
        stbtt_packedchar* cdata = nullptr;
        int idx = -1;
        if (cp >= 32 && cp < 256) {
            cdata = g_cdata_ascii; idx = cp - 32;
        } else if (cp >= 0x0400 && cp < 0x0500) {
            cdata = g_cdata_cyrillic; idx = cp - 0x0400;
        } else {
            cx += g_cell_w; continue;
        }
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(cdata, ATLAS_W, ATLAS_H, idx, &cx, &dummy, &q, 1);
    }
    return cx;
}

// Truncate UTF-8 string to fit within max_width pixels
static std::string truncate_string(const char* text, float max_width) {
    std::string result;
    const uint8_t* p = (const uint8_t*)text;

    while (*p && string_width(result.c_str()) < max_width) {
        if (*p < 0x80) {
            result += (char)*p++;
        } else if ((*p & 0xE0) == 0xC0) {
            result += (char)*p++;
            if (*p) result += (char)*p++;
        } else if ((*p & 0xF0) == 0xE0) {
            result += (char)*p++;
            if (*p) result += (char)*p++;
            if (*p) result += (char)*p++;
        } else if ((*p & 0xF8) == 0xF0) {
            result += (char)*p++;
            if (*p) result += (char)*p++;
            if (*p) result += (char)*p++;
            if (*p) result += (char)*p++;
        } else {
            result += (char)*p++;
        }
    }
    return result;
}

// ─── Public font helpers (used by settings overlay) ───────────────────────────
float term_draw_string(float x, float y, const char* text,
                       float r, float g, float b, float a) {
    return draw_string(x, y, text, r, g, b, a);
}
float term_string_width(const char* text) { return string_width(text); }
float term_cell_h()                       { return g_cell_h; }

// ─── Login screen ─────────────────────────────────────────────────────────────
static void render_login(int W, int H) {
    glClearColor(0.03f, 0.03f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    set_ortho(W, H);
    glDisable(GL_DEPTH_TEST);

    float cx = W * 0.5f, cy = H * 0.5f;

    // Background stars
    srand(0xDEAD1234);
    glPointSize(1.5f); glColor3f(0.12f,0.15f,0.22f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 200; ++i)
        glVertex2f((float)(rand()%W), (float)(rand()%H));
    glEnd();

    // Title
    const char* title = "HORIZON  PRIME";
    float tw = string_width(title) * 1.0f;
    // Glow layers
    for (int d = 3; d >= 1; --d) {
        float a = 0.04f * d;
        draw_string(cx - tw*0.5f - d, cy - 130 - d, title, 0.2f*a, 0.4f*a, a, 0.6f);
    }
    draw_string(cx - tw*0.5f, cy - 130, title, 0.45f, 0.65f, 1.0f);

    // Subtitle
    const char* sub = "//  Terminal Interface  //";
    draw_string(cx - string_width(sub)*0.5f, cy - 104, sub, 0.25f, 0.30f, 0.55f);

    // Separator
    glColor4f(0.15f,0.18f,0.45f,0.8f); glLineWidth(1.0f);
    glBegin(GL_LINES);
        glVertex2f(cx-220, cy-82); glVertex2f(cx+220, cy-82);
    glEnd();

    // Fields
    float fW = 360.0f, fX = cx - fW*0.5f, fH = 40.0f;
    float fY1 = cy - 68, fY2 = fY1 + 56;

    bool nf = (g_focused_login == 0), pf = (g_focused_login == 1);

    // Nick field
    draw_rect(fX, fY1, fW, fH, 0.05f,0.05f,0.12f);
    float nb = nf ? 0.4f : 0.18f;
    glColor4f(nf?0.35f:0.15f, nf?0.50f:0.18f, nf?1.0f:0.40f, 1.0f);
    glLineWidth(nf?1.8f:1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(fX,fY1); glVertex2f(fX+fW,fY1);
        glVertex2f(fX+fW,fY1+fH); glVertex2f(fX,fY1+fH);
    glEnd();
    (void)nb;
    draw_string(fX+10, fY1+8, "Nickname:", 0.30f,0.40f,0.70f);
    std::string nd = g_field_nick + (nf ? "_" : "");
    draw_string(fX+120, fY1+8, nd.c_str(), 0.85f,0.92f,1.0f);

    // Pass field
    draw_rect(fX, fY2, fW, fH, 0.05f,0.05f,0.12f);
    glColor4f(pf?0.35f:0.15f, pf?0.50f:0.18f, pf?1.0f:0.40f, 1.0f);
    glLineWidth(pf?1.8f:1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(fX,fY2); glVertex2f(fX+fW,fY2);
        glVertex2f(fX+fW,fY2+fH); glVertex2f(fX,fY2+fH);
    glEnd();
    draw_string(fX+10, fY2+8, "Password:", 0.30f,0.40f,0.70f);
    std::string pd = std::string(g_field_pass.size(),'*') + (pf ? "_" : "");
    draw_string(fX+120, fY2+8, pd.c_str(), 0.85f,0.92f,1.0f);

    // Buttons
    float bY = fY2 + fH + 14, bW = 170;
    draw_rect(fX, bY, bW, 34, 0.04f,0.18f,0.06f);
    glColor3f(0.15f,0.65f,0.20f); glLineWidth(1.2f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(fX,bY); glVertex2f(fX+bW,bY);
        glVertex2f(fX+bW,bY+34); glVertex2f(fX,bY+34);
    glEnd();
    draw_string(fX+12, bY+8, "Enter  - Login", 0.25f,0.90f,0.30f);

    float b2x = fX + fW - bW;
    draw_rect(b2x, bY, bW, 34, 0.04f,0.05f,0.20f);
    glColor3f(0.15f,0.20f,0.65f); glLineWidth(1.2f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(b2x,bY); glVertex2f(b2x+bW,bY);
        glVertex2f(b2x+bW,bY+34); glVertex2f(b2x,bY+34);
    glEnd();
    draw_string(b2x+12, bY+8, "F2  - Register", 0.35f,0.40f,0.90f);

    const char* hint = "Tab - switch field     Esc - quit";
    draw_string(cx - string_width(hint)*0.5f, bY+50, hint, 0.22f,0.25f,0.45f);

    if (!g_auth_error.empty()) {
        float ew = std::max(fW, string_width(g_auth_error.c_str())+28);
        float ex = cx - ew*0.5f;
        draw_rect(ex, bY+72, ew, 32, 0.22f,0.04f,0.04f);
        glColor3f(0.80f,0.20f,0.20f); glLineWidth(1.2f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(ex,bY+72); glVertex2f(ex+ew,bY+72);
            glVertex2f(ex+ew,bY+104); glVertex2f(ex,bY+104);
        glEnd();
        draw_string(ex+12, bY+80, g_auth_error.c_str(), 1.0f,0.35f,0.35f);
    }

    // Server status bar at the bottom
    {
        ConnStatus cs  = g_conn_status.load();
        int        cnt = g_online_count.load();

        float dot_r, dot_g, dot_b;
        const char* status_text;
        char online_buf[48] = {};

        if (cs == ConnStatus::ONLINE) {
            dot_r = 0.20f; dot_g = 0.85f; dot_b = 0.30f;
            std::snprintf(online_buf, sizeof(online_buf), "  Online: %d", cnt);
            status_text = online_buf;
        } else if (cs == ConnStatus::CONNECTING) {
            dot_r = 0.85f; dot_g = 0.75f; dot_b = 0.10f;
            status_text = "  Connecting...";
        } else {
            dot_r = 0.85f; dot_g = 0.20f; dot_b = 0.20f;
            status_text = "  Server offline";
        }

        float sy = (float)H - 26.0f;
        // Dot (filled circle approximation via point)
        glPointSize(10.0f);
        glColor3f(dot_r, dot_g, dot_b);
        glBegin(GL_POINTS); glVertex2f(14.0f, sy + 8.0f); glEnd();
        draw_string(22.0f, sy, status_text, dot_r * 0.8f, dot_g * 0.8f, dot_b * 0.8f);
    }
}

// ─── Scene: starfield ─────────────────────────────────────────────────────────
static void render_scene_starfield(int W, int H, float t) {
    glClearColor(0.0f,0.0f,0.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    set_ortho(W, H);
    float cx = W*0.5f, cy = H*0.5f;
    srand(0xBEEF);
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 300; ++i) {
        float angle = (float)(rand()%6283)*0.001f;
        float base_r = 20.0f + (rand()%300);
        float speed  = 1.0f + (float)(rand()%40)*0.1f;
        float r2 = base_r + std::fmod(t * speed * 30.0f, (float)(W));
        float x = cx + std::cos(angle) * r2;
        float y = cy + std::sin(angle) * r2;
        float br = std::min(1.0f, (r2 - base_r) / 300.0f);
        glColor4f(br*0.8f, br*0.9f, br, br);
        glVertex2f(x, y);
    }
    glEnd();
    // Center glow
    glPointSize(40.0f); glColor4f(0.15f,0.25f,0.60f,0.25f);
    glBegin(GL_POINTS); glVertex2f(cx,cy); glEnd();
    glPointSize(12.0f); glColor4f(0.40f,0.60f,1.0f,0.50f);
    glBegin(GL_POINTS); glVertex2f(cx,cy); glEnd();
}

static void render_scene_warp(int W, int H, float t) {
    glClearColor(0.0f,0.0f,0.02f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    set_ortho(W, H);
    float cx = W*0.5f, cy = H*0.5f;
    srand(0xCAFE);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < 200; ++i) {
        float angle = (float)(rand()%6283)*0.001f;
        float r1 = 10.0f + (float)(rand()%50);
        float len = 20.0f + std::fmod(t*200.0f, 400.0f);
        float r2 = r1 + len;
        float br = std::min(1.0f, len/200.0f);
        glColor4f(br*0.6f, br*0.8f, br, br*0.9f);
        glVertex2f(cx + std::cos(angle)*r1, cy + std::sin(angle)*r1);
        glVertex2f(cx + std::cos(angle)*r2, cy + std::sin(angle)*r2);
    }
    glEnd();
}

static void render_scene(int W, int H, float dt) {
    static float scene_time = 0.0f;
    scene_time += dt;
    uint8_t sid;
    float time_left;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        sid       = g_scene.scene_id;
        time_left = g_scene.time_left;
        if (time_left > 0.0f) {
            g_scene.time_left -= dt;
            if (g_scene.time_left <= 0.0f) {
                g_scene.active = false;
                scene_time = 0.0f;
                return;
            }
        }
    }
    switch (sid) {
    case 0: render_scene_starfield(W, H, scene_time); break;
    case 1: render_scene_warp(W, H, scene_time);      break;
    default:
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    }
}

// ─── Terminal screen ──────────────────────────────────────────────────────────
static int  g_scroll_offset = 0;  // lines scrolled up from bottom

// ─── Command history (up/down arrows) ────────────────────────────────────────
static std::vector<std::string> g_cmd_history;
static int                      g_history_idx = -1;  // -1 = current input
static std::string              g_history_saved;     // saved current input while browsing
static bool                     g_input_selection_all = false;  // for Ctrl+A visual state

static void render_terminal(int W, int H, float dt) {
    (void)dt;
    glClearColor(0.02f,0.02f,0.05f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    set_ortho(W, H);
    glDisable(GL_DEPTH_TEST);

    // Layout
    const float PAD_L   = 12.0f;
    const float PAD_R   = 12.0f;
    const float INPUT_H = g_cell_h + 14.0f;
    const float PROMPT_Y = (float)H - INPUT_H + 6.0f;
    const float TEXT_AREA_H = (float)H - INPUT_H - 6.0f;

    // Background
    draw_rect(0,0,(float)W,(float)H, 0.015f,0.015f,0.04f);

    // Scanline effect (controlled by settings)
    if (g_settings.scanlines) {
        float sc_alpha = (g_settings.scanline_bright / 10.0f) * 0.12f;
        glColor4f(0.0f, 0.0f, 0.0f, sc_alpha);
        glBegin(GL_LINES);
        for (float ly = 0; ly < H; ly += 3.0f) {
            glVertex2f(0, ly); glVertex2f((float)W, ly);
        }
        glEnd();
    }

    // ── Text lines ────────────────────────────────────────────────────────────
    int lines_visible = (int)(TEXT_AREA_H / g_cell_h);

    std::vector<TermLine> lines_snapshot;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        lines_snapshot = g_lines;
    }

    int total = (int)lines_snapshot.size();
    int start_idx = std::max(0, total - lines_visible - g_scroll_offset);
    int end_idx   = std::max(0, total - g_scroll_offset);

    float ty = TEXT_AREA_H - g_cell_h * (end_idx - start_idx);
    for (int i = start_idx; i < end_idx; ++i) {
        const auto& ln = lines_snapshot[i];
        float r = ln.r / 255.0f;
        float g = ln.g / 255.0f;
        float b = ln.b / 255.0f;
        float a = (ln.flags & 0x02) ? 0.55f : 1.0f;  // dim flag
        float max_text_width = (float)W - PAD_L - PAD_R;
        std::string truncated = truncate_string(ln.text.c_str(), max_text_width);
        draw_string(PAD_L, ty, truncated.c_str(), r, g, b, a);
        ty += g_cell_h;
    }
    (void)PAD_R;

    // Scroll indicator
    if (g_scroll_offset > 0) {
        char sc[32]; std::snprintf(sc, sizeof(sc), "  ^ %d lines up", g_scroll_offset);
        draw_string(PAD_L, 6, sc, 0.35f,0.40f,0.65f);
    }

    // ── Input line ────────────────────────────────────────────────────────────
    // Separator
    glColor4f(0.12f,0.14f,0.40f,0.90f); glLineWidth(1.0f);
    glBegin(GL_LINES);
        glVertex2f(0, (float)H - INPUT_H);
        glVertex2f((float)W, (float)H - INPUT_H);
    glEnd();

    draw_rect(0, (float)H - INPUT_H, (float)W, INPUT_H, 0.02f,0.02f,0.07f);

    // Prompt
    std::string prompt;
    std::string input;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        prompt = g_prompt;
        input  = g_input_buf;
    }
    float prompt_x = draw_string(PAD_L, PROMPT_Y, prompt.c_str(), 0.35f,0.55f,1.0f);

    // Cursor blink
    double blink = std::fmod(glfwGetTime(), 1.0);
    std::string display = input + (blink < 0.55 ? "_" : " ");
    float max_input_width = (float)W - prompt_x - PAD_R;
    std::string truncated_input = truncate_string(display.c_str(), max_input_width);
    draw_string(prompt_x, PROMPT_Y, truncated_input.c_str(), 0.85f,0.92f,1.0f);
}

// ─── Chat overlay panel ───────────────────────────────────────────────────────
static double g_chat_uptime = 0.0;  // updated each frame from glfwGetTime

static void render_chat_overlay(int W, int H) {
    const int   VISIBLE = 6;
    const float FADE_START = 5.0f;   // seconds before fade starts
    const float FADE_END   = 8.0f;   // fully faded
    const float PAD = 10.0f;
    const float LINE_H = g_cell_h;

    std::vector<ChatLine> snap;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        snap = g_chat_lines;
    }
    if (snap.empty()) return;

    // Take last VISIBLE lines
    int start = std::max(0, (int)snap.size() - VISIBLE);
    int count = (int)snap.size() - start;

    // Bottom of chat panel sits just above the input separator
    const float INPUT_H = g_cell_h + 14.0f;
    float base_y = (float)H - INPUT_H - PAD - LINE_H;

    for (int i = count - 1; i >= 0; --i) {
        const auto& cl = snap[start + i];
        float age = (float)(g_chat_uptime - cl.timestamp);
        if (age > FADE_END) continue;

        float alpha = 1.0f;
        if (age > FADE_START)
            alpha = 1.0f - (age - FADE_START) / (FADE_END - FADE_START);
        alpha = std::max(0.0f, std::min(1.0f, alpha));

        float y = base_y - (float)(count - 1 - i) * LINE_H;

        // Semi-transparent background per line
        float tw = string_width(cl.text.c_str()) + 8.0f;
        draw_rect(PAD - 4.0f, y - 2.0f, tw, LINE_H,
                  0.0f, 0.0f, 0.0f, alpha * 0.45f);

        draw_string(PAD, y, cl.text.c_str(),
                    cl.r / 255.0f, cl.g / 255.0f, cl.b / 255.0f, alpha);
    }
}

// ─── Main render entry ────────────────────────────────────────────────────────
void terminal_render(int W, int H, float dt) {
    g_chat_uptime = glfwGetTime();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    bool scene_active;
    { std::lock_guard<std::mutex> lock(g_state_mutex); scene_active = g_scene.active; }

    if (g_screen == Screen::LOGIN) {
        render_login(W, H);
    } else if (scene_active) {
        render_scene(W, H, dt);
    } else {
        render_terminal(W, H, dt);
        // Chat overlay on top of terminal (not during scenes)
        set_ortho(W, H);
        glDisable(GL_DEPTH_TEST);
        render_chat_overlay(W, H);
    }

    // Settings overlay renders on top of whatever is visible (except login)
    if (g_screen != Screen::LOGIN && g_settings_open) {
        set_ortho(W, H);
        glDisable(GL_DEPTH_TEST);
        settings_render(W, H);
    }

    // Stats overlay (also on top, but takes priority over settings in key handling)
    if (g_screen != Screen::LOGIN && g_stat_open) {
        set_ortho(W, H);
        glDisable(GL_DEPTH_TEST);
        stat_render(W, H);
    }

    // Scrollback overlay
    if (g_screen != Screen::LOGIN && g_scr_open) {
        set_ortho(W, H);
        glDisable(GL_DEPTH_TEST);
        scr_render(W, H);
    }

    // Report overlay (staff inbox / player view)
    if (g_screen != Screen::LOGIN && g_report_open) {
        set_ortho(W, H);
        glDisable(GL_DEPTH_TEST);
        report_render(W, H);
    }
}

// ─── Input callbacks ──────────────────────────────────────────────────────────
void terminal_cb_char(GLFWwindow*, unsigned int cp) {
    if (g_screen == Screen::LOGIN) {
        // Accept printable ASCII only for login fields
        if (cp >= 32 && cp < 127) {
            // Nickname: restrict to the server's allowed set (letters/digits/_-.)
            if (g_focused_login == 0) {
                bool ok = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
                          (cp >= '0' && cp <= '9') || cp == '_' || cp == '-' || cp == '.';
                if (!ok) return;
            }
            if (g_focused_login == 0 && (int)g_field_nick.size() < NICKNAME_MAX_LEN-1)
                g_field_nick += (char)cp;
            else if (g_focused_login == 1 && (int)g_field_pass.size() < PASSWORD_MAX_LEN-1)
                g_field_pass += (char)cp;
        }
        return;
    }
    // Terminal: accept any UTF-8 char via codepoint
    if (cp >= 32 && (int)g_input_buf.size() < MESSAGE_MAX_LEN-1) {
        sound_play(SoundEvent::KEY_TYPE);
        // Encode codepoint to UTF-8
        if (cp < 0x80) {
            g_input_buf += (char)cp;
        } else if (cp < 0x800) {
            g_input_buf += (char)(0xC0 | (cp >> 6));
            g_input_buf += (char)(0x80 | (cp & 0x3F));
        } else {
            g_input_buf += (char)(0xE0 | (cp >> 12));
            g_input_buf += (char)(0x80 | ((cp >> 6) & 0x3F));
            g_input_buf += (char)(0x80 | (cp & 0x3F));
        }
    }
}

void terminal_cb_key(GLFWwindow* win, int key, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (g_screen == Screen::LOGIN) {
        if (key == GLFW_KEY_ESCAPE)  { g_running = false; return; }
        if (key == GLFW_KEY_TAB)     { g_focused_login ^= 1; return; }
        if (key == GLFW_KEY_BACKSPACE) {
            if (g_focused_login == 0 && !g_field_nick.empty()) g_field_nick.pop_back();
            if (g_focused_login == 1 && !g_field_pass.empty()) g_field_pass.pop_back();
            return;
        }
        if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) {
            const char* clip = glfwGetClipboardString(win);
            if (clip) {
                std::string& field = (g_focused_login == 0) ? g_field_nick : g_field_pass;
                int max_len = (g_focused_login == 0) ? NICKNAME_MAX_LEN - 1 : PASSWORD_MAX_LEN - 1;
                bool nick_field = (g_focused_login == 0);
                for (const char* p = clip; *p && (int)field.size() < max_len; ++p) {
                    unsigned char c = (unsigned char)*p;
                    if (c < 32 || c >= 127) continue;  // printable ASCII only
                    if (nick_field) {
                        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
                        if (!ok) continue;
                    }
                    field += (char)c;
                }
            }
            return;
        }
        if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) {
            if (g_focused_login == 0) g_field_nick.clear();
            else                      g_field_pass.clear();
            return;
        }
        if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
            && !g_field_nick.empty() && !g_field_pass.empty()) {
            if (g_conn_status != ConnStatus::ONLINE) {
                g_auth_error = "Server is offline. Connecting...";
                return;
            }
            g_auth_error.clear();
            net_send_login(g_field_nick, g_field_pass, false);
            return;
        }
        if (key == GLFW_KEY_F2
            && !g_field_nick.empty() && !g_field_pass.empty()) {
            if (g_conn_status != ConnStatus::ONLINE) {
                g_auth_error = "Server is offline. Connecting...";
                return;
            }
            // Quick local checks for registration (server re-validates anyway).
            if (g_field_nick.size() < 4) {
                g_auth_error = "Nickname must be at least 4 characters.";
                return;
            }
            if (g_field_pass.size() < 5) {
                g_auth_error = "Password must be at least 5 characters.";
                return;
            }
            g_auth_error.clear();
            net_send_login(g_field_nick, g_field_pass, true);
            return;
        }
        return;
    }

    // Report overlay has top priority while open
    if (g_report_open) {
        report_on_key(key, action);
        return;
    }

    // Stats overlay has priority over settings
    if (g_stat_open) {
        stat_on_key(key, action);
        return;
    }

    // Scrollback overlay has priority over settings
    if (g_scr_open) {
        scr_on_key(key, action);
        return;
    }

    // Settings overlay consumes all keys while open
    if (g_settings_open) {
        settings_on_key(key, action);
        return;
    }

    // Terminal mode
    if (key == GLFW_KEY_ESCAPE) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_scene.active) { g_scene.active = false; return; }
        return;
    }
    if (key == GLFW_KEY_BACKSPACE) {
        if (!g_input_buf.empty()) {
            // Pop UTF-8 char from end
            while (!g_input_buf.empty() && (g_input_buf.back() & 0xC0) == 0x80)
                g_input_buf.pop_back();
            if (!g_input_buf.empty()) g_input_buf.pop_back();
            sound_play(SoundEvent::BACKSPACE);
        }
        return;
    }

    // Text editing: Ctrl+A (select all), Ctrl+X (cut), Ctrl+C (copy), Ctrl+V (paste)
    if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL)) {
        // Ctrl+A: mark entire input as selected (visual only for now)
        // For simplicity, just select all by moving cursor logic
        // We'll store selection state if needed, but for MVP just select-all for copy
        g_input_selection_all = true;
        return;
    }
    if (key == GLFW_KEY_X && (mods & GLFW_MOD_CONTROL)) {
        // Ctrl+X: cut (copy to clipboard and clear)
        if (!g_input_buf.empty()) {
#ifdef _WIN32
            if (OpenClipboard(nullptr)) {
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, g_input_buf.size() + 1);
                if (h) {
                    char* p = (char*)GlobalLock(h);
                    std::strcpy(p, g_input_buf.c_str());
                    GlobalUnlock(h);
                    EmptyClipboard();
                    SetClipboardData(CF_TEXT, h);
                    CloseClipboard();
                }
            }
#endif
            g_input_buf.clear();
            g_input_selection_all = false;
        }
        return;
    }
    if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
        // Ctrl+C: copy to clipboard
        if (!g_input_buf.empty()) {
#ifdef _WIN32
            if (OpenClipboard(nullptr)) {
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, g_input_buf.size() + 1);
                if (h) {
                    char* p = (char*)GlobalLock(h);
                    std::strcpy(p, g_input_buf.c_str());
                    GlobalUnlock(h);
                    EmptyClipboard();
                    SetClipboardData(CF_TEXT, h);
                    CloseClipboard();
                }
            }
#endif
        }
        return;
    }
    if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) {
        // Ctrl+V: paste from clipboard
#ifdef _WIN32
        if (OpenClipboard(nullptr)) {
            HANDLE h = GetClipboardData(CF_TEXT);
            if (h) {
                const char* text = (const char*)GlobalLock(h);
                if (text) {
                    // Append clipboard to input (up to MESSAGE_MAX_LEN)
                    size_t room = MESSAGE_MAX_LEN - 1 - g_input_buf.size();
                    size_t to_paste = std::min(room, std::strlen(text));
                    g_input_buf.append(text, to_paste);
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
        }
#endif
        g_input_selection_all = false;
        return;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (g_warping) {
            std::lock_guard<std::mutex> lk(g_state_mutex);
            g_lines.push_back({"  [warp] Navigation locked during jump sequence.", 180, 120, 60});
            return;
        }
        if (!g_input_buf.empty()) {
            // Check for local-only commands first
            {
                std::string trimmed = g_input_buf;
                size_t s = trimmed.find_first_not_of(" \t");
                if (s != std::string::npos) trimmed = trimmed.substr(s);
                std::string lc;
                for (auto c : trimmed) lc += (char)std::tolower((unsigned char)c);
                if (lc == "settings") {
                    g_settings_open = true;
                    sound_play(SoundEvent::CMD_CLEAR);
                    // Save to history
                    if (g_cmd_history.empty() || g_cmd_history.back() != g_input_buf)
                        g_cmd_history.push_back(g_input_buf);
                    g_history_idx = -1; g_history_saved.clear();
                    // Echo to terminal
                    std::string echo;
                    { std::lock_guard<std::mutex> lock(g_state_mutex); echo = g_prompt + g_input_buf; }
                    { std::lock_guard<std::mutex> lock(g_state_mutex);
                      g_lines.push_back({echo, 120, 160, 120});
                      if ((int)g_lines.size() > g_term_buf_size) g_lines.erase(g_lines.begin()); }
                    g_input_buf.clear();
                    g_scroll_offset = 0;
                    return;
                }
                // "report" with no argument: open the overlay locally (in this
                // thread, like settings) so it can't race the network thread,
                // then ask the server to fill it with data.
                if (lc == "report") {
                    report_open_pending();   // opens g_report_open + shows "loading"
                    sound_play(SoundEvent::CMD_SEND);
                    if (g_cmd_history.empty() || g_cmd_history.back() != g_input_buf)
                        g_cmd_history.push_back(g_input_buf);
                    g_history_idx = -1; g_history_saved.clear();
                    std::string echo;
                    { std::lock_guard<std::mutex> lock(g_state_mutex); echo = g_prompt + g_input_buf; }
                    { std::lock_guard<std::mutex> lock(g_state_mutex);
                      g_lines.push_back({echo, 120, 160, 120});
                      if ((int)g_lines.size() > g_term_buf_size) g_lines.erase(g_lines.begin()); }
                    g_input_buf.clear();
                    g_scroll_offset = 0;
                    net_send_input("report");  // server replies with S_REPORT_LIST
                    return;
                }
                if (lc == "scr") {
                    g_scr_open = true;
                    sound_play(SoundEvent::CMD_CLEAR);
                    // Save to history
                    if (g_cmd_history.empty() || g_cmd_history.back() != g_input_buf)
                        g_cmd_history.push_back(g_input_buf);
                    g_history_idx = -1; g_history_saved.clear();
                    // Echo to terminal
                    std::string echo;
                    { std::lock_guard<std::mutex> lock(g_state_mutex); echo = g_prompt + g_input_buf; }
                    { std::lock_guard<std::mutex> lock(g_state_mutex);
                      g_lines.push_back({echo, 120, 160, 120});
                      if ((int)g_lines.size() > g_term_buf_size) g_lines.erase(g_lines.begin()); }
                    g_input_buf.clear();
                    g_scroll_offset = 0;
                    return;
                }
                if (lc == "stat") {
                    // Send stat request to server (server will respond with S_STATS packet)
                    sound_play(SoundEvent::CMD_SEND);
                    // Save to history
                    if (g_cmd_history.empty() || g_cmd_history.back() != g_input_buf)
                        g_cmd_history.push_back(g_input_buf);
                    g_history_idx = -1; g_history_saved.clear();
                    // Echo to terminal
                    std::string echo;
                    { std::lock_guard<std::mutex> lock(g_state_mutex); echo = g_prompt + g_input_buf; }
                    { std::lock_guard<std::mutex> lock(g_state_mutex);
                      g_lines.push_back({echo, 120, 160, 120});
                      if ((int)g_lines.size() > g_term_buf_size) g_lines.erase(g_lines.begin()); }
                    g_input_buf.clear();
                    g_scroll_offset = 0;
                    net_send_input("stat");  // Send to server
                    return;
                }
            }

            // Determine sound by command
            std::string cmd = g_input_buf;
            // lowercase first word
            std::string lcmd;
            for (auto c : cmd) lcmd += (char)std::tolower((unsigned char)c);
            size_t sp = lcmd.find(' ');
            std::string first = (sp == std::string::npos) ? lcmd : lcmd.substr(0, sp);
            if (first == "clear")
                sound_play(SoundEvent::CMD_CLEAR);
            else if (first == "stars" || first == "warp")
                sound_play(SoundEvent::CMD_STARS);
            else if (first == "logout")
                sound_play(SoundEvent::CMD_LOGOUT);
            else
                sound_play(SoundEvent::CMD_SEND);

            // Echo input to terminal
            std::string echo;
            {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                echo = g_prompt + g_input_buf;
            }
            {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_lines.push_back({echo, 120, 160, 120});
                if ((int)g_lines.size() > g_term_buf_size)
                    g_lines.erase(g_lines.begin());
            }
            // Save to history (skip empty and exact duplicate of last entry)
            if (g_cmd_history.empty() || g_cmd_history.back() != g_input_buf) {
                g_cmd_history.push_back(g_input_buf);
                if ((int)g_cmd_history.size() > 50)
                    g_cmd_history.erase(g_cmd_history.begin());
            }
            g_history_idx   = -1;
            g_history_saved.clear();

            net_send_input(g_input_buf);
            g_input_buf.clear();
            g_scroll_offset = 0;
        }
        return;
    }
    // Command history navigation (Up/Down arrows)
    if (key == GLFW_KEY_UP) {
        if (g_cmd_history.empty()) return;
        if (g_history_idx == -1) {
            g_history_saved = g_input_buf;
            g_history_idx = (int)g_cmd_history.size() - 1;
        } else if (g_history_idx > 0) {
            --g_history_idx;
        }
        g_input_buf = g_cmd_history[g_history_idx];
        return;
    }
    if (key == GLFW_KEY_DOWN) {
        if (g_history_idx == -1) return;
        if (g_history_idx < (int)g_cmd_history.size() - 1) {
            ++g_history_idx;
            g_input_buf = g_cmd_history[g_history_idx];
        } else {
            g_history_idx = -1;
            g_input_buf = g_history_saved;
        }
        return;
    }

    // ── Tab autocomplete ──────────────────────────────────────────────────────
    if (key == GLFW_KEY_TAB) {
        // Build command list based on player access level
        // g_player_access: 0=admin, 1=mod, 2=helper, 3=user
        struct CmdEntry { const char* cmd; int min_access; };
        static const CmdEntry ALL_CMDS[] = {
            { "help",       3 },
            { "welcome",    3 },
            { "logo",       3 },
            { "ru",         3 },
            { "eng",        3 },
            { "who",        3 },
            { "scan",       3 },
            { "stat",       3 },
            { "say",        3 },
            { "warp",       3 },
            { "scr",        3 },
            { "clear",      3 },
            { "stars",      3 },
            { "settings",   3 },
            { "logout",     3 },
            { "exit",       3 },
            { "report",     3 },
            { "admin",      2 },  // helper+
            { "admin help",     2 },
            { "admin users",    2 },
            { "admin who",      2 },
            { "admin info",     2 },
            { "admin logs",     2 },
            { "admin kick",     2 },  // staff
            { "admin mute",     2 },
            { "admin unmute",   2 },
            { "admin announce", 2 },
            { "admin setaccess",0 },  // admin only
            { "admin ban",      0 },  // admin only
            { "admin unban",    0 },  // admin only
            { "reply",          2 },  // staff
        };

        std::string prefix;
        { std::lock_guard<std::mutex> lock(g_state_mutex); prefix = g_input_buf; }

        // Lower-case prefix for matching
        std::string lc_prefix = prefix;
        for (auto& c : lc_prefix) c = (char)std::tolower((unsigned char)c);

        std::vector<std::string> matches;
        int access;
        { std::lock_guard<std::mutex> lock(g_state_mutex); access = g_player_access; }
        for (auto& e : ALL_CMDS) {
            if (access > e.min_access) continue;  // not enough access
            std::string ec = e.cmd;
            if (ec.substr(0, lc_prefix.size()) == lc_prefix)
                matches.push_back(ec);
        }

        if (matches.empty()) return;

        if (matches.size() == 1) {
            // Complete the input
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_input_buf = matches[0];
        } else {
            // Show all matches as a hint line in the terminal
            std::string hint = "  ";
            for (size_t i = 0; i < matches.size(); ++i) {
                if (i) hint += "  ";
                hint += matches[i];
            }
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_lines.push_back({hint, 80, 120, 200});
            if ((int)g_lines.size() > g_term_buf_size) g_lines.erase(g_lines.begin());

            // Complete common prefix
            std::string common = matches[0];
            for (size_t i = 1; i < matches.size(); ++i) {
                size_t j = 0;
                while (j < common.size() && j < matches[i].size() && common[j] == matches[i][j]) ++j;
                common = common.substr(0, j);
            }
            if (common.size() > lc_prefix.size())
                g_input_buf = common;
        }
        return;
    }

    // Scroll
    if (key == GLFW_KEY_PAGE_UP)   { g_scroll_offset += 5; return; }
    if (key == GLFW_KEY_PAGE_DOWN) { g_scroll_offset = std::max(0, g_scroll_offset-5); return; }
    (void)win;
}

void terminal_cb_cursor_pos(GLFWwindow* win, double mx, double my) {
    if (g_settings_open)
        settings_on_cursor((float)mx, (float)my);
    (void)win;
}

void terminal_cb_mouse_button(GLFWwindow* win, int button, int action, int) {
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    if (g_settings_open) {
        settings_on_mouse_button((float)mx, (float)my, button, action);
    }
}

void terminal_cb_scroll(GLFWwindow*, double /*xoffset*/, double yoffset) {
    if (g_scr_open) {
        scr_on_scroll(yoffset);
        return;
    }
    // Scroll main terminal buffer
    std::lock_guard<std::mutex> lock(g_state_mutex);
    int total = (int)g_lines.size();
    int delta = (yoffset > 0) ? 3 : -3;
    g_scroll_offset = std::clamp(g_scroll_offset + delta, 0, std::max(0, total - 1));
}
