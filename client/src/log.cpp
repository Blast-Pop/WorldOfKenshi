#include "log.hpp"

#include <core/paths.hpp>

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace wok {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
}  // namespace

const std::string& modDir() {
    static const std::string dir = [] {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string d = modDirFromExe(path);
        CreateDirectoryA(d.c_str(), nullptr);
        return d;
    }();
    return dir;
}

void logInit() {
    std::lock_guard lock(g_mutex);
    if (g_file) return;
    g_file = std::fopen(modFile(modDir(), "wok_client.log").c_str(), "w");
}

void log(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (!g_file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_file, fmt, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

}  // namespace wok
