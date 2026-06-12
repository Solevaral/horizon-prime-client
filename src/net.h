#pragma once
#include <string>
#include <asio.hpp>

using asio::ip::tcp;
extern tcp::socket* g_socket;

void net_send(const void* data, size_t len);
void net_send_login(const std::string& nick, const std::string& pass, bool do_register);
void net_send_input(const std::string& text);
void network_thread_func(const std::string& host, const std::string& port);
