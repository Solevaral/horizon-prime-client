#include "state.h"
#include "net.h"
#include "terminal.h"
#include "settings.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

// Enable UTF-8 console output
void setup_console_utf8() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
}
#else
void setup_console_utf8() {}
#endif

// Font path — next to executable, or fall back to Windows system font
static const char* FONT_PATHS[] = {
    "assets/consola.ttf",
    "C:/Windows/Fonts/consola.ttf",
    nullptr
};

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string host     = "127.0.0.1";
    std::string port_str = std::to_string(SERVER_PORT);
    if (argc > 1) { char buf[256]={}; WideCharToMultiByte(CP_UTF8,0,wargv[1],-1,buf,sizeof(buf),0,0); host=buf; }
    if (argc > 2) { char buf[32] ={}; WideCharToMultiByte(CP_UTF8,0,wargv[2],-1,buf,sizeof(buf),0,0); port_str=buf; }
    LocalFree(wargv);
#else
int main(int argc, char* argv[]) {
    std::string host     = (argc > 1) ? argv[1] : "127.0.0.1";
    std::string port_str = (argc > 2) ? argv[2] : std::to_string(SERVER_PORT);
#endif

    setup_console_utf8();
    settings_load("settings.cfg");
    g_term_buf_size = g_settings.term_buffer_size;  // sync global from settings

    // Pre-fill the login fields from remembered credentials (if opted in).
    saved_login_load("login.cfg");
    if (g_settings.remember_login) g_field_nick = g_saved_login.nick;
    if (g_settings.remember_pass)  g_field_pass = g_saved_login.pass;

    // ── HUD preview mode (temporary) ──────────────────────────────────────────
    // HP_PREVIEW=1 skips login/server and drops straight onto the terminal screen
    // with mock data so the docked HUD widgets can be eyeballed without auth.
    const char* preview_env = std::getenv("HP_PREVIEW");
    bool preview = preview_env && std::strcmp(preview_env, "0") != 0;
    if (preview) {
        g_screen = Screen::TERMINAL;
        {
            std::lock_guard<std::mutex> lk(g_state_mutex);
            g_self_sx = 12; g_self_sy = -4; g_self_sz = 7;
            g_map_players.push_back({ "Nova",  13, -4,  7, 2 });
            g_map_players.push_back({ "Drake", 12, -3,  8, 3 });
            g_map_players.push_back({ "YOU",   12, -4,  7, 1 });
            g_player_id = 1;
            g_lines.push_back({ "  Horizon Prime — HUD preview", 120, 180, 255 });
            g_lines.push_back({ "  > scan", 120, 160, 120 });
            g_lines.push_back({ "  3 asteroids detected in this sector.", 180, 200, 180 });
            g_lines.push_back({ "  > mine asteroid 1", 120, 160, 120 });
            g_ship.activity = ShipActivity::MINING;
            std::strncpy(g_ship.target, "asteroid 1", sizeof(g_ship.target)-1);
            g_ship.hull = 0.82f; g_ship.fuel = 0.64f;
        }
    }

    void (*net_fn)(const std::string&, const std::string&) =
        preview ? +[](const std::string&, const std::string&){} : network_thread_func;
    std::thread net_thread(net_fn, host, port_str);

    if (!glfwInit()) { g_running=false; net_thread.join(); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* win = glfwCreateWindow(1100, 680, "Horizon Prime", nullptr, nullptr);
    if (!win) { glfwTerminate(); g_running=false; net_thread.join(); return 1; }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwTerminate(); g_running=false; net_thread.join(); return 1;
    }

    // Load font
    bool font_ok = false;
    for (int i = 0; FONT_PATHS[i] && !font_ok; ++i)
        font_ok = terminal_init(FONT_PATHS[i]);
    if (!font_ok) {
        glfwTerminate(); g_running=false; net_thread.join(); return 1;
    }

    glfwSetCharCallback(win,        terminal_cb_char);
    glfwSetKeyCallback(win,         terminal_cb_key);
    glfwSetMouseButtonCallback(win, terminal_cb_mouse_button);
    glfwSetCursorPosCallback(win,   terminal_cb_cursor_pos);
    glfwSetScrollCallback(win,      terminal_cb_scroll);

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(win) && g_running) {
        double now = glfwGetTime();
        float  dt  = (float)(now - last_time);
        last_time  = now;

        glfwPollEvents();

        // Preview: drive mock ship activity so all widget states are visible.
        if (preview) {
            static float t = 0.0f; t += dt;
            std::lock_guard<std::mutex> lk(g_state_mutex);
            int phase = ((int)(t / 6.0f)) % 3;   // 6s per phase: mine -> warp -> idle
            if (phase == 0) {
                g_ship.activity = ShipActivity::MINING;
                std::strncpy(g_ship.target, "asteroid 1", sizeof(g_ship.target)-1);
                g_ship.progress = std::fmod(t / 6.0f, 1.0f);
            } else if (phase == 1) {
                g_ship.activity = ShipActivity::WARPING;
                std::strncpy(g_ship.target, "Vega-7", sizeof(g_ship.target)-1);
                g_ship.progress = std::fmod(t / 6.0f, 1.0f);
            } else {
                g_ship.activity = ShipActivity::IDLE;
                g_ship.target[0] = '\0';
            }
        }

        int W, H;
        glfwGetFramebufferSize(win, &W, &H);
        if (H == 0) H = 1;

        terminal_render(W, H, dt);
        glfwSwapBuffers(win);

        // Frame-rate cap. At 60 FPS we rely on vsync (swap interval 1); at
        // 15/30 we additionally sleep off the surplus so the CPU/GPU idle.
        int fps = g_settings.fps_limit;
        if (fps == 15 || fps == 30) {
            double target = 1.0 / fps;
            double elapsed = glfwGetTime() - now;
            double remain  = target - elapsed;
            if (remain > 0.0)
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(remain));
        }
    }

    g_running = false;
    settings_save("settings.cfg");
    terminal_shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    net_thread.join();
    return 0;
}
