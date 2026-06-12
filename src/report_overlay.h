#pragma once
#include <string>

extern bool g_report_open;

// Populate the overlay from a server S_REPORT_LIST packet and open it.
//   mode 0 = staff inbox (Enter pulls "reply <id> " into the terminal input)
//   mode 1 = player view (read-only: own reports + answers)
// `blob` records are separated by 0x1E, fields by 0x1F (see PacketHelpers.h).
void report_set_data(int mode, const std::string& blob);

void report_render(int W, int H);
bool report_on_key(int key, int action);
