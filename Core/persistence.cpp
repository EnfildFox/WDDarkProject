#include "persistence.h"

#include <windows.h>

#include <string>

namespace {

void log_debug(const std::string& message) {
    std::string line = message + "\r\n";
    OutputDebugStringA(line.c_str());
}

bool confirm_action(const char* title, const char* prompt) {
    int result = MessageBoxA(
        NULL,
        prompt,
        title,
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
    );

    return result == IDYES;
}

}  // namespace

bool install_demo_task_mock(const std::string& exePath) {
    const char* title = "TitanLab Demo Task";
    const char* prompt =
        "This demo will only log a simulated scheduled task installation.\n"
        "No system changes will be made.\n\n"
        "Do you want to continue?";

    if (!confirm_action(title, prompt)) {
        log_debug("Scheduled task demo installation was cancelled by the user.");
        MessageBoxA(NULL, "Demo action cancelled by user.", title, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    log_debug("Demo only: would create a scheduled task for executable path: " + exePath);
    MessageBoxA(
        NULL,
        "Demo only: no system changes were made.",
        title,
        MB_OK | MB_ICONINFORMATION
    );

    return true;
}

bool install_demo_registry_mock(const std::string& exePath) {
    const char* title = "TitanLab Demo Registry";
    const char* prompt =
        "This demo will only log a simulated Run-key registration.\n"
        "No registry changes will be made.\n\n"
        "Do you want to continue?";

    if (!confirm_action(title, prompt)) {
        log_debug("Registry demo installation was cancelled by the user.");
        MessageBoxA(NULL, "Demo action cancelled by user.", title, MB_OK | MB_ICONINFORMATION);
        return false;
    }

    log_debug("Demo only: would register a Run entry for executable path: " + exePath);
    MessageBoxA(
        NULL,
        "Demo only: no system changes were made.",
        title,
        MB_OK | MB_ICONINFORMATION
    );

    return true;
}
