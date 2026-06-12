#include "state.h"

std::atomic<bool>       g_running   {true};
std::atomic<bool>       g_connected {false};
std::mutex              g_state_mutex;

uint32_t                g_player_id   = 0;
std::string             g_player_nick;
bool                    g_authed      = false;

std::vector<TermLine>   g_lines;
std::vector<ChatLine>   g_chat_lines;
std::string             g_prompt      = "> ";
SceneState              g_scene;

std::string             g_input_buf;
bool                    g_input_active = true;

std::string             g_auth_error;
std::string             g_field_nick;
std::string             g_field_pass;
int                     g_focused_login = 0;

std::atomic<Screen>     g_screen {Screen::LOGIN};
std::atomic<bool>       g_logout_requested {false};
std::atomic<ConnStatus> g_conn_status {ConnStatus::CONNECTING};
std::atomic<int>        g_online_count {0};
