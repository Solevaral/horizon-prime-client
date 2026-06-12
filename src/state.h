#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

#include "protocol/packets.h"
using namespace hp;

// ─── Terminal line ────────────────────────────────────────────────────────────
struct TermLine {
    std::string text;
    uint8_t     r = 180, g = 220, b = 180;  // default: pale green
    uint8_t     flags = 0;
};

// ─── Chat line (separate overlay panel) ──────────────────────────────────────
struct ChatLine {
    std::string text;
    uint8_t     r = 160, g = 200, b = 255;
    double      timestamp = 0.0;  // glfwGetTime() when received
};

// ─── Scene state ──────────────────────────────────────────────────────────────
struct SceneState {
    bool    active       = false;
    uint8_t scene_id     = 0;
    float   time_left    = 0.0f;  // 0 = indefinite
    char    params[128]  = {};
};

// ─── Globals ──────────────────────────────────────────────────────────────────
constexpr int MAX_TERM_LINES = 512;

extern std::atomic<bool>       g_running;
extern std::atomic<bool>       g_connected;
extern std::mutex              g_state_mutex;

// Player info (set on auth)
extern uint32_t                g_player_id;
extern std::string             g_player_nick;
extern bool                    g_authed;

// Terminal buffer
extern std::vector<TermLine>   g_lines;       // scrollback buffer
extern std::string             g_prompt;      // current prompt string
extern SceneState              g_scene;

// Chat overlay buffer (separate from terminal output)
constexpr int MAX_CHAT_LINES = 64;
extern std::vector<ChatLine>   g_chat_lines;

// Input
extern std::string             g_input_buf;   // current input line
extern bool                    g_input_active;

// Auth error (shown on login screen)
extern std::string             g_auth_error;

// Login screen fields
extern std::string             g_field_nick;
extern std::string             g_field_pass;
extern int                     g_focused_login; // 0=nick, 1=pass

enum class Screen { LOGIN, TERMINAL };
extern std::atomic<Screen>     g_screen;
extern std::atomic<bool>       g_logout_requested;

// Server connection status shown on login screen
enum class ConnStatus { CONNECTING, ONLINE, OFFLINE };
extern std::atomic<ConnStatus> g_conn_status;
extern std::atomic<int>        g_online_count;  // players online (from S_WORLD_STATE)

// Forward declare for stat_overlay functions
void stat_open();
