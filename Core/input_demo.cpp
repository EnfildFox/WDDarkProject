#include "input_demo.h"

#include <windows.h>

#include <fstream>
#include <stdexcept>
#include <string>

namespace {

constexpr UINT kSaveMessage = WM_APP + 1;
constexpr int kEditControlId = 1001;
const char kWindowClassName[] = "TitanLabInputDemoWindow";

WNDPROC g_originalEditProc = nullptr;

std::string get_executable_directory() {
    char buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable path for input_demo.");
    }

    std::string path(buffer, length);
    const std::string::size_type separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

void append_input_line(const std::string& text) {
    const std::string outputPath = get_executable_directory() + "\\input_demo.txt";
    std::ofstream output(outputPath.c_str(), std::ios::out | std::ios::app);
    if (!output) {
        throw std::runtime_error("Failed to open input_demo.txt for writing.");
    }

    SYSTEMTIME localTime = {};
    GetLocalTime(&localTime);

    char timestamp[64] = {};
    wsprintfA(
        timestamp,
        "%04u-%02u-%02u %02u:%02u:%02u",
        localTime.wYear,
        localTime.wMonth,
        localTime.wDay,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond
    );

    output << "[" << timestamp << "] " << text << "\n";
    if (!output) {
        throw std::runtime_error("Failed while writing input_demo.txt.");
    }
}

LRESULT CALLBACK InputEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        PostMessageA(GetParent(hwnd), kSaveMessage, 0, 0);
        return 0;
    }

    return CallWindowProcA(g_originalEditProc, hwnd, message, wParam, lParam);
}

LRESULT CALLBACK InputDemoWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            HFONT guiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            HWND label = CreateWindowExA(
                0,
                "STATIC",
                "Enter text and press Enter to save it to input_demo.txt:",
                WS_CHILD | WS_VISIBLE,
                16,
                16,
                360,
                20,
                hwnd,
                NULL,
                NULL,
                NULL
            );

            HWND edit = CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "EDIT",
                "",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16,
                44,
                360,
                24,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditControlId)),
                NULL,
                NULL
            );

            SendMessageA(label, WM_SETFONT, reinterpret_cast<WPARAM>(guiFont), TRUE);
            SendMessageA(edit, WM_SETFONT, reinterpret_cast<WPARAM>(guiFont), TRUE);

            g_originalEditProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InputEditProc))
            );

            SetFocus(edit);
            return 0;
        }

        case kSaveMessage: {
            HWND edit = GetDlgItem(hwnd, kEditControlId);
            const int length = GetWindowTextLengthA(edit);
            if (length <= 0) {
                MessageBoxA(
                    hwnd,
                    "Please enter some text before pressing Enter.",
                    "TitanLab Input Demo",
                    MB_OK | MB_ICONINFORMATION
                );
                return 0;
            }

            std::string text(static_cast<std::size_t>(length + 1), '\0');
            GetWindowTextA(edit, &text[0], length + 1);
            text.resize(static_cast<std::size_t>(length));

            try {
                append_input_line(text);
                DestroyWindow(hwnd);
            } catch (const std::exception& exception) {
                MessageBoxA(
                    hwnd,
                    exception.what(),
                    "TitanLab Input Demo Error",
                    MB_OK | MB_ICONERROR
                );
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

}  // namespace

void input_demo() {
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = InputDemoWindowProc;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursor(NULL, IDC_IBEAM);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassA(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Failed to register input_demo window class.");
    }

    HWND window = CreateWindowExA(
        0,
        kWindowClassName,
        "TitanLab Input Demo",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        400,
        130,
        NULL,
        NULL,
        windowClass.hInstance,
        NULL
    );

    if (window == NULL) {
        throw std::runtime_error("Failed to create input_demo window.");
    }

    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);

    MSG message = {};
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}
