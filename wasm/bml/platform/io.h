#ifndef BML_WASM_IO_H
#define BML_WASM_IO_H

#include "StringUtils.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

struct _wfinddata_t {
    unsigned attrib = 0;
    wchar_t name[260] = {};
};

constexpr unsigned _A_SUBDIR = 0x10;

namespace WasmPlatform {
struct FindData {
    std::vector<_wfinddata_t> Entries;
    std::size_t Position = 0;
};

inline int ReadNext(FindData &find, _wfinddata_t *output) {
    if (!output || find.Position >= find.Entries.size()) {
        errno = ENOENT;
        return -1;
    }
    *output = find.Entries[find.Position++];
    return 0;
}
}

inline intptr_t _wfindfirst(const wchar_t *pattern, _wfinddata_t *output) {
    if (!pattern || !output) {
        errno = EINVAL;
        return -1;
    }
    std::string nativePattern = utils::Utf16ToUtf8(pattern);
    std::replace(nativePattern.begin(), nativePattern.end(), '\\', '/');
    const std::filesystem::path path(nativePattern);
    const std::filesystem::path directory = path.parent_path();
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        errno = std::filesystem::exists(directory, error) ? EIO : ENOENT;
        return -1;
    }
    auto find = std::make_unique<WasmPlatform::FindData>();
    for (const auto &entry : iterator) {
        const std::string name = entry.path().filename().string();
        _wfinddata_t data;
        data.attrib = entry.is_directory(error) ? _A_SUBDIR : 0;
        const std::wstring wideName = utils::Utf8ToUtf16(name);
        if (wideName.size() >= std::size(data.name))
            continue;
        std::wcsncpy(data.name, wideName.c_str(), std::size(data.name) - 1);
        find->Entries.push_back(data);
    }
    if (ReadNext(*find, output) != 0)
        return -1;
    return reinterpret_cast<intptr_t>(find.release());
}

inline int _wfindnext(intptr_t handle, _wfinddata_t *output) {
    return handle > 0 ? WasmPlatform::ReadNext(*reinterpret_cast<WasmPlatform::FindData *>(handle), output) : -1;
}

inline int _findclose(intptr_t handle) {
    delete reinterpret_cast<WasmPlatform::FindData *>(handle);
    return 0;
}

#endif
