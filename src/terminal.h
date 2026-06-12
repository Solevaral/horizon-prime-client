#pragma once

bool terminal_init(const char* font_path);
void terminal_shutdown();

// Call each frame — renders login screen or terminal depending on g_screen
void terminal_render(int W, int H, float dt);

// Input callbacks for GLFW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
void terminal_cb_char(GLFWwindow*, unsigned int codepoint);
void terminal_cb_key(GLFWwindow*, int key, int scancode, int action, int mods);
void terminal_cb_mouse_button(GLFWwindow*, int button, int action, int mods);
void terminal_cb_cursor_pos(GLFWwindow*, double mx, double my);

// Public font helpers — used by settings overlay and other modules
float term_draw_string(float x, float y, const char* text,
                       float r, float g, float b, float a);
float term_string_width(const char* text);
float term_cell_h();
