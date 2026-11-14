#include "PathUtils.h"
#include "StringUtils.h"
#include "Windows.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace {
namespace fs = std::filesystem;

std::string Slashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

fs::path Path(std::string_view value) {
    return fs::path(Slashes(std::string(value)));
}

fs::path Path(const std::wstring &value) {
    return Path(utils::Utf16ToUtf8(value));
}

std::string Generic(const fs::path &path) {
    return path.generic_string();
}

std::wstring GenericW(const fs::path &path) {
    return utils::Utf8ToUtf16(Generic(path));
}

std::string Normalize(std::string_view value) {
    return Generic(Path(value).lexically_normal());
}

std::wstring NormalizeW(const std::wstring &value) {
    return utils::Utf8ToUtf16(Normalize(utils::Utf16ToUtf8(value)));
}

std::uint32_t ErrorCode(int error = errno) {
    switch (error) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    default: return ERROR_INVALID_DATA;
    }
}

bool IsAbsolute(std::string_view value) {
    if (value.empty())
        return false;
    if (value.front() == '/' || value.front() == '\\')
        return true;
    return value.size() >= 3 &&
           ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')) &&
           value[1] == ':' && (value[2] == '/' || value[2] == '\\');
}

bool IsValid(std::string_view value) {
    return !value.empty() && value.find('\0') == std::string_view::npos;
}

bool IsInside(const fs::path &candidate, const fs::path &root) {
    const fs::path relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty())
        return candidate.lexically_normal() == root.lexically_normal();
    const std::string text = Generic(relative);
    return text != ".." && !text.starts_with("../") && !IsAbsolute(text);
}

bool ReadBytes(const fs::path &path, std::string &out, std::uint32_t *error = nullptr) {
    out.clear();
    errno = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = ErrorCode();
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) {
        if (error)
            *error = ErrorCode(EIO);
        out.clear();
        return false;
    }
    if (error)
        *error = ERROR_SUCCESS;
    return true;
}

bool WriteBytes(const fs::path &path, std::string_view data) {
    std::error_code error;
    const fs::path parent = path.parent_path();
    if (!parent.empty())
        fs::create_directories(parent, error);
    if (error)
        return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();
    return static_cast<bool>(output);
}

std::vector<uint8_t> ReadBinary(const fs::path &path) {
    std::string content;
    if (!ReadBytes(path, content))
        return {};
    return std::vector<uint8_t>(content.begin(), content.end());
}

bool WildcardMatch(std::string_view pattern, std::string_view value) {
    std::size_t p = 0;
    std::size_t v = 0;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0;
    while (v < value.size()) {
        if (p < pattern.size() && (pattern[p] == '?' ||
            std::tolower(static_cast<unsigned char>(pattern[p])) ==
            std::tolower(static_cast<unsigned char>(value[v])))) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = v;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            v = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

template <typename String>
String JoinPaths(const String &first, const String &second);

template <>
std::string JoinPaths(const std::string &first, const std::string &second) {
    if (first.empty())
        return Normalize(second);
    if (second.empty())
        return Normalize(first);
    if (IsAbsolute(second))
        return Normalize(second);
    return Generic((Path(first) / Path(second)).lexically_normal());
}

template <>
std::wstring JoinPaths(const std::wstring &first, const std::wstring &second) {
    return utils::Utf8ToUtf16(JoinPaths(utils::Utf16ToUtf8(first), utils::Utf16ToUtf8(second)));
}

template <typename String>
std::vector<String> ListEntries(const String &directory, const String &pattern, bool directories) {
    std::vector<String> result;
    const std::string patternUtf8 = [&]() {
        if constexpr (std::is_same_v<String, std::string>)
            return pattern;
        else
            return utils::Utf16ToUtf8(pattern);
    }();
    std::error_code error;
    const fs::path root = Path(directory);
    for (fs::directory_iterator iterator(root, error), end; !error && iterator != end; iterator.increment(error)) {
        const fs::path entry = iterator->path();
        const std::string name = entry.filename().string();
        if (!WildcardMatch(patternUtf8, name))
            continue;
        const bool isDirectory = iterator->is_directory(error);
        if (error)
            break;
        if (isDirectory != directories)
            continue;
        if constexpr (std::is_same_v<String, std::string>)
            result.push_back(name);
        else
            result.push_back(utils::Utf8ToUtf16(name));
    }
    return result;
}

utils::FileTime ReadFileTime(const fs::path &path) {
    std::error_code error;
    const auto modified = fs::last_write_time(path, error);
    if (error)
        return {0, 0, 0};
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        modified - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        systemTime.time_since_epoch()).count();
    return {microseconds, microseconds, microseconds};
}
}

