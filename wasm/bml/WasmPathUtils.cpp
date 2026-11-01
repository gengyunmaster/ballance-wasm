#include "PathUtils.h"
#include "StringUtils.h"
#include "Windows.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
std::filesystem::path Path(const std::wstring &value) {
    return std::filesystem::path(utils::Utf16ToUtf8(value));
}

std::uint32_t ErrorCode(int error = errno) {
    switch (error) {
    case 0:
        return ERROR_SUCCESS;
    case ENOENT:
        return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
        return ERROR_ACCESS_DENIED;
    case EEXIST:
        return ERROR_ALREADY_EXISTS;
    default:
        return ERROR_INVALID_DATA;
    }
}
}

namespace utils {
bool ReadFileBytesW(const std::wstring &path, std::string &out) {
    std::uint32_t error = ERROR_SUCCESS;
    return ReadFileBytesW(path, out, error);
}

bool ReadFileBytesW(const std::wstring &path, std::string &out, std::uint32_t &error) {
    out.clear();
    errno = 0;
    std::ifstream input(Path(path), std::ios::binary);
    if (!input) {
        error = ErrorCode();
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) {
        error = ErrorCode(EIO);
        out.clear();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool WriteFileBytesAtomicW(const std::wstring &path, std::string_view data,
                           std::uint32_t &error, bool replaceExisting) {
    const std::filesystem::path destination = Path(path);
    std::error_code filesystemError;
    if (!replaceExisting && std::filesystem::exists(destination, filesystemError)) {
        error = ERROR_ALREADY_EXISTS;
        return false;
    }
    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, filesystemError);
    if (filesystemError) {
        error = ErrorCode(filesystemError.value());
        return false;
    }

    const std::filesystem::path temporary = destination.string() + ".wasm-tmp";
    errno = 0;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = ErrorCode();
            return false;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            error = ErrorCode(EIO);
            output.close();
            std::filesystem::remove(temporary, filesystemError);
            return false;
        }
    }

    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(temporary, filesystemError);
        error = ErrorCode(filesystemError.value());
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool CreateFileTreeUtf8(const std::string &path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error;
}

bool FileExistsUtf8(const std::string &file) {
    std::error_code error;
    return std::filesystem::is_regular_file(file, error) && !error;
}
}
