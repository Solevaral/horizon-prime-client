#include "ship_widget.h"
#include "state.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>

// ─── Font helpers (terminal.cpp) ──────────────────────────────────────────────
extern float term_draw_string(float x, float y, const char* text,
                              float r, float g, float b, float a);
extern float term_string_width(const char* text);
extern float term_cell_h();

namespace {

constexpr float PI = 3.14159265358979f;

// ─── Voxel model description ──────────────────────────────────────────────────
// A ship is a list of unit cubes on an integer grid plus a tint. We keep models
// tiny (a couple dozen voxels) so the whole thing reads as pixel-art and costs
// almost nothing to draw. Coordinates: +x right, +y up, +z forward (nose).
struct Voxel { int8_t x, y, z; uint8_t mat; };

// Materials: index into a small palette. 0=hull, 1=accent, 2=cockpit glass,
// 3=engine block (the bit that glows when thrusting).
struct Mat { float r, g, b; };
const Mat PALETTE[] = {
    { 0.62f, 0.66f, 0.74f },  // 0 hull — cool steel
    { 0.30f, 0.55f, 0.95f },  // 1 accent — blue trim
    { 0.45f, 0.95f, 1.00f },  // 2 cockpit glass — cyan
    { 1.00f, 0.55f, 0.20f },  // 3 engine — orange (modulated by thrust)
};

// A compact arrow/fighter shape. Built once. Symmetric across x.
const std::vector<Voxel>& fighter_model() {
    static const std::vector<Voxel> m = [] {
        std::vector<Voxel> v;
        auto put = [&](int x, int y, int z, int mat) {
            v.push_back({ (int8_t)x, (int8_t)y, (int8_t)z, (uint8_t)mat });
        };
        // Central spine (nose -> tail), z from +3 (nose) to -3 (tail)
        put(0, 0,  3, 0);                 // nose tip
        put(0, 0,  2, 0); put(0, 0, 1, 0);
        put(0, 0,  0, 2);                 // cockpit
        put(0, 1,  0, 2);                 // canopy bump
        put(0, 0, -1, 0); put(0, 0, -2, 0);
        put(0, 0, -3, 3);                // engine core
        // Wings (mirror across x) sweeping back
        for (int s = -1; s <= 1; s += 2) {
            put(s*1, 0,  0, 0);
            put(s*2, 0, -1, 0);
            put(s*2, 0, -2, 1);          // wingtip accent
            put(s*3, 0, -2, 1);          // wingtip light mount
            put(s*1, 0, -1, 0);
            put(s*1, 0, -2, 0);
            put(s*1, 0, -3, 3);          // outboard engine
        }
        return v;
    }();
    return m;
}

// ─── Tiny hand-rolled 3D math ─────────────────────────────────────────────────
struct Vec3 { float x, y, z; };

// One unit cube centered at (cx,cy,cz), lit by a fixed directional light so the
// six faces read with distinct brightness — that flat per-face shading is what
// sells the voxel look.
void draw_cube(float cx, float cy, float cz, float s,
               float r, float g, float b, float emissive) {
    const float h = s * 0.5f;
    // face: {nx,ny,nz, and the 4 corner offsets}
    struct Face { Vec3 n; Vec3 v[4]; };
    static const Face F[6] = {
        {{ 0, 0, 1}, {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}}}, // +z front
        {{ 0, 0,-1}, {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}}}, // -z back
        {{ 1, 0, 0}, {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}}}, // +x right
        {{-1, 0, 0}, {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}}, // -x left
        {{ 0, 1, 0}, {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}}}, // +y top
        {{ 0,-1, 0}, {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}}}, // -y bottom
    };
    // Light direction (in model space, before camera rotation). Normalized-ish.
    const Vec3 L = { -0.4f, 0.85f, 0.35f };
    glBegin(GL_QUADS);
    for (const Face& f : F) {
        float d = f.n.x*L.x + f.n.y*L.y + f.n.z*L.z;   // -1..1
        float lit = 0.45f + 0.55f * (d * 0.5f + 0.5f); // 0.45..1.0 ambient+diffuse
        lit = std::fmin(1.0f, lit + emissive);
        glColor3f(r * lit, g * lit, b * lit);
        for (const Vec3& c : f.v)
            glVertex3f(cx + c.x*h, cy + c.y*h, cz + c.z*h);
    }
    glEnd();
}

// A glowing additive billboard-ish quad in 3D (for thruster plume / nav lights).
void draw_glow(float cx, float cy, float cz, float size,
               float r, float g, float b, float a) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);          // additive
    glBegin(GL_QUADS);
        glColor4f(r, g, b, a);
        glVertex3f(cx - size, cy - size, cz);
        glVertex3f(cx + size, cy - size, cz);
        glColor4f(r, g, b, 0.0f);
        glVertex3f(cx + size, cy + size, cz);
        glVertex3f(cx - size, cy + size, cz);
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

