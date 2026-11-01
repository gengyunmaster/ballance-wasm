#include "BML/BML.h"
#include "Loader/ModContext.h"

#include <cstdlib>
#include <cstring>

void BML_CDECL BML_GetVersion(int *major, int *minor, int *patch) {
    if (major) *major = BML_MAJOR_VERSION;
    if (minor) *minor = BML_MINOR_VERSION;
    if (patch) *patch = BML_PATCH_VERSION;
}

const char *BML_CDECL BML_GetVersionString() {
    return BML_VERSION;
}

void *BML_CDECL BML_Malloc(size_t size) {
    return size ? std::malloc(size) : nullptr;
}

void *BML_CDECL BML_Calloc(size_t count, size_t size) {
    return count && size ? std::calloc(count, size) : nullptr;
}

void *BML_CDECL BML_Realloc(void *ptr, size_t size) {
    if (!size) {
        std::free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, size);
}

void BML_CDECL BML_Free(void *ptr) {
    std::free(ptr);
}

char *BML_CDECL BML_Strdup(const char *str) {
    if (!str)
        return nullptr;
    const size_t size = std::strlen(str) + 1;
    auto *copy = static_cast<char *>(std::malloc(size));
    if (copy)
        std::memcpy(copy, str, size);
    return copy;
}

const char *BML_CDECL BML_GetErrorString(int errorCode) {
    switch (errorCode) {
    case BML_OK: return "Success";
    case BML_ERROR_FAIL: return "Operation failed";
    case BML_ERROR_NOT_FOUND: return "Item not found";
    case BML_ERROR_NOT_IMPLEMENTED: return "Feature not implemented";
    case BML_ERROR_INVALID_PARAMETER: return "Invalid parameter";
    case BML_ERROR_ACCESS_DENIED: return "Access denied";
    default: return "Unknown error";
    }
}

int BML_CDECL BML_UnregisterCommand(const char *name) {
    ModContext *context = BML_GetModContext();
    return context ? context->UnregisterCommand(name) : BML_ERROR_FAIL;
}

const wchar_t *BML_CDECL BML_GetLoaderPathW(BML_LoaderDirectory) {
    return nullptr;
}

const char *BML_CDECL BML_GetLoaderPathUtf8(BML_LoaderDirectory directory) {
    switch (directory) {
    case BML_DIR_WORKING:
    case BML_DIR_GAME:
        return "/";
    case BML_DIR_TEMP:
        return "/tmp";
    case BML_DIR_LOADER:
        return "/ModLoader";
    case BML_DIR_CONFIG:
        return "/ModLoader/Configs";
    default:
        return nullptr;
    }
}
