#include "config.h"
#include "network.h"
#include "persistence.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void log_debug(const std::string& message) {
    std::string line = message + "\r\n";
    OutputDebugStringA(line.c_str());
}

std::string get_executable_path() {
    char buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);

    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable path.");
    }

    return std::string(buffer, length);
}

std::string get_directory_from_path(const std::string& path) {
    const std::string::size_type separator = path.find_last_of("\\/");

    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

std::string read_text_file(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::string extract_json_string(const std::string& text, const std::string& key) {
    const std::string quotedKey = "\"" + key + "\"";
    const std::string::size_type keyPos = text.find(quotedKey);
    if (keyPos == std::string::npos) {
        throw std::runtime_error("Missing string key: " + key);
    }

    const std::string::size_type colonPos = text.find(':', keyPos + quotedKey.size());
    if (colonPos == std::string::npos) {
        throw std::runtime_error("Missing separator for key: " + key);
    }

    const std::string::size_type openQuotePos = text.find('"', colonPos + 1);
    if (openQuotePos == std::string::npos) {
        throw std::runtime_error("Missing opening quote for key: " + key);
    }

    const std::string::size_type closeQuotePos = text.find('"', openQuotePos + 1);
    if (closeQuotePos == std::string::npos) {
        throw std::runtime_error("Missing closing quote for key: " + key);
    }

    return text.substr(openQuotePos + 1, closeQuotePos - openQuotePos - 1);
}

unsigned short extract_json_port(const std::string& text, const std::string& key) {
    const std::string quotedKey = "\"" + key + "\"";
    const std::string::size_type keyPos = text.find(quotedKey);
    if (keyPos == std::string::npos) {
        throw std::runtime_error("Missing numeric key: " + key);
    }

    const std::string::size_type colonPos = text.find(':', keyPos + quotedKey.size());
    if (colonPos == std::string::npos) {
        throw std::runtime_error("Missing separator for key: " + key);
    }

    std::string::size_type valuePos = colonPos + 1;
    while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos])) != 0) {
        ++valuePos;
    }

    const std::string::size_type valueEnd = text.find_first_not_of("0123456789", valuePos);
    const std::string value = text.substr(valuePos, valueEnd - valuePos);
    if (value.empty()) {
        throw std::runtime_error("Missing port value.");
    }

    const unsigned long parsed = std::stoul(value);
    if (parsed > 65535UL) {
        throw std::runtime_error("Port value is out of range.");
    }

    return static_cast<unsigned short>(parsed);
}

Config load_config(const std::string& configPath) {
    const std::string content = read_text_file(configPath);

    Config config;
    config.ip = extract_json_string(content, "ip");
    config.port = extract_json_port(content, "port");
    return config;
}

std::string to_lower_copy(const std::string& value) {
    std::string result = value;
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );
    return result;
}

std::string get_computer_name_normalized() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;

    if (!GetComputerNameA(buffer, &size)) {
        throw std::runtime_error("Failed to read computer name.");
    }

    return to_lower_copy(std::string(buffer, size));
}

unsigned long long fnv1a_hash64(const std::string& value) {
    const unsigned long long offsetBasis = 14695981039346656037ull;
    const unsigned long long prime = 1099511628211ull;

    unsigned long long hash = offsetBasis;
    for (unsigned char ch : value) {
        hash ^= static_cast<unsigned long long>(ch);
        hash *= prime;
    }

    return hash;
}

std::string build_mutex_name() {
    const std::string computerName = get_computer_name_normalized();
    const unsigned long long hash = fnv1a_hash64(computerName);

    std::ostringstream stream;
    stream << "Local\\TitanLabDemoMutex_"
           << std::hex
           << std::setw(16)
           << std::setfill('0')
           << hash;
    return stream.str();
}

HANDLE create_single_instance_mutex() {
    const std::string mutexName = build_mutex_name();
    HANDLE mutexHandle = CreateMutexA(NULL, FALSE, mutexName.c_str());

    if (mutexHandle == NULL) {
        throw std::runtime_error("Failed to create instance mutex.");
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(
            NULL,
            "TitanLab agent is already running (demo mode).",
            "Debug",
            MB_OK | MB_ICONINFORMATION
        );
        CloseHandle(mutexHandle);
        return NULL;
    }

    return mutexHandle;
}

std::vector<std::wstring> get_command_line_arguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        throw std::runtime_error("Failed to parse command line.");
    }

    std::vector<std::wstring> arguments(argv, argv + argc);
    LocalFree(argv);
    return arguments;
}

bool has_argument(const std::vector<std::wstring>& arguments, const wchar_t* value) {
    const std::wstring expected(value);
    return std::find(arguments.begin(), arguments.end(), expected) != arguments.end();
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HANDLE mutexHandle = NULL;
    bool winsockInitialized = false;

    try {
        const std::vector<std::wstring> arguments = get_command_line_arguments();
        const std::string exePath = get_executable_path();
        const std::string exeDirectory = get_directory_from_path(exePath);
        const std::string configPath = exeDirectory + "\\config.json";

        const Config config = load_config(configPath);
        log_debug("Loaded config from: " + configPath);
        log_debug("Config.ip=" + config.ip);
        log_debug("Config.port=" + std::to_string(config.port));

        mutexHandle = create_single_instance_mutex();
        if (mutexHandle == NULL) {
            return 1;
        }

        const bool installTask = has_argument(arguments, L"--install");
        const bool installRegistry = has_argument(arguments, L"--install-reg");

        if (installTask || installRegistry) {
            if (installTask) {
                install_demo_task_mock(exePath);
            }

            if (installRegistry) {
                install_demo_registry_mock(exePath);
            }

            CloseHandle(mutexHandle);
            return 0;
        }

        if (!initialize_winsock()) {
            throw std::runtime_error("Failed to initialize WinSock.");
        }
        winsockInitialized = true;

        SOCKET serverSocket = connect_to_server(config.ip.c_str(), static_cast<int>(config.port));
        const std::string hostname = get_hostname();
        const std::string osVersion = get_os_version();

        if (!send_register(serverSocket, hostname.c_str(), osVersion.c_str())) {
            closesocket(serverSocket);
            throw std::runtime_error("Failed to send registration payload.");
        }

        std::thread heartbeatThread(
            heartbeat_loop,
            serverSocket,
            config.ip.c_str(),
            static_cast<int>(config.port)
        );

        MessageBoxA(NULL, "TitanLab agent started (demo mode)", "Debug", MB_OK);

        heartbeatThread.join();

        CloseHandle(mutexHandle);
        cleanup_winsock();
        return 0;
    } catch (const std::exception& exception) {
        log_debug(std::string("TitanLab startup error: ") + exception.what());
        MessageBoxA(NULL, exception.what(), "TitanLab Demo Error", MB_OK | MB_ICONERROR);

        if (mutexHandle != NULL) {
            CloseHandle(mutexHandle);
        }

        if (winsockInitialized) {
            cleanup_winsock();
        }

        return 1;
    }
}
