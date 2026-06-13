#include "map_overlay.h"
#include "state.h"
#include "sound.h"
#include "settings.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// ─── Font helpers (implemented in terminal.cpp) ───────────────────────────────
extern float term_draw_string(float x, float y, const char* text,
                              float r, float g, float b, float a);
extern float term_string_width(const char* text);
extern float term_cell_h();

// ─── State ────────────────────────────────────────────────────────────────────
bool  g_map_open = false;

static float s_yaw    = 0.6f;   // rotation around vertical axis (radians)
static float s_pitch  = 0.5f;   // tilt toward the viewer
static int   s_radius = 4;      // sectors shown around the player (per axis)

// ─── Deterministic star generation (ported from server StarGen.h) ─────────────
// Kept byte-for-byte identical to the server so the client shows the same stars.
namespace {

uint32_t sector_hash(int32_t x, int32_t y, int32_t z) {
    uint32_t h = 2166136261u;
    auto mix = [&](int32_t v) {
        h ^= (uint32_t)v;
        h *= 16777619u;
        h ^= (h >> 13);
        h *= 0x85ebca6bu;
        h ^= (h >> 15);
    };
    mix(x); mix(y); mix(z);
    return h;
}

struct StarInfo { const char* cls; float r, g, b; };

StarInfo get_star_info(int32_t x, int32_t y, int32_t z) {
    uint32_t h = sector_hash(x, y, z);
    static const StarInfo classes[] = {
        { "M", 0.9f, 0.35f, 0.25f },
        { "K", 0.9f, 0.60f, 0.30f },
        { "G", 1.0f, 0.95f, 0.50f },
        { "F", 1.0f, 1.00f, 0.75f },
        { "A", 0.9f, 0.95f, 1.00f },
        { "B", 0.6f, 0.75f, 1.00f },
        { "O", 0.4f, 0.55f, 1.00f },
    };
    static const int thresholds[] = { 760, 880, 950, 980, 995, 999, 1000 };
    int roll = (int)(h % 1000);
    for (int i = 0; i < 7; ++i)
        if (roll < thresholds[i]) return classes[i];
    return classes[6];
}

std::string star_name(int32_t x, int32_t y, int32_t z) {
    uint32_t h = sector_hash(x, y, z) ^ 0xDEAD1234u;
    static const char* prefixes[] = {
        "Alph","Bet","Gam","Del","Eps","Zet","Eta",
        "Kapp","Lam","Omeg","Sig","Tau","Ups","Phi"
    };
    static const char* suffixes[] = {
        "ara","ori","exi","una","ius","ari","ini",
        "ega","yon","sis","rix","ela","von","ax"
    };
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%s-%d",
        prefixes[(h >> 8) % 14], suffixes[(h & 0xFF) % 14],
        (int)(h % 999) + 1);
    return buf;
}

// Not every sector hosts a star — keep the field sparse so it reads as a map,
// not a solid block of dots.
bool sector_has_star(int32_t x, int32_t y, int32_t z) {
    return (sector_hash(x, y, z) % 100) < 35;  // ~35% of sectors
}

} // namespace

// ─── Geometry ─────────────────────────────────────────────────────────────────
static void draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a = 1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x+w, y);
        glVertex2f(x+w, y+h); glVertex2f(x, y+h);
    glEnd();
}

static void draw_rect_outline(float x, float y, float w, float h,
                               float r, float g, float b, float lw = 1.5f) {
    glColor3f(r, g, b);
    glLineWidth(lw);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x, y); glVertex2f(x+w, y);
        glVertex2f(x+w, y+h); glVertex2f(x, y+h);
    glEnd();
}

// Project a sector offset (relative to the player, in sector units) to screen.
// We rotate around the vertical (yaw), tilt (pitch), then drop to 2D — a
// hand-rolled orthographic camera, so a 3D grid reads as 3D on a flat surface.
struct Proj { float x, y, depth; };

