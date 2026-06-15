#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "terminal.h"
#include "state.h"
#include "net.h"
#include "sound.h"
#include "settings.h"

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

static const float PI = 3.14159265f;

// ─── Font atlas (reused from v2) ──────────────────────────────────────────────
static stbtt_packedchar g_cdata_ascii[224];
static stbtt_packedchar g_cdata_cyrillic[256];
static GLuint           g_font_tex  = 0;
static float            g_font_size = 17.0f;
static float            g_cell_w    = 0.0f;
static float            g_cell_h    = 0.0f;
static const int        ATLAS_W = 1024, ATLAS_H = 1024;

bool terminal_init(const char* font_path) {
    FILE* f = fopen(font_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> ttf_buf(sz);
    fread(ttf_buf.data(), 1, sz, f); fclose(f);

    std::vector<uint8_t> bitmap(ATLAS_W * ATLAS_H);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap.data(), ATLAS_W, ATLAS_H, 0, 1, nullptr)) return false;
    stbtt_PackSetOversampling(&pc, 1, 1);
    stbtt_pack_range ranges[2];
    ranges[0] = { g_font_size, 32,     nullptr, 224, g_cdata_ascii,    0, 0 };
    ranges[1] = { g_font_size, 0x0400, nullptr, 256, g_cdata_cyrillic, 0, 0 };
    if (!stbtt_PackFontRanges(&pc, ttf_buf.data(), 0, ranges, 2)) return false;
    stbtt_PackEnd(&pc);

    glGenTextures(1, &g_font_tex);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, ATLAS_H, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbtt_aligned_quad q; float cx = 0, cy = 0;
    stbtt_GetPackedQuad(g_cdata_ascii, ATLAS_W, ATLAS_H, 'M' - 32, &cx, &cy, &q, 1);
    g_cell_w = cx;
    g_cell_h = g_font_size * 1.35f;
    return true;
}

void terminal_shutdown() {
    if (g_font_tex) { glDeleteTextures(1, &g_font_tex); g_font_tex = 0; }
}

// ─── 2D text helpers ──────────────────────────────────────────────────────────
static void set_ortho(int W, int H) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
}

static void draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a = 1.0f) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x + w, y);
        glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

