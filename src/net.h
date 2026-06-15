#pragma once
#include <string>
#include <asio.hpp>

using asio::ip::tcp;
extern tcp::socket* g_socket;

void net_send(const void* data, size_t len);
void net_send_login(const std::string& nick, const std::string& pass, bool do_register);
void net_send_move(int tile_x, int tile_y);
void net_send_interact(uint8_t object_id, uint8_t action = 0);
void net_send_menu_pick(uint8_t menu_id, uint8_t action);
void net_send_chat(const std::string& text);
void net_send_command(const std::string& text);
void net_shutdown();
void net_logout();
void network_thread_func(const std::string& host, const std::string& port);
