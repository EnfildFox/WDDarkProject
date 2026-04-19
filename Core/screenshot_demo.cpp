#include "screenshot_demo.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string get_executable_directory() {
    char buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable path for screenshot.");
    }

    std::string path(buffer, length);
    const std::string::size_type separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

std::uint32_t calculate_bitmap_stride(int width, int bitsPerPixel) {
    return ((static_cast<std::uint32_t>(width) * bitsPerPixel + 31u) / 32u) * 4u;
}

}  // namespace

void capture_screenshot_demo() {
    const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (screenWidth <= 0 || screenHeight <= 0) {
        throw std::runtime_error("Invalid screen dimensions for screenshot capture.");
    }

    HDC screenDc = GetDC(NULL);
    if (screenDc == NULL) {
        throw std::runtime_error("GetDC failed for screenshot capture.");
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (memoryDc == NULL) {
        ReleaseDC(NULL, screenDc);
        throw std::runtime_error("CreateCompatibleDC failed for screenshot capture.");
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, screenWidth, screenHeight);
    if (bitmap == NULL) {
        DeleteDC(memoryDc);
        ReleaseDC(NULL, screenDc);
        throw std::runtime_error("CreateCompatibleBitmap failed for screenshot capture.");
    }

    HGDIOBJ previousObject = SelectObject(memoryDc, bitmap);
    if (previousObject == NULL || previousObject == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(NULL, screenDc);
        throw std::runtime_error("SelectObject failed for screenshot capture.");
    }

    if (!BitBlt(memoryDc, 0, 0, screenWidth, screenHeight, screenDc, screenX, screenY, SRCCOPY)) {
        SelectObject(memoryDc, previousObject);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(NULL, screenDc);
        throw std::runtime_error("BitBlt failed for screenshot capture.");
    }

    BITMAPINFOHEADER bitmapInfoHeader = {};
    bitmapInfoHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfoHeader.biWidth = screenWidth;
    bitmapInfoHeader.biHeight = -screenHeight;
    bitmapInfoHeader.biPlanes = 1;
    bitmapInfoHeader.biBitCount = 32;
    bitmapInfoHeader.biCompression = BI_RGB;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader = bitmapInfoHeader;

    const std::uint32_t stride = calculate_bitmap_stride(screenWidth, 32);
    const std::uint32_t imageSize = stride * static_cast<std::uint32_t>(screenHeight);
    std::vector<std::uint8_t> pixels(imageSize);

    if (
        GetDIBits(
            memoryDc,
            bitmap,
            0,
            static_cast<UINT>(screenHeight),
            pixels.data(),
            &bitmapInfo,
            DIB_RGB_COLORS
        ) == 0
    ) {
        SelectObject(memoryDc, previousObject);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(NULL, screenDc);
        throw std::runtime_error("GetDIBits failed for screenshot capture.");
    }

    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + imageSize;

    const std::string outputPath = get_executable_directory() + "\\screenshot_demo.bmp";
    std::ofstream output(outputPath.c_str(), std::ios::binary);
    if (!output) {
        SelectObject(memoryDc, previousObject);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(NULL, screenDc);
        throw std::runtime_error("Failed to open screenshot_demo.bmp for writing.");
    }

    output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    output.write(reinterpret_cast<const char*>(&bitmapInfoHeader), sizeof(bitmapInfoHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));

    output.close();

    SelectObject(memoryDc, previousObject);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(NULL, screenDc);

    if (!output) {
        throw std::runtime_error("Failed while writing screenshot_demo.bmp.");
    }
}