static Proj project(float dx, float dy, float dz, float cx, float cy, float scale) {
    // Yaw around Y (here our "vertical" map axis is Z): rotate X/Y plane.
    float cy_ = std::cos(s_yaw),  sy_ = std::sin(s_yaw);
    float rx = dx * cy_ - dy * sy_;
    float ry = dx * sy_ + dy * cy_;
    // Pitch: tilt the Z axis toward the viewer.
    float cp = std::cos(s_pitch), sp = std::sin(s_pitch);
    float screen_x = rx;
    float screen_y = ry * cp - dz * sp;
    float depth    = ry * sp + dz * cp;   // larger = closer to viewer
    return { cx + screen_x * scale, cy + screen_y * scale, depth };
}

// ─── Render ───────────────────────────────────────────────────────────────────
void map_render(int W, int H, float dt) {
    (void)dt;
    float ch = term_cell_h();

    // Backdrop
    glColor4f(0.0f, 0.0f, 0.0f, 0.78f);
    glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f((float)W,0);
        glVertex2f((float)W,(float)H); glVertex2f(0,(float)H);
    glEnd();

    // Snapshot world state
    int32_t self_sx, self_sy, self_sz;
    std::vector<MapPlayer> players;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        self_sx = g_self_sx; self_sy = g_self_sy; self_sz = g_self_sz;
        players = g_map_players;
    }

    float cx = W * 0.5f;
    float cy = H * 0.5f + 10.0f;
    float scale = std::min(W, H) / (float)(s_radius * 2 + 2) * 0.85f;

    // Header
    char hdr[96];
    std::snprintf(hdr, sizeof(hdr), "//  GALAXY MAP  //   sector [%d, %d, %d]",
                  self_sx, self_sy, self_sz);
    term_draw_string(20.0f, 16.0f, hdr, 0.45f, 0.70f, 1.0f, 1.0f);

    // ── Build draw list of stars + players, sorted back-to-front by depth ──────
    struct Dot {
        float x, y, depth, size;
        float r, g, b;
        bool  is_player, is_self;
        std::string label;
    };
    std::vector<Dot> dots;

    // Stars in a cube around the player
    for (int dz = -s_radius; dz <= s_radius; ++dz)
    for (int dy = -s_radius; dy <= s_radius; ++dy)
    for (int dx = -s_radius; dx <= s_radius; ++dx) {
        int32_t sx = self_sx + dx, sy = self_sy + dy, sz = self_sz + dz;
        if (!sector_has_star(sx, sy, sz)) continue;
        auto si = get_star_info(sx, sy, sz);
        Proj p = project((float)dx, (float)dy, (float)dz, cx, cy, scale);
        Dot d;
        d.x = p.x; d.y = p.y; d.depth = p.depth;
        d.size = 2.0f + (p.depth + s_radius) / (2.0f * s_radius) * 4.0f;
        d.r = si.r; d.g = si.g; d.b = si.b;
        d.is_player = d.is_self = false;
        dots.push_back(std::move(d));
    }

    // Players (drawn larger, with nick labels)
    for (auto& mp : players) {
        int dx = mp.sx - self_sx, dy = mp.sy - self_sy, dz = mp.sz - self_sz;
        Proj p = project((float)dx, (float)dy, (float)dz, cx, cy, scale);
        Dot d;
        d.x = p.x; d.y = p.y; d.depth = p.depth;
        d.is_self   = (mp.id == g_player_id);
        d.is_player = true;
        d.size = d.is_self ? 9.0f : 7.0f;
        if (d.is_self) { d.r = 0.30f; d.g = 1.00f; d.b = 0.45f; }
        else           { d.r = 1.00f; d.g = 0.75f; d.b = 0.20f; }
        d.label = mp.nick;
        dots.push_back(std::move(d));
    }

    std::sort(dots.begin(), dots.end(),
              [](const Dot& a, const Dot& b){ return a.depth < b.depth; });

    // ── Grid floor at the player's Z plane (faint, for spatial reference) ──────
    glLineWidth(1.0f);
    glColor4f(0.12f, 0.18f, 0.40f, 0.45f);
    glBegin(GL_LINES);
    for (int g = -s_radius; g <= s_radius; ++g) {
        Proj a = project((float)-s_radius, (float)g, 0.0f, cx, cy, scale);
        Proj b = project((float) s_radius, (float)g, 0.0f, cx, cy, scale);
        glVertex2f(a.x, a.y); glVertex2f(b.x, b.y);
        Proj c = project((float)g, (float)-s_radius, 0.0f, cx, cy, scale);
        Proj d = project((float)g, (float) s_radius, 0.0f, cx, cy, scale);
        glVertex2f(c.x, c.y); glVertex2f(d.x, d.y);
    }
    glEnd();

    // ── Dots ──────────────────────────────────────────────────────────────────
    glEnable(GL_POINT_SMOOTH);
    for (auto& d : dots) {
        // Depth-based brightness: closer = brighter.
        float t = (d.depth + s_radius) / (2.0f * s_radius);
        t = std::clamp(t, 0.25f, 1.0f);
        float br = d.is_player ? 1.0f : t;

        if (d.is_self) {
            // Pulsing halo ring on the player.
            float pulse = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 3.0f);
            glPointSize(d.size + 8.0f + pulse * 4.0f);
            glColor4f(d.r, d.g, d.b, 0.20f);
            glBegin(GL_POINTS); glVertex2f(d.x, d.y); glEnd();
        }

        glPointSize(d.size);
        glColor4f(d.r * br, d.g * br, d.b * br, d.is_player ? 1.0f : 0.85f);
        glBegin(GL_POINTS); glVertex2f(d.x, d.y); glEnd();

        if (d.is_player && !d.label.empty()) {
            float lw = term_string_width(d.label.c_str());
            term_draw_string(d.x - lw * 0.5f, d.y - ch - 4.0f, d.label.c_str(),
                             d.r, d.g, d.b, 0.95f);
        }
    }
    glDisable(GL_POINT_SMOOTH);

    // ── Footer / hint bar ──────────────────────────────────────────────────────
    const char* hint =
        "\xe2\x86\x90\xe2\x86\x92 rotate   \xe2\x86\x91\xe2\x86\x93 tilt   "
        "+/- zoom   wheel range   Esc close";
    float hw = term_string_width(hint);
    draw_rect(0, (float)H - ch - 12.0f, (float)W, ch + 12.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    term_draw_string((W - hw) * 0.5f, (float)H - ch - 6.0f, hint,
                     0.45f, 0.55f, 0.80f, 1.0f);

    // Online count
    char oc[48];
    std::snprintf(oc, sizeof(oc), "online: %d", (int)players.size());
    term_draw_string(20.0f, (float)H - ch - 6.0f + 0.0f, oc, 0.50f, 0.65f, 0.85f, 1.0f);
}

