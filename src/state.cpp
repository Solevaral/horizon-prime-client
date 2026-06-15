#include "state.h"

std::atomic<bool>       g_running   {true};
std::atomic<bool>       g_connected {false};
std::mutex              g_state_mutex;

uint32_t                g_player_id     = 0;
std::string             g_player_nick;
int                     g_player_access = 3;
bool                    g_authed        = false;

SectorInfo              g_sector;
std::unordered_map<uint32_t, Entity> g_entities;
std::atomic<bool>       g_riding {false};

std::vector<TermLine>   g_term;
std::vector<ChatLine>   g_chat_lines;

std::string             g_input_buf;
bool                    g_term_open = false;
bool                    g_pause_menu = false;
int                     g_term_buf_size = 128;

std::string             g_field_nick;
std::string             g_field_pass;
int                     g_focused_login = 0;
std::string             g_auth_error;

std::atomic<Screen>     g_screen {Screen::LOGIN};
std::atomic<bool>       g_logout_requested {false};
std::atomic<ConnStatus> g_conn_status {ConnStatus::CONNECTING};
std::atomic<int>        g_online_count {0};
