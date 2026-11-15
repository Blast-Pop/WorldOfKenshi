#include "bug_report.hpp"

#include "log.hpp"

#include <windows.h>
#include <winhttp.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace wok {

namespace {

constexpr size_t kMaxFileBytes = 10 * 1024 * 1024;   // a log's newest 10 MB (the bot zips them below 10 MB)
constexpr double kCrashDumpMaxAgeHours = 3.0;

struct Part {
    std::string name, filename, data;
};

// The newest `limit` bytes of a file (a log still being written can be read).
std::optional<std::string> readTail(const std::filesystem::path& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(limit) ? size - static_cast<std::streamoff>(limit) : 0;
    in.seekg(start);
    std::string data(static_cast<size_t>(size - start), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(in.gcount()));
    return data;
}

std::vector<Part> collectFiles(const std::string& modDir) {
    namespace fs = std::filesystem;
    std::vector<Part> files;
    std::error_code ec;
    const fs::path mod(modDir), kenshi = fs::path(modDir).parent_path();
    for (const fs::path& p : {mod / "wok_client.log", mod / "wok_client.prev.log", kenshi / "kenshi.log",
                              kenshi / "kenshi_info.log"})
        if (auto data = readTail(p, kMaxFileBytes)) files.push_back({"file", p.filename().string(), std::move(*data)});
    // Kenshi's last crash dump, if it is recent: it explains a crash better than any log.
    fs::path newest;
    fs::file_time_type newestTime{};
    for (auto& e : fs::directory_iterator(kenshi, ec))
        if (e.path().filename().string().rfind("crashDump", 0) == 0 && e.path().extension() == ".zip" &&
            e.last_write_time(ec) > newestTime) {
            newest = e.path();
            newestTime = e.last_write_time(ec);
        }
    if (!newest.empty()) {
        const auto age = fs::file_time_type::clock::now() - newestTime;
        if (age < std::chrono::duration<double, std::ratio<3600>>(kCrashDumpMaxAgeHours))
            if (auto data = readTail(newest, kMaxFileBytes)) files.push_back({"file", newest.filename().string(), std::move(*data)});
    }
    return files;
}

std::string multipart(const std::string& boundary, const std::vector<Part>& parts) {
    std::string body;
    for (const Part& p : parts) {
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + p.name + "\"";
        if (!p.filename.empty())
            body += "; filename=\"" + p.filename + "\"\r\nContent-Type: application/octet-stream";
        body += "\r\n\r\n" + p.data + "\r\n";
    }
    return body + "--" + boundary + "--\r\n";
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// POST; returns {status, body} or nothing if the server could not be reached.
std::optional<std::pair<DWORD, std::string>> post(const std::string& url, const std::string& contentType,
                                                  const std::string& body) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[1024] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) return std::nullopt;
    std::optional<std::pair<DWORD, std::string>> result;
    HINTERNET session = WinHttpOpen(L"WorldOfKenshi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return std::nullopt;
    WinHttpSetTimeouts(session, 10000, 10000, 60000, 60000);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
    const std::wstring header = L"Content-Type: " + widen(contentType);
    if (request &&
        WinHttpSendRequest(request, header.c_str(), static_cast<DWORD>(-1), const_cast<char*>(body.data()),
                           static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        std::string answer;
        for (;;) {
            DWORD available = 0, read = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0 || answer.size() > 4096) break;
            std::string chunk(available, '\0');
            if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
            answer.append(chunk.data(), read);
        }
        result = std::make_pair(status, answer);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

}  // namespace

BugReporter::~BugReporter() {
    if (thread_.joinable()) thread_.join();
}

void BugReporter::setStatus(std::string s) {
    std::lock_guard lock(mutex_);
    status_ = std::move(s);
}

std::string BugReporter::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void BugReporter::send(const std::string& url, BugReportInfo info) {
    if (busy_) return;
    if (thread_.joinable()) thread_.join();
    busy_ = true;
    setStatus("Sending your report...");
    thread_ = std::thread([this, url, info = std::move(info)] {
        std::vector<Part> parts = {{"title", "", info.title},       {"description", "", info.description},
                                   {"version", "", info.version},   {"server", "", info.server},
                                   {"character", "", info.character}};
        log("report: sending '%s'", info.title.c_str());  // logged first, so the report shows it too
        for (Part& f : collectFiles(info.modDir)) parts.push_back(std::move(f));
        const std::string boundary = "----WorldOfKenshiReport" + std::to_string(GetTickCount64());
        const std::string body = multipart(boundary, parts);
        auto answer = post(url, "multipart/form-data; boundary=" + boundary, body);
        if (!answer) {
            setStatus("Could not reach the report server. Check your connection and try again.");
        } else if (answer->first == 200) {
            setStatus("Sent, thank you! It is in #bug-reports on the World of Kenshi Discord.");
        } else {
            setStatus("Not sent: " + trimmed(answer->second.empty() ? "error " + std::to_string(answer->first)
                                                                     : answer->second));
        }
        log("report: %s (%zu bytes)", answer && answer->first == 200 ? "sent" : "NOT sent", body.size());
        busy_ = false;
    });
}

}  // namespace wok
