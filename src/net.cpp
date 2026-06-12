#include "net.h"
#include "state.h"
#include "sound.h"
#include "stat_overlay.h"
#include "report_overlay.h"
#include "settings.h"

#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

tcp::socket* g_socket = nullptr;

void net_send(const void* data, size_t len) {
    if (!g_socket) return;
    try {
        asio::write(*g_socket, asio::buffer(data, len));
    } catch (...) {}
}

void net_send_login(const std::string& nick, const std::string& pass, bool do_register) {
    MsgType type = do_register ? MsgType::C_REGISTER : MsgType::C_LOGIN;
    uint16_t body_len = NICKNAME_MAX_LEN + PASSWORD_MAX_LEN;
    std::vector<char> pkt(sizeof(PacketHeader) + body_len, 0);
    auto* hdr = reinterpret_cast<PacketHeader*>(pkt.data());
    hdr->type     = type;
    hdr->body_len = htons(body_len);
    std::strncpy(pkt.data() + sizeof(PacketHeader),                        nick.c_str(), NICKNAME_MAX_LEN-1);
    std::strncpy(pkt.data() + sizeof(PacketHeader) + NICKNAME_MAX_LEN, pass.c_str(), PASSWORD_MAX_LEN-1);
    net_send(pkt.data(), pkt.size());
}

void net_send_input(const std::string& text) {
    uint16_t body_len = MESSAGE_MAX_LEN;
    std::vector<char> pkt(sizeof(PacketHeader) + body_len, 0);
    auto* hdr = reinterpret_cast<PacketHeader*>(pkt.data());
    hdr->type     = MsgType::C_INPUT;
    hdr->body_len = htons(body_len);
    std::strncpy(pkt.data() + sizeof(PacketHeader), text.c_str(), MESSAGE_MAX_LEN-1);
    net_send(pkt.data(), pkt.size());
}

// ─── Receive loop ─────────────────────────────────────────────────────────────
static void push_line(const char* text, uint8_t r, uint8_t g, uint8_t b, uint8_t flags=0) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (flags & TERM_FLAG_OVERWRITE) {
        // Replace last overwrite line if exists, otherwise append
        for (int i = (int)g_lines.size() - 1; i >= 0; --i) {
            if (g_lines[i].flags & TERM_FLAG_OVERWRITE) {
                g_lines[i] = {std::string(text), r, g, b, flags};
                return;
            }
        }
    }
    g_lines.push_back({std::string(text), r, g, b, flags});
    if ((int)g_lines.size() > g_term_buf_size)
        g_lines.erase(g_lines.begin());
}

static double net_uptime() {
    static auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
}

static void push_chat(const char* text, uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_chat_lines.push_back({std::string(text), r, g, b, net_uptime()});
    if ((int)g_chat_lines.size() > MAX_CHAT_LINES)
        g_chat_lines.erase(g_chat_lines.begin());
}

