#include "net.h"
#include "state.h"
#include "sound.h"

#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

tcp::socket* g_socket = nullptr;

void net_send(const void* data, size_t len) {
    if (!g_socket) return;
    try { asio::write(*g_socket, asio::buffer(data, len)); } catch (...) {}
}

// Tell the server we're leaving and tear down the socket. Closing the socket
// unblocks the synchronous asio::read in the network thread so it can exit and
// the main thread's join() returns instead of hanging the process forever.
void net_shutdown() {
    if (!g_socket) return;
    try {
        PacketHeader bye{};
        bye.type     = MsgType::C_DISCONNECT;
        bye.body_len = htons(0);
        asio::write(*g_socket, asio::buffer(&bye, sizeof(bye)));
    } catch (...) {}
    try {
        std::error_code ec;
        g_socket->shutdown(tcp::socket::shutdown_both, ec);
        g_socket->close(ec);
    } catch (...) {}
}

void net_send_login(const std::string& nick, const std::string& pass, bool do_register) {
    MsgType type = do_register ? MsgType::C_REGISTER : MsgType::C_LOGIN;
    uint16_t body_len = NICKNAME_MAX_LEN + PASSWORD_MAX_LEN;
    std::vector<char> pkt(sizeof(PacketHeader) + body_len, 0);
    auto* hdr = reinterpret_cast<PacketHeader*>(pkt.data());
    hdr->type     = type;
    hdr->body_len = htons(body_len);
    std::strncpy(pkt.data() + sizeof(PacketHeader),                    nick.c_str(), NICKNAME_MAX_LEN - 1);
    std::strncpy(pkt.data() + sizeof(PacketHeader) + NICKNAME_MAX_LEN, pass.c_str(), PASSWORD_MAX_LEN - 1);
    net_send(pkt.data(), pkt.size());
}

void net_send_move(int tile_x, int tile_y) {
    PktMoveTo p{};
    p.header.type     = MsgType::C_MOVE_TO;
    p.header.body_len = htons(8);
    p.tile_x = htonl((uint32_t)tile_x);
    p.tile_y = htonl((uint32_t)tile_y);
    net_send(&p, sizeof(PacketHeader) + 8);
}

void net_send_interact(uint8_t object_id, uint8_t action) {
    PktInteract p{};
    p.header.type     = MsgType::C_INTERACT;
    p.header.body_len = htons(2);
    p.object_id = object_id;
    p.action    = action;
    net_send(&p, sizeof(PacketHeader) + 2);
}

void net_send_chat(const std::string& text) {
    uint16_t body = MESSAGE_MAX_LEN;
    std::vector<char> pkt(sizeof(PacketHeader) + body, 0);
    auto* hdr = reinterpret_cast<PacketHeader*>(pkt.data());
    hdr->type     = MsgType::C_CHAT;
    hdr->body_len = htons(body);
    std::strncpy(pkt.data() + sizeof(PacketHeader), text.c_str(), MESSAGE_MAX_LEN - 1);
    net_send(pkt.data(), pkt.size());
}

void net_send_command(const std::string& text) {
    uint16_t body = MESSAGE_MAX_LEN;
    std::vector<char> pkt(sizeof(PacketHeader) + body, 0);
    auto* hdr = reinterpret_cast<PacketHeader*>(pkt.data());
    hdr->type     = MsgType::C_COMMAND;
    hdr->body_len = htons(body);
    std::strncpy(pkt.data() + sizeof(PacketHeader), text.c_str(), MESSAGE_MAX_LEN - 1);
    net_send(pkt.data(), pkt.size());
}

