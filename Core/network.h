#pragma once

#include <winsock2.h>
#include <string>

bool initialize_winsock();
void cleanup_winsock();

SOCKET connect_to_server(const char* server_ip, int port);
bool send_register(SOCKET sock, const char* hostname, const char* os);
void heartbeat_loop(SOCKET sock, const char* server_ip, int port);
void send_raw_string(SOCKET sock, const std::string& message);

std::string get_hostname();
std::string get_os_version();

