#ifndef BML_WASM_WINDOWS_SHIM_H
#define BML_WASM_WINDOWS_SHIM_H

#include <cstdarg>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <thread>
#include <cwctype>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using WORD = unsigned short;
using UINT = unsigned int;
using DWORD = std::uint32_t;
using BOOL = int;
using HANDLE = void *;
using HWND = void *;
using HMODULE = void *;
using LPVOID = void *;
using LPCSTR = const char *;
using ULONG_PTR = std::uintptr_t;
using INT_PTR = std::intptr_t;

#ifndef MAX_PATH
#define MAX_PATH 260
#endif
constexpr int SM_CXSCREEN = 0;
constexpr int SM_CYSCREEN = 1;

union LARGE_INTEGER {
    std::int64_t QuadPart;
};

inline BOOL QueryPerformanceCounter(LARGE_INTEGER *value) {
    if (!value)
        return 0;
    value->QuadPart = std::chrono::steady_clock::now().time_since_epoch().count();
    return 1;
}

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *value) {
    if (!value)
        return 0;
    value->QuadPart = std::chrono::steady_clock::period::den;
    return 1;
}

inline int GetSystemMetrics(int metric) {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        const canvas = document.getElementById("canvas");
        if (!canvas)
            return $0 == 0 ? 640 : 480;
        return $0 == 0 ? canvas.width : canvas.height;
    }, metric);
#else
    return metric == SM_CXSCREEN ? 640 : 480;
#endif
}

inline DWORD GetCurrentThreadId() {
    return static_cast<DWORD>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

inline DWORD GetCurrentProcessId() {
    return 1;
}

inline void OutputDebugStringA(const char *text) {
    if (text)
        std::fputs(text, stderr);
}

#ifndef _ReturnAddress
#define _ReturnAddress() __builtin_return_address(0)
#endif

struct SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

inline void GetLocalTime(SYSTEMTIME *value) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    value->wYear = static_cast<WORD>(local.tm_year + 1900);
    value->wMonth = static_cast<WORD>(local.tm_mon + 1);
    value->wDayOfWeek = static_cast<WORD>(local.tm_wday);
    value->wDay = static_cast<WORD>(local.tm_mday);
    value->wHour = static_cast<WORD>(local.tm_hour);
    value->wMinute = static_cast<WORD>(local.tm_min);
    value->wSecond = static_cast<WORD>(local.tm_sec);
    value->wMilliseconds = 0;
}

constexpr UINT CP_UTF8 = 65001;
constexpr UINT CP_ACP = 0;
constexpr DWORD MB_ERR_INVALID_CHARS = 0x00000008u;
constexpr DWORD WC_NO_BEST_FIT_CHARS = 0x00000400u;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
constexpr unsigned long ERROR_SUCCESS = 0;
constexpr unsigned long ERROR_FILE_NOT_FOUND = 2;
constexpr unsigned long ERROR_PATH_NOT_FOUND = 3;
constexpr unsigned long ERROR_ACCESS_DENIED = 5;
constexpr unsigned long ERROR_INVALID_DATA = 13;
constexpr unsigned long ERROR_INVALID_NAME = 123;
constexpr unsigned long ERROR_ALREADY_EXISTS = 183;
constexpr DWORD INVALID_FILE_ATTRIBUTES = 0xffffffffu;
constexpr DWORD FILE_ATTRIBUTE_DIRECTORY = 0x10;
constexpr DWORD FILE_ATTRIBUTE_NORMAL = 0x80;

DWORD GetFileAttributesW(const wchar_t *path);
BOOL DeleteFileW(const wchar_t *path);
BOOL RemoveDirectoryW(const wchar_t *path);

inline std::uint64_t GetTickCount64() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline DWORD GetLastError() {
    return errno == ENOTDIR ? ERROR_PATH_NOT_FOUND : static_cast<DWORD>(errno);
}

inline int _snwprintf(wchar_t *buffer, size_t size, const wchar_t *format, ...) {
    if (!buffer || size == 0)
        return -1;
    va_list args;
    va_start(args, format);
    const int result = std::vswprintf(buffer, size, format, args);
    va_end(args);
    buffer[size - 1] = L'\0';
    return result;
}

inline UINT GetACP() {
    return CP_UTF8;
}

#ifndef _TRUNCATE
#define _TRUNCATE (static_cast<size_t>(-1))
#endif

inline int _vscprintf(const char *format, va_list args) {
    return std::vsnprintf(nullptr, 0, format, args);
}

inline int vsnprintf_s(char *buffer, size_t size, size_t, const char *format, va_list args) {
    if (!buffer || size == 0)
        return -1;
    const int result = std::vsnprintf(buffer, size, format, args);
    buffer[size - 1] = '\0';
    return result;
}

inline int sprintf_s(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = vsnprintf_s(buffer, size, _TRUNCATE, format, args);
    va_end(args);
    return result;
}

inline int _stricmp(const char *left, const char *right) {
    if (!left || !right)
        return left == right ? 0 : left ? 1 : -1;
    while (*left && *right) {
        const int a = std::tolower(static_cast<unsigned char>(*left++));
        const int b = std::tolower(static_cast<unsigned char>(*right++));
        if (a != b)
            return a - b;
    }
    return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
}

inline int _strnicmp(const char *left, const char *right, size_t count) {
    if (!left || !right)
        return left == right ? 0 : left ? 1 : -1;
    for (size_t index = 0; index < count; ++index) {
        const int a = std::tolower(static_cast<unsigned char>(left[index]));
        const int b = std::tolower(static_cast<unsigned char>(right[index]));
        if (a != b)
            return a - b;
        if (left[index] == '\0' || right[index] == '\0')
            break;
    }
    return 0;
}

inline int _wcsicmp(const wchar_t *left, const wchar_t *right) {
    if (!left || !right)
        return left == right ? 0 : left ? 1 : -1;
    while (*left && *right) {
        const wint_t a = std::towlower(*left++);
        const wint_t b = std::towlower(*right++);
        if (a != b)
            return a < b ? -1 : 1;
    }
    return *left == *right ? 0 : *left ? 1 : -1;
}

inline FILE *_wfopen(const wchar_t *path, const wchar_t *mode) {
    if (!path || !mode)
        return nullptr;
    std::string narrowPath;
    std::string narrowMode;
    for (; *path; ++path)
        narrowPath.push_back(*path <= 0x7f ? static_cast<char>(*path) : '?');
    for (; *mode; ++mode)
        narrowMode.push_back(*mode <= 0x7f ? static_cast<char>(*mode) : '?');
    return std::fopen(narrowPath.c_str(), narrowMode.c_str());
}

inline UINT GetWindowsDirectoryW(wchar_t *buffer, UINT size) {
    constexpr wchar_t path[] = L"/Fonts";
    constexpr UINT length = static_cast<UINT>(std::size(path) - 1);
    if (!buffer || size <= length)
        return 0;
    std::memcpy(buffer, path, sizeof(path));
    return length;
}

inline UINT GetDpiForWindow(void *) {
    return 96;
}

inline HANDLE GetKeyboardLayout(DWORD) {
    return nullptr;
}

#endif