namespace utils {
#define BML_WASM_PATH_STRING_OPS(Name, Expression) \
    bool Name##A(const std::string &value) { return Expression(Path(value)); } \
    bool Name##W(const std::wstring &value) { return Expression(Path(value)); } \
    bool Name##Utf8(const std::string &value) { return Expression(Path(value)); }

BML_WASM_PATH_STRING_OPS(FileExists, fs::is_regular_file)
BML_WASM_PATH_STRING_OPS(DirectoryExists, fs::is_directory)
BML_WASM_PATH_STRING_OPS(PathExists, fs::exists)
#undef BML_WASM_PATH_STRING_OPS

bool CreateDirectoryA(const std::string &path) { std::error_code e; return fs::create_directory(Path(path), e) || fs::is_directory(Path(path), e); }
bool CreateDirectoryW(const std::wstring &path) { return CreateDirectoryA(Utf16ToUtf8(path)); }
bool CreateDirectoryUtf8(const std::string &path) { return CreateDirectoryA(path); }
bool CreateFileTreeA(const std::string &path) { std::error_code e; fs::create_directories(Path(path), e); return !e; }
bool CreateFileTreeW(const std::wstring &path) { return CreateFileTreeA(Utf16ToUtf8(path)); }
bool CreateFileTreeUtf8(const std::string &path) { return CreateFileTreeA(path); }

bool DeleteFileA(const std::string &path) { std::error_code e; return fs::remove(Path(path), e); }
bool DeleteFileW(const std::wstring &path) { return DeleteFileA(Utf16ToUtf8(path)); }
bool DeleteFileUtf8(const std::string &path) { return DeleteFileA(path); }
bool DeleteDirectoryA(const std::string &path) {
    const std::string normalized = Normalize(path);
    if (normalized.empty() || normalized == "/")
        return false;
    std::error_code e;
    fs::remove_all(Path(normalized), e);
    return !e;
}
bool DeleteDirectoryW(const std::wstring &path) { return DeleteDirectoryA(Utf16ToUtf8(path)); }
bool DeleteDirectoryUtf8(const std::string &path) { return DeleteDirectoryA(path); }

bool CopyFileA(const std::string &path, const std::string &dest) {
    std::error_code e;
    fs::create_directories(Path(dest).parent_path(), e);
    return !e && fs::copy_file(Path(path), Path(dest), fs::copy_options::overwrite_existing, e);
}
bool CopyFileW(const std::wstring &path, const std::wstring &dest) { return CopyFileA(Utf16ToUtf8(path), Utf16ToUtf8(dest)); }
bool CopyFileUtf8(const std::string &path, const std::string &dest) { return CopyFileA(path, dest); }
bool MoveFileA(const std::string &path, const std::string &dest) {
    std::error_code e;
    fs::create_directories(Path(dest).parent_path(), e);
    if (e)
        return false;
    fs::rename(Path(path), Path(dest), e);
    return !e;
}
bool MoveFileW(const std::wstring &path, const std::wstring &dest) { return MoveFileA(Utf16ToUtf8(path), Utf16ToUtf8(dest)); }
bool MoveFileUtf8(const std::string &path, const std::string &dest) { return MoveFileA(path, dest); }

bool ExtractZipA(const std::string &, const std::string &) { return false; }
bool ExtractZipW(const std::wstring &, const std::wstring &) { return false; }
bool ExtractZipUtf8(const std::string &, const std::string &) { return false; }

std::string GetDriveA(const std::string &path) { return Path(path).root_name().string() == "/" ? std::string() : Path(path).root_name().string(); }
std::wstring GetDriveW(const std::wstring &path) { return Utf8ToUtf16(GetDriveA(Utf16ToUtf8(path))); }
std::string GetDriveUtf8(const std::string &path) { return GetDriveA(path); }
std::string GetDirectoryA(const std::string &path) { return Generic(Path(path).parent_path()); }
std::wstring GetDirectoryW(const std::wstring &path) { return Utf8ToUtf16(GetDirectoryA(Utf16ToUtf8(path))); }
std::string GetDirectoryUtf8(const std::string &path) { return GetDirectoryA(path); }
std::pair<std::string, std::string> GetDriveAndDirectoryA(const std::string &path) { return {GetDriveA(path), GetDirectoryA(path)}; }
std::pair<std::wstring, std::wstring> GetDriveAndDirectoryW(const std::wstring &path) { return {GetDriveW(path), GetDirectoryW(path)}; }
std::pair<std::string, std::string> GetDriveAndDirectoryUtf8(const std::string &path) { return GetDriveAndDirectoryA(path); }
std::string GetFileNameA(const std::string &path) { return Path(path).filename().string(); }
std::wstring GetFileNameW(const std::wstring &path) { return Utf8ToUtf16(GetFileNameA(Utf16ToUtf8(path))); }
std::string GetFileNameUtf8(const std::string &path) { return GetFileNameA(path); }
std::string GetExtensionA(const std::string &path) { return Path(path).extension().string(); }
std::wstring GetExtensionW(const std::wstring &path) { return Utf8ToUtf16(GetExtensionA(Utf16ToUtf8(path))); }
std::string GetExtensionUtf8(const std::string &path) { return GetExtensionA(path); }
std::string RemoveExtensionA(const std::string &path) { fs::path result = Path(path); result.replace_extension(); return Generic(result); }
std::wstring RemoveExtensionW(const std::wstring &path) { return Utf8ToUtf16(RemoveExtensionA(Utf16ToUtf8(path))); }
std::string RemoveExtensionUtf8(const std::string &path) { return RemoveExtensionA(path); }
std::string CombinePathA(const std::string &first, const std::string &second) { return JoinPaths(first, second); }
std::wstring CombinePathW(const std::wstring &first, const std::wstring &second) { return JoinPaths(first, second); }
std::string CombinePathUtf8(const std::string &first, const std::string &second) { return JoinPaths(first, second); }
std::string NormalizePathA(const std::string &path) { return Normalize(path); }
std::wstring NormalizePathW(const std::wstring &path) { return NormalizeW(path); }
std::string NormalizePathUtf8(const std::string &path) { return Normalize(path); }
std::wstring TrimTrailingSeparatorsW(const std::wstring &path) {
    std::wstring result = NormalizeW(path);
    while (result.size() > 1 && (result.back() == L'/' || result.back() == L'\\'))
        result.pop_back();
    return result;
}
std::wstring GetParentDirectoryW(const std::wstring &path) {
    const fs::path parent = Path(path).parent_path();
    return parent.empty() || parent == Path(path).root_path() ? std::wstring() : GenericW(parent);
}

bool IsPathInsideRootW(const std::wstring &path, const std::wstring &root) { return IsInside(Path(path), Path(root)); }
bool TryGetFinalPathW(const std::wstring &path, std::wstring &finalPath) {
    std::error_code error;
    fs::path resolved = fs::exists(Path(path), error) ? fs::canonical(Path(path), error) : fs::weakly_canonical(Path(path), error);
    if (error)
        return false;
    finalPath = GenericW(resolved);
    return true;
}
bool TryGetFinalPathInsideRootW(const std::wstring &path, const std::wstring &root, std::wstring &finalPath) {
    if (!TryGetFinalPathW(path, finalPath))
        return false;
    return IsPathInsideRootW(finalPath, root);
}
bool TryEncodePathForCodePage(std::wstring_view path, std::uint32_t, std::string &encodedPath) {
    encodedPath = Utf16ToUtf8(std::wstring(path));
    return Utf8ToUtf16(encodedPath) == path;
}
bool TryEncodePathForActiveCodePage(std::wstring_view path, std::string &encodedPath) { return TryEncodePathForCodePage(path, CP_UTF8, encodedPath); }
std::wstring GetShortPathW(const std::wstring &path) { return path; }

bool IsPathValidA(const std::string &path) { return IsValid(path); }
bool IsPathValidW(const std::wstring &path) { return IsPathValidA(Utf16ToUtf8(path)); }
bool IsPathValidUtf8(const std::string &path) { return IsValid(path); }
bool IsAbsolutePathA(const std::string &path) { return IsAbsolute(path); }
bool IsAbsolutePathW(const std::wstring &path) { return IsAbsolutePathA(Utf16ToUtf8(path)); }
bool IsAbsolutePathUtf8(const std::string &path) { return IsAbsolute(path); }
bool IsRelativePathA(const std::string &path) { return IsValid(path) && !IsAbsolute(path); }
bool IsRelativePathW(const std::wstring &path) { return IsRelativePathA(Utf16ToUtf8(path)); }
bool IsRelativePathUtf8(const std::string &path) { return IsRelativePathA(path); }
bool IsPathRootedA(const std::string &path) { return IsAbsolute(path); }
bool IsPathRootedW(const std::wstring &path) { return IsPathRootedA(Utf16ToUtf8(path)); }
bool IsPathRootedUtf8(const std::string &path) { return IsPathRootedA(path); }

std::string ResolvePathA(const std::string &path) { std::error_code e; return Generic(fs::absolute(Path(path), e).lexically_normal()); }
std::wstring ResolvePathW(const std::wstring &path) { return Utf8ToUtf16(ResolvePathA(Utf16ToUtf8(path))); }
std::string ResolvePathUtf8(const std::string &path) { return ResolvePathA(path); }
std::string MakeRelativePathA(const std::string &path, const std::string &basePath) {
    return Generic(fs::absolute(Path(path)).lexically_normal().lexically_relative(fs::absolute(Path(basePath)).lexically_normal()));
}
std::wstring MakeRelativePathW(const std::wstring &path, const std::wstring &basePath) { return Utf8ToUtf16(MakeRelativePathA(Utf16ToUtf8(path), Utf16ToUtf8(basePath))); }
std::string MakeRelativePathUtf8(const std::string &path, const std::string &basePath) { return MakeRelativePathA(path, basePath); }

std::string GetTempPathA() { return "/tmp"; }
std::wstring GetTempPathW() { return L"/tmp"; }
std::string GetTempPathUtf8() { return "/tmp"; }
std::string GetCurrentDirectoryA() { std::error_code e; return Generic(fs::current_path(e)); }
std::wstring GetCurrentDirectoryW() { return Utf8ToUtf16(GetCurrentDirectoryA()); }
std::string GetCurrentDirectoryUtf8() { return GetCurrentDirectoryA(); }
bool SetCurrentDirectoryA(const std::string &path) { std::error_code e; fs::current_path(Path(path), e); return !e; }
bool SetCurrentDirectoryW(const std::wstring &path) { return SetCurrentDirectoryA(Utf16ToUtf8(path)); }
bool SetCurrentDirectoryUtf8(const std::string &path) { return SetCurrentDirectoryA(path); }
std::string GetExecutablePathA() { return "/Player"; }
std::wstring GetExecutablePathW() { return L"/Player"; }
std::string GetExecutablePathUtf8() { return "/Player"; }

int64_t GetFileSizeA(const std::string &path) { std::error_code e; const auto size = fs::file_size(Path(path), e); return e ? -1 : static_cast<int64_t>(size); }
int64_t GetFileSizeW(const std::wstring &path) { return GetFileSizeA(Utf16ToUtf8(path)); }
int64_t GetFileSizeUtf8(const std::string &path) { return GetFileSizeA(path); }
FileTime GetFileTimeA(const std::string &path) { return ReadFileTime(Path(path)); }
FileTime GetFileTimeW(const std::wstring &path) { return ReadFileTime(Path(path)); }
FileTime GetFileTimeUtf8(const std::string &path) { return ReadFileTime(Path(path)); }

std::string ReadTextFileA(const std::string &path) { std::string result; ReadBytes(Path(path), result); return result; }
std::wstring ReadTextFileW(const std::wstring &path) { return Utf8ToUtf16(ReadTextFileA(Utf16ToUtf8(path))); }
std::string ReadTextFileUtf8(const std::string &path) { return ReadTextFileA(path); }
bool WriteTextFileA(const std::string &path, const std::string &content) { return WriteBytes(Path(path), content); }
bool WriteTextFileW(const std::wstring &path, const std::wstring &content) { return WriteBytes(Path(path), Utf16ToUtf8(content)); }
bool WriteTextFileUtf8(const std::string &path, const std::string &content) { return WriteBytes(Path(path), content); }
std::vector<uint8_t> ReadBinaryFileA(const std::string &path) { return ReadBinary(Path(path)); }
std::vector<uint8_t> ReadBinaryFileW(const std::wstring &path) { return ReadBinary(Path(path)); }
std::vector<uint8_t> ReadBinaryFileUtf8(const std::string &path) { return ReadBinary(Path(path)); }
bool ReadFileBytesW(const std::wstring &path, std::string &out) { std::uint32_t error; return ReadFileBytesW(path, out, error); }
bool ReadFileBytesW(const std::wstring &path, std::string &out, std::uint32_t &error) { return ReadBytes(Path(path), out, &error); }
bool ReadFileBytesUtf8(const std::string &path, std::string &out) { return ReadBytes(Path(path), out); }
bool WriteFileBytesAtomicW(const std::wstring &path, std::string_view data, std::uint32_t &error, bool replaceExisting) {
    const fs::path destination = Path(path);
    std::error_code filesystemError;
    if (!replaceExisting && fs::exists(destination, filesystemError)) {
        error = ERROR_ALREADY_EXISTS;
        return false;
    }
    const fs::path temporary = destination.string() + ".wasm-tmp";
    if (!WriteBytes(temporary, data)) {
        error = ErrorCode();
        return false;
    }
    fs::rename(temporary, destination, filesystemError);
    if (filesystemError) {
        fs::remove(temporary, filesystemError);
        error = ErrorCode(filesystemError.value());
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}
bool WriteBinaryFileA(const std::string &path, const std::vector<uint8_t> &data) { return WriteBytes(Path(path), std::string_view(reinterpret_cast<const char *>(data.data()), data.size())); }
bool WriteBinaryFileW(const std::wstring &path, const std::vector<uint8_t> &data) { return WriteBinaryFileA(Utf16ToUtf8(path), data); }
bool WriteBinaryFileUtf8(const std::string &path, const std::vector<uint8_t> &data) { return WriteBinaryFileA(path, data); }

std::string CreateTempFileA(const std::string &prefix) {
    static uint64_t sequence = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::string candidate = "/tmp/" + (prefix.empty() ? "BML" : prefix) + std::to_string(++sequence) + ".tmp";
        if (fs::exists(candidate))
            continue;
        if (WriteBytes(candidate, {}))
            return candidate;
    }
    return {};
}
std::wstring CreateTempFileW(const std::wstring &prefix) { return Utf8ToUtf16(CreateTempFileA(Utf16ToUtf8(prefix))); }
std::string CreateTempFileUtf8(const std::string &prefix) { return CreateTempFileA(prefix); }

std::vector<std::string> ListFilesA(const std::string &dir, const std::string &pattern) { return ListEntries<std::string>(dir, pattern, false); }
std::vector<std::wstring> ListFilesW(const std::wstring &dir, const std::wstring &pattern) { return ListEntries<std::wstring>(dir, pattern, false); }
std::vector<std::string> ListFilesUtf8(const std::string &dir, const std::string &pattern) { return ListFilesA(dir, pattern); }
bool ListFilePathsRecursiveW(const std::wstring &dir, std::vector<std::wstring> &files) {
    std::error_code error;
    for (fs::recursive_directory_iterator iterator(Path(dir), error), end; !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file(error))
            files.push_back(GenericW(iterator->path()));
    }
    return !error;
}
std::vector<std::string> ListDirectoriesA(const std::string &dir, const std::string &pattern) { return ListEntries<std::string>(dir, pattern, true); }
std::vector<std::wstring> ListDirectoriesW(const std::wstring &dir, const std::wstring &pattern) { return ListEntries<std::wstring>(dir, pattern, true); }
std::vector<std::string> ListDirectoriesUtf8(const std::string &dir, const std::string &pattern) { return ListDirectoriesA(dir, pattern); }
}