// Return to the login screen without quitting: notify the server, drop the
// socket (so the read loop exits), and let the network thread reconnect for a
// fresh login session.
static void reset_to_login();  // fwd
void net_logout() {
    g_logout_requested = true;
    if (g_socket) {
        try {
            PacketHeader bye{};
            bye.type     = MsgType::C_DISCONNECT;
            bye.body_len = htons(0);
            asio::write(*g_socket, asio::buffer(&bye, sizeof(bye)));
        } catch (...) {}
        try {
            std::error_code ec;
            g_socket->shutdown(tcp::socket::shutdown_both, ec);
            g_socket->close(ec);
        } catch (...) {}
    }
    reset_to_login();
    g_screen = Screen::LOGIN;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
static double net_uptime() {
    static auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
}

static void push_term(const char* text, uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_term.push_back({ std::string(text), r, g, b });
    if ((int)g_term.size() > MAX_TERM_LINES) g_term.erase(g_term.begin());
}

static void push_chat(const char* text, uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_chat_lines.push_back({ std::string(text), r, g, b, net_uptime() });
    if ((int)g_chat_lines.size() > MAX_CHAT_LINES) g_chat_lines.erase(g_chat_lines.begin());
}

static void reset_to_login() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_entities.clear();
    g_term.clear();
    g_chat_lines.clear();
    g_input_buf.clear();
    g_term_open = false;
    g_pause_menu = false;
    g_authed    = false;
    g_player_id = 0;
    g_player_nick.clear();
    g_field_nick.clear();
    g_field_pass.clear();
    g_focused_login = 0;
    g_riding = false;
}