// ─── Input ────────────────────────────────────────────────────────────────────
bool map_on_key(int key, int action) {
    if (!g_map_open) return false;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return true;

    switch (key) {
    case GLFW_KEY_ESCAPE:
        g_map_open = false;
        if (g_settings.sounds_enabled) sound_play(SoundEvent::CMD_CLEAR);
        return true;
    case GLFW_KEY_LEFT:  s_yaw   -= 0.12f; return true;
    case GLFW_KEY_RIGHT: s_yaw   += 0.12f; return true;
    case GLFW_KEY_UP:    s_pitch  = std::clamp(s_pitch + 0.08f, 0.05f, 1.45f); return true;
    case GLFW_KEY_DOWN:  s_pitch  = std::clamp(s_pitch - 0.08f, 0.05f, 1.45f); return true;
    case GLFW_KEY_EQUAL:        // '+' (and '=')
    case GLFW_KEY_KP_ADD:
        s_radius = std::max(2, s_radius - 1);  // smaller radius = zoom in
        return true;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        s_radius = std::min(10, s_radius + 1);
        return true;
    }
    return true;  // consume all keys while open
}

void map_on_scroll(double yoffset) {
    if (!g_map_open) return;
    if (yoffset > 0) s_radius = std::max(2,  s_radius - 1);
    else             s_radius = std::min(10, s_radius + 1);
}
