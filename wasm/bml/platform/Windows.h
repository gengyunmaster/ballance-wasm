#ifndef BML_WASM_WINDOWS_SHIM_H
#define BML_WASM_WINDOWS_SHIM_H

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>

using WORD = unsigned short;
using UINT = unsigned int;

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
constexpr unsigned long ERROR_SUCCESS = 0;
constexpr unsigned long ERROR_FILE_NOT_FOUND = 2;
constexpr unsigned long ERROR_PATH_NOT_FOUND = 3;
constexpr unsigned long ERROR_ACCESS_DENIED = 5;
constexpr unsigned long ERROR_INVALID_DATA = 13;
constexpr unsigned long ERROR_INVALID_NAME = 123;
constexpr unsigned long ERROR_ALREADY_EXISTS = 183;

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

#endif