static float draw_string(float x, float y, const char* text,
                         float r, float g, float b, float a = 1.0f) {
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
        if (*s < 0x80) cp = *s++;
        else if ((*s & 0xE0) == 0xC0) { cp = (*s++ & 0x1F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF0) == 0xE0) { cp = (*s++ & 0x0F) << 12; if (*s) cp |= (*s++ & 0x3F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF8) == 0xF0) { cp = (*s++ & 0x07) << 18; if (*s) cp |= (*s++ & 0x3F) << 12; if (*s) cp |= (*s++ & 0x3F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else cp = *s++;
        stbtt_packedchar* cdata = nullptr; int idx = -1;
        if (cp >= 32 && cp < 256) { cdata = g_cdata_ascii; idx = cp - 32; }
        else if (cp >= 0x0400 && cp < 0x0500) { cdata = g_cdata_cyrillic; idx = cp - 0x0400; }
        else { cx += g_cell_w; continue; }
        stbtt_aligned_quad q; float bx = cx, by = baseline_y;
        stbtt_GetPackedQuad(cdata, ATLAS_W, ATLAS_H, idx, &bx, &by, &q, 1);
        glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
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
        if (*s < 0x80) cp = *s++;
        else if ((*s & 0xE0) == 0xC0) { cp = (*s++ & 0x1F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF0) == 0xE0) { cp = (*s++ & 0x0F) << 12; if (*s) cp |= (*s++ & 0x3F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF8) == 0xF0) { cp = (*s++ & 0x07) << 18; if (*s) cp |= (*s++ & 0x3F) << 12; if (*s) cp |= (*s++ & 0x3F) << 6; if (*s) cp |= (*s++ & 0x3F); }
        else cp = *s++;
        stbtt_packedchar* cdata = nullptr; int idx = -1;
        if (cp >= 32 && cp < 256) { cdata = g_cdata_ascii; idx = cp - 32; }
        else if (cp >= 0x0400 && cp < 0x0500) { cdata = g_cdata_cyrillic; idx = cp - 0x0400; }
        else { cx += g_cell_w; continue; }
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(cdata, ATLAS_W, ATLAS_H, idx, &cx, &dummy, &q, 1);
    }
    return cx;
}

float term_draw_string(float x, float y, const char* t, float r, float g, float b, float a) {
    return draw_string(x, y, t, r, g, b, a);
}
float term_string_width(const char* t) { return string_width(t); }
float term_cell_h() { return g_cell_h; }

// ─── Login screen (reused from v2) ────────────────────────────────────────────
struct LoginLayout {
    float fX, fW, fH, fY1, fY2;
    float bY, bW, bH, b_login_x, b_reg_x;
};
static LoginLayout login_layout(int W, int H) {
    LoginLayout L;
    float cx = W * 0.5f, cy = H * 0.5f;
    L.fW = 360.0f; L.fX = cx - L.fW * 0.5f; L.fH = 40.0f;
    L.fY1 = cy - 60; L.fY2 = L.fY1 + 56;
    L.bY = L.fY2 + L.fH + 18.0f; L.bW = 170.0f; L.bH = 34.0f;
    L.b_login_x = L.fX; L.b_reg_x = L.fX + L.fW - L.bW;
    return L;
}
static bool in_rect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

static void login_submit(bool do_register) {
    if (g_field_nick.empty() || g_field_pass.empty()) return;
    if (g_conn_status != ConnStatus::ONLINE) { g_auth_error = "Server is offline. Connecting..."; return; }
    if (do_register) {
        if (g_field_nick.size() < 4) { g_auth_error = "Nickname must be at least 4 characters."; return; }
        if (g_field_pass.size() < 5) { g_auth_error = "Password must be at least 5 characters."; return; }
    }
    g_auth_error.clear();
    net_send_login(g_field_nick, g_field_pass, do_register);
}

static void render_login(int W, int H) {
    glClearColor(0.03f, 0.03f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    set_ortho(W, H);
    float cx = W * 0.5f, cy = H * 0.5f;

    srand(0xDEAD1234);
    glPointSize(1.5f); glColor3f(0.12f, 0.15f, 0.22f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 200; ++i) glVertex2f((float)(rand() % W), (float)(rand() % H));
    glEnd();

    const char* title = "HORIZON  PRIME";
    float tw = string_width(title);
    draw_string(cx - tw * 0.5f, cy - 130, title, 0.45f, 0.65f, 1.0f);
    const char* sub = "//  Deep Space Online  //";
    draw_string(cx - string_width(sub) * 0.5f, cy - 104, sub, 0.25f, 0.30f, 0.55f);

    LoginLayout L = login_layout(W, H);
    bool nf = (g_focused_login == 0), pf = (g_focused_login == 1);

    draw_rect(L.fX, L.fY1, L.fW, L.fH, 0.05f, 0.05f, 0.12f);
    draw_string(L.fX + 10, L.fY1 + 8, "Nickname:", 0.30f, 0.40f, 0.70f);
    std::string nd = g_field_nick + (nf ? "_" : "");
    draw_string(L.fX + 120, L.fY1 + 8, nd.c_str(), 0.85f, 0.92f, 1.0f);

    draw_rect(L.fX, L.fY2, L.fW, L.fH, 0.05f, 0.05f, 0.12f);
    draw_string(L.fX + 10, L.fY2 + 8, "Password:", 0.30f, 0.40f, 0.70f);
    std::string pd = std::string(g_field_pass.size(), '*') + (pf ? "_" : "");
    draw_string(L.fX + 120, L.fY2 + 8, pd.c_str(), 0.85f, 0.92f, 1.0f);

    draw_rect(L.b_login_x, L.bY, L.bW, L.bH, 0.04f, 0.18f, 0.06f);
    draw_string(L.b_login_x + 12, L.bY + 8, "Enter  - Login", 0.25f, 0.90f, 0.30f);
    draw_rect(L.b_reg_x, L.bY, L.bW, L.bH, 0.04f, 0.05f, 0.20f);
    draw_string(L.b_reg_x + 12, L.bY + 8, "F2  - Register", 0.35f, 0.40f, 0.90f);

    const char* hint = "Tab - switch field     Esc - quit";
    draw_string(cx - string_width(hint) * 0.5f, L.bY + 50, hint, 0.22f, 0.25f, 0.45f);

    if (!g_auth_error.empty()) {
        float ew = std::max(L.fW, string_width(g_auth_error.c_str()) + 28);
        float ex = cx - ew * 0.5f;
        draw_rect(ex, L.bY + 72, ew, 32, 0.22f, 0.04f, 0.04f);
        draw_string(ex + 12, L.bY + 80, g_auth_error.c_str(), 1.0f, 0.35f, 0.35f);
    }

    ConnStatus cs = g_conn_status.load();
    float dr, dg, db; const char* st; char buf[48] = {};
    if (cs == ConnStatus::ONLINE)     { dr=0.2f; dg=0.85f; db=0.3f; std::snprintf(buf,sizeof(buf),"  Online: %d", g_online_count.load()); st=buf; }
    else if (cs == ConnStatus::CONNECTING) { dr=0.85f; dg=0.75f; db=0.1f; st="  Connecting..."; }
    else                              { dr=0.85f; dg=0.2f; db=0.2f; st="  Server offline"; }
    float sy = (float)H - 26.0f;
    glPointSize(10.0f); glColor3f(dr, dg, db);
    glBegin(GL_POINTS); glVertex2f(14.0f, sy + 8.0f); glEnd();
    draw_string(22.0f, sy, st, dr * 0.8f, dg * 0.8f, db * 0.8f);
}

// ─── 3D world ─────────────────────────────────────────────────────────────────
// Tile (tx, ty) maps to world (x = tx, y = 0, z = ty). The camera orbits the
// player, angled down like OSRS. Movement between tiles is interpolated.
static float g_cam_yaw   = 0.7f;     // radians, rotatable
static float g_cam_dist  = 16.0f;    // zoom
static const float CAM_PITCH = 0.95f; // fixed downward tilt (radians from horizontal)
static int   g_mouse_x = 0, g_mouse_y = 0;
static bool  g_rotating = false;
static double g_rot_last_x = 0;

// Where the player's render position is (for camera target). Updated each frame.
static float g_focus_x = 4.0f, g_focus_z = 4.0f;

// 4x4 matrices, column-major (OpenGL style). Minimal helpers.
static void mat_identity(float* m) { for (int i=0;i<16;i++) m[i]=(i%5==0)?1.f:0.f; }
static void mat_mul(const float* a, const float* b, float* out) {
    float r[16];
    for (int c=0;c<4;c++) for (int row=0;row<4;row++) {
        r[c*4+row] = a[0*4+row]*b[c*4+0] + a[1*4+row]*b[c*4+1]
                   + a[2*4+row]*b[c*4+2] + a[3*4+row]*b[c*4+3];
    }
    std::memcpy(out, r, sizeof(r));
}
static void mat_perspective(float* m, float fovy, float aspect, float zn, float zf) {
    float fH = std::tan(fovy * 0.5f * PI / 180.0f) * zn;
    float fW = fH * aspect;
    mat_identity(m);
    m[0]=zn/fW; m[5]=zn/fH;
    m[10]=-(zf+zn)/(zf-zn); m[11]=-1.f;
    m[14]=-(2.f*zf*zn)/(zf-zn); m[15]=0.f;
}
// Camera eye position from orbit params, looking at (tx,0,tz).
static void compute_eye(float tx, float tz, float* ex, float* ey, float* ez) {
    float horiz = std::cos(CAM_PITCH) * g_cam_dist;
    *ex = tx + std::sin(g_cam_yaw) * horiz;
    *ez = tz + std::cos(g_cam_yaw) * horiz;
    *ey = std::sin(CAM_PITCH) * g_cam_dist;
}
static void mat_lookat(float* m, float ex,float ey,float ez, float cx,float cy,float cz) {
    float fx=cx-ex, fy=cy-ey, fz=cz-ez;
    float fl=std::sqrt(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
    float ux=0,uy=1,uz=0;
    float sx=fy*uz-fz*uy, sy=fz*ux-fx*uz, sz=fx*uy-fy*ux;
    float sl=std::sqrt(sx*sx+sy*sy+sz*sz); sx/=sl; sy/=sl; sz/=sl;
    float ux2=sy*fz-sz*fy, uy2=sz*fx-sx*fz, uz2=sx*fy-sy*fx;
    mat_identity(m);
    m[0]=sx; m[4]=sy; m[8]=sz;
    m[1]=ux2; m[5]=uy2; m[9]=uz2;
    m[2]=-fx; m[6]=-fy; m[10]=-fz;
    m[12]=-(sx*ex+sy*ey+sz*ez);
    m[13]=-(ux2*ex+uy2*ey+uz2*ez);
    m[14]=(fx*ex+fy*ey+fz*ez);
}

// Store the last view/proj for click picking.
static float g_view[16], g_proj[16];
static int   g_vp_w = 1, g_vp_h = 1;

// Draw a box centred at (x,z), base on the ground, of given half-size and height.
static void draw_box(float x, float z, float half, float height,
                     float r, float g, float b) {
    float x0=x-half, x1=x+half, z0=z-half, z1=z+half, y0=0.0f, y1=height;
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    // top
    glColor3f(r, g, b);
    glVertex3f(x0,y1,z0); glVertex3f(x1,y1,z0); glVertex3f(x1,y1,z1); glVertex3f(x0,y1,z1);
    // sides (slightly shaded)
    glColor3f(r*0.8f, g*0.8f, b*0.8f);
    glVertex3f(x0,y0,z1); glVertex3f(x1,y0,z1); glVertex3f(x1,y1,z1); glVertex3f(x0,y1,z1);
    glVertex3f(x1,y0,z0); glVertex3f(x0,y0,z0); glVertex3f(x0,y1,z0); glVertex3f(x1,y1,z0);
    glColor3f(r*0.65f, g*0.65f, b*0.65f);
    glVertex3f(x0,y0,z0); glVertex3f(x0,y0,z1); glVertex3f(x0,y1,z1); glVertex3f(x0,y1,z0);
    glVertex3f(x1,y0,z1); glVertex3f(x1,y0,z0); glVertex3f(x1,y1,z0); glVertex3f(x1,y1,z1);
    glEnd();
}

// Unproject a screen click onto the ground plane (y=0). Returns tile or {-1,-1}.
static void screen_to_tile(double mx, double my, int W, int H, int& out_tx, int& out_ty) {
    out_tx = out_ty = -1;
    // NDC
    float nx = (float)(2.0 * mx / W - 1.0);
    float ny = (float)(1.0 - 2.0 * my / H);
    // Inverse of proj*view applied to two points (near/far) — but we can build a
    // ray from the camera eye through the clicked direction more simply.
    float invVP[16], vp[16];
    mat_mul(g_proj, g_view, vp);
    // Invert vp (general 4x4 inverse).
    auto inv4 = [](const float* m, float* o)->bool {
        float inv[16], det;
        inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
        inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
        inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
        inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
        inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
        inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
        inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
        inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
        inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
        inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
        inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
        inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
        inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
        inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
        inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
        inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
        det=m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
        if (std::fabs(det)<1e-9f) return false;
        det=1.0f/det; for(int i=0;i<16;i++) o[i]=inv[i]*det; return true;
    };
    if (!inv4(vp, invVP)) return;
    auto unproject = [&](float z, float& wx, float& wy, float& wz) {
        float ix=invVP[0]*nx+invVP[4]*ny+invVP[8]*z+invVP[12];
        float iy=invVP[1]*nx+invVP[5]*ny+invVP[9]*z+invVP[13];
        float iz=invVP[2]*nx+invVP[6]*ny+invVP[10]*z+invVP[14];
        float iw=invVP[3]*nx+invVP[7]*ny+invVP[11]*z+invVP[15];
        if (std::fabs(iw)<1e-9f) iw=1; wx=ix/iw; wy=iy/iw; wz=iz/iw;
    };
    float ax,ay,az, bx,by,bz;
    unproject(-1.0f, ax,ay,az);   // near
    unproject( 1.0f, bx,by,bz);   // far
    float dy = by - ay;
    if (std::fabs(dy) < 1e-6f) return;
    float t = -ay / dy;           // intersect y=0
    if (t < 0) return;
    float hx = ax + (bx-ax)*t;
    float hz = az + (bz-az)*t;
    int tx = (int)std::floor(hx + 0.5f);
    int ty = (int)std::floor(hz + 0.5f);
    if (tx < 0 || ty < 0 || tx >= g_sector.tiles_x || ty >= g_sector.tiles_y) return;
    out_tx = tx; out_ty = ty;
}

// Cockpit panel shown when seated in the ship.
static void render_cockpit(int W, int H) {
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    set_ortho(W, H);

    // Viewport "window" looking at stars.
    float vx = 40, vy = 40, vw = W - 80, vh = H * 0.45f;
    draw_rect(vx, vy, vw, vh, 0.01f, 0.01f, 0.03f);
    srand(0xBEEF77);
    glPointSize(1.6f); glColor3f(0.7f, 0.8f, 1.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 260; ++i)
        glVertex2f(vx + rand() % (int)vw, vy + rand() % (int)vh);
    glEnd();
    // Frame
    glColor3f(0.25f, 0.45f, 0.7f); glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(vx,vy); glVertex2f(vx+vw,vy); glVertex2f(vx+vw,vy+vh); glVertex2f(vx,vy+vh);
    glEnd();

    // Dashboard
    float dy = vy + vh + 20;
    draw_rect(vx, dy, vw, H - dy - 40, 0.06f, 0.07f, 0.10f);
    glColor3f(0.20f, 0.30f, 0.45f); glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(vx,dy); glVertex2f(vx+vw,dy); glVertex2f(vx+vw,H-40); glVertex2f(vx,H-40);
    glEnd();

    draw_string(vx + 16, dy + 14, "С: CONTROL PANEL — STARSHIP", 0.55f, 0.80f, 1.0f);
    char sec[96];
    std::snprintf(sec, sizeof(sec), "  Sector [%d,%d,%d]   Star: %s (%c)",
        g_sector.sector_x, g_sector.sector_y, g_sector.sector_z,
        g_sector.star_name.c_str(), g_sector.star_class);
    draw_string(vx + 16, dy + 40, sec, 0.70f, 0.85f, 0.75f);
    draw_string(vx + 16, dy + 64, "  Hull 100%   Shield 100%   Fuel 100%   (flight not yet implemented)",
                0.55f, 0.70f, 0.60f);

    // Ship terminal lines
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        float ty = dy + 96;
        int start = std::max(0, (int)g_term.size() - 8);
        for (int i = start; i < (int)g_term.size(); ++i) {
            auto& l = g_term[i];
            draw_string(vx + 16, ty, l.text.c_str(), l.r/255.f, l.g/255.f, l.b/255.f);
            ty += g_cell_h;
        }
        // Input line
        if (g_term_open) {
            std::string in = "  :: " + g_input_buf + "_";
            draw_string(vx + 16, H - 64, in.c_str(), 0.7f, 0.9f, 0.7f);
        }
    }

    const char* hint = "Space - exit ship    Enter - ship terminal    Esc - quit";
    draw_string(vx + 16, H - 40 + 6, hint, 0.35f, 0.45f, 0.60f);
}

static void render_world(int W, int H, float dt) {
    glClearColor(0.01f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // Update interpolated render positions and find the player's focus point.
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        float lerp = std::min(1.0f, dt * 8.0f);
        for (auto& [id, e] : g_entities) {
            e.rx += ((float)e.tile_x - e.rx) * lerp;
            e.ry += ((float)e.tile_y - e.ry) * lerp;
            if (id == g_player_id) { g_focus_x = e.rx; g_focus_z = e.ry; }
        }
    }

    // Projection + view
    mat_perspective(g_proj, 50.0f, (float)W / (float)H, 0.1f, 200.0f);
    float ex, ey, ez;
    compute_eye(g_focus_x, g_focus_z, &ex, &ey, &ez);
    mat_lookat(g_view, ex, ey, ez, g_focus_x, 0.0f, g_focus_z);
    g_vp_w = W; g_vp_h = H;

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(g_proj);
    glMatrixMode(GL_MODELVIEW);  glLoadMatrixf(g_view);

    int TX = g_sector.tiles_x, TY = g_sector.tiles_y;

    // Ground grid (checker tiles).
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    for (int ty = 0; ty < TY; ++ty)
        for (int tx = 0; tx < TX; ++tx) {
            bool dark = ((tx + ty) & 1);
            if (dark) glColor3f(0.10f, 0.12f, 0.16f);
            else      glColor3f(0.13f, 0.16f, 0.21f);
            float x0=tx-0.5f, x1=tx+0.5f, z0=ty-0.5f, z1=ty+0.5f;
            glVertex3f(x0,0,z0); glVertex3f(x1,0,z0); glVertex3f(x1,0,z1); glVertex3f(x0,0,z1);
        }
    glEnd();
    // Grid lines
    glColor3f(0.20f, 0.28f, 0.38f); glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = 0; i <= TX; ++i) { glVertex3f(i-0.5f,0.01f,-0.5f); glVertex3f(i-0.5f,0.01f,TY-0.5f); }
    for (int j = 0; j <= TY; ++j) { glVertex3f(-0.5f,0.01f,j-0.5f); glVertex3f(TX-0.5f,0.01f,j-0.5f); }
    glEnd();

    // The ship (bigger box) at its tile.
    draw_box((float)g_sector.ship_tile_x, (float)g_sector.ship_tile_y, 0.9f, 1.4f,
             0.55f, 0.60f, 0.72f);

    // Players (boxes). Self is highlighted.
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        for (auto& [id, e] : g_entities) {
            if (e.riding) continue;  // hidden — they're inside the ship
            if (id == g_player_id) draw_box(e.rx, e.ry, 0.30f, 0.9f, 0.30f, 0.85f, 0.45f);
            else                   draw_box(e.rx, e.ry, 0.30f, 0.9f, 0.75f, 0.55f, 0.35f);
        }
    }

    // ── 2D overlay (HUD + chat + nameplates) ───────────────────────────────────
    glDisable(GL_DEPTH_TEST);
    set_ortho(W, H);

    // HUD
    char hud[128];
    std::snprintf(hud, sizeof(hud), "Sector [%d,%d,%d]  Star %s (%c)  Pilots: %d",
        g_sector.sector_x, g_sector.sector_y, g_sector.sector_z,
        g_sector.star_name.c_str(), g_sector.star_class, g_online_count.load());
    draw_rect(8, 8, string_width(hud) + 20, 26, 0.0f, 0.0f, 0.0f, 0.5f);
    draw_string(16, 12, hud, 0.7f, 0.85f, 1.0f);

    const char* hint = "ЛКМ - идти   ПКМ по кораблю - сесть   ←→ - камера   колесо - зум   Esc - меню";
    draw_string(16, H - 28, hint, 0.45f, 0.55f, 0.70f);

    // Chat (bottom-left, fades after 8s)
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        double now = glfwGetTime();
        float yy = H - 60;
        for (int i = (int)g_chat_lines.size() - 1; i >= 0 && yy > H * 0.5f; --i) {
            auto& c = g_chat_lines[i];
            double age = now - c.timestamp;
            if (age > 8.0) continue;
            float a = age > 6.0 ? (float)(1.0 - (age - 6.0) / 2.0) : 1.0f;
            draw_string(16, yy, c.text.c_str(), c.r/255.f, c.g/255.f, c.b/255.f, a);
            yy -= g_cell_h;
        }
    }
}

