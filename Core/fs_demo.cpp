#include "fs_demo.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void log_debug(const std::string& message) {
    std::string line = message + "\r\n";
    OutputDebugStringA(line.c_str());
}

std::string get_executable_directory() {
    char buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable path for fs_demo.");
    }

    std::string path(buffer, length);
    const std::string::size_type separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

unsigned long long get_file_size_bytes(const WIN32_FIND_DATAA& findData) {
    ULARGE_INTEGER size = {};
    size.LowPart = findData.nFileSizeLow;
    size.HighPart = findData.nFileSizeHigh;
    return size.QuadPart;
}

}  // namespace

void fs_demo() {
    const std::string baseDirectory = get_executable_directory();
    const std::string searchPattern = baseDirectory + "\\*";
    const std::string outputPath = baseDirectory + "\\fs_demo.txt";

    std::ofstream output(outputPath.c_str(), std::ios::out | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open fs_demo.txt for writing.");
    }

    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = FindFirstFileA(searchPattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("FindFirstFileA failed for fs_demo.");
    }

    try {
        do {
            const std::string name = findData.cFileName;
            if (name == "." || name == "..") {
                continue;
            }

            const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::ostringstream line;
            line << (isDirectory ? "[DIR] " : "[FILE] ")
                 << name
                 << " | "
                 << get_file_size_bytes(findData)
                 << " bytes";

            output << line.str() << "\n";
            log_debug("[FS DEMO] " + line.str());
        } while (FindNextFileA(findHandle, &findData) != 0);
    } catch (...) {
        FindClose(findHandle);
        throw;
    }

    FindClose(findHandle);

    if (!output) {
        throw std::runtime_error("Failed while writing fs_demo.txt.");
    }
}
