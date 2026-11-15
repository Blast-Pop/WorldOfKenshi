#include "master_fetch.hpp"

#include "log.hpp"

#include <windows.h>
#include <winhttp.h>

namespace wok {

namespace {
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // URLs are ASCII
}  // namespace

std::optional<std::string> httpGet(const std::string& url, size_t maxBytes, int timeoutMs) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[1024] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) return std::nullopt;

    std::optional<std::string> result;
    HINTERNET session = WinHttpOpen(L"WorldOfKenshi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return std::nullopt;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            std::string body;
            bool complete = false;
            for (;;) {
                DWORD available = 0, read = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) break;
                if (available == 0) {
                    complete = true;
                    break;
                }
                if (body.size() + available > maxBytes) break;
                std::string chunk(available, '\0');
                if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
                body.append(chunk.data(), read);
            }
            if (complete) result = body;
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

MasterFetch::~MasterFetch() {
    if (thread_.joinable()) thread_.join();
}

void MasterFetch::start(const std::string& masterUrl) {
    if (running_ || masterUrl.empty()) return;
    if (thread_.joinable()) thread_.join();
    running_ = true;
    thread_ = std::thread([this, masterUrl] {
        auto body = httpGet(masterUrl + "/master/servers");
        auto list = body ? parseServerList(*body) : std::vector<ServerEntry>{};
        log("master: %s -> %s, %zu server(s)", masterUrl.c_str(), body ? "ok" : "unreachable", list.size());
        if (body) {
            std::lock_guard lock(mutex_);
            servers_ = std::move(list);
        }
        running_ = false;
    });
}

std::vector<ServerEntry> MasterFetch::servers() const {
    std::lock_guard lock(mutex_);
    return servers_;
}

}  // namespace wok