// ─── ESC pause menu ───────────────────────────────────────────────────────────
// Three stacked buttons centred on screen: Settings / Leave to menu / Quit.
struct PauseLayout {
    float bx, bw, bh;
    float by[3];   // tops of the three buttons
};
static PauseLayout pause_layout(int W, int H) {
    PauseLayout L;
    L.bw = 280.0f; L.bh = 46.0f;
    L.bx = W * 0.5f - L.bw * 0.5f;
    float gap = 14.0f;
    float total = 3 * L.bh + 2 * gap;
    float top = H * 0.5f - total * 0.5f + 20.0f;
    for (int i = 0; i < 3; ++i) L.by[i] = top + i * (L.bh + gap);
    return L;
}

static void render_pause_menu(int W, int H) {
    // Dim the world behind the menu.
    set_ortho(W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_rect(0, 0, (float)W, (float)H, 0.0f, 0.0f, 0.0f, 0.55f);

    const char* title = "ПАУЗА";
    draw_string(W * 0.5f - string_width(title) * 0.5f, H * 0.5f - 130, title, 0.7f, 0.85f, 1.0f);

    PauseLayout L = pause_layout(W, H);
    const char* labels[3] = { "Настройки", "Выйти в меню", "Выйти из игры" };
    float cols[3][3] = {
        { 0.12f, 0.18f, 0.30f },
        { 0.20f, 0.16f, 0.06f },
        { 0.26f, 0.07f, 0.07f },
    };
    for (int i = 0; i < 3; ++i) {
        bool hot = in_rect((float)g_mouse_x, (float)g_mouse_y, L.bx, L.by[i], L.bw, L.bh);
        float m = hot ? 1.6f : 1.0f;
        draw_rect(L.bx, L.by[i], L.bw, L.bh, cols[i][0]*m, cols[i][1]*m, cols[i][2]*m);
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.4f, 0.5f, 0.7f); glLineWidth(1.2f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(L.bx, L.by[i]); glVertex2f(L.bx + L.bw, L.by[i]);
            glVertex2f(L.bx + L.bw, L.by[i] + L.bh); glVertex2f(L.bx, L.by[i] + L.bh);
        glEnd();
        draw_string(L.bx + L.bw * 0.5f - string_width(labels[i]) * 0.5f,
                    L.by[i] + L.bh * 0.5f - g_font_size * 0.5f,
                    labels[i], 0.88f, 0.92f, 1.0f);
    }
    const char* hint = "Esc - закрыть меню";
    draw_string(W * 0.5f - string_width(hint) * 0.5f, L.by[2] + L.bh + 24,
                hint, 0.4f, 0.5f, 0.65f);
}

// ─── Top-level render ─────────────────────────────────────────────────────────
void terminal_render(int W, int H, float dt) {
    if (g_screen == Screen::LOGIN) {
        render_login(W, H);
        return;
    }
    if (g_riding) render_cockpit(W, H);
    else          render_world(W, H, dt);

    if (g_settings_open) settings_render(W, H);
    if (g_pause_menu && !g_settings_open) render_pause_menu(W, H);
}

// ─── Input ────────────────────────────────────────────────────────────────────
void terminal_cb_char(GLFWwindow*, unsigned int cp) {
    if (g_screen == Screen::LOGIN) {
        if (cp >= 32 && cp < 127) {
            std::string& f = (g_focused_login == 0) ? g_field_nick : g_field_pass;
            if (f.size() < 60) f += (char)cp;
        }
        return;
    }
    if (g_term_open && cp >= 32 && cp < 127) {
        if (g_input_buf.size() < 200) g_input_buf += (char)cp;
    }
}

void terminal_cb_key(GLFWwindow* win, int key, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (g_screen == Screen::LOGIN) {
        if (key == GLFW_KEY_ESCAPE) { g_running = false; glfwSetWindowShouldClose(win, 1); }
        else if (key == GLFW_KEY_TAB)   g_focused_login ^= 1;
        else if (key == GLFW_KEY_ENTER) login_submit(false);
        else if (key == GLFW_KEY_F2)    login_submit(true);
        else if (key == GLFW_KEY_BACKSPACE) {
            std::string& f = (g_focused_login == 0) ? g_field_nick : g_field_pass;
            if (!f.empty()) f.pop_back();
        }
        return;
    }

    // WORLD / cockpit

    // Settings overlay (opened from the pause menu) gets first crack at keys.
    if (g_settings_open) {
        if (key == GLFW_KEY_ESCAPE) g_settings_open = false;  // back to pause menu
        else settings_on_key(key, GLFW_PRESS);
        return;
    }

    // Pause menu: Esc toggles it shut.
    if (g_pause_menu) {
        if (key == GLFW_KEY_ESCAPE) g_pause_menu = false;
        return;
    }

    // Ship terminal input line.
    if (g_term_open) {
        if (key == GLFW_KEY_ENTER) {
            std::string cmd = g_input_buf; g_input_buf.clear(); g_term_open = false;
            if (!cmd.empty()) net_send_command(cmd);
        } else if (key == GLFW_KEY_ESCAPE) { g_term_open = false; g_input_buf.clear(); }
        else if (key == GLFW_KEY_BACKSPACE && !g_input_buf.empty()) g_input_buf.pop_back();
        return;
    }

    switch (key) {
    case GLFW_KEY_ESCAPE: g_pause_menu = true; break;   // open the pause menu
    case GLFW_KEY_LEFT:   g_cam_yaw -= 0.08f; break;
    case GLFW_KEY_RIGHT:  g_cam_yaw += 0.08f; break;
    case GLFW_KEY_ENTER:  if (g_riding) g_term_open = true; break;
    case GLFW_KEY_SPACE:  if (g_riding) net_send_interact(OBJECT_SHIP); break;
    default: break;
    }
}

void terminal_cb_mouse_button(GLFWwindow* win, int button, int action, int) {
    if (g_screen == Screen::LOGIN) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            int W, H; glfwGetFramebufferSize(win, &W, &H);
            LoginLayout L = login_layout(W, H);
            float mx = (float)g_mouse_x, my = (float)g_mouse_y;
            if (in_rect(mx, my, L.fX, L.fY1, L.fW, L.fH)) g_focused_login = 0;
            else if (in_rect(mx, my, L.fX, L.fY2, L.fW, L.fH)) g_focused_login = 1;
            else if (in_rect(mx, my, L.b_login_x, L.bY, L.bW, L.bH)) login_submit(false);
            else if (in_rect(mx, my, L.b_reg_x, L.bY, L.bW, L.bH)) login_submit(true);
        }
        return;
    }

    int W, H; glfwGetFramebufferSize(win, &W, &H);

    // Settings overlay (from pause menu) consumes clicks.
    if (g_settings_open) {
        settings_on_mouse_button((float)g_mouse_x, (float)g_mouse_y, button, action);
        return;
    }

    // Pause menu buttons.
    if (g_pause_menu) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            PauseLayout L = pause_layout(W, H);
            float mx = (float)g_mouse_x, my = (float)g_mouse_y;
            if (in_rect(mx, my, L.bx, L.by[0], L.bw, L.bh)) {        // Settings
                g_settings_open = true;
            } else if (in_rect(mx, my, L.bx, L.by[1], L.bw, L.bh)) { // Leave to menu
                g_pause_menu = false;
                g_settings_open = false;
                net_logout();
            } else if (in_rect(mx, my, L.bx, L.by[2], L.bw, L.bh)) { // Quit game
                g_running = false;
                glfwSetWindowShouldClose(win, 1);
            }
        }
        return;
    }

    if (g_riding) return;  // cockpit: no world clicks

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        int tx, ty; screen_to_tile(g_mouse_x, g_mouse_y, W, H, tx, ty);
        if (tx >= 0) {
            // Clicking the ship tile boards it if adjacent; else just walk there.
            if (tx == g_sector.ship_tile_x && ty == g_sector.ship_tile_y)
                net_send_interact(OBJECT_SHIP);
            else
                net_send_move(tx, ty);
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        int tx, ty; screen_to_tile(g_mouse_x, g_mouse_y, W, H, tx, ty);
        if (tx == g_sector.ship_tile_x && ty == g_sector.ship_tile_y)
            net_send_interact(OBJECT_SHIP);
        else { g_rotating = true; g_rot_last_x = g_mouse_x; }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        g_rotating = false;
    }
}

void terminal_cb_cursor_pos(GLFWwindow*, double mx, double my) {
    g_mouse_x = (int)mx; g_mouse_y = (int)my;
    if (g_rotating) {
        g_cam_yaw += (float)((mx - g_rot_last_x) * 0.01);
        g_rot_last_x = mx;
    }
}

void terminal_cb_scroll(GLFWwindow*, double, double yoff) {
    g_cam_dist -= (float)yoff * 1.5f;
    g_cam_dist = std::clamp(g_cam_dist, 6.0f, 40.0f);
}
