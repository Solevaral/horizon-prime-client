#pragma once
#include <string>

extern bool g_stat_open;

void stat_render(int W, int H);
bool stat_on_key(int key, int action);
void stat_set_data(int played_min, int ships, int npcs, int quests, int jumps, int pms,
                    const std::string& created, const std::string& online);
