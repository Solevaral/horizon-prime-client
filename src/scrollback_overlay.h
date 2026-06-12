#pragma once

extern bool g_scr_open;
extern int  g_scr_scroll_offset;  // 0 = newest, positive = older

void scr_render(int W, int H);
bool scr_on_key(int key, int action);
void scr_on_scroll(double yoffset);