float s_clock = 0.0f;

} // namespace

// ─── Public render ────────────────────────────────────────────────────────────
void ship_widget_render(float x, float y, float w, float h, float dt) {
    s_clock += dt;

    // Snapshot ship state under the lock.
    ShipState ship;
    { std::lock_guard<std::mutex> lk(g_state_mutex); ship = g_ship; }

    // ── Panel frame ────────────────────────────────────────────────────────────
    float ch = term_cell_h();
    glDisable(GL_TEXTURE_2D);
    // backdrop
    glColor4f(0.02f, 0.03f, 0.06f, 0.82f);
    glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x+w, y);
        glVertex2f(x+w, y+h); glVertex2f(x, y+h);
    glEnd();
    // border
    glColor4f(0.18f, 0.30f, 0.55f, 0.9f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x+0.5f, y+0.5f); glVertex2f(x+w-0.5f, y+0.5f);
        glVertex2f(x+w-0.5f, y+h-0.5f); glVertex2f(x+0.5f, y+h-0.5f);
    glEnd();

    // Title + status string.
    const char* act = "IDLE";
    switch (ship.activity) {
        case ShipActivity::MINING:  act = "MINING";  break;
        case ShipActivity::FARMING: act = "FARMING"; break;
        case ShipActivity::WARPING: act = "WARP";    break;
        default: break;
    }
    term_draw_string(x + 8, y + 5, "// SHIP", 0.40f, 0.65f, 1.0f, 0.95f);
    float aw = term_string_width(act);
    // Status colour pulses when busy.
    float pulse = 0.5f + 0.5f * std::sin(s_clock * 4.0f);
    float sr = (ship.activity == ShipActivity::IDLE) ? 0.45f : (0.6f + 0.4f*pulse);
    term_draw_string(x + w - aw - 8, y + 5, act, sr, 0.85f, 0.55f, 0.95f);

    // ── 3D viewport (carve the inner area, keep room for a footer bar) ──────────
    float vx = x + 2, vy = y + ch + 4;
    float vw = w - 4, vh = h - (ch + 4) - (ch + 6);
    if (vw < 8 || vh < 8) return;

    // GL state was set up by the caller for 2D ortho; switch to a perspective
    // camera scoped to this widget, then restore everything afterwards.
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    // glViewport is in real framebuffer pixels with origin bottom-left, while our
    // UI rect is top-left. Flip y.
    int fb_h = prev_vp[3] + prev_vp[1];      // total framebuffer height (vp covers it)
    glViewport((GLint)vx, (GLint)(fb_h - (vy + vh)), (GLsizei)vw, (GLsizei)vh);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    // Simple perspective (fovy ~ 38°)
    float aspect = vw / vh;
    float f = 1.0f / std::tan(0.33f * PI * 0.5f);
    float zn = 0.1f, zf = 100.0f;
    float proj[16] = {
        f/aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (zf+zn)/(zn-zf), -1,
        0, 0, (2*zf*zn)/(zn-zf), 0
    };
    glMultMatrixf(proj);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Camera: pull back, look slightly down at the ship.
    glTranslatef(0.0f, -0.2f, -10.5f);
    glRotatef(22.0f, 1, 0, 0);                       // tilt down
    // Auto-rotation. Idle = slow drift; warp = fast spin-up.
    float spin = (ship.activity == ShipActivity::WARPING) ? 60.0f : 18.0f;
    glRotatef(s_clock * spin, 0, 1, 0);              // yaw
    // Gentle bob so an idle ship still feels alive.
    glTranslatef(0.0f, std::sin(s_clock * 1.2f) * 0.15f, 0.0f);

    // ── Draw voxels ────────────────────────────────────────────────────────────
    const float VS = 0.92f;     // cube size (<1 leaves seams = reads as voxels)
    bool mining = (ship.activity == ShipActivity::MINING);
    bool thrust = (ship.activity == ShipActivity::WARPING);
    float engine_glow = 0.0f;
    if (thrust)      engine_glow = 0.5f + 0.5f * pulse;
    else if (mining) engine_glow = 0.15f + 0.1f * pulse;

    for (const auto& vx_ : fighter_model()) {
        const Mat& m = PALETTE[vx_.mat];
        float emis = 0.0f;
        float rr = m.r, gg = m.g, bb = m.b;
        if (vx_.mat == 2)                       // cockpit gently glows
            emis = 0.10f + 0.08f * std::sin(s_clock * 2.0f + vx_.z);
        if (vx_.mat == 3)                       // engine blocks
            emis = engine_glow;
        draw_cube((float)vx_.x, (float)vx_.y, (float)vx_.z, VS, rr, gg, bb, emis);
    }

    // ── Animated extras (blending, after solid pass) ──────────────────────────
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    // Blinking nav lights at the wingtips (red port / green starboard).
    float blink = (std::fmod(s_clock, 1.4f) < 0.7f) ? 1.0f : 0.15f;
    draw_glow(-3.0f, 0.0f, -2.0f, 0.9f, 1.0f, 0.25f, 0.2f, 0.8f * blink);   // port red
    draw_glow( 3.0f, 0.0f, -2.0f, 0.9f, 0.25f, 1.0f, 0.3f, 0.8f * blink);   // stbd green

    // Engine plume(s) when thrusting/warping — stretched glow behind the engines.
    if (thrust) {
        float plume = 1.2f + 0.6f * pulse;
        for (int s = -2; s <= 2; s += 2) {
            draw_glow((float)s, 0.0f, -3.6f - plume*0.4f, plume,
                      1.0f, 0.6f, 0.2f, 0.55f);
            draw_glow((float)s, 0.0f, -3.4f, plume*0.6f,
                      1.0f, 0.9f, 0.6f, 0.7f);
        }
    }

    // Mining laser + sparks: a beam shooting from the nose forward, sparkling.
    if (mining) {
        glDisable(GL_TEXTURE_2D);
        glLineWidth(2.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        float beam = 0.6f + 0.4f * std::sin(s_clock * 25.0f);
        glBegin(GL_LINES);
            glColor4f(1.0f, 0.4f, 0.25f, beam);
            glVertex3f(0.0f, 0.0f, 3.0f);
            glColor4f(1.0f, 0.8f, 0.4f, 0.0f);
            glVertex3f(0.0f, 0.0f, 6.0f);
        glEnd();
        // sparks bouncing off the impact point
        glPointSize(2.0f);
        glBegin(GL_POINTS);
        for (int i = 0; i < 10; ++i) {
            float t = std::fmod(s_clock * 3.0f + i * 0.37f, 1.0f);
            float a = (i * 2.399f);                 // golden-angle spread
            float rad = t * 1.4f;
            float px = std::cos(a) * rad;
            float py = std::sin(a) * rad;
            glColor4f(1.0f, 0.7f, 0.3f, 1.0f - t);
            glVertex3f(px, py, 6.0f);
        }
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);

    // ── Restore 2D ortho camera + viewport ─────────────────────────────────────
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    // ── Footer: hull / fuel bars (or activity progress when busy) ──────────────
    float by = y + h - ch - 4;
    if (ship.activity == ShipActivity::IDLE) {
        auto bar = [&](float bx, float bw, float frac, float r, float g, float b,
                       const char* lbl) {
            glColor4f(0.08f, 0.10f, 0.16f, 1.0f);
            glBegin(GL_QUADS);
                glVertex2f(bx, by+3); glVertex2f(bx+bw, by+3);
                glVertex2f(bx+bw, by+3+8); glVertex2f(bx, by+3+8);
            glEnd();
            glColor3f(r, g, b);
            glBegin(GL_QUADS);
                glVertex2f(bx, by+3); glVertex2f(bx+bw*frac, by+3);
                glVertex2f(bx+bw*frac, by+3+8); glVertex2f(bx, by+3+8);
            glEnd();
            term_draw_string(bx, by + 12, lbl, r, g, b, 0.85f);
        };
        float half = (w - 18) * 0.5f;
        bar(x + 6,            half, ship.hull, 0.35f, 0.85f, 0.45f, "HULL");
        bar(x + 12 + half,    half, ship.fuel, 0.95f, 0.75f, 0.25f, "FUEL");
    } else {
        // Activity progress bar.
        float bw = w - 12;
        glColor4f(0.08f, 0.10f, 0.16f, 1.0f);
        glBegin(GL_QUADS);
            glVertex2f(x+6, by+3); glVertex2f(x+6+bw, by+3);
            glVertex2f(x+6+bw, by+3+10); glVertex2f(x+6, by+3+10);
        glEnd();
        glColor3f(0.95f * (0.7f + 0.3f*pulse), 0.55f, 0.25f);
        glBegin(GL_QUADS);
            glVertex2f(x+6, by+3); glVertex2f(x+6+bw*ship.progress, by+3);
            glVertex2f(x+6+bw*ship.progress, by+3+10); glVertex2f(x+6, by+3+10);
        glEnd();
        char buf[64];
        const char* verb = (ship.activity == ShipActivity::MINING)  ? "drilling"
                         : (ship.activity == ShipActivity::FARMING) ? "tending"
                         : "jumping";
        std::snprintf(buf, sizeof(buf), "%s %s  %d%%", verb,
                      ship.target[0] ? ship.target : "", (int)(ship.progress*100));
        term_draw_string(x + 6, by - ch + 4, buf, 0.85f, 0.7f, 0.45f, 0.95f);
    }

    glColor4f(1, 1, 1, 1);
}