void network_thread_func(const std::string& host, const std::string& port) {
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

        // Hello: announce protocol version.
        {
            std::vector<char> hello(sizeof(PacketHeader) + sizeof(uint32_t), 0);
            auto* h = reinterpret_cast<PacketHeader*>(hello.data());
            h->type     = MsgType::C_HELLO;
            h->body_len = htons(sizeof(uint32_t));
            uint32_t ver = htonl(PROTOCOL_VERSION);
            std::memcpy(hello.data() + sizeof(PacketHeader), &ver, sizeof(uint32_t));
            net_send(hello.data(), hello.size());
        }

        auto read_exact = [&](void* dst, size_t n) {
            asio::read(socket, asio::buffer(dst, n));
        };

        while (g_running && !g_logout_requested) {
            PacketHeader hdr{};
            read_exact(&hdr, sizeof(hdr));
            uint16_t body_len = ntohs(hdr.body_len);
            std::vector<char> body(body_len, 0);
            if (body_len > 0) read_exact(body.data(), body_len);

            switch (hdr.type) {

            case MsgType::S_AUTH_OK: {
                if (body.size() < 20) break;
                uint32_t pid; std::memcpy(&pid, body.data() + 0, 4); pid = ntohl(pid);
                uint32_t acc; std::memcpy(&acc, body.data() + 16, 4);
                char nick[NICKNAME_MAX_LEN + 1] = {};
                std::memcpy(nick, body.data() + 20,
                            std::min((size_t)NICKNAME_MAX_LEN, body.size() - 20));
                {
                    std::lock_guard<std::mutex> lock(g_state_mutex);
                    g_player_id     = pid;
                    g_player_access = (int)ntohl(acc);
                    g_player_nick   = nick;
                    g_authed        = true;
                    g_entities.clear();
                    g_term.clear();
                }
                g_screen = Screen::WORLD;
                sound_play(SoundEvent::AUTH_OK);
                push_term("  HORIZON PRIME — boarded.", 80, 160, 255);
                push_term("  Left-click to walk. Right-click the ship to board.", 140, 170, 200);
                break;
            }

            case MsgType::S_AUTH_FAIL: {
                char reason[129] = {};
                std::memcpy(reason, body.data(), std::min((size_t)128, body.size()));
                { std::lock_guard<std::mutex> lock(g_state_mutex); g_auth_error = reason; }
                sound_play(SoundEvent::AUTH_FAIL);
                break;
            }

            case MsgType::S_SECTOR_LOAD: {
                if (body.size() < (size_t)(sizeof(PktSectorLoad) - sizeof(PacketHeader))) break;
                auto rd = [&](int off) {
                    int32_t v; std::memcpy(&v, body.data() + off, 4);
                    return (int)(int32_t)ntohl((uint32_t)v);
                };
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_sector.sector_x    = rd(0);
                g_sector.sector_y    = rd(4);
                g_sector.sector_z    = rd(8);
                g_sector.tiles_x     = rd(12);
                g_sector.tiles_y     = rd(16);
                g_sector.ship_tile_x = rd(20);
                g_sector.ship_tile_y = rd(24);
                g_sector.star_class  = body[28];
                char nm[49] = {};
                std::memcpy(nm, body.data() + 29, std::min((size_t)48, body.size() - 29));
                g_sector.star_name = nm;
                break;
            }

            case MsgType::S_ENTITY_STATE: {
                if (body.size() < 2) break;
                uint16_t cnt = ntohs(*reinterpret_cast<uint16_t*>(body.data()));
                constexpr size_t STRIDE = 4 + 4 + 4 + 1 + 1 + NICKNAME_MAX_LEN;
                std::lock_guard<std::mutex> lock(g_state_mutex);
                std::unordered_map<uint32_t, Entity> fresh;
                for (uint16_t i = 0; i < cnt; ++i) {
                    size_t off = 2 + (size_t)i * STRIDE;
                    if (off + STRIDE > body.size()) break;
                    uint32_t id;  std::memcpy(&id, body.data() + off, 4); id = ntohl(id);
                    int32_t tx;   std::memcpy(&tx, body.data() + off + 4, 4); tx = (int32_t)ntohl((uint32_t)tx);
                    int32_t ty;   std::memcpy(&ty, body.data() + off + 8, 4); ty = (int32_t)ntohl((uint32_t)ty);
                    uint8_t riding = (uint8_t)body[off + 12];
                    uint8_t acc    = (uint8_t)body[off + 13];
                    char nm[NICKNAME_MAX_LEN + 1] = {};
                    std::memcpy(nm, body.data() + off + 14, NICKNAME_MAX_LEN);
                    Entity e;
                    e.id = id; e.tile_x = tx; e.tile_y = ty;
                    e.riding = riding != 0; e.access = acc; e.nick = nm;
                    // Preserve the interpolated render position across snapshots.
                    auto it = g_entities.find(id);
                    if (it != g_entities.end()) { e.rx = it->second.rx; e.ry = it->second.ry; }
                    else { e.rx = (float)tx; e.ry = (float)ty; }
                    fresh[id] = std::move(e);
                }
                g_entities = std::move(fresh);
                g_online_count = (int)g_entities.size();
                break;
            }

            case MsgType::S_ENTITY_LEAVE: {
                if (body.size() < 4) break;
                uint32_t id; std::memcpy(&id, body.data(), 4); id = ntohl(id);
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_entities.erase(id);
                break;
            }

            case MsgType::S_RIDE_STATE: {
                bool riding = (!body.empty() && body[0] == 1);
                g_riding = riding;
                break;
            }

            case MsgType::S_TERM_TEXT: {
                if (body.size() < 3) break;
                uint8_t r = (uint8_t)body[0], g = (uint8_t)body[1], b = (uint8_t)body[2];
                char text[481] = {};
                std::memcpy(text, body.data() + 3, std::min(body.size() - 3, (size_t)480));
                push_term(text, r, g, b);
                break;
            }

            case MsgType::S_CHAT: {
                char sender[NICKNAME_MAX_LEN + 1] = {};
                char text[MESSAGE_MAX_LEN + 1]    = {};
                std::memcpy(sender, body.data(), std::min(body.size(), (size_t)NICKNAME_MAX_LEN));
                if (body.size() > NICKNAME_MAX_LEN)
                    std::memcpy(text, body.data() + NICKNAME_MAX_LEN,
                                std::min(body.size() - NICKNAME_MAX_LEN, (size_t)MESSAGE_MAX_LEN));
                char line[NICKNAME_MAX_LEN + MESSAGE_MAX_LEN + 8];
                std::snprintf(line, sizeof(line), "%s: %s", sender, text);
                push_chat(line, 160, 200, 255);
                break;
            }

            case MsgType::S_LOGOUT: {
                bool is_exit = (!body.empty() && body[0] == 1);
                if (is_exit) { g_running = false; glfwPostEmptyEvent(); }
                else { g_logout_requested = true; reset_to_login(); g_screen = Screen::LOGIN; }
                break;
            }

            default: break;
            }
        }
    } catch (std::exception&) {
        if (!g_logout_requested) {
            if (g_screen == Screen::WORLD) { reset_to_login(); g_screen = Screen::LOGIN; }
            else { std::lock_guard<std::mutex> lock(g_state_mutex); g_auth_error = "Server offline. Retrying..."; }
        }
    }
    g_connected    = false;
    g_socket       = nullptr;
    g_conn_status  = ConnStatus::OFFLINE;
    g_online_count = 0;

    if (g_logout_requested) continue;
    if (!g_running) break;
    for (int i = 0; i < 30 && g_running && !g_logout_requested; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
