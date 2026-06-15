#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "protocol/packets.h"
using namespace hp;

// ─── Ship terminal line ───────────────────────────────────────────────────────
struct TermLine {
    std::string text;
    uint8_t     r = 180, g = 220, b = 180;
};

// ─── Chat line (overlay panel, fades out) ─────────────────────────────────────
struct ChatLine {
    std::string text;
    uint8_t     r = 160, g = 200, b = 255;
    double      timestamp = 0.0;
};

// ─── An entity in the current sector ──────────────────────────────────────────
// Authoritative tile is (tile_x, tile_y); the client interpolates a render
// position toward it for smooth movement between server ticks.
struct Entity {
    uint32_t id = 0;
    std::string nick;
    int      tile_x = 0, tile_y = 0;     // authoritative target tile
    float    rx = 0.0f, ry = 0.0f;       // interpolated render position (tiles)
    bool     riding = false;
    uint8_t  access = 3;
};

// ─── Sector description (from S_SECTOR_LOAD) ──────────────────────────────────
struct SectorInfo {
    int  sector_x = 0, sector_y = 0, sector_z = 0;
    int  tiles_x = 24, tiles_y = 24;
    int  ship_tile_x = 12, ship_tile_y = 12;
    char star_class = 'G';
    std::string star_name = "Unknown";
};

// ─── Globals ──────────────────────────────────────────────────────────────────
extern std::atomic<bool>       g_running;
extern std::atomic<bool>       g_connected;
extern std::mutex              g_state_mutex;

// Player identity (set on auth)
extern uint32_t                g_player_id;
extern std::string             g_player_nick;
extern int                     g_player_access;
extern bool                    g_authed;

// World state (guarded by g_state_mutex)
extern SectorInfo              g_sector;
extern std::unordered_map<uint32_t, Entity> g_entities;
extern std::atomic<bool>       g_riding;       // this player is in the ship cockpit

// Ship terminal buffer + chat
extern std::vector<TermLine>   g_term;
constexpr int MAX_TERM_LINES = 200;
extern std::vector<ChatLine>   g_chat_lines;
constexpr int MAX_CHAT_LINES = 64;

// Ship-terminal input line (only used while riding)
extern std::string             g_input_buf;
extern bool                    g_term_open;    // ship terminal panel visible
extern bool                    g_pause_menu;   // ESC pause menu (settings/leave/quit) visible

// Login screen fields
extern std::string             g_field_nick;
extern std::string             g_field_pass;
extern int                     g_focused_login; // 0=nick, 1=pass
extern std::string             g_auth_error;

// Kept only to satisfy the settings overlay (term buffer size knob).
extern int                     g_term_buf_size;

enum class Screen { LOGIN, WORLD };
extern std::atomic<Screen>     g_screen;
extern std::atomic<bool>       g_logout_requested;

enum class ConnStatus { CONNECTING, ONLINE, OFFLINE };
extern std::atomic<ConnStatus> g_conn_status;
extern std::atomic<int>        g_online_count;
