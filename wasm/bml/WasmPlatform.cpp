#include <cstddef>

#include "HookUtils.h"

#include "Diagnostics/SystemReport.h"
#include "Loader/ModContext.h"
#include "StringUtils.h"
#include "UI/Overlay.h"

#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace utils {
void OutputDebugA(const char *format, ...) {
    if (!format)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

void OutputDebugW(const wchar_t *format, ...) {
    if (!format)
        return;
    va_list args;
    va_start(args, format);
    std::vfwprintf(stderr, format, args);
    va_end(args);
}
}

namespace BML::Diagnostics {
std::vector<std::string> BuildSystemReport(ModContext &context) {
    std::vector<std::string> lines;
    lines.emplace_back("BML+ " BML_VERSION " (WebAssembly, Release)");
    CKRenderContext *render = context.GetRenderContext();
    if (render) {
        std::ostringstream display;
        display << "Display: " << render->GetWidth() << 'x' << render->GetHeight()
                << ", WebGL2 through bgfx";
        lines.push_back(display.str());
    } else {
        lines.emplace_back("Display: headless");
    }
    std::ostringstream mods;
    mods << "Mods: count=" << context.GetModCount()
         << ", active=" << (context.IsInited() ? "yes" : "no");
    lines.push_back(mods.str());
    lines.emplace_back(Overlay::IsImGuiReady()
        ? "ImGui renderer: CK2 backend ready"
        : "ImGui renderer: not initialized");
    return lines;
}
}

namespace {
std::wstring NativePath(const wchar_t *path) {
    std::wstring native = path ? path : L"";
    std::replace(native.begin(), native.end(), L'\\', L'/');
    return native;
}
}

DWORD GetFileAttributesW(const wchar_t *path) {
    if (!path) {
        errno = ENOENT;
        return INVALID_FILE_ATTRIBUTES;
    }
    const std::filesystem::path file(utils::Utf16ToUtf8(NativePath(path)));
    std::error_code error;
    const auto status = std::filesystem::status(file, error);
    if (error || !std::filesystem::exists(status)) {
        errno = ENOENT;
        return INVALID_FILE_ATTRIBUTES;
    }
    return std::filesystem::is_directory(status) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

BOOL DeleteFileW(const wchar_t *path) {
    if (!path) {
        errno = ENOENT;
        return 0;
    }
    std::error_code error;
    const bool removed = std::filesystem::remove(
        std::filesystem::path(utils::Utf16ToUtf8(NativePath(path))), error);
    errno = error ? error.value() : 0;
    return removed ? 1 : 0;
}

BOOL RemoveDirectoryW(const wchar_t *path) {
    if (!path) {
        errno = ENOENT;
        return 0;
    }
    std::error_code error;
    const bool removed = std::filesystem::remove(
        std::filesystem::path(utils::Utf16ToUtf8(NativePath(path))), error);
    errno = error ? error.value() : 0;
    return removed ? 1 : 0;
}