void network_thread_func(const std::string& host, const std::string& port) {
    // Debug log to file
    static FILE* dbg = fopen("net_debug.log", "w");
    if (dbg) { fprintf(dbg, "network_thread started\n"); fflush(dbg); }
  while (g_running) {
    g_logout_requested = false;
    g_conn_status = ConnStatus::CONNECTING;
    try {
        asio::io_context io;
        tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, port);
        tcp::socket socket(io);
        asio::connect(socket, endpoints);
        g_socket      = &socket;
        g_connected   = true;
        g_conn_status = ConnStatus::ONLINE;

        auto read_exact = [&](void* dst, size_t n) {
            asio::read(socket, asio::buffer(dst, n));
        };

        while (g_running && !g_logout_requested) {
            PacketHeader hdr{};
            read_exact(&hdr, sizeof(hdr));
            uint16_t body_len = ntohs(hdr.body_len);
            if (dbg) { fprintf(dbg, "[net] pkt type=0x%02x body_len=%u\n", (int)hdr.type, body_len); fflush(dbg); }
            std::vector<char> body(body_len, 0);
            if (body_len > 0) read_exact(body.data(), body_len);

            switch (hdr.type) {

            case MsgType::S_AUTH_OK: {
                if (dbg) { fprintf(dbg, "[net] S_AUTH_OK body=%zu need=%zu\n", body.size(), sizeof(PktAuthOk)-sizeof(PacketHeader)); fflush(dbg); }
                // body: player_id(4)+sx(4)+sy(4)+sz(4)+access(4)+nickname(32) = 52
                if (body.size() < 20) {
                    if (dbg) { fprintf(dbg, "[net] S_AUTH_OK TOO SMALL\n"); fflush(dbg); }
                    break;
                }
                // body layout: player_id(4), sector_x(4), sector_y(4), sector_z(4), access(4), nickname(32)
                uint32_t pid; std::memcpy(&pid, body.data() +  0, 4); pid = ntohl(pid);
                int32_t  sx;  std::memcpy(&sx,  body.data() +  4, 4); sx  = (int32_t)ntohl((uint32_t)sx);
                int32_t  sy;  std::memcpy(&sy,  body.data() +  8, 4); sy  = (int32_t)ntohl((uint32_t)sy);
                int32_t  sz;  std::memcpy(&sz,  body.data() + 12, 4); sz  = (int32_t)ntohl((uint32_t)sz);
                uint32_t acc_raw; std::memcpy(&acc_raw, body.data() + 16, 4); int acc = (int)ntohl(acc_raw);
                char nick[NICKNAME_MAX_LEN+1] = {};
                std::memcpy(nick, body.data() + 20, std::min((size_t)NICKNAME_MAX_LEN, body.size() - 20));
                {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_player_id     = pid;
                    g_player_access = acc;
                    g_player_nick   = nick;
                    g_authed        = true;
                    g_lines.clear();  // clear terminal on (re)login
                }
                if (dbg) { fprintf(dbg, "[net] -> TERMINAL acc=%d nick=%s\n", acc, nick); fflush(dbg); }
                g_screen = Screen::TERMINAL;
                sound_play(SoundEvent::AUTH_OK);
                push_line("", 40, 40, 40);
                push_line("  HORIZON PRIME  //  Terminal v1.0", 80, 160, 255);
                push_line("", 40, 40, 40);
                char welcome[128];
                std::snprintf(welcome, sizeof(welcome), "  Welcome, %s.", nick);
                push_line(welcome, 150, 220, 150);
                push_line("  Type 'help' to see available commands.", 100, 150, 100);
                push_line("", 40, 40, 40);
                // Optionally request the daily logo. Language is chosen on the
                // server (bilingual prompt for new players) or via Settings.
                if (g_settings.welcome_logo)
                    net_send_input("logo");
                break;
            }

            case MsgType::S_AUTH_FAIL: {
                char reason[129] = {};
                std::memcpy(reason, body.data(), std::min((size_t)128, body.size()));
                {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_auth_error = reason;
                }
                sound_play(SoundEvent::AUTH_FAIL);
                break;
            }

            case MsgType::S_CHAT: {
                // Legacy chat — show as terminal line
                char sender[NICKNAME_MAX_LEN+1] = {};
                char text[MESSAGE_MAX_LEN+1]    = {};
                std::memcpy(sender, body.data(), std::min(body.size(), (size_t)NICKNAME_MAX_LEN));
                if (body.size() > NICKNAME_MAX_LEN)
                    std::memcpy(text, body.data()+NICKNAME_MAX_LEN,
                                std::min(body.size()-NICKNAME_MAX_LEN, (size_t)MESSAGE_MAX_LEN));
                char line[NICKNAME_MAX_LEN + MESSAGE_MAX_LEN + 8];
                std::snprintf(line, sizeof(line), "  [%s] %s", sender, text);
                push_line(line, 120, 180, 255);
                break;
            }

            case MsgType::S_PLAYER_JOIN: {
                char nick[NICKNAME_MAX_LEN+1] = {};
                std::memcpy(nick, body.data()+sizeof(uint32_t), NICKNAME_MAX_LEN);
                char line[64]; std::snprintf(line, sizeof(line), "  * %s connected", nick);
                push_line(line, 80, 200, 80);
                sound_play(SoundEvent::PLAYER_JOIN);
                break;
            }

            case MsgType::S_PLAYER_LEAVE: {
                char nick[NICKNAME_MAX_LEN+1] = {};
                if (body.size() > sizeof(uint32_t))
                    std::memcpy(nick, body.data()+sizeof(uint32_t), NICKNAME_MAX_LEN);
                char line[64]; std::snprintf(line, sizeof(line), "  * %s disconnected", nick);
                push_line(line, 150, 100, 80);
                sound_play(SoundEvent::PLAYER_LEAVE);
                break;
            }

            // ── Terminal render packets ──────────────────────────────────────
            case MsgType::S_TERM_CLEAR: {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_lines.clear();
                break;
            }

            case MsgType::S_TERM_TEXT: {
                if (body.size() < 4) break;
                uint8_t r = (uint8_t)body[0];
                uint8_t g = (uint8_t)body[1];
                uint8_t b2= (uint8_t)body[2];
                uint8_t fl= (uint8_t)body[3];
                char text[481] = {};
                std::memcpy(text, body.data()+4, std::min(body.size()-4, (size_t)480));
                push_line(text, r, g, b2, fl);
                // OVERWRITE flag = warp in progress; plain text = warp ended
                if (fl & TERM_FLAG_OVERWRITE) {
                    g_warping = true;
                } else if (g_warping) {
                    g_warping = false;
                    // Seal off the finished progress-bar line(s): drop the OVERWRITE
                    // flag so the next warp starts a fresh bar at the bottom instead
                    // of re-using this stale anchor higher up in the buffer.
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    for (auto& ln : g_lines)
                        ln.flags &= ~TERM_FLAG_OVERWRITE;
                }
                break;
            }

            case MsgType::S_TERM_PROMPT: {
                char text[65] = {};
                std::memcpy(text, body.data(), std::min(body.size(), (size_t)64));
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_prompt = text;
                break;
            }

            case MsgType::S_TERM_SCENE: {
                if (body.empty()) break;
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_scene.scene_id    = (uint8_t)body[0];
                g_scene.time_left   = (body.size() > 1 && body[1] > 0)
                                        ? (float)(uint8_t)body[1] : 0.0f;
                if (body.size() > 2)
                    std::memcpy(g_scene.params, body.data()+2,
                                std::min(body.size()-2, (size_t)127));
                g_scene.active = (g_scene.scene_id != 255);
                break;
            }

            case MsgType::S_TERM_ANIM: {
                if (body.size() < 4) break;
                uint8_t line_count = (uint8_t)body[0];
                uint8_t r  = (uint8_t)body[1];
                uint8_t g  = (uint8_t)body[2];
                uint8_t b2 = (uint8_t)body[3];
                char text[481] = {};
                std::memcpy(text, body.data()+4, std::min(body.size()-4, (size_t)480));
                {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    // Replace last line_count lines
                    int to_remove = std::min((int)line_count, (int)g_lines.size());
                    g_lines.resize(g_lines.size() - to_remove);
                    g_lines.push_back({std::string(text), r, g, b2});
                }
                break;
            }

            case MsgType::S_LOGOUT: {
                bool is_exit = (!body.empty() && body[0] == 1);
                if (is_exit) {
                    g_running = false;
                } else {
                    g_logout_requested = true;
                    {
                        std::lock_guard<std::mutex> lock(g_state_mutex);
                        g_lines.clear();
                        g_chat_lines.clear();
                        g_input_buf.clear();
                        g_prompt    = "> ";
                        g_authed    = false;
                        g_player_id = 0;
                        g_player_nick.clear();
                        g_auth_error.clear();
                        g_field_nick.clear();
                        g_field_pass.clear();
                        g_focused_login = 0;
                        g_scene.active  = false;
                    }
                    g_screen = Screen::LOGIN;
                }
                break;
            }

            case MsgType::S_TERM_CHAT: {
                if (body.size() < 3) break;
                uint8_t r = (uint8_t)body[0];
                uint8_t g = (uint8_t)body[1];
                uint8_t b = (uint8_t)body[2];
                char text[481] = {};
                std::memcpy(text, body.data()+3, std::min(body.size()-3, (size_t)480));
                push_chat(text, r, g, b);
                break;
            }

            case MsgType::S_STATS: {
                if (body.size() >= 24) {
                    uint32_t* stats_ptr = reinterpret_cast<uint32_t*>(body.data());
                    int played_min = ntohl(stats_ptr[0]);
                    int ships = ntohl(stats_ptr[1]);
                    int npcs = ntohl(stats_ptr[2]);
                    int quests = ntohl(stats_ptr[3]);
                    int jumps = ntohl(stats_ptr[4]);
                    int pms = ntohl(stats_ptr[5]);

                    // Parse dates (null-terminated strings after stats)
                    const char* created_ptr = body.data() + 24;
                    const char* online_ptr = created_ptr + std::strlen(created_ptr) + 1;

                    std::string created(created_ptr);
                    std::string online(online_ptr);

                    stat_set_data(played_min, ships, npcs, quests, jumps, pms, created, online);
                    stat_open();
                }
                break;
            }

            case MsgType::S_REPORT_LIST: {
                if (!body.empty()) {
                    uint8_t mode = (uint8_t)body[0];
                    std::string blob(body.data() + 1, body.size() - 1);
                    report_set_data(mode, blob);
                }
                break;
            }

            case MsgType::S_WORLD_STATE: {
                if (body.size() >= 2) {
                    uint16_t cnt = ntohs(*reinterpret_cast<uint16_t*>(body.data()));
                    g_online_count = (int)cnt;
                }
                break;
            }

            default: break;
            }
        }
    } catch (std::exception& e) {
        if (dbg) { fprintf(dbg, "[net] EXCEPTION: %s\n", e.what()); fflush(dbg); }
        if (!g_logout_requested) {
            if (g_screen == Screen::TERMINAL) {
                push_line((std::string("  [!] Connection lost: ") + e.what()).c_str(),
                          220, 80, 80);
                // Return to login on connection loss
                {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_lines.clear();
                    g_chat_lines.clear();
                    g_input_buf.clear();
                    g_prompt    = "> ";
                    g_authed    = false;
                    g_player_id = 0;
                    g_player_nick.clear();
                    g_auth_error.clear();
                    g_field_nick.clear();
                    g_field_pass.clear();
                    g_focused_login = 0;
                    g_scene.active  = false;
                }
                g_screen = Screen::LOGIN;
            } else {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_auth_error = "Server offline. Retrying...";
            }
        }
    }
    g_connected   = false;
    g_socket      = nullptr;
    g_conn_status = ConnStatus::OFFLINE;
    g_online_count = 0;

    if (g_logout_requested) {
        // Reconnect immediately for next login session
        continue;
    }
    if (!g_running) break;

    // Auto-reconnect after 3 seconds
    for (int i = 0; i < 30 && g_running && !g_logout_requested; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } // while g_running
}
