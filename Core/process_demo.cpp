#include "process_demo.h"

#include <windows.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

void log_debug(const std::string& message) {
    std::string line = message + "\r\n";
    OutputDebugStringA(line.c_str());
}

std::string get_executable_directory() {
    char buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable path for process_demo.");
    }

    std::string path(buffer, length);
    const std::string::size_type separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

}  // namespace

void process_demo() {
    const std::string outputPath = get_executable_directory() + "\\process_demo.txt";
    std::string commandLine = "cmd.exe /c dir C:\\ > \"" + outputPath + "\"";
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOWNORMAL;

    PROCESS_INFORMATION processInfo = {};
    if (
        !CreateProcessA(
            NULL,
            mutableCommand.data(),
            NULL,
            NULL,
            FALSE,
            CREATE_NEW_CONSOLE,
            NULL,
            get_executable_directory().c_str(),
            &startupInfo,
            &processInfo
        )
    ) {
        throw std::runtime_error("Failed to start cmd.exe for process_demo.");
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    log_debug("[DEMO] process_demo completed");
}
