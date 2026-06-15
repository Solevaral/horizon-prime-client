#include "state.h"
#include "net.h"
#include "terminal.h"
#include "settings.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <thread>
#include <chrono>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
void setup_console_utf8() { SetConsoleCP(65001); SetConsoleOutputCP(65001); }
#else
void setup_console_utf8() {}
#endif

static const char* FONT_PATHS[] = {
    "assets/consola.ttf",
    "C:/Windows/Fonts/consola.ttf",
    nullptr
};

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string host = "127.0.0.1";
    std::string port_str = std::to_string(SERVER_PORT);
    if (argc > 1) { char b[256]={}; WideCharToMultiByte(CP_UTF8,0,wargv[1],-1,b,sizeof(b),0,0); host=b; }
    if (argc > 2) { char b[32]={};  WideCharToMultiByte(CP_UTF8,0,wargv[2],-1,b,sizeof(b),0,0); port_str=b; }
    LocalFree(wargv);
#else
int main(int argc, char* argv[]) {
    std::string host     = (argc > 1) ? argv[1] : "127.0.0.1";
    std::string port_str = (argc > 2) ? argv[2] : std::to_string(SERVER_PORT);
#endif
    setup_console_utf8();
    settings_load("settings.cfg");
    g_term_buf_size = g_settings.term_buffer_size;

    saved_login_load("login.cfg");
    if (g_settings.remember_login) g_field_nick = g_saved_login.nick;
    if (g_settings.remember_pass)  g_field_pass = g_saved_login.pass;

    std::thread net_thread(network_thread_func, host, port_str);

    if (!glfwInit()) { g_running = false; net_thread.join(); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* win = glfwCreateWindow(1100, 680, "Horizon Prime", nullptr, nullptr);
    if (!win) { glfwTerminate(); g_running = false; net_thread.join(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwTerminate(); g_running = false; net_thread.join(); return 1;
    }

    bool font_ok = false;
    for (int i = 0; FONT_PATHS[i] && !font_ok; ++i)
        font_ok = terminal_init(FONT_PATHS[i]);
    if (!font_ok) { glfwTerminate(); g_running = false; net_thread.join(); return 1; }

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

        int W, H; glfwGetFramebufferSize(win, &W, &H);
        if (H == 0) H = 1;

        terminal_render(W, H, dt);
        glfwSwapBuffers(win);

        int fps = g_settings.fps_limit;
        if (fps == 15 || fps == 30) {
            double target = 1.0 / fps;
            double elapsed = glfwGetTime() - now;
            double remain  = target - elapsed;
            if (remain > 0.0)
                std::this_thread::sleep_for(std::chrono::duration<double>(remain));
        }
    }

    g_running = false;
    // Close the socket so the network thread's blocking read returns and the
    // join below doesn't hang the process. Also notifies the server we left.
    net_shutdown();
    settings_save("settings.cfg");
    terminal_shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    net_thread.join();
    return 0;
}
