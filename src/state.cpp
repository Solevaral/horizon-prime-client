#include "state.h"

std::atomic<bool>       g_running   {true};
std::atomic<bool>       g_connected {false};
std::mutex              g_state_mutex;

uint32_t                g_player_id     = 0;
std::string             g_player_nick;
int                     g_player_access = 3;  // default: user
bool                    g_authed        = false;

int32_t                 g_self_sx = 0, g_self_sy = 0, g_self_sz = 0;
std::vector<MapPlayer>  g_map_players;

std::vector<TermLine>   g_lines;
std::vector<ChatLine>   g_chat_lines;
std::string             g_prompt      = "> ";
SceneState              g_scene;
int                     g_term_buf_size = DEFAULT_TERM_LINES;
bool                    g_scr_open      = false;

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
std::atomic<bool>       g_warping      {false};
