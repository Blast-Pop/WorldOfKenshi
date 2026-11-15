#include "updater.hpp"

#include "log.hpp"
#include "master_fetch.hpp"

#include <core/update.hpp>

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <mutex>
#include <thread>

namespace wok {

namespace {

std::mutex g_noticeMutex;
std::string g_notice;

void setNotice(std::string text) {
    std::lock_guard lock(g_noticeMutex);
    g_notice = std::move(text);
}

std::wstring ownPath() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ownPath), &self))
        return {};
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(path, n) : std::wstring();
}

std::string sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    unsigned char hash[32] = {};
    const bool ok = BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                               static_cast<ULONG>(data.size()), hash, sizeof(hash)) == 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    std::string hex;
    char two[3];
    for (unsigned char b : hash) {
        std::snprintf(two, sizeof(two), "%02x", b);
        hex += two;
    }
    return hex;
}

bool writeFile(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

void run(bool enabled) {
    const std::wstring dll = ownPath();
    if (dll.empty()) return;
    const std::wstring old = dll + L".old", fresh = dll + L".new";
    DeleteFileW(old.c_str());  // the version replaced by the last update
    DeleteFileW(fresh.c_str());
    if (!enabled) {
        log("update: automatic updates are off (version %s)", kClientVersion);
        return;
    }
    const auto releases = httpGet(kReleasesUrl, 2 * 1024 * 1024, 8000);
    if (!releases) {
        log("update: GitHub not reachable, staying on %s", kClientVersion);
        return;
    }
    const auto info = findUpdate(*releases, kClientVersion);
    if (!info) {
        log("update: version %s is up to date", kClientVersion);
        return;
    }
    log("update: version %s found (this is %s), downloading", info->version.c_str(), kClientVersion);
    const auto file = httpGet(info->url, 64 * 1024 * 1024, 30000);
    if (!file || file->size() < 64 * 1024 || (*file)[0] != 'M' || (*file)[1] != 'Z') {
        log("update: download failed, staying on %s", kClientVersion);
        return;
    }
    if (!info->sha256.empty() && sha256Hex(*file) != info->sha256) {
        log("update: the download does not match GitHub's checksum, ignored");
        return;
    }
    // A loaded DLL cannot be overwritten but can be renamed: the new file takes its name.
    if (!writeFile(fresh, *file)) {
        log("update: cannot write next to the mod (folder not writable?)");
        return;
    }
    if (!MoveFileExW(dll.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(fresh.c_str());
        log("update: cannot rename the running mod (error %lu)", GetLastError());
        return;
    }
    if (!MoveFileExW(fresh.c_str(), dll.c_str(), 0)) {
        MoveFileExW(old.c_str(), dll.c_str(), 0);  // put the running one back
        DeleteFileW(fresh.c_str());
        log("update: cannot install the new mod (error %lu)", GetLastError());
        return;
    }
    log("update: version %s installed, active at the next start", info->version.c_str());
    setNotice("World of Kenshi " + info->version + " installed - restart Kenshi to use it");
}

}  // namespace

void startAutoUpdate(bool enabled) {
    std::thread([enabled] {
        __try {
            [](bool on) { run(on); }(enabled);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }).detach();
}

std::string updateNotice() {
    std::lock_guard lock(g_noticeMutex);
    return g_notice;
}

}  // namespace wok
